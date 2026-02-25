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
    DomainCarousel domain_carousel;
    // Seed frontier with seed list (with correct hash)

    // start crawler threads
    ThreadPool crawler_pool(NUM_THREADS);
    for (size_t i = 0; i < NUM_THREADS; i++) {
        // TODO: enqueue crawler tasks here
    }

    return 0;
}

// TODO : Switch the frontier to be block based,
//  only push or fetch page-sized blocks of urls that are accumulated in memory
//  both on the producer and consumer end. This is because the frontier is fundamentally blocking

// Does the network call and parse
void worker(DomainCarousel& domain_carousel) {
    while (true) {
        // Pop from carousel
        // dns lookup (ig the os or something caches this hopefully)
        // network call
        // parse
        // write to disk (likely make a dedicated disk write queue and disk writer thread)
    }
}

// Single thread, sole purpose is to feed the domain_carousel
void domain_feeder(Frontier& frontier, DomainCarousel& domain_carousel) {
    while (true) {
        // Check if domain_carousel size falls below threshold
        // Pull Chunk (100s or thousands at a time tbd) from frontier
        //      (in one step to minimize blocking)
        // Feed url one at a time to domain carousel with loop
    }
}


// Manages frontier and receives urls from other processes
void frontier_manager(Frontier& frontier, UrlStore& url_store) {
    while (true) {
        // Listen/receive urls
        // Check if in URL store + increment seen count
        // add to frontier if new
    }
}