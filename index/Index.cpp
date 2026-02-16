#include "Index.h"

#include "lib/algorithm.h"
#include "lib/utils.h"

void init_index() {
    // Find the latest chunk ID
    if (chunk == 0) {
        while (file_exists("index_chunk_" + string::to_string(WORKER_NUMBER) + "_" + string::to_string(chunk) + ".txt")) chunk++;
    }

    IndexChunk index;
}

void IndexChunk::persist() {
    // Create a file (if it already exists, fail -- don't want to overwrite)
    FILE* fd = fopen("index_chunk_" + string::to_string(WORKER_NUMBER) + "_" + string::to_string(chunk) + ".txt", "wx");

    if (fd == nullptr) perror("Error opening index chunk file for writing.");

    // 4 bytes for each uint32_t ID, one byte for each char in string, one byte for each new line char
    uint64_t urls_bytes = urls.size() * 4;
    for (string s : urls) urls_bytes += s.size() + 1;

    // Write the size of the ID->URL mapping
    fwrite(&urls_bytes, sizeof(urls_bytes), 1, fd);
    fwrite("\n", sizeof(char), 1, fd);

    // Write the ID->URL mapping
    for (uint32_t i = 0; i < urls.size(); i++) {
        fwrite(&i, sizeof(i), 1, fd);
        fwrite(" ", sizeof(char), 1, fd);
        fwrite(&urls[i], sizeof(char), urls[i].size(), fd);
        fwrite("\n", sizeof(char), 1, fd);
    }

    fwrite("\n", sizeof(char), 1, fd);

    vector<string> alphabetized_entries = sort_entries();

    // TODO: Left off here

    fclose(fd);
}

vector<string> IndexChunk::sort_entries() {
    vector<string> res(index.size());

    // TODO: Have to copy all the keys into res

    radix_sort(res);
    return res;
}