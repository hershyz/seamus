#!/usr/bin/env python3
"""
Score every URL in the url_store binary with TUNABLE weights + thresholds.

Mirrors lib/Frontier.h:calcPriorityScore + get_priority_bucket, but every
weight is exposed as a CLI flag so you can iterate quickly without
recompiling the crawler. Also offers an npz feature cache so you parse
the 50M-record binary ONCE (~15-30 min on CPython) and re-score in
seconds.

USAGE
-----

    # First run: parse binary, build feature cache, score with defaults
    python3 analyze_urlstore_scores.py \\
        --path  /var/seamus/urlstore_output/urlstore.txt \\
        --cache /tmp/urlstore_features.npz \\
        --out   ./run_baseline

    # Subsequent runs: reuse cache, iterate on weights in seconds
    python3 analyze_urlstore_scores.py \\
        --cache /tmp/urlstore_features.npz \\
        --fix-tld-bug \\
        --wiki-boost 2.5 \\
        --thresholds 500000,380000,280000,200000,140000,90000,60000,40000 \\
        --tld-weights gov=1.2,edu=1.2,mil=1.2,org=1.15,com=1.0,net=1.0,info=0.7,biz=0.7,other=0.5 \\
        --seed-decay 0.08 --seed-floor 0.3 \\
        --path-slope 0.15 --path-floor 0.3 \\
        --subdomain-coef 0.15 \\
        --out ./run_tuned

OUTPUTS
-------
    <out>/
        report.txt                 # text stats
        bucket_distribution.png    # bar chart, all vs wikipedia (log y)
        score_histogram.png        # histogram, all vs wikipedia
        wikipedia_crawled.png      # per-bucket crawled-vs-uncrawled split

NOTE
----
Pure-CPython parsing is ~50k-150k records/sec. For 50M that's 5-15 min
on a recent VM; more on slow disks. PyPy is NOT needed for the re-score
path (vectorized numpy), only the initial parse.
"""

import argparse
import array as pyarray
import mmap
import os
import struct
import sys
import time
from dataclasses import dataclass, field
from typing import Optional

import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt


# -----------------------------------------------------------------------------
# TLD enum
# -----------------------------------------------------------------------------

TLD_NAMES = ["gov", "edu", "mil", "org", "com", "net", "info", "biz", "other"]
TLD_NAME_TO_IDX = {n: i for i, n in enumerate(TLD_NAMES)}
TLD_OTHER = TLD_NAME_TO_IDX["other"]

# byte-form of known TLDs for fast dict lookup in parser hot loop
TLD_BYTES_TO_IDX = {n.encode(): i for i, n in enumerate(TLD_NAMES) if n != "other"}

# Score-histogram bins: 0..1,500,000 in 10k steps = 150 bins; final bin = overflow.
SCORE_BIN_WIDTH = 10_000
SCORE_BIN_COUNT = 150

# number of priority buckets (matches lib/consts.h:PRIORITY_BUCKETS)
NUM_BUCKETS = 8
DROPPED = NUM_BUCKETS  # bucket index used for "dropped / below min threshold"


# -----------------------------------------------------------------------------
# Weights (mirrors lib/Frontier.h)
# -----------------------------------------------------------------------------

