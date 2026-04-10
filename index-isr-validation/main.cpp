#include "index/Index.h"
#include "lib/consts.h"
#include "lib/logger.h"
#include "lib/string.h"
#include "lib/vector.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <random>
#include <sys/stat.h>
#include <unistd.h>


// ---- Benchmark configuration ----------------------------------------------

namespace bench {

constexpr size_t NUM_FILES       = 10;
constexpr size_t DOCS_PER_FILE   = 10;
constexpr size_t WORDS_PER_DOC   = 1000;

// Size of the per-letter word pool we draw document words from.
constexpr size_t WORDS_PER_LETTER = 100;
constexpr size_t MIN_WORD_LEN     = 3;
constexpr size_t MAX_WORD_LEN     = 10;

// Fixed seed so the benchmark is reproducible.
constexpr uint64_t RNG_SEED = 0xBEEFCAFEULL;

// Where to write the generated parser-format doc files.
constexpr const char* BENCH_DOC_DIR = "/tmp/index_isr_validation";

} // namespace bench


// ---- Word pool -------------------------------------------------------------
//
// A single flat vector of words. Entries are grouped by starting letter:
// [0 .. WORDS_PER_LETTER)          start with 'a'
// [WORDS_PER_LETTER .. 2*WPL)      start with 'b'
// ...
// All words are pure lowercase [a-z].

struct WordPool {
    vector<string> words;
};


static WordPool build_word_pool(std::mt19937_64& rng) {
    WordPool pool;
    pool.words.reserve(26 * bench::WORDS_PER_LETTER);

    std::uniform_int_distribution<size_t> len_dist(bench::MIN_WORD_LEN, bench::MAX_WORD_LEN);
    std::uniform_int_distribution<int>    tail_dist('a', 'z');

    char buf[bench::MAX_WORD_LEN];

    for (int letter = 0; letter < 26; ++letter) {
        for (size_t i = 0; i < bench::WORDS_PER_LETTER; ++i) {
            size_t len = len_dist(rng);
            buf[0] = static_cast<char>('a' + letter);
            for (size_t k = 1; k < len; ++k) {
                buf[k] = static_cast<char>(tail_dist(rng));
            }
            pool.words.push_back(string(buf, len));
        }
    }
    return pool;
}


static void log_word_pool(const WordPool& pool) {
    char line[4096];
    for (int letter = 0; letter < 26; ++letter) {
        size_t pos = 0;
        line[pos++] = static_cast<char>('a' + letter);
        line[pos++] = ':';
        size_t start = letter * bench::WORDS_PER_LETTER;
        size_t end   = start + bench::WORDS_PER_LETTER;
        for (size_t i = start; i < end && pos + pool.words[i].size() + 2 < sizeof(line); ++i) {
            line[pos++] = ' ';
            memcpy(line + pos, pool.words[i].data(), pool.words[i].size());
            pos += pool.words[i].size();
        }
        line[pos] = '\0';
        logger::instr("%s", line);
    }
}


// ---- Corpus ----------------------------------------------------------------
//
// A Corpus holds the ground-truth sequence of words for every generated
// document. Layout:
//
//   urls[g]      = URL for global doc g (g = file*DOCS_PER_FILE + doc_in_file)
//   doc_words[g] = sequence of pool indices (one per word position) for doc g
//
// The order inside each doc_words[g] is exactly the order we write to the
// parser-format file, which is also the order IndexChunk will observe during
// indexing. Later, we can rebuild the expected doc from the index and diff
// against this.

struct Corpus {
    vector<string> urls;
    vector<vector<uint32_t>> doc_words;
};


