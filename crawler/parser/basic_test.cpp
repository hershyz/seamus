#include <stdio.h>
#include <cstddef>
#include <cstring>

#include <fcntl.h>
#include <unistd.h>

#include "parser.h"

int main() {
    int fd = open("in.txt", O_RDONLY);
    int out = open("out.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);

    HtmlParser h(fd, out);

    while (h.read_and_parse() > 0);
    h.finish();
    close(fd);

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
