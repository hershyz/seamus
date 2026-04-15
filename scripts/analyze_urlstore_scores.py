#!/usr/bin/env python3
"""
Score every URL in the url_store binary and plot the bucket distribution.

Mirrors lib/Frontier.h:calcPriorityScore and get_priority_bucket EXACTLY,
including the `len_ext += 1` on the first separator, which leaks the
'/' character into the TLD-map lookup and makes factor_2 fall back to
0.6 for nearly every URL that has a path.

USAGE
-----
    pip3 install --user numpy matplotlib
    python3 analyze_urlstore_scores.py \\
        --path /var/seamus/urlstore_output/urlstore.txt \\
        --out  ./urlstore_analysis

OUTPUTS
-------
    urlstore_analysis/
        report.txt                 # text stats
        bucket_distribution.png    # bar chart, all vs wikipedia (log y)
        score_histogram.png        # histogram, all vs wikipedia
        wikipedia_crawled.png      # per-bucket crawled-vs-uncrawled split

NOTE
----
Pure-CPython will take ~15-30 minutes for 50M records. If pypy3 is
available on the VM, run with pypy3 for a 3-5x speedup. After the run
finishes, download the PNGs with scp from your workstation:
    scp user@vm:/path/to/urlstore_analysis/*.png .
"""

import argparse
import math
import mmap
import os
import struct
import sys
import time

import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt


# -----------------------------------------------------------------------------
# Constants mirroring lib/Frontier.h
# -----------------------------------------------------------------------------

TLD_WEIGHT = {
    b"gov": 1.2, b"edu": 1.2, b"mil": 1.2,
    b"org": 1.1,
    b"com": 1.0, b"net": 1.0,
    b"info": 0.8, b"biz": 0.8,
}
DEFAULT_TLD = 0.6

# get_priority_bucket thresholds (index = bucket)
BUCKET_THRESHOLDS = (450_000, 300_000, 200_000, 100_000,
                     50_000,  40_000,  30_000,  20_000)
NUM_BUCKETS = len(BUCKET_THRESHOLDS)  # 8
DROPPED = NUM_BUCKETS                  # 8 = "don't add to any bucket"

# Score-histogram bins: 0..1,500,000 in 10k steps = 150 bins; final bin = overflow.
SCORE_BIN_WIDTH = 10_000
SCORE_BIN_COUNT = 150


# -----------------------------------------------------------------------------
# Scoring
# -----------------------------------------------------------------------------

def calc_score(url: bytes, seed_dist: int) -> int:
    """Exact port of calcPriorityScore in lib/Frontier.h.

    Replicates the `len_ext += 1` on separator (which includes the '/'
    in the TLD substring). Do NOT fix this here -- we want parity with
    what the live crawler is computing.
    """
    n = len(url)
    if n < 8:
        return 0
    if url[:4] != b"http":
        return 0
    if url[4:5] == b"s":
        f1 = 1.0
        start_pos = 8
    else:
        f1 = 0.6
        start_pos = 7

    subdomain = 0
    digit_count = 0
    domain_size = 0
    path_depth = 0
    qmark = False
    start = 0
    len_ext = 0

    i = start_pos
    while i < n:
        c = url[i]
        if c == 47 or c == 63 or c == 35 or c == 58:  # / ? # :
            len_ext += 1  # MATCHES C++ BUG: separator leaks into TLD string
            while i < n:
                ci = url[i]
                if ci == 47:
                    path_depth += 1
                elif ci == 63:
                    qmark = True
                i += 1
            break
        elif c == 46:  # '.'
            subdomain += 1
            start = i + 1
            len_ext = 0
        else:
            len_ext += 1
            if 48 <= c <= 57:
                digit_count += 1
        domain_size += 1
        i += 1

    ext = bytes(url[start:start + len_ext])
    f2 = TLD_WEIGHT.get(ext, DEFAULT_TLD)
    f3 = max(math.exp(-0.04 * seed_dist), 0.4)
    f4 = max((50.0 - domain_size) / 50.0, 0.5)
    f5 = 1.0 / (1.0 + 0.1 * subdomain)
    f6 = 1.0 / (1.0 + 0.15 * digit_count)
    f7 = max((150.0 - n) / 100.0, 0.5)
    f8 = max(1.0 - 0.1 * path_depth, 0.4)
    f9 = 0.75 if qmark else 1.0

    return int((f1 * f2 * f3 * f4 * f5 * f6 * f7 * f8 * f9) * 1_000_000)


def get_bucket(score: int) -> int:
    for i, t in enumerate(BUCKET_THRESHOLDS):
        if score >= t:
            return i
    return DROPPED


# -----------------------------------------------------------------------------
# URL store parser -- see url_store/url_store.cpp:223-297 for the write format.
# -----------------------------------------------------------------------------