static Corpus generate_corpus(const WordPool& pool, std::mt19937_64& rng) {
    Corpus c;
    const size_t total_docs = bench::NUM_FILES * bench::DOCS_PER_FILE;
    c.urls.reserve(total_docs);
    c.doc_words.reserve(total_docs);

    std::uniform_int_distribution<uint32_t> word_dist(0, pool.words.size() - 1);

    char url_buf[64];
    for (size_t f = 0; f < bench::NUM_FILES; ++f) {
        for (size_t d = 0; d < bench::DOCS_PER_FILE; ++d) {
            int n = snprintf(url_buf, sizeof(url_buf), "http://bench.local/f%zu/d%zu", f, d);
            c.urls.push_back(string(url_buf, static_cast<size_t>(n)));

            vector<uint32_t> words;
            words.reserve(bench::WORDS_PER_DOC);
            for (size_t w = 0; w < bench::WORDS_PER_DOC; ++w) {
                words.push_back(word_dist(rng));
            }
            c.doc_words.push_back(static_cast<vector<uint32_t>&&>(words));
        }
    }
    return c;
}


// ---- File writing ----------------------------------------------------------
//
// Parser output format, per IndexChunk::index_file():
//
//   <doc>\n
//   <url>\n
//   word1\n
//   word2\n
//   ...
//   </doc>\n

static string doc_file_path(size_t file_idx) {
    return string::join("",
                        string(bench::BENCH_DOC_DIR),
                        "/docs_",
                        string(static_cast<uint32_t>(file_idx)),
                        ".txt");
}


// Remove all regular files inside `dir` and then the directory itself.
// Non-recursive: assumes the bench dir only ever contains flat files.
static void cleanup_dir(const char* dir) {
    DIR* d = opendir(dir);
    if (d == nullptr) {
        if (errno != ENOENT) {
            logger::error("opendir %s failed (errno=%d: %s)", dir, errno, strerror(errno));
        }
        return;
    }

    char path[1024];
    struct dirent* entry;
    while ((entry = readdir(d)) != nullptr) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
        if (unlink(path) != 0) {
            logger::error("unlink %s failed (errno=%d: %s)", path, errno, strerror(errno));
        }
    }
    closedir(d);

    if (rmdir(dir) != 0 && errno != ENOENT) {
        logger::error("rmdir %s failed (errno=%d: %s)", dir, errno, strerror(errno));
    }
}


// mkdir -p equivalent: create each path component in turn, ignoring EEXIST.
static void mkdir_p(const char* path) {
    char buf[512];
    size_t n = strlen(path);
    if (n >= sizeof(buf)) return;
    memcpy(buf, path, n + 1);
    for (size_t i = 1; i < n; ++i) {
        if (buf[i] == '/') {
            buf[i] = '\0';
            if (mkdir(buf, 0755) != 0 && errno != EEXIST) {
                logger::error("mkdir %s failed (errno=%d: %s)", buf, errno, strerror(errno));
            }
            buf[i] = '/';
        }
    }
    if (mkdir(buf, 0755) != 0 && errno != EEXIST) {
        logger::error("mkdir %s failed (errno=%d: %s)", buf, errno, strerror(errno));
    }
}


static void write_corpus_files(const WordPool& pool, const Corpus& c) {
    mkdir_p(bench::BENCH_DOC_DIR);

    for (size_t f = 0; f < bench::NUM_FILES; ++f) {
        string path = doc_file_path(f);
        FILE* fd = fopen(path.data(), "w");
        if (fd == nullptr) {
            logger::error("failed to open %s for writing (errno=%d: %s)",
                          path.data(), errno, strerror(errno));
            return;
        }

        for (size_t d = 0; d < bench::DOCS_PER_FILE; ++d) {
            size_t g = f * bench::DOCS_PER_FILE + d;

            fputs("<doc>\n", fd);
            fwrite(c.urls[g].data(), 1, c.urls[g].size(), fd);
            fputc('\n', fd);

            const vector<uint32_t>& words = c.doc_words[g];
            for (size_t w = 0; w < words.size(); ++w) {
                const string& word = pool.words[words[w]];
                fwrite(word.data(), 1, word.size(), fd);
                fputc('\n', fd);
            }

            fputs("</doc>\n", fd);
        }

        fclose(fd);
        logger::instr("wrote %s (%zu docs)", path.data(), bench::DOCS_PER_FILE);
    }
}