@dataclass
class Weights:
    # f2 -- TLD factor (index = TLD enum; length 9)
    tld: np.ndarray = field(default_factory=lambda: np.array(
        [1.2, 1.2, 1.2, 1.1, 1.0, 1.0, 0.8, 0.8, 0.6], dtype=np.float32))

    # f1 -- http vs https. HTTPS = 1.0, HTTP = http_weight.
    http_weight: float = 0.6

    # f3 -- seed-distance decay: max(exp(-seed_decay * seed_dist), seed_floor)
    seed_decay: float = 0.04
    seed_floor: float = 0.4

    # f_dd -- domain-distance decay: max(exp(-dd_decay * domain_dist), dd_floor)
    # Default 0.0/1.0 = disabled (matches current C++ which doesn't use domain_dist)
    domain_dist_decay: float = 0.0
    domain_dist_floor: float = 1.0

    # f4 -- domain-size: max((domain_numer - domain_size) / domain_denom, domain_floor)
    domain_numer: float = 50.0
    domain_denom: float = 50.0
    domain_floor: float = 0.5

    # f5 -- subdomain penalty: 1 / (1 + subdomain_coef * n)
    subdomain_coef: float = 0.1

    # f6 -- digit penalty: 1 / (1 + digit_coef * n)
    digit_coef: float = 0.15

    # f7 -- url-len: max((url_len_numer - n) / url_len_denom, url_len_floor)
    url_len_numer: float = 150.0
    url_len_denom: float = 100.0
    url_len_floor: float = 0.5

    # f8 -- path depth: max(1 - path_slope * path_depth, path_floor)
    path_slope: float = 0.1
    path_floor: float = 0.4

    # f9 -- querystring: qmark_weight if '?' present else 1.0
    qmark_weight: float = 0.75

    # EXTRA (not in C++ heuristic -- CLI-only tunable boosts)
    # f10 -- wiki boost: multiplier applied if URL contains "wikipedia.org"
    wiki_boost: float = 1.0

    # bucket thresholds (descending; length 8)
    thresholds: np.ndarray = field(default_factory=lambda: np.array(
        [450_000, 300_000, 200_000, 100_000,
         50_000,  40_000,  30_000,  20_000], dtype=np.int64))


def parse_tld_weights(s: str, base: np.ndarray) -> np.ndarray:
    w = base.copy()
    for pair in s.split(","):
        pair = pair.strip()
        if not pair:
            continue
        if "=" not in pair:
            raise ValueError(f"--tld-weights pair missing '=': {pair!r}")
        k, v = pair.split("=", 1)
        k = k.strip().lower()
        if k not in TLD_NAME_TO_IDX:
            raise ValueError(f"unknown TLD {k!r}; must be one of {TLD_NAMES}")
        w[TLD_NAME_TO_IDX[k]] = float(v)
    return w


def parse_thresholds(s: str) -> np.ndarray:
    vals = [int(x.strip()) for x in s.split(",") if x.strip()]
    if len(vals) != NUM_BUCKETS:
        raise ValueError(
            f"--thresholds needs exactly {NUM_BUCKETS} comma-separated values, "
            f"got {len(vals)}")
    if any(vals[i] <= vals[i+1] for i in range(len(vals) - 1)):
        raise ValueError("--thresholds must be strictly descending")
    return np.array(vals, dtype=np.int64)


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
        fd.close()
        return

    try:
        total = mm.size()
        pos = 0

        # Skip anchor table
        num_anchors = u32(mm, pos)[0]; pos += 4
        for _ in range(num_anchors):
            alen = u32(mm, pos)[0]; pos += 4 + alen

        while pos < total:
            url_len = u32(mm, pos)[0];                 pos += 4
            url = mm[pos:pos + url_len];               pos += url_len
            pos += 4                                   # num_encountered
            seed_dist = u16(mm, pos)[0];               pos += 2
            pos += 2                                   # eot
            title_len = u64(mm, pos)[0];               pos += 8
            pos += title_len                           # title
            pos += 2                                   # eod
            domain_dist = u16(mm, pos)[0];             pos += 2
            crawled = mm[pos] != 0;                    pos += 1
            num_freqs = u32(mm, pos)[0];               pos += 4
            pos += num_freqs * 8                       # anchor_freqs entries
            yield url, seed_dist, domain_dist, crawled
    finally:
        mm.close()
        fd.close()


# -----------------------------------------------------------------------------
# Feature extraction (weight-independent) -- one pass over the URL.
# Replicates lib/Frontier.h parse EXACTLY, including the `len_ext += 1` on
# separator. We also record `tld_bug_hits_sep` so we can toggle the bug.
# -----------------------------------------------------------------------------

