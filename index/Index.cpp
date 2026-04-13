#include "Index.h"
#include "lib/consts.h"
#include "lib/algorithm.h"
#include "lib/string.h"
#include "lib/utf8.h"
#include "lib/utils.h"
#include "lib/logger.h"
#include <sys/stat.h>


string IndexChunk::get_index_chunk_path() const {
    return string::join("", string(INDEX_OUTPUT_DIR), "/index_chunk_", string(WORKER_NUMBER), "_", string(chunk), ".txt");
}


IndexChunk::IndexChunk(uint32_t worker_number) : curr_doc_(1), chunk(0), doc_count(0), WORKER_NUMBER(worker_number){
    // Important: Init curr_doc_ to 1 to allow for 00 to be used as new doc flag
    // Find the latest chunk ID for this thread
    while (file_exists(get_index_chunk_path())) chunk++;
}


void IndexChunk::persist() {
    mkdir(INDEX_OUTPUT_DIR, 0755); // no-op if already exists
    // Create a file (if it already exists, fail -- don't want to overwrite)
    string path = get_index_chunk_path();
    FILE* fd = fopen(path.data(), "wx");

    if (fd == nullptr) {
        logger::error("Worker %u: failed to open '%s' for writing (errno=%d: %s)", WORKER_NUMBER, path.data(), errno, strerror(errno));
        return;
    }

    // ---- URL table ----
    // <64b SIZE>\n
    // <32b ID> <varlen URL>\n  (repeated)
    // \n

    uint64_t urls_bytes = 0;
    for (size_t i = 0; i < urls.size(); i++) urls_bytes += 4 + 1 + urls[i].size() + 1;

    fwrite(&urls_bytes, sizeof(urls_bytes), 1, fd);
    fwrite("\n", sizeof(char), 1, fd);

    for (uint32_t i = 0; i < urls.size(); i++) {
        uint32_t id = i + 1; // 1 indexed
        fwrite(&id, sizeof(id), 1, fd);
        fwrite(" ", sizeof(char), 1, fd);
        fwrite(urls[i].data(), sizeof(char), urls[i].size(), fd);
        fwrite("\n", sizeof(char), 1, fd);
    }

    fwrite("\n", sizeof(char), 1, fd);

    // ---- Dictionary + posting list layout ----

    vector<string> alphabetized_entries = sort_entries();
    const uint32_t N = alphabetized_entries.size();

    // First pass: compute dictionary offsets and posting list locations/sizes

    vector<uint64_t> posting_list_locations(N);
    uint64_t posting_list_size = 0;

    uint64_t dict_offsets[26];
    dict_offsets[0] = 0;
    uint64_t curr_offset = 0;
    char curr_char = 'a';

    for (uint32_t i = 0; i < N; i++) {
        if (alphabetized_entries[i][0] > curr_char) {
            curr_char = alphabetized_entries[i][0];
            dict_offsets[curr_char - 'a'] = curr_offset;
        }

        // Dictionary entry: <word> <1B space> <8B offset> <1B newline>
        curr_offset += alphabetized_entries[i].size() + 1 + sizeof(uint64_t) + 1;

        posting_list_locations[i] = posting_list_size;

        postings& entry = index[alphabetized_entries[i].str_view(0, alphabetized_entries[i].size())];

        // Header: <8B num_posts> <1B space> <4B n_docs> <1B newline>
        posting_list_size += sizeof(uint64_t) + 1 + sizeof(uint32_t) + 1;

        // Posts
        uint32_t last_doc = 0;
        uint32_t last_loc = 0;

        for (post p : entry.posts) {
            uint64_t post_size = SizeOfUtf8(p.loc - last_loc);

            if (p.doc > last_doc) {
                post_size += 1 + SizeOfUtf8(p.doc - last_doc);
                last_doc = p.doc;
                last_loc = 0;
            } else {
                last_loc = p.loc;
            }

            posting_list_size += post_size;
        }

        // Trailing newline per word
        posting_list_size += 1;
    }

    // ---- Write dictionary TOC ----
    // <1B letter> <1B space> <8B offset> <1B newline>  (x26)
    // \n

    for (int i = 0; i < 26; i++) {
        char c = char(i + 'a');
        fwrite(&c, sizeof(char), 1, fd);
        fwrite(" ", sizeof(char), 1, fd);
        fwrite(dict_offsets + i, sizeof(uint64_t), 1, fd);
        fwrite("\n", sizeof(char), 1, fd);
    }

    fwrite("\n", sizeof(char), 1, fd);

    // ---- Write dictionary ----
    // <varlen word> <1B space> <8B offset> <1B newline>  (per word)
    // \n

    for (uint32_t i = 0; i < N; i++) {
        fwrite(alphabetized_entries[i].data(), sizeof(char), alphabetized_entries[i].size(), fd);
        fwrite(" ", sizeof(char), 1, fd);
        fwrite(&posting_list_locations[i], sizeof(uint64_t), 1, fd);
        fwrite("\n", sizeof(char), 1, fd);
    }

    fwrite("\n", sizeof(char), 1, fd);

    // ---- Write posting lists ----
    // Per word:
    //   <8B num_posts> <1B space> <4B n_docs> <1B newline>
    //   <posts: utf8-encoded deltas>
    //   <1B newline>

    for (uint32_t i = 0; i < N; i++) {
        postings& entry = index[alphabetized_entries[i].str_view(0, alphabetized_entries[i].size())];
        uint64_t size = entry.posts.size();

        // Header
        fwrite(&size, sizeof(uint64_t), 1, fd);
        fwrite(" ", sizeof(char), 1, fd);
        fwrite(&entry.n_docs, sizeof(uint32_t), 1, fd);
        fwrite("\n", sizeof(char), 1, fd);

        // Posts
        uint32_t last_doc = 0;
        uint32_t last_loc = 0;

        Utf8 doc_buff[MAX_UTF8_LEN + 1];
        Utf8 loc_buff[MAX_UTF8_LEN];
        doc_buff[0] = 0; // flag byte for new doc

        for (post p : entry.posts) {
            if (p.doc > last_doc) {
                Utf8* doc_end = WriteUtf8(doc_buff + 1, p.doc - last_doc, doc_buff + MAX_UTF8_LEN + 1);
                fwrite(doc_buff, sizeof(Utf8), doc_end - doc_buff, fd);
                last_loc = 0;
            }

            Utf8* loc_end = WriteUtf8(loc_buff, p.loc - last_loc, loc_buff + MAX_UTF8_LEN);
            fwrite(loc_buff, sizeof(Utf8), loc_end - loc_buff, fd);

            last_loc = p.loc;
            if (p.doc > last_doc) {
                last_doc = p.doc;
            }
        }

        fwrite("\n", sizeof(char), 1, fd);
    }

    fclose(fd);
    chunk++;
}

