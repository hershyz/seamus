#include <cstddef>    // for size_t
#include <cassert>
#include <cstdint>
#include <iomanip>
#include "lib/vector.h"
#include "lib/string.h"
#include "lib/unordered_map.h"
#include "lib/priority_queue.h"

static const unordered_map<string, double> tldWeight = {
    {"gov", 1.2},
    {"edu", 1.2},
    {"mil", 1.2},
    {"com", 1.0},
    {"org", 1.0},
    {"net", 1.0},
    {"info", 0.8},
    {"biz", 0.8},
    {"tk", 0.8}
};

bool is_digit(char c) { }

double max(double i, double j) { }

double calcPriorityScore(const string& url, int seed_list_dist) { }

struct UncrawledItem {
    string url;
    uint16_t seed_list_dist;
    uint16_t priority_score; // only to be set once inserted

    UncrawledItem(const string &init_url, uint16_t init_seed_list_dist) : url(init_url), seed_list_dist(init_seed_list_dist),
        priority_score(calcPriorityScore(init_url, init_seed_list_dist)) { }
};

struct CrawledItem {
    string url;
    uint16_t seed_list_dist;
    uint32_t times_seen;

    CrawledItem(const string &init_url, uint16_t init_seed_list_dist, uint16_t times_seen_init) : url(init_url), seed_list_dist(init_seed_list_dist), 
        times_seen(times_seen_init) { }
};

bool uncrawledComp(const UncrawledItem& u1, const UncrawledItem& u2) { }

class Frontier {
private:
    priority_queue<UncrawledItem, vector<UncrawledItem>, uncrawledComp> pq;
    unordered_map<string, uint32_t> curr_urls;
public:
    Frontier() { }

    void push(const UncrawledItem &u) { }

    void push(const string &url, int seed_list_dist) { }

    CrawledItem front() { }

    size_t size() { }
};