// ---- Indexing worker -------------------------------------------------------
//
// Mirrors index/main.cpp::worker but:
//   - runs on a single thread (worker 0)
//   - iterates the benchmark's doc files directly
//   - always calls flush() at the end (our NUM_FILES * DOCS_PER_FILE docs never hit the
//     DOCS_PER_INDEX_CHUNK=500k auto-flush threshold)
//   - times the in-memory indexing and the persist-to-disk phases separately

struct BenchTimings {
    double index_ms;
    double flush_ms;
};


static BenchTimings run_index_worker() {
    using clock = std::chrono::steady_clock;

    IndexChunk idx(0);

    auto t0 = clock::now();
    for (size_t f = 0; f < bench::NUM_FILES; ++f) {
        string path = doc_file_path(f);
        if (!idx.index_file(path)) {
            logger::error("index_file failed for %s", path.data());
        }
    }
    auto t1 = clock::now();
    idx.flush();
    auto t2 = clock::now();

    BenchTimings out;
    out.index_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    out.flush_ms = std::chrono::duration<double, std::milli>(t2 - t1).count();
    return out;
}


// Remove stale worker-0 chunk files so IndexChunk's constructor starts at
// chunk 0 and persist() doesn't hit `wx` EEXIST.
static void cleanup_worker0_chunks() {
    DIR* d = opendir(INDEX_OUTPUT_DIR);
    if (d == nullptr) {
        if (errno != ENOENT) {
            logger::error("opendir %s failed (errno=%d: %s)", INDEX_OUTPUT_DIR, errno, strerror(errno));
        }
        return;
    }

    const char* prefix = "index_chunk_0_";
    const size_t prefix_len = strlen(prefix);
    char path[1024];

    struct dirent* entry;
    while ((entry = readdir(d)) != nullptr) {
        if (strncmp(entry->d_name, prefix, prefix_len) != 0) continue;
        snprintf(path, sizeof(path), "%s/%s", INDEX_OUTPUT_DIR, entry->d_name);
        if (unlink(path) != 0) {
            logger::error("unlink %s failed (errno=%d: %s)", path, errno, strerror(errno));
        }
    }
    closedir(d);
}


int main(int /*argc*/, char* /*argv*/[]) {
    logger::instr("index-isr-validation: starting");

    cleanup_dir(bench::BENCH_DOC_DIR);
    mkdir_p(INDEX_OUTPUT_DIR);
    cleanup_worker0_chunks();

    std::mt19937_64 rng(bench::RNG_SEED);

    WordPool pool = build_word_pool(rng);
    logger::instr("Built word pool: 26 letters x %zu words", bench::WORDS_PER_LETTER);
    log_word_pool(pool);

    Corpus corpus = generate_corpus(pool, rng);
    logger::instr("Generated corpus: %zu files x %zu docs x %zu words",
                  bench::NUM_FILES, bench::DOCS_PER_FILE, bench::WORDS_PER_DOC);

    write_corpus_files(pool, corpus);

    BenchTimings t = run_index_worker();
    const size_t total_docs  = bench::NUM_FILES * bench::DOCS_PER_FILE;
    const size_t total_words = total_docs * bench::WORDS_PER_DOC;
    logger::instr("Index (in-mem):  %.3f ms", t.index_ms);
    logger::instr("Flush (persist): %.3f ms", t.flush_ms);
    logger::instr("Total:           %.3f ms  (%zu docs, %zu words)",
                  t.index_ms + t.flush_ms, total_docs, total_words);

    cleanup_dir(bench::BENCH_DOC_DIR);
    cleanup_worker0_chunks();

    return 0;
}
