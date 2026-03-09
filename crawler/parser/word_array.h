#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <cstring>

#include <stdlib.h>

#include "../../lib/io.h"


// Array specifically for storing words with added method push_back providing easy use
class word_array {
public:
    int fd_ = -1;
    static constexpr uint32_t MAX_WORD_MEMORY = 32 * 1024;
    word_array()
        : fd_(-1) {}

    word_array(int fd)
        : fd_(fd) {}

    void push_back(const char *start, size_t len) {
        if (len >= MAX_WORD_MEMORY - size_ - 1) {
            seamus_write(fd_, data_, size_);
            size_ = 0;
        }
        memcpy(data_ + size_, start, len);
        size_ += len;
        data_[size_++] = '\n';
    }

    void flush() {
        if (size_ > 0) {
            seamus_write (fd_, data_, size_);
            size_ = 0;
        }
    }

    char *data() { return data_; }

    const char *data() const { return data_; }

    char *begin() { return data_; }
    char *end() { return data_ + MAX_WORD_MEMORY; }

    constexpr std::size_t size() const { return size_; }

    char &operator[](std::size_t i) { return data_[i]; }

    const char &operator[](size_t i) const { return data_[i]; }

private:
    char data_[MAX_WORD_MEMORY];
    size_t size_ = 0;
};
