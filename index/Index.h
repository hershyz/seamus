#include "lib/vector.h"
#include "lib/string.h"
#include "lib/unordered_map.h"

struct post {
    uint32_t doc;
    uint32_t loc;
};

struct postings {
    vector<post> posts;
    uint32_t n_docs;
};

class IndexChunk {
private:
    unordered_map<string, postings> index;
    vector<string> urls;
    uint32_t curr_doc_;
    uint32_t curr_loc_;
    uint32_t curr_bytes_; 

    vector<string> sort_entries();
public:
    IndexChunk() : curr_doc_ (0), curr_loc_ (0), curr_bytes_(0) {}

    bool add_page(const string &url); // TODO: update add_page to add to curr_bytes_
    void persist();
    void readFromFile();
    uint32_t size() const {
        return curr_bytes_;
    }
};

uint32_t chunk = 0;

// TODO: This needs to be defined globally on bootup (defining here for now)
const uint32_t WORKER_NUMBER = 0;

void update_chunk_number();