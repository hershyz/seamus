#include <cstdint>
#include <cstdlib>
#include "word_array.h"
#include "../lib/rpc_urlstore.h"
#include "../lib/string.h"
#include "../lib/vector.h"
#include "../url_store/url_store.h"

class UrlManager {
public:
    static constexpr int MAX_LINK_MEMORY = 8 * 1024; // TODO: Move to consts -- this is duplicated from parser
    UrlManager() : ME(0) {} // TODO: Remove this default constructor when distributed
    UrlManager(size_t machine_id) : ME(machine_id) {}

    // TODO: Do we want to pass this by reference instead? Memory vs being able to reuse
    bool add_urls(word_array<MAX_LINK_MEMORY> urls) {
        /**
         * URL array should have a list of docs of format:
         * 
         * <doc>
         * [old hops] [old domain hops]
         * [domain]
         * [
         *      list of links in format:
         *      [link]
         *      [anchor text word 1] ... [anchor text word n]
         * ]
         * </doc>
        */

        const char* p = urls.data();
        const char* const start = p;
        const char* const end = p + urls.size();

        while (p < end) {
            uint16_t hops = 0;
            uint16_t domain_hops = 0;
            string domain("");

            // Expect start of document token
            if (memcmp(p, "<doc>", 5)) {
                // Didn't find expected token
                perror("Missing expected start of document token.\n");
                return false;
            }

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

            // Parse URLs until </doc> is reached
            while (memcmp(p, "</doc>", 6)) {
                // Get the URL itself
                word_start = p;
                while(*(++p) != '\n') {}
                string url(word_start, p - word_start);
                uint16_t dhop = extract_domain(string(word_start, p - word_start)) != move(domain) ? 1 : 0;
                vector<string> anchor_words;

                // Parse space-separated anchor texts
                while (*(++p) != '\n') {
                    word_start = p;
                    while(*(++p) != ' ') {}
                    anchor_words.push_back(string(word_start, p - word_start));
                }

                size_t recipient = get_destination_machine_from_url(url);
                // URL belongs to this machine
                if (recipient == ME) {
                    // TODO: Does this get to the crawler/frontier?? Or do I need to send to crawler AND url store
                    // To me it seems like the ideal thing is that addUrl in URL store should send link to frontier/crawler as needed
                    // Since updateUrl calls addUrl for a new URL, that would solve the issue
                    // Also TODO: I get an error when I try to move(anchor_words) here, but I fear string will break if I don't move

                    urlStore.updateUrl(move(url), anchor_words, hops + 1, domain_hops + dhop, 1);
                } 
                // Send RPC to destination machine
                else {
                    URLStoreUpdateRequest rpc{
                        move(url), 
                        move(anchor_words), 
                        1,
                        hops + 1,
                        domain_hops + dhop,
                    };

                    rpcs[recipient].reqs.push_back(move(rpc));
                }
            }

            p += 7; // Skip </doc>\n
        }
    }

    // TODO: When a certain threshold is met, send all the RPCs in vector
    bool send_rpcs();

private:
    BatchURLStoreUpdateRequest rpcs[NUM_MACHINES];
    UrlStore urlStore; // TODO: This should be passed in or declared elsewhere
    size_t ME; // TODO: Initialize a machine ID on crawler startup
};