def extract_features(url, seed_dist):
    """
    Returns a 12-tuple of features, or None to skip.
        (seed_dist, https, tld_idx, tld_bug_hit, domain_size, subdomain,
         digits, url_len, path_depth, qmark, is_wiki)
    """
    n = len(url)
    if n < 8 or url[:4] != b"http":
        return None
    https = url[4] == 0x73  # 's'
    start_pos = 8 if https else 7
    if not https and url[4:7] != b"://":
        return None

    subdomain = 0
    digit_count = 0
    domain_size = 0
    path_depth = 0
    qmark = False
    start = 0
    len_ext = 0
    hit_sep = False

    i = start_pos
    while i < n:
        c = url[i]
        if c == 47 or c == 63 or c == 35 or c == 58:  # / ? # :
            hit_sep = True
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

    clean_ext = bytes(url[start:start + len_ext])
    tld_idx = TLD_BYTES_TO_IDX.get(clean_ext, TLD_OTHER)

    is_wiki = b"wikipedia.org" in url

    return (
        min(seed_dist, 65535),
        1 if https else 0,
        tld_idx,
        1 if hit_sep else 0,
        min(domain_size, 255),
        min(subdomain, 255),
        min(digit_count, 255),
        min(n, 65535),
        min(path_depth, 255),
        1 if qmark else 0,
        1 if is_wiki else 0,
    )


# -----------------------------------------------------------------------------
# Parse binary -> feature column arrays. Uses array.array for compact
# growable storage (avoids a two-pass count or 7GB of Python ints).
# -----------------------------------------------------------------------------

def parse_all(path, progress_every=1_000_000):
    cols = {
        "seed_dist":   pyarray.array("H"),
        "domain_dist": pyarray.array("H"),
        "crawled":     pyarray.array("B"),
        "is_wiki":     pyarray.array("B"),
        "https":       pyarray.array("B"),
        "tld_idx":     pyarray.array("B"),
        "tld_bug":     pyarray.array("B"),
        "domain_size": pyarray.array("B"),
        "subdomain":   pyarray.array("B"),
        "digits":      pyarray.array("B"),
        "url_len":     pyarray.array("H"),
        "path_depth":  pyarray.array("B"),
        "qmark":       pyarray.array("B"),
    }

    t0 = time.time()
    last_log = t0
    total = 0
    skipped = 0

    for url, sd, dd, cr in iter_urlstore(path):
        feats = extract_features(url, sd)
        if feats is None:
            skipped += 1
            continue
        (seed_dist, https, tld_idx, tld_bug, domain_size, subdomain,
         digits, url_len, path_depth, qmark, is_wiki) = feats
        cols["seed_dist"].append(seed_dist)
        cols["domain_dist"].append(dd)
        cols["crawled"].append(1 if cr else 0)
        cols["is_wiki"].append(is_wiki)
        cols["https"].append(https)
        cols["tld_idx"].append(tld_idx)
        cols["tld_bug"].append(tld_bug)
        cols["domain_size"].append(domain_size)
        cols["subdomain"].append(subdomain)
        cols["digits"].append(digits)
        cols["url_len"].append(url_len)
        cols["path_depth"].append(path_depth)
        cols["qmark"].append(qmark)
        total += 1

        if total % progress_every == 0:
            now = time.time()
            rate = progress_every / max(now - last_log, 1e-9)
            last_log = now
            elapsed = int(now - t0)
            print(f"[PROG] parsed {total:>12,}   elapsed={elapsed}s   "
                  f"rate={rate:,.0f}/s", flush=True)

    elapsed = time.time() - t0
    print(f"[INFO] parse done: {total:,} records ({skipped:,} skipped) "
          f"in {elapsed:.1f}s")

    # Convert array.array -> np.ndarray (zero-copy via frombuffer where dtype matches)
    out = {}
    for name, arr in cols.items():
        if arr.typecode == "H":
            out[name] = np.frombuffer(memoryview(arr).tobytes(), dtype=np.uint16).copy()
        else:  # 'B'
            out[name] = np.frombuffer(memoryview(arr).tobytes(), dtype=np.uint8).copy()
    return out


def save_cache(features, path):
    tmp = path + ".tmp"
    np.savez(tmp, **features)
    # savez adds .npz suffix if not present
    src = tmp if tmp.endswith(".npz") else tmp + ".npz"
    dst = path if path.endswith(".npz") else path + ".npz"
    os.replace(src, dst)
    print(f"[INFO] saved feature cache: {dst} ({os.path.getsize(dst)/1e6:.1f} MB)")


