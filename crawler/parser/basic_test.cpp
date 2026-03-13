#include <stdio.h>
#include <cstddef>
#include <cstring>

#include <fcntl.h>
#include <unistd.h>

#include "parser.h"

int main() {
    int fd = open("in.txt", O_RDONLY);
    int words_out = open("out.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    int links_out = open("links.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    int title_out = open("title_lens.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);

    HtmlParser h(fd, words_out, links_out, title_out, "in.txt");

    while (h.read_and_parse() > 0);
    h.finish();
    close(fd);
    close(words_out);
    close(links_out);
    close(title_out);
}