//
// Created by Aiden Mizhen on 2/13/26.
//

#pragma once

#include <algorithm>
#include <cstring>
#include <cassert>
#include <cstdint>


class string {

public:
    static constexpr size_t MAX_SHORT_LENGTH = 14;

private:
    union {
        struct {
            size_t flag_and_size; // LSB must be zero, used as flag
            const char* data;
        } l;
        struct {
            unsigned char flag_and_size; // least significant bit = flag, upper 7 = str size
            char data[MAX_SHORT_LENGTH + 1]; // 14 bits short string length + 1 null terminator
        } s;
    };

    // To determine short or long state
    [[nodiscard]] bool is_short() const {
        return s.flag_and_size & 1;
    }

    // Moves the contents of another string to this
    void move_from(string&& other) noexcept {
        if (other.is_short()) {
            s.flag_and_size = other.s.flag_and_size;
            std::memcpy(s.data, other.s.data, MAX_SHORT_LENGTH + 1);
        } else {
            l.flag_and_size = other.l.flag_and_size;
            l.data = other.l.data;
        }
        // Mark as empty short string so destructor isn't ran
        other.s.flag_and_size = 1;
        other.s.data[0] = '\0';
    }


public:
    explicit string (const char *c_str) : l{0, nullptr}{
        if (not c_str) {
            s.flag_and_size = 1; // Length 0, Flag 1
            s.data[0] = '\0';
            return;
        }

        size_t len = strlen(c_str);

        if (len <= MAX_SHORT_LENGTH) {
            // Short string
            s.flag_and_size = static_cast<unsigned char>(len << 1 | 1);
            memcpy(s.data, c_str, len+1);

        } else {
            l.flag_and_size = ((len<< 1) | 0); // Set flag to zero
            auto buffer = new char[len+1];
            memcpy(buffer, c_str, len+1);
            l.data = buffer;
        }
    }

    ~string() {
        if (!is_short()) {
            delete[] l.data;
        }
    }

    // No copying
    string(const string& other) = delete;

    // No copying
    string& operator=(const string& other) = delete;

    string(string&& other) noexcept : l{0, nullptr}{
        move_from(static_cast<string&&>(other));
    }

    string& operator=(string&& other) noexcept {
        if (this != &other) {
            if (!is_short()) {
                delete[] l.data;
            }

            move_from(static_cast<string&&>(other));
        }
        return *this;
    }

    explicit string (uint32_t n) : l{0, nullptr}{
        // At most 4 Billion = 10 digits => short string
        if (n == 0) {
            s.data[0] = '0';
            s.data[1] = '\0';
            s.flag_and_size = static_cast<unsigned char>((1 << 1) | 1); // Len 1, Flag 1
            return;
        }

        char tmp[11];
        tmp[10] = '\0';
        int i = 10;

        while (n > 0) {
            tmp[--i] = static_cast<char>(n % 10 + '0');
            n /= 10;
        }

        const int len = 10-i;
        memcpy(s.data,tmp+i, len + 1);

        s.flag_and_size = static_cast<unsigned char>((len << 1) | 1);
    }


    [[nodiscard]] size_t size() const {
        //Remove flag bit
        if (is_short()) {
            return static_cast<size_t>(s.flag_and_size >> 1);
        }
        return l.flag_and_size >> 1;
    }


    char operator[](const size_t i) const {
        assert(i < size());
        if (is_short()) {
            return s.data[i];
        }
        return l.data[i];
    }


    bool operator==(const string &other) const {
        const size_t len = size();
        if (len != other.size())  return false;

        if (&other == this) return true;

        if (is_short()) {
            return memcmp(s.data, other.s.data, len) == 0;
        } else {
            if (l.data == other.l.data) {
                return true;
            }
            return memcmp(l.data, other.l.data, len) == 0;
        }
    }
};