def iter_urlstore(path):
    """Yield (url_bytes, seed_dist, crawled) for every record."""
    u32 = struct.Struct("<I").unpack_from
    u16 = struct.Struct("<H").unpack_from
    u64 = struct.Struct("<Q").unpack_from

    fd = open(path, "rb")
    try:
        mm = mmap.mmap(fd.fileno(), 0, access=mmap.ACCESS_READ)
    except ValueError:
        # Empty file
        fd.close()
        return

    try:
        total = mm.size()
        pos = 0

        # -- Anchor table (we don't need the values, just skip) --
        num_anchors = u32(mm, pos)[0]
        pos += 4
        for _ in range(num_anchors):
            alen = u32(mm, pos)[0]
            pos += 4 + alen

        # -- URL records until EOF --
        while pos < total:
            url_len = u32(mm, pos)[0];                 pos += 4
            url = mm[pos:pos + url_len];               pos += url_len
            pos += 4                                   # num_encountered
            seed_dist = u16(mm, pos)[0];               pos += 2
            pos += 2                                   # eot
            title_len = u64(mm, pos)[0];               pos += 8
            pos += title_len                           # title
            pos += 4                                   # eod + domain_dist
            crawled = mm[pos] != 0;                    pos += 1
            num_freqs = u32(mm, pos)[0];               pos += 4
            pos += num_freqs * 8                       # anchor_freqs entries
            yield url, seed_dist, crawled
    finally:
        mm.close()
        fd.close()


# -----------------------------------------------------------------------------
# Analysis
# -----------------------------------------------------------------------------

class Accum:
    """Streaming accumulator -- no per-URL storage, only histograms."""
    def __init__(self):
        self.total = 0
        self.crawled = 0
        self.wiki = 0
        self.wiki_crawled = 0
        self.bucket        = np.zeros(NUM_BUCKETS + 1, dtype=np.int64)
        self.bucket_crwl   = np.zeros(NUM_BUCKETS + 1, dtype=np.int64)
        self.wbucket       = np.zeros(NUM_BUCKETS + 1, dtype=np.int64)
        self.wbucket_crwl  = np.zeros(NUM_BUCKETS + 1, dtype=np.int64)
        self.score_hist    = np.zeros(SCORE_BIN_COUNT + 1, dtype=np.int64)
        self.wscore_hist   = np.zeros(SCORE_BIN_COUNT + 1, dtype=np.int64)
        # crawl-breakdown per bucket for wiki
        self.score_sum      = 0
        self.wscore_sum     = 0
        # percentile support via coarse histogram (fine enough for P1..P99)
        # (we derive percentiles directly from score_hist / wscore_hist below)


def analyze(path, progress_every=1_000_000):
    acc = Accum()
    t0 = time.time()
    last_log = t0

    for url, seed_dist, crawled in iter_urlstore(path):
        score = calc_score(url, seed_dist)
        bucket = get_bucket(score)
        is_wiki = b"wikipedia.org" in url

        acc.total += 1
        acc.bucket[bucket] += 1
        acc.score_sum += score
        bin_idx = score // SCORE_BIN_WIDTH
        if bin_idx > SCORE_BIN_COUNT:
            bin_idx = SCORE_BIN_COUNT
        acc.score_hist[bin_idx] += 1

        if crawled:
            acc.crawled += 1
            acc.bucket_crwl[bucket] += 1

        if is_wiki:
            acc.wiki += 1
            acc.wbucket[bucket] += 1
            acc.wscore_sum += score
            acc.wscore_hist[bin_idx] += 1
            if crawled:
                acc.wiki_crawled += 1
                acc.wbucket_crwl[bucket] += 1

        if acc.total % progress_every == 0:
            now = time.time()
            rate = progress_every / max(now - last_log, 1e-9)
            last_log = now
            elapsed = int(now - t0)
            print(f"[PROG] {acc.total:>12,} urls   "
                  f"elapsed={elapsed}s   rate={rate:,.0f}/s",
                  flush=True)

    return acc


# -----------------------------------------------------------------------------
# Reporting
# -----------------------------------------------------------------------------

def _percentiles_from_hist(hist, percentiles=(1, 10, 25, 50, 75, 90, 99)):
    """Percentiles from a score histogram. bin midpoints as estimates."""
    total = hist.sum()
    if total == 0:
        return {p: 0 for p in percentiles}
    cdf = np.cumsum(hist)
    result = {}
    for p in percentiles:
        target = total * p / 100.0
        idx = int(np.searchsorted(cdf, target))
        idx = min(idx, SCORE_BIN_COUNT)
        # midpoint of the bin
        if idx == SCORE_BIN_COUNT:
            result[p] = SCORE_BIN_COUNT * SCORE_BIN_WIDTH  # overflow lower bound
        else:
            result[p] = idx * SCORE_BIN_WIDTH + SCORE_BIN_WIDTH // 2
    return result


