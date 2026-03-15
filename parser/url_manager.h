#include <cstdint>
#include <cstdlib>
#include "word_array.h"
#include "../lib/string.h"

class UrlManager {
public:
    static constexpr int MAX_LINK_MEMORY = 8 * 1024; // TODO: Move to consts -- this is duplicated from parser
    UrlManager() : ME(0) {} // TODO: Remove this default constructor when distributed
    UrlManager(size_t machine_id) : ME(machine_id) {}

    // TODO: Do we want to pass this by reference instead? Memory vs being able to reuse
    bool add_urls(word_array<MAX_LINK_MEMORY> urls) {
        /**
         * TODO: Parse document by document. URL array should have format:
         * 
         * <doc>
         * [old hops] [old domain hops]
         * [domain]
         * [
         *      list of links in format:
         *      [link]
         *      [anchor text word 1] ... [anchor text word n]
         * ]
         */

         // Send relevant updates to URL store
        const char* p = urls.data();
        const char* const start = p;
        const char* const end = p + urls.size();

        while (p < end) {
            uint16_t hops = 0;
            uint16_t domain_hops = 0;
            string domain("");

            // Tag -- should be start or end of document
            if (*p == '<') {
                // Start of document
                if (!memcmp(p, "<doc>", 5)) {
                    p += 6; // Skip <doc>\n

                    // Read number (TODO: Test this use of strtol)
                    char* next;
                    hops = strtol(p, &next, 10);
                    domain_hops = strtol(next, &next, 10);

                    // Advance p to point past the nums and the new line
                    p = next + 1;

                    // Read the domain of the page the URLs were found on
                    const char* word_start = p;
                    while (*(++p) != '\n') {}
                    domain = string(word_start, p - word_start);
                    p++;

                    // TODO: Parse URLs with getline() until </doc> is reached
                }

                // TODO: Move this inside to the getline parsing
                // End of document
                else if (!memcmp(p, "</doc>", 6)) {
                    p += 7; // Skip </doc>\n
                }
                // Error case
                else {
                    perror("Unexpected < token. Advancing to next character.\n");
                    p++;
                }
            }
        }
         
         // If link is new, send to crawler (or have URL store do that)
    }

private:
    // TODO: Initialize a machine ID on crawler startup
    size_t ME;
};