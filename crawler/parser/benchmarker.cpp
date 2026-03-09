#include <cstddef>
#include <cstring>
#include <chrono>

#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

#include "parser.h"

using Clock = std::chrono::high_resolution_clock;
using Duration = std::chrono::duration<double>;

int main() {
    int out_fd = open("bench_out.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);

    HtmlParser h(STDIN_FILENO, out_fd);
    ssize_t status;
    size_t total_bytes = 0;

    auto t0 = Clock::now();
    while ((status = h.read_and_parse()) > 0)
        total_bytes += status;
    h.finish();
    auto t1 = Clock::now();

    double time_total = Duration(t1 - t0).count();

    size_t words_bytes = h.words.size();
    size_t url_bytes = 0;
    for (auto &l : h.links) url_bytes += l.URL.size();

    size_t kept = words_bytes + url_bytes;
    size_t discarded = (total_bytes > kept) ? total_bytes - kept : 0;

    printf("=== Content Breakdown ===\n");
    printf("Total bytes:     %zu\n", total_bytes);
    printf("Words:           %zu (%.1f%%)\n", words_bytes, 100.0 * words_bytes / total_bytes);
    printf("URLs:            %zu (%.1f%%)\n", url_bytes, 100.0 * url_bytes / total_bytes);
    printf("Discarded:       %zu (%.1f%%)\n", discarded, 100.0 * discarded / total_bytes);
    printf("\nTotal time:      %.3f ms\n", time_total * 1000);

    close(out_fd);
}