def bucket_label(i):
    if i == DROPPED:
        return "dropped (<20k)"
    lo = BUCKET_THRESHOLDS[i]
    hi = BUCKET_THRESHOLDS[i - 1] if i > 0 else None
    return f"bucket {i} (>={lo:,})" if hi is None else f"bucket {i} ({lo:,}..{hi-1:,})"


def format_report(acc: Accum) -> str:
    lines = []
    w = lines.append
    total = acc.total or 1
    wtotal = acc.wiki or 1

    w("=" * 72)
    w(" URL STORE FRONTIER-SCORE ANALYSIS")
    w("=" * 72)
    w(f" Total URL records:            {acc.total:>15,}")
    w(f"   of which crawled:           {acc.crawled:>15,}"
      f"   ({acc.crawled / total * 100:6.2f}%)")
    w(f" Wikipedia URLs (substr match): {acc.wiki:>14,}"
      f"   ({acc.wiki / total * 100:6.3f}%)")
    w(f"   of which crawled:           {acc.wiki_crawled:>15,}"
      f"   ({acc.wiki_crawled / wtotal * 100:6.2f}%)")
    w("")
    w(" NOTE ON SCORING BUG")
    w(" -------------------")
    w(" lib/Frontier.h:70 increments len_ext when the URL-domain loop hits")
    w(" a '/', so the TLD substring includes the slash (e.g. 'org/' not")
    w(" 'org'). The TLD map misses, so factor_2 = 0.6 for every URL with")
    w(" a non-empty path -- effectively disabling the .gov/.edu/.org bonus.")
    w(" This script replicates that behavior to match the live crawler.")
    w("")
    w(" BUCKET DISTRIBUTION (all URLs)")
    w(" " + "-" * 70)
    w(f" {'bucket':<32}{'count':>14}{'share':>10}"
      f"{'crawled':>12}{'crwl%':>8}")
    for i in range(NUM_BUCKETS + 1):
        c = int(acc.bucket[i])
        cr = int(acc.bucket_crwl[i])
        share = c / total * 100
        crwl = (cr / c * 100) if c else 0.0
        w(f" {bucket_label(i):<32}{c:>14,}{share:>9.2f}%"
          f"{cr:>12,}{crwl:>7.2f}%")
    w("")
    w(" BUCKET DISTRIBUTION (wikipedia.org only)")
    w(" " + "-" * 70)
    w(f" {'bucket':<32}{'count':>14}{'share':>10}"
      f"{'crawled':>12}{'crwl%':>8}")
    for i in range(NUM_BUCKETS + 1):
        c = int(acc.wbucket[i])
        cr = int(acc.wbucket_crwl[i])
        share = c / wtotal * 100
        crwl = (cr / c * 100) if c else 0.0
        w(f" {bucket_label(i):<32}{c:>14,}{share:>9.2f}%"
          f"{cr:>12,}{crwl:>7.2f}%")
    w("")
    w(" SCORE PERCENTILES")
    w(" " + "-" * 70)
    all_pct = _percentiles_from_hist(acc.score_hist)
    wiki_pct = _percentiles_from_hist(acc.wscore_hist)
    w(f" {'percentile':<12}{'all URLs':>14}{'wikipedia':>14}")
    for p in (1, 10, 25, 50, 75, 90, 99):
        w(f" P{p:<11}{all_pct[p]:>14,}{wiki_pct[p]:>14,}")
    w("")
    mean_all = acc.score_sum / total
    mean_wiki = acc.wscore_sum / wtotal
    w(f" mean score  all={mean_all:,.0f}   wiki={mean_wiki:,.0f}")
    w("=" * 72)
    return "\n".join(lines)


# -----------------------------------------------------------------------------
# Plotting
# -----------------------------------------------------------------------------

def plot_bucket_distribution(acc: Accum, out_path: str):
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 5))

    xs = list(range(NUM_BUCKETS + 1))
    labels = [f"b{i}" for i in range(NUM_BUCKETS)] + ["drop"]

    ax1.bar(xs, acc.bucket, color="#4c72b0")
    ax1.set_yscale("log")
    ax1.set_title("All URLs -- bucket distribution (log y)")
    ax1.set_xticks(xs)
    ax1.set_xticklabels(labels)
    ax1.set_xlabel("priority bucket (0 = elite)")
    ax1.set_ylabel("count")
    ax1.grid(True, alpha=0.3)

    ax2.bar(xs, acc.wbucket, color="#dd8452")
    if acc.wbucket.sum() > 0:
        ax2.set_yscale("log")
    ax2.set_title(f"Wikipedia URLs -- bucket distribution (N={acc.wiki:,})")
    ax2.set_xticks(xs)
    ax2.set_xticklabels(labels)
    ax2.set_xlabel("priority bucket (0 = elite)")
    ax2.set_ylabel("count")
    ax2.grid(True, alpha=0.3)

    fig.tight_layout()
    fig.savefig(out_path, dpi=130)
    plt.close(fig)


