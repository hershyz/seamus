#include "index/Index.h"
#include "lib/logger.h"
#include "lib/string.h"
#include "lib/vector.h"

#include <cstdint>
#include <random>


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

} // namespace bench


// ---- Word set --------------------------------------------------------------
//
// 26 buckets, one per starting letter. All words are pure lowercase [a-z].
// Each bucket holds WORDS_PER_LETTER distinct-ish words starting with that
// letter. Document words are sampled uniformly from one of these buckets.

struct WordSet {
    vector<string> by_letter[26];
};


static WordSet build_word_set(std::mt19937_64& rng) {
    WordSet set;

    std::uniform_int_distribution<size_t> len_dist(bench::MIN_WORD_LEN, bench::MAX_WORD_LEN);
    std::uniform_int_distribution<int>    tail_dist('a', 'z');

    char buf[bench::MAX_WORD_LEN];

    for (int letter = 0; letter < 26; ++letter) {
        set.by_letter[letter].reserve(bench::WORDS_PER_LETTER);
        for (size_t i = 0; i < bench::WORDS_PER_LETTER; ++i) {
            size_t len = len_dist(rng);
            buf[0] = static_cast<char>('a' + letter);
            for (size_t k = 1; k < len; ++k) {
                buf[k] = static_cast<char>(tail_dist(rng));
            }
            set.by_letter[letter].push_back(string(buf, len));
        }
    }
    return set;
}


static void log_word_set(const WordSet& set) {
    char line[1024];
    for (int letter = 0; letter < 26; ++letter) {
        size_t pos = 0;
        line[pos++] = static_cast<char>('a' + letter);
        line[pos++] = ':';
        const vector<string>& bucket = set.by_letter[letter];
        for (size_t i = 0; i < bucket.size() && pos + bucket[i].size() + 2 < sizeof(line); ++i) {
            line[pos++] = ' ';
            memcpy(line + pos, bucket[i].data(), bucket[i].size());
            pos += bucket[i].size();
        }
        line[pos] = '\0';
        logger::instr("%s", line);
    }
}


int main(int /*argc*/, char* /*argv*/[]) {
    logger::instr("index-isr-validation: starting");

    std::mt19937_64 rng(bench::RNG_SEED);

    WordSet word_set = build_word_set(rng);
    logger::instr("Built word set: 26 letters x %zu words", bench::WORDS_PER_LETTER);
    log_word_set(word_set);

    return 0;
}