void IndexChunk::reset() {
    index = unordered_map<string, postings>();
    urls = vector<string>();
    doc_count = 0;
    curr_doc_ = 1; // Curr_doc must start at 1, 0 reserved for flag
}


void IndexChunk::flush() {
    persist();
    reset();
    logger::info("Worker %u: flush chunk: %u", WORKER_NUMBER, chunk-1);
}


vector<string> IndexChunk::sort_entries() {
    vector<string> res;
    res.reserve(index.size());

    for (auto it = index.begin(); it != index.end(); ++it) {
        res.push_back(string((*it).key.data(), (*it).key.size()));
    }

    radix_sort(res);
    return res;
}

bool IndexChunk::index_file(const string &path) {
    FILE* fd = fopen(path.data(), "r");
    if (fd == nullptr) {
        perror("Error opening file.\n");
        return false;
    }

    char buff[4096];
    char url[2048];

    while (true) {
        // Set of words already encountered in the document to track number of documents word appears in
        unordered_map<string, bool> word_set;

        // Check doc header (or EOF between documents)
        if (!fgets(buff, sizeof(buff), fd)) break;
        if (strcmp(buff, "<doc>\n")) {
            // Corrupt data -- scan forward for the next <doc> header instead of bailing on the file
            logger::warn("Worker %u: expected <doc> header, got: %s in file: %s (scanning for next <doc>)", WORKER_NUMBER, buff, path.data());
            while (fgets(buff, sizeof(buff), fd)) {
                if (!strcmp(buff, "<doc>\n")) break;
            }
            if (feof(fd) || ferror(fd)) break;
        }

        // Read in the URL (will end with \n\0)
        if (!fgets(url, sizeof(url), fd)) break;

        // Defensive: skip doc if URL line is empty or has no content
        size_t url_len = strlen(url);
        if (url_len == 0) {
            logger::warn("Worker %u: empty URL line in file: %s (skipping doc)", WORKER_NUMBER, path.data());
            continue;
        }
        // Strip trailing newline if present
        size_t url_content_len = (url[url_len - 1] == '\n') ? url_len - 1 : url_len;
        if (url_content_len == 0) {
            logger::warn("Worker %u: blank URL in file: %s (skipping doc)", WORKER_NUMBER, path.data());
            continue;
        }

        // Increment the doc count
        uint32_t doc = curr_doc_++;
        urls.push_back(string(url, url_content_len));

        // Start a counter for word locations
        uint32_t loc = 0;

        // Parse title and body words
        while(fgets(buff, sizeof(buff), fd)) {
            if (!strcmp(buff, "</doc>\n")) {
                // Doc ended, go back to outer loop
                break;
            } else if (strcmp(buff, "</title>\n") && strcmp(buff, "<title>\n")) { // Don't push the title tags
                // -1 because all words have new line at the end from fgets
                size_t len = strlen(buff);
                if (len <= 1) continue; // skip empty lines
                string_view word_view = string_view(buff, len - 1);

                if (!word_set[word_view]) {
                    word_set[word_view] = true;
                    index[word_view].n_docs++;
                }

                index[word_view].posts.push_back({doc, ++loc});
            }
        }

        if (ferror(fd)) {
            logger::error("Worker %u: file read error on file: %s, with error: %s", WORKER_NUMBER, path.data(), strerror(errno));
            fclose(fd);
            return false;
        }

        // Flush after every DOCS_PER_INDEX_CHUNK documents to bound memory usage
        if (++doc_count == DOCS_PER_INDEX_CHUNK) {
            flush();
        }
    }

    fclose(fd);
    logger::info("Worker %u: indexed file: %s", WORKER_NUMBER, path.data());
    return true;
}