def load_cache(path):
    p = path if os.path.exists(path) else path + ".npz"
    if not os.path.exists(p):
        raise FileNotFoundError(p)
    data = np.load(p)
    return {k: data[k] for k in data.files}


# -----------------------------------------------------------------------------
# Vectorized scoring -- the whole point of the cache
# -----------------------------------------------------------------------------

def compute_scores(feats, w: Weights, fix_tld_bug: bool) -> np.ndarray:
    tld_idx = feats["tld_idx"].astype(np.int64)
    if fix_tld_bug:
        effective_tld = tld_idx
    else:
        # When the C++ loop hits a separator, `len_ext += 1` puts '/' into the
        # ext string, guaranteeing a TLD miss. We replicate that by forcing
        # tld_bug records to TLD_OTHER.
        tld_bug = feats["tld_bug"].astype(bool)
        effective_tld = np.where(tld_bug, TLD_OTHER, tld_idx)

    f2 = w.tld[effective_tld]

    f1 = np.where(feats["https"].astype(bool), 1.0, w.http_weight).astype(np.float32)

    seed_dist = feats["seed_dist"].astype(np.float32)
    f3 = np.maximum(np.exp(-w.seed_decay * seed_dist), w.seed_floor)

    domain_dist = feats["domain_dist"].astype(np.float32)
    f_dd = np.maximum(np.exp(-w.domain_dist_decay * domain_dist), w.domain_dist_floor)

    domain_size = feats["domain_size"].astype(np.float32)
    f4 = np.maximum((w.domain_numer - domain_size) / w.domain_denom, w.domain_floor)

    subdomain = feats["subdomain"].astype(np.float32)
    f5 = 1.0 / (1.0 + w.subdomain_coef * subdomain)

    digits = feats["digits"].astype(np.float32)
    f6 = 1.0 / (1.0 + w.digit_coef * digits)

    url_len = feats["url_len"].astype(np.float32)
    f7 = np.maximum((w.url_len_numer - url_len) / w.url_len_denom, w.url_len_floor)

    path_depth = feats["path_depth"].astype(np.float32)
    f8 = np.maximum(1.0 - w.path_slope * path_depth, w.path_floor)

    f9 = np.where(feats["qmark"].astype(bool), w.qmark_weight, 1.0).astype(np.float32)

    scores = f1 * f2 * f3 * f_dd * f4 * f5 * f6 * f7 * f8 * f9

    if w.wiki_boost != 1.0:
        is_wiki = feats["is_wiki"].astype(bool)
        scores = np.where(is_wiki, scores * w.wiki_boost, scores)

    return (scores * 1_000_000).astype(np.int64)


def compute_buckets(scores: np.ndarray, thresholds: np.ndarray) -> np.ndarray:
    """Vectorized bucket assignment. thresholds is descending length-8."""
    buckets = np.full(scores.shape, DROPPED, dtype=np.int8)
    # Walk from lowest threshold up -- later writes win, so we end up with
    # the highest bucket whose threshold the score meets.
    for i in range(len(thresholds) - 1, -1, -1):
        buckets = np.where(scores >= thresholds[i], i, buckets)
    return buckets


# -----------------------------------------------------------------------------
# Reporting
# -----------------------------------------------------------------------------

def _percentiles_from_hist(hist, percentiles=(1, 10, 25, 50, 75, 90, 99)):
    total = hist.sum()
    if total == 0:
        return {p: 0 for p in percentiles}
    cdf = np.cumsum(hist)
    result = {}
    for p in percentiles:
        target = total * p / 100.0
        idx = int(np.searchsorted(cdf, target))
        idx = min(idx, SCORE_BIN_COUNT)
        if idx == SCORE_BIN_COUNT:
            result[p] = SCORE_BIN_COUNT * SCORE_BIN_WIDTH
        else:
            result[p] = idx * SCORE_BIN_WIDTH + SCORE_BIN_WIDTH // 2
    return result


def bucket_label(i, thresholds):
    if i == DROPPED:
        return f"dropped (<{thresholds[-1]:,})"
    lo = int(thresholds[i])
    hi = int(thresholds[i - 1]) if i > 0 else None
    return (f"bucket {i} (>={lo:,})" if hi is None
            else f"bucket {i} ({lo:,}..{hi-1:,})")


