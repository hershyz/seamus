#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>

#include <fcntl.h>
#include <unistd.h>

#include <sys/socket.h>

class buffer {
public:
    // Can change later - this is 32KB
    static constexpr uint32_t CAPACITY = 32 * 1024;

    buffer() : size_(0), data_(new char[CAPACITY]) {}

    ~buffer() { delete[] data_; }

    buffer(const buffer &) = delete;
    buffer &operator=(const buffer &) = delete;
    buffer(buffer &&) = delete;
    buffer &operator=(buffer &&) = delete;

    char &operator[](size_t i) { return data_[i]; }
    const char &operator[](size_t i) const { return data_[i]; }

    char &front() { return data_[0]; }
    char &back() { return data_[size_ - 1]; }
    const char &front() const { return data_[0]; }
    const char &back() const { return data_[size_ - 1]; }

    char *data() { return data_; }
    const char *data() const { return data_; }

    size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }

    char *begin() { return data_; }
    char *end() { return data_ + size_; }
    const char *begin() const { return data_; }
    const char *end() const { return data_ + size_; }

    void clear() { size_ = 0; }
    void set_size(size_t n) { size_ = n; }

    // Read up to CAPACITY bytes into the buffer. Returns 1 on successful read,
    // 0 on empty read, and -1 on error
    int8_t read(int fd) {
        uint32_t bytes_read = 0;
        ssize_t bytes_in = 0;
        while ((bytes_in = recv(fd, data_ + bytes_read, CAPACITY - bytes_read, 0)) > 0) {
            bytes_read += bytes_in;
        }

        size_ = bytes_read;
        if (bytes_in < 0) return -1;
        if (bytes_read == 0) return 0;
        return 1;
    }

    // Write contents to disk and reset buffer
    void write_to_disk(const char *filepath) {
        int fd = open(filepath, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd == -1)
            throw std::runtime_error("buffer::write_to_disk: failed to open file");

        size_t written = 0;
        while (written < size_) {
            ssize_t w = write(fd, data_ + written, size_ - written);
            if (w == -1) {
                close(fd);
                throw std::runtime_error("buffer::write_to_disk: write failed");
            }
            written += w;
        }
        close(fd);
        size_ = 0;
    }

private:
    size_t size_;
    char *data_;
};