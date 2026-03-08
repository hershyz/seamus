#include <stdio.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>

#include <iostream>

#include <fcntl.h>
#include <unistd.h>

#include <sys/socket.h>

#include "../../lib/buffer.h"
#include "parser.h"

int main() {
    int fd = open("in.txt", O_RDONLY);

    const char* filename = "out.txt";
    buffer buf;
    HtmlParser h;
    ssize_t status;

    while((status = buf.read(fd)) > 0) {
        size_t bytes_consumed = h.parse(buf);
        buf.shift_data(bytes_consumed);
    }
    if (status < 0) return 1;
    h.finish(buf);
    close(fd);

    int out = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    write(out, "=== TITLE ===\n", 14);
    for (auto &w : h.titleWords) {
        write(out, w.c_str(), w.size());
        write(out, "\n", 1);
    }
    write(out, "=== WORDS ===\n", 14);
    for (auto &w : h.words) {
        write(out, w.c_str(), w.size());
        write(out, "\n", 1);
    }
    write(out, "=== LINKS ===\n", 14);
    for (auto &l : h.links) {
        write(out, l.URL.c_str(), l.URL.size());
        for (auto &a : l.anchorText) {
            write(out, " ", 1);
            write(out, a.c_str(), a.size());
        }
        write(out, "\n", 1);
    }
    close(out);
}