class Stats:
    """Post-scoring summary over the feature arrays + computed scores."""
    def __init__(self, feats, scores, buckets, thresholds):
        total = feats["seed_dist"].size
        self.total = total
        self.thresholds = thresholds

        crawled_mask = feats["crawled"].astype(bool)
        wiki_mask    = feats["is_wiki"].astype(bool)
        wiki_crawled = crawled_mask & wiki_mask

        self.crawled      = int(crawled_mask.sum())
        self.wiki         = int(wiki_mask.sum())
        self.wiki_crawled = int(wiki_crawled.sum())

        self.score_sum  = int(scores.sum())
        self.wscore_sum = int(scores[wiki_mask].sum()) if self.wiki else 0

        # bucket histograms
        nb = NUM_BUCKETS + 1
        self.bucket       = np.bincount(buckets, minlength=nb)[:nb]
        self.bucket_crwl  = np.bincount(buckets[crawled_mask], minlength=nb)[:nb]
        self.wbucket      = np.bincount(buckets[wiki_mask], minlength=nb)[:nb]
        self.wbucket_crwl = np.bincount(buckets[wiki_crawled], minlength=nb)[:nb]

        # score histograms (bin by SCORE_BIN_WIDTH)
        bins = np.minimum(scores // SCORE_BIN_WIDTH, SCORE_BIN_COUNT).astype(np.int64)
        self.score_hist  = np.bincount(bins, minlength=SCORE_BIN_COUNT + 1)
        self.wscore_hist = np.bincount(bins[wiki_mask], minlength=SCORE_BIN_COUNT + 1)


def format_report(s: Stats, w: Weights, fix_tld_bug: bool) -> str:
    lines = []
    p = lines.append
    total = s.total or 1
    wtotal = s.wiki or 1

    p("=" * 72)
    p(" URL STORE FRONTIER-SCORE ANALYSIS")
    p("=" * 72)
    p(" WEIGHTS IN EFFECT")
    p(" " + "-" * 70)
    p(f"   tld                 {dict(zip(TLD_NAMES, [round(float(x),3) for x in w.tld]))}")
    p(f"   http (f1)           {w.http_weight}")
    p(f"   seed_decay (f3)     {w.seed_decay}   floor={w.seed_floor}")
    p(f"   domain_dist (f_dd)  decay={w.domain_dist_decay}   floor={w.domain_dist_floor}")
    p(f"   domain (f4)         numer={w.domain_numer} denom={w.domain_denom} "
      f"floor={w.domain_floor}")
    p(f"   subdomain (f5)      coef={w.subdomain_coef}")
    p(f"   digit (f6)          coef={w.digit_coef}")
    p(f"   url_len (f7)        numer={w.url_len_numer} denom={w.url_len_denom} "
      f"floor={w.url_len_floor}")
    p(f"   path_depth (f8)     slope={w.path_slope} floor={w.path_floor}")
    p(f"   qmark (f9)          {w.qmark_weight}")
    p(f"   wiki_boost (f10)    {w.wiki_boost}")
    p(f"   fix_tld_bug         {fix_tld_bug}")
    p(f"   thresholds          {list(int(t) for t in w.thresholds)}")
    p("")
    p(f" Total URL records:            {s.total:>15,}")
    p(f"   of which crawled:           {s.crawled:>15,}"
      f"   ({s.crawled / total * 100:6.2f}%)")
    p(f" Wikipedia URLs:               {s.wiki:>15,}"
      f"   ({s.wiki / total * 100:6.3f}%)")
    p(f"   of which crawled:           {s.wiki_crawled:>15,}"
      f"   ({s.wiki_crawled / wtotal * 100:6.2f}%)")
    p("")
    p(" NOTE ON THE TLD BUG")
    p(" " + "-" * 70)
    p(" lib/Frontier.h:70 does `len_ext += 1` when the URL-domain loop hits")
    p(" a separator ('/', '?', '#', ':'), which leaks that separator into")
    p(" the TLD substring (e.g. 'org/' instead of 'org'). The TLD map misses,")
    p(" so factor_2 = 0.6 (other) for EVERY URL with a non-empty path --")
    p(" effectively disabling the .gov/.edu/.org bonus in the live crawler.")
    p(" Pass --fix-tld-bug to see what scores look like without the bug.")
    p("")
    p(" BUCKET DISTRIBUTION (all URLs)")
    p(" " + "-" * 70)
    p(f" {'bucket':<32}{'count':>14}{'share':>10}"
      f"{'crawled':>12}{'crwl%':>8}")
    for i in range(NUM_BUCKETS + 1):
        c = int(s.bucket[i])
        cr = int(s.bucket_crwl[i])
        share = c / total * 100
        crwl = (cr / c * 100) if c else 0.0
        p(f" {bucket_label(i, w.thresholds):<32}{c:>14,}{share:>9.2f}%"
          f"{cr:>12,}{crwl:>7.2f}%")
    p("")
    p(" BUCKET DISTRIBUTION (wikipedia.org only)")
    p(" " + "-" * 70)
    p(f" {'bucket':<32}{'count':>14}{'share':>10}"
      f"{'crawled':>12}{'crwl%':>8}")
    for i in range(NUM_BUCKETS + 1):
        c = int(s.wbucket[i])
        cr = int(s.wbucket_crwl[i])
        share = c / wtotal * 100
        crwl = (cr / c * 100) if c else 0.0
        p(f" {bucket_label(i, w.thresholds):<32}{c:>14,}{share:>9.2f}%"
          f"{cr:>12,}{crwl:>7.2f}%")
    p("")
    p(" SCORE PERCENTILES")
    p(" " + "-" * 70)
    all_pct = _percentiles_from_hist(s.score_hist)
    wiki_pct = _percentiles_from_hist(s.wscore_hist)
    p(f" {'percentile':<12}{'all URLs':>14}{'wikipedia':>14}")
    for pc in (1, 10, 25, 50, 75, 90, 99):
        p(f" P{pc:<11}{all_pct[pc]:>14,}{wiki_pct[pc]:>14,}")
    p("")
    mean_all = s.score_sum / total
    mean_wiki = s.wscore_sum / wtotal
    p(f" mean score  all={mean_all:,.0f}   wiki={mean_wiki:,.0f}   "
      f"wiki/all={mean_wiki/max(mean_all,1):.2f}x")
    p("=" * 72)
    return "\n".join(lines)


# -----------------------------------------------------------------------------
# Plotting
# -----------------------------------------------------------------------------

def plot_bucket_distribution(s: Stats, out_path: str):
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 5))
    xs = list(range(NUM_BUCKETS + 1))
    labels = [f"b{i}" for i in range(NUM_BUCKETS)] + ["drop"]

    ax1.bar(xs, s.bucket, color="#4c72b0")
    ax1.set_yscale("log")
    ax1.set_title("All URLs -- bucket distribution (log y)")
    ax1.set_xticks(xs); ax1.set_xticklabels(labels)
    ax1.set_xlabel("priority bucket (0 = elite)"); ax1.set_ylabel("count")
    ax1.grid(True, alpha=0.3)

    ax2.bar(xs, s.wbucket, color="#dd8452")
    if s.wbucket.sum() > 0:
        ax2.set_yscale("log")
    ax2.set_title(f"Wikipedia URLs -- bucket distribution (N={s.wiki:,})")
    ax2.set_xticks(xs); ax2.set_xticklabels(labels)
    ax2.set_xlabel("priority bucket (0 = elite)"); ax2.set_ylabel("count")
    ax2.grid(True, alpha=0.3)

    fig.tight_layout(); fig.savefig(out_path, dpi=130); plt.close(fig)


