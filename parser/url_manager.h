#include <cstdint>
#include "word_array.h"

class UrlManager {
public:
    static constexpr int MAX_LINK_MEMORY = 8 * 1024; // TODO: Move to consts -- this is duplicated from parser
    UrlManager() : hops(0), domain_hops(0) {}

    // TODO: Do we want to pass this by reference instead? Memory vs being able to reuse
    bool add_urls(word_array<MAX_LINK_MEMORY> urls) {
        /**
         * TODO: Parse document by document. URL array should have format:
         * 
         * <doc>
         * [hops] [domain hops]
         * [domain]
         * [list of links in format:
         * [link]
         * [anchor text words]
         * ]
         */

         // Send relevant updates to URL store
         
         // If link is new, send to crawler (or have URL store do that)
    }

private:
    uint16_t hops;
    uint16_t domain_hops;
};