def plot_score_histogram(acc: Accum, out_path: str):
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 5))
    edges = np.arange(SCORE_BIN_COUNT + 2) * SCORE_BIN_WIDTH
    centers = edges[:-1] + SCORE_BIN_WIDTH / 2

    ax1.bar(centers, acc.score_hist, width=SCORE_BIN_WIDTH * 0.95,
            color="#4c72b0")
    ax1.set_yscale("log")
    ax1.set_title("All URLs -- score distribution (log y)")
    ax1.set_xlabel("priority score")
    ax1.set_ylabel("count")
    for t in BUCKET_THRESHOLDS:
        ax1.axvline(t, color="red", linewidth=0.5, alpha=0.5)
    ax1.grid(True, alpha=0.3)

    ax2.bar(centers, acc.wscore_hist, width=SCORE_BIN_WIDTH * 0.95,
            color="#dd8452")
    if acc.wscore_hist.sum() > 0:
        ax2.set_yscale("log")
    ax2.set_title(f"Wikipedia URLs -- score distribution (N={acc.wiki:,})")
    ax2.set_xlabel("priority score")
    ax2.set_ylabel("count")
    for t in BUCKET_THRESHOLDS:
        ax2.axvline(t, color="red", linewidth=0.5, alpha=0.5)
    ax2.grid(True, alpha=0.3)

    fig.tight_layout()
    fig.savefig(out_path, dpi=130)
    plt.close(fig)


def plot_wikipedia_crawled(acc: Accum, out_path: str):
    fig, ax = plt.subplots(figsize=(10, 5))
    xs = list(range(NUM_BUCKETS + 1))
    labels = [f"b{i}" for i in range(NUM_BUCKETS)] + ["drop"]
    uncrawled = acc.wbucket - acc.wbucket_crwl

    ax.bar(xs, acc.wbucket_crwl, color="#55a868", label="crawled")
    ax.bar(xs, uncrawled, bottom=acc.wbucket_crwl, color="#c44e52",
           label="uncrawled")
    if acc.wbucket.sum() > 0:
        ax.set_yscale("log")
    ax.set_xticks(xs)
    ax.set_xticklabels(labels)
    ax.set_xlabel("priority bucket (0 = elite)")
    ax.set_ylabel("count")
    ax.set_title(f"Wikipedia URLs per bucket: crawled vs uncrawled "
                 f"(N={acc.wiki:,}, crawled={acc.wiki_crawled:,})")
    ax.legend()
    ax.grid(True, alpha=0.3, axis="y")

    fig.tight_layout()
    fig.savefig(out_path, dpi=130)
    plt.close(fig)


# -----------------------------------------------------------------------------
# Entrypoint
# -----------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--path", default="/var/seamus/urlstore_output/urlstore.txt",
                    help="path to urlstore binary")
    ap.add_argument("--out", default="./urlstore_analysis",
                    help="output directory for PNGs + report")
    ap.add_argument("--progress-every", type=int, default=1_000_000,
                    help="print a progress line every N records")
    args = ap.parse_args()

    if not os.path.exists(args.path):
        print(f"[ERR] {args.path} does not exist", file=sys.stderr)
        sys.exit(1)
    os.makedirs(args.out, exist_ok=True)

    sz = os.path.getsize(args.path)
    print(f"[INFO] analyzing {args.path} ({sz / 1e9:.2f} GB)")
    print(f"[INFO] writing outputs to {args.out}/")
    print(f"[INFO] python={sys.version.split()[0]} numpy={np.__version__}")
    t0 = time.time()
    acc = analyze(args.path, progress_every=args.progress_every)
    print(f"[INFO] parse+score done in {time.time() - t0:.1f}s "
          f"({acc.total:,} records)")

    report = format_report(acc)
    print()
    print(report)
    with open(os.path.join(args.out, "report.txt"), "w") as f:
        f.write(report + "\n")

    print("[INFO] rendering plots...")
    plot_bucket_distribution(acc, os.path.join(args.out, "bucket_distribution.png"))
    plot_score_histogram(acc,    os.path.join(args.out, "score_histogram.png"))
    plot_wikipedia_crawled(acc,  os.path.join(args.out, "wikipedia_crawled.png"))
    print(f"[INFO] done. outputs in {args.out}/")


if __name__ == "__main__":
    main()