def plot_score_histogram(s: Stats, thresholds, out_path: str):
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 5))
    edges = np.arange(SCORE_BIN_COUNT + 2) * SCORE_BIN_WIDTH
    centers = edges[:-1] + SCORE_BIN_WIDTH / 2

    ax1.bar(centers, s.score_hist, width=SCORE_BIN_WIDTH * 0.95, color="#4c72b0")
    ax1.set_yscale("log")
    ax1.set_title("All URLs -- score distribution (log y)")
    ax1.set_xlabel("priority score"); ax1.set_ylabel("count")
    for t in thresholds:
        ax1.axvline(int(t), color="red", linewidth=0.5, alpha=0.5)
    ax1.grid(True, alpha=0.3)

    ax2.bar(centers, s.wscore_hist, width=SCORE_BIN_WIDTH * 0.95, color="#dd8452")
    if s.wscore_hist.sum() > 0:
        ax2.set_yscale("log")
    ax2.set_title(f"Wikipedia URLs -- score distribution (N={s.wiki:,})")
    ax2.set_xlabel("priority score"); ax2.set_ylabel("count")
    for t in thresholds:
        ax2.axvline(int(t), color="red", linewidth=0.5, alpha=0.5)
    ax2.grid(True, alpha=0.3)

    fig.tight_layout(); fig.savefig(out_path, dpi=130); plt.close(fig)


