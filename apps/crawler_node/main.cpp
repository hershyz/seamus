#include "lib/thread_pool.h"
#include "url_store/url_store.h"
#include "frontier/Frontier.h"
#include "index/Index.h"
#include "crawler/domain_carousel.h"


// TODO(charlie): This needs to be defined globally on bootup (defining here for now)
const uint32_t NUM_THREADS = 16;
const uint32_t WORKER_NUMBER = 0;

int main(int argc, char* argv[]) {
    
    // recover index from disk if exists
    IndexChunk index_chunk;
    // TODO(David): add index_chunk recovery logic here

    // recover urlStore from disk if exists
    UrlStore url_store;
    url_store.readFromFile(url_store, WORKER_NUMBER);

    // recover frontier
        // frontier recovery should use snapshots + WAL logs to recover recent state as much as possible
    Frontier frontier(WORKER_NUMBER);

    // start crawler threads
    ThreadPool crawler_pool(NUM_THREADS);
    for (size_t i = 0; i < NUM_THREADS; i++) {
        // TODO: enqueue crawler tasks here
    }

    return 0;
}