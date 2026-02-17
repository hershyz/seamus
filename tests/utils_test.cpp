//
// Created by Aiden Mizhen on 2/17/26.
//
#include <cassert>
#include "../lib/string.h"
#include "../lib/utils.h"
#include <iostream>


void test_stem_word() {
    std::cout << "Testing stem_word..." << std::endl;

    // Helper lambda to make the asserts cleaner and strictly type-safe
    auto check = [](const char* input, size_t in_len, const char* expected, size_t exp_len) {
        string_view result = stem_word(string_view(input, in_len));
        // Assuming your string_view has an operator==, otherwise use memcmp
        assert(result.size() == exp_len);
        if (exp_len > 0) {
            assert(std::memcmp(result.data(), expected, exp_len) == 0);
        }
    };

    // 1. Basic Suffix Removal
    check("badly", 5, "bad", 3);
    check("joyful", 6, "joy", 3);
    check("sadness", 7, "sad", 3);
    check("foxes", 5, "fox", 3);
    check("cats", 4, "cat", 3);
    check("teacher", 7, "teach", 5);

    // 2. The "ed" and "ing" Rules (No double consonants)
    check("jumped", 6, "jump", 4);
    check("playing", 7, "play", 4);

    // 3. Double Consonant Reduction (double_check = true)
    check("running", 7, "run", 3);  // runn -> run
    check("hopped", 6, "hop", 3);   // hopp -> hop
    check("stopped", 7, "stop", 4); // stopp -> stop

    // 4. Double Consonant Exceptions ('l', 's', 'z')
    check("falling", 7, "fall", 4); // fall -> fall (l exception)
    check("hissing", 7, "hiss", 4); // hiss -> hiss (s exception)
    check("buzzing", 7, "buzz", 4); // buzz -> buzz (z exception)

    // 5. Length Guards (Words too short to trigger the stemmer)
    check("sing", 4, "sing", 4); // < 6 chars for "ing"
    check("red", 3, "red", 3);   // < 5 chars for "ed"
    check("sly", 3, "sly", 3);   // < 5 chars for "ly"
    check("yes", 3, "yes", 3);   // < 5 chars for "es"
    check("is", 2, "is", 2);     // < 4 chars for "s"
}

int main() {
    std::cout << "--- Running Utils Tests ---\n";

    test_stem_word();

    std::cout << "All utils tests passed successfully!\n";
    std::cout << "----------------------------------------\n";

    return 0;
}