def plot_wikipedia_crawled(s: Stats, out_path: str):
    fig, ax = plt.subplots(figsize=(10, 5))
    xs = list(range(NUM_BUCKETS + 1))
    labels = [f"b{i}" for i in range(NUM_BUCKETS)] + ["drop"]
    uncrawled = s.wbucket - s.wbucket_crwl

    ax.bar(xs, s.wbucket_crwl, color="#55a868", label="crawled")
    ax.bar(xs, uncrawled, bottom=s.wbucket_crwl, color="#c44e52", label="uncrawled")
    if s.wbucket.sum() > 0:
        ax.set_yscale("log")
    ax.set_xticks(xs); ax.set_xticklabels(labels)
    ax.set_xlabel("priority bucket (0 = elite)"); ax.set_ylabel("count")
    ax.set_title(f"Wikipedia URLs per bucket: crawled vs uncrawled "
                 f"(N={s.wiki:,}, crawled={s.wiki_crawled:,})")
    ax.legend(); ax.grid(True, alpha=0.3, axis="y")

    fig.tight_layout(); fig.savefig(out_path, dpi=130); plt.close(fig)


# -----------------------------------------------------------------------------
# Entrypoint
# -----------------------------------------------------------------------------

def build_weights_from_args(args) -> Weights:
    w = Weights()
    if args.tld_weights:
        w.tld = parse_tld_weights(args.tld_weights, w.tld).astype(np.float32)
    if args.thresholds:
        w.thresholds = parse_thresholds(args.thresholds)

    # scalar overrides
    for field_name in ("http_weight", "seed_decay", "seed_floor",
                       "domain_dist_decay", "domain_dist_floor",
                       "domain_numer", "domain_denom", "domain_floor",
                       "subdomain_coef", "digit_coef",
                       "url_len_numer", "url_len_denom", "url_len_floor",
                       "path_slope", "path_floor",
                       "qmark_weight", "wiki_boost"):
        v = getattr(args, field_name, None)
        if v is not None:
            setattr(w, field_name, v)
    return w


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)

    # paths
    ap.add_argument("--path", default="/var/seamus/urlstore_output/urlstore.txt",
                    help="path to urlstore binary (only needed on first run)")
    ap.add_argument("--out", default="./urlstore_analysis",
                    help="output directory for PNGs + report")
    ap.add_argument("--cache", default=None,
                    help="npz feature cache path. If provided and exists, load "
                         "from it instead of re-parsing the binary. Parse once, "
                         "rescore many times.")
    ap.add_argument("--rebuild-cache", action="store_true",
                    help="ignore existing cache and re-parse the binary")
    ap.add_argument("--progress-every", type=int, default=1_000_000)

    # bug toggle (TLD bug is FIXED by default in this script)
    ap.add_argument("--replicate-tld-bug", action="store_true",
                    help="replicate the C++ TLD-substring bug (factor_2 = 0.6 "
                         "for every URL with a path). Default: bug is FIXED.")

    # weights
    ap.add_argument("--tld-weights", default=None,
                    help="comma-separated key=val pairs, keys in "
                         f"{TLD_NAMES}. Unlisted keys keep their defaults.")
    ap.add_argument("--thresholds", default=None,
                    help=f"{NUM_BUCKETS} descending comma-separated ints, "
                         "e.g. 500000,380000,280000,200000,140000,90000,60000,40000")
    ap.add_argument("--http-weight",     type=float)
    ap.add_argument("--seed-decay",      type=float, dest="seed_decay")
    ap.add_argument("--seed-floor",      type=float, dest="seed_floor")
    ap.add_argument("--domain-dist-decay", type=float, dest="domain_dist_decay",
                    help="exponential decay rate for domain distance. "
                         "0 = disabled (default). Try 0.3-0.5 for strong domain bias.")
    ap.add_argument("--domain-dist-floor", type=float, dest="domain_dist_floor",
                    help="minimum domain-distance factor. Default 1.0 (disabled).")
    ap.add_argument("--domain-numer",    type=float, dest="domain_numer")
    ap.add_argument("--domain-denom",    type=float, dest="domain_denom")
    ap.add_argument("--domain-floor",    type=float, dest="domain_floor")
    ap.add_argument("--subdomain-coef",  type=float, dest="subdomain_coef")
    ap.add_argument("--digit-coef",      type=float, dest="digit_coef")
    ap.add_argument("--url-len-numer",   type=float, dest="url_len_numer")
    ap.add_argument("--url-len-denom",   type=float, dest="url_len_denom")
    ap.add_argument("--url-len-floor",   type=float, dest="url_len_floor")
    ap.add_argument("--path-slope",      type=float, dest="path_slope")
    ap.add_argument("--path-floor",      type=float, dest="path_floor")
    ap.add_argument("--qmark-weight",    type=float, dest="qmark_weight")
    ap.add_argument("--wiki-boost",      type=float, dest="wiki_boost",
                    help="multiplier applied to any URL containing "
                         "'wikipedia.org'. Default 1.0 (disabled). Try 2.0-3.0 "
                         "to force wiki URLs into higher buckets.")
    args = ap.parse_args()

    os.makedirs(args.out, exist_ok=True)
    weights = build_weights_from_args(args)
    print(f"[INFO] python={sys.version.split()[0]} numpy={np.__version__}")

    # --- Parse or load features ---
    feats = None
    if args.cache:
        cache_exists = os.path.exists(args.cache) or os.path.exists(args.cache + ".npz")
        if cache_exists and not args.rebuild_cache:
            print(f"[INFO] loading feature cache from {args.cache}")
            t0 = time.time()
            feats = load_cache(args.cache)
            n = feats["seed_dist"].size
            print(f"[INFO] loaded {n:,} records in {time.time()-t0:.1f}s")

    if feats is None:
        if not os.path.exists(args.path):
            print(f"[ERR] {args.path} does not exist and no cache to load",
                  file=sys.stderr)
            sys.exit(1)
        sz = os.path.getsize(args.path)
        print(f"[INFO] parsing {args.path} ({sz / 1e9:.2f} GB)")
        feats = parse_all(args.path, progress_every=args.progress_every)
        if args.cache:
            save_cache(feats, args.cache)

    # --- Score + bucket (vectorized, ~seconds for 50M) ---
    t0 = time.time()
    fix_tld = not args.replicate_tld_bug
    scores = compute_scores(feats, weights, fix_tld_bug=fix_tld)
    buckets = compute_buckets(scores, weights.thresholds)
    print(f"[INFO] score+bucket done in {time.time()-t0:.1f}s")

    t0 = time.time()
    stats = Stats(feats, scores, buckets, weights.thresholds)
    print(f"[INFO] stats accumulated in {time.time()-t0:.1f}s")

    report = format_report(stats, weights, fix_tld)
    print()
    print(report)
    with open(os.path.join(args.out, "report.txt"), "w") as f:
        f.write(report + "\n")

    print("[INFO] rendering plots...")
    plot_bucket_distribution(stats, os.path.join(args.out, "bucket_distribution.png"))
    plot_score_histogram(stats, weights.thresholds,
                         os.path.join(args.out, "score_histogram.png"))
    plot_wikipedia_crawled(stats, os.path.join(args.out, "wikipedia_crawled.png"))
    print(f"[INFO] done. outputs in {args.out}/")


if __name__ == "__main__":
    main()
