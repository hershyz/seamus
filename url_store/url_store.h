#include "../lib/unordered_map.h"
#include "../lib/vector.h"
#include "../lib/string.h"
#include "../lib/rpc_listener.h"
#include "../lib/rpc_urlstore.h"
#include <cstdint>
#include <thread>
#include <mutex>

struct UserAnchorData {
    const string* anchor_text;
    uint32_t freq;
};

struct UrlStoreState {
    unordered_map<string, UrlData> url_data;
    vector<string> anchor_to_id; // anchor text to corresponding id (index)

    void persist();
};

class UrlStore {
private:
    std::mutex m;
    UrlStoreState state;

    const UrlData* findUrlData(const string& url) const;
    UrlData* findUrlData(const string& url);

    RPCListener* rpc_listener;      // Listener for client requests
    std::thread listener_thread;    // Thread running the listener loop
    void client_handler(int fd);    // Detached handler for client requests


public:
    UrlStore();
    ~UrlStore();

    // to read urlStore from disk after a crash, each worker thread will read from its corresponding files and update it's urlstore object accordingly
    void readFromFile(const int worker_number);

    void persist_snapshot();

    bool addUrl(const string& url, const vector<string>& anchor_texts, const uint16_t seed_distance, const uint16_t eot, const uint16_t eod, const uint32_t num_encountered);
    bool updateUrl(const string& url, const vector<string>& anchor_texts, const uint32_t num_encountered);

    uint32_t findAnchorId(const string& anchor_text);

    UrlStoreState extractState() const {
        return state;
    }

    void resetState() {
        state.url_data = unordered_map<string, UrlData>();
        state.anchor_to_id = vector<string>();
    }

    vector<UserAnchorData> getUrlAnchorInfo(const string& url) {
        const UrlData* it = findUrlData(url);
        if (it == nullptr) return {};
        if (it->anchor_freqs.empty()) return {};

        vector<UserAnchorData> user_anchor_data;
        user_anchor_data.reserve(it->anchor_freqs.size());
        for (const AnchorData& anchor_freq : it->anchor_freqs) {
            user_anchor_data.push_back({&state.anchor_to_id[anchor_freq.anchor_id] , anchor_freq.freq});
        }

        return user_anchor_data;
    }


    uint32_t getUrlNumEncountered(const string& url) {
        const UrlData* it = findUrlData(url);
        if (it == nullptr) return 0;
        return it->num_encountered;
    }


    uint16_t getUrlSeedDistance(const string& url) {
        const UrlData* it = findUrlData(url);
        return it ? it->seed_distance : UINT16_MAX;
    }


    bool inTitle(const string& url, uint16_t word_pos) {
        const UrlData* it = findUrlData(url);
        return it ? word_pos < it->eot : false;
    }


    bool inDescription(const string& url, uint16_t word_pos) {
        const UrlData* it = findUrlData(url);
        return it ? it->eot <= word_pos && word_pos < it->eod : false;
    }
};


// TODO: This needs to be defined globally on bootup (defining here for now)
const uint32_t WORKER_NUMBER = 0;
const uint32_t NUM_THREADS = 8;
const uint32_t PORT = 9000;

const uint32_t MAX_URL_LEN = 4096; // 4 KB max url length
const uint32_t MAX_ANCHOR_TEXT_LEN = 512; // 0.5 KB max anchor text length
