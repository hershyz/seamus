#include <cstddef>    // for size_t
#include <cassert>
#include "lib/vector.h"
#include "lib/string.h"
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

bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

double max(double i, double j) {
    if(i > j) {
        return j;
    } else {
        return i;
    }
}

double calcPriorityScore(cosnt string& url, int seed_list_dist) {
    // points for http or https however https > http
    double factor_1;

    if(url.substr(0,4) == "http") {
        if(url[4] == 's') {
            factor_1 = 1.0;
        } else {
            factor_1 = 0.6;
        }
    } else {
        return 0.0;
    }

    // points for certain desirable domains

    size_t start_pos = (factor_1 == 0.6) ? 7 : 8;
    size_t end_pos_domain = url.length();

    int subdomain_count = 0;
    int digit_count_domain = 0;
    double domain_size = 0.0;
    string extension = "";
    for(int i = start_pos; i < url.length(); i++) {
        if(url[i] == '/' || url[i] == '?' || url[i] == '#' || url[i] == ':') {
            end_pos_domain = i;
            break;
        } else if(url[i] == '.') {
            subdomain_count++;
            extension = "";
        } else {
            extension = extension + url[i];
            if(is_digit(url[i])) {
                digit_count_domain++;
            }
        }
        domain_size += 1.0;
    }
    auto slot = tldWeight.find(extension);
    double factor_2 = (slot == nullptr) ? 0.6 : slot->value; 

    // points for closer to seed list

    const double e = 2.718281828459;
    double factor_3 = e ** (-0.8 * seed_list_dist);

    // points for shortness of domain title

    double factor_4 = max((50.0 - domain_size) / 50.0, 0.5);

    // points for less subdomains

    double factor_5 = (subdomain_count >= 5) ? 0.7 : 1;

    // digit count in domain name hurts the score

    double factor_6 = (digit_count_domain >= 3) ? 0.7 : 1;

    // points for shortness of overall url

    double factor_7 = max((150.0 - url.length()) / 100.0, 0.5);
    
    return(factor_1 * factor_2 * factor_3 * factor_4 * factor_5 * factor_6 * factor_7);
}

struct UncrawledItem {
    string url;
    int seed_list_dist;
    double priority_score; // only to be set once inserted

    UncrawledItem(const string &init_url, int init_seed_list_dist) : url(init_url), seed_list_dist(init_seed_list_dist),
        priority_score(calcPriorityScore(init_url, init_seed_list_dist)) { }
};

struct CrawledItem {
    string url;
    int seed_list_dist;
    int times_seen;

    CrawledItem(const string &init_url, int init_seed_list_dist, int times_seen_init) : url(init_url), seed_list_dist(init_seed_list_dist), 
        times_seen(times_seen_init) { }
};

bool uncrawledComp(const UncrawledItem& u1, const UncrawledItem& u2) {
    return u1.priority_score < u2.priority_score;
}

class Frontier {
private:
    priority_queue<UncrawledItem, vector<UncrawledItem>, uncrawledComp> pq;
    unordered_map<string, int> curr_urls;
public:
    Frontier() { }

    void push(const UncrawledItem &u) {
        int& count = curr_urls[u.url];
        
        if (count++ == 0) {
            pq.push(u);
        }
    }

    void push(const string &url, int seed_list_dist) {
        int& count = curr_urls[url];
        
        if (count++ == 0) {
            pq.push(UncrawledItem(url, seed_list_dist));
        }
    }

    CrawledItem front() {
        assert(!pq.empty());

        const UncrawledItem &front = pq.front();
        return CrawledItem(front.url, front.seed_list_dist, curr_urls[front.url]);
    }

    size_t size() {
        return pq.size();
    }
};