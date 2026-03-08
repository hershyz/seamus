#include <cstddef>
#include <cstring>
#include <chrono>

#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

#include "../../lib/buffer.h"
#include "parser.h"
#include "HtmlTags.h"

#include <string>
#include <vector>

using Clock = std::chrono::high_resolution_clock;
using Duration = std::chrono::duration<double>;

int main() {
    double time_read = 0, time_parse = 0, time_write = 0;

    auto t0 = Clock::now();
    int fd = STDIN_FILENO;
    buffer buf;
    HtmlParser h;
    ssize_t status;
    size_t total_bytes = 0;

    while (true) {
        auto r0 = Clock::now();
        status = buf.read(fd);
        auto r1 = Clock::now();
        time_read += Duration(r1 - r0).count();

        if (status <= 0) break;
        total_bytes += status;

        auto p0 = Clock::now();
        size_t consumed = h.parse(buf);
        auto p1 = Clock::now();
        time_parse += Duration(p1 - p0).count();

        auto w0 = Clock::now();
        buf.write_to_disk("bench_out.txt", consumed);
        auto w1 = Clock::now();
        time_write += Duration(w1 - w0).count();
    }

    auto p0 = Clock::now();
    h.finish(buf);
    auto p1 = Clock::now();
    time_parse += Duration(p1 - p0).count();

    double time_total = Duration(Clock::now() - t0).count();

    // --- Content breakdown ---
    size_t words_bytes = 0, title_bytes = 0, url_bytes = 0;
    for (auto &w : h.words) words_bytes += w.size();
    for (auto &w : h.titleWords) title_bytes += w.size();
    for (auto &l : h.links) url_bytes += l.URL.size();

    size_t kept = words_bytes + title_bytes + url_bytes;
    size_t discarded = (total_bytes > kept) ? total_bytes - kept : 0;

    printf("=== Content Breakdown ===\n");
    printf("Total bytes:     %zu\n", total_bytes);
    printf("Words:           %zu (%.1f%%)\n", words_bytes, 100.0 * words_bytes / total_bytes);
    printf("Title:           %zu (%.1f%%)\n", title_bytes, 100.0 * title_bytes / total_bytes);
    printf("URLs:            %zu (%.1f%%)\n", url_bytes, 100.0 * url_bytes / total_bytes);
    printf("Discarded:       %zu (%.1f%%)\n", discarded, 100.0 * discarded / total_bytes);

    printf("\n=== Time Breakdown ===\n");
    printf("Total:           %.3f ms\n", time_total * 1000);
    printf("Read (network):  %.3f ms (%.1f%%)\n", time_read * 1000, 100.0 * time_read / time_total);
    printf("Parse:           %.3f ms (%.1f%%)\n", time_parse * 1000, 100.0 * time_parse / time_total);
    printf("Write (disk):    %.3f ms (%.1f%%)\n", time_write * 1000, 100.0 * time_write / time_total);
    double time_other = time_total - time_read - time_parse - time_write;
    printf("Other:           %.3f ms (%.1f%%)\n", time_other * 1000, 100.0 * time_other / time_total);

    unlink("bench_out.txt");
}
