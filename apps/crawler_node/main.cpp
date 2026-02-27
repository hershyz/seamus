// todo(hershey): revisit the app/crawler_node structure, write memory mapping logic for the crawling priority buckets

#include "lib/thread_pool.h"
#include "url_store/url_store.h"
#include "frontier/Frontier.h"
#include "index/Index.h"
#include "crawler/domain_carousel.h"

#include <mutex>
#include <condition_variable>

std::mutex index_mutex;
std::mutex frontier_mutex;
std::mutex urlstore_mutex;
std::condition_variable cv;

// TODO(charlie): This needs to be defined globally on bootup (defining here for now)
const uint32_t NUM_THREADS = 16;
const uint32_t WORKER_NUMBER = 0;
static const uint64_t MAX_CHUNK_SIZE = 100ull * 1024 * 1024 * 1024; // 100GB

int main(int argc, char* argv[]) {
    
    // recover index from disk if exists
    update_chunk_number();
    IndexChunk index_chunk;

    // recover urlStore from disk if exists
        // urlStore is persisted at same time as index chunk persists
    UrlStore url_store;
    url_store.readFromFile(WORKER_NUMBER);

    // recover frontier
        // frontier is persisted at same time as index chunk persists
    Frontier frontier(WORKER_NUMBER);

    DomainCarousel domain_carousel;
    // Seed frontier with seed list (with correct hash)

    // start crawler threads
    ThreadPool crawler_pool(NUM_THREADS);
    for (size_t i = 0; i < NUM_THREADS; i++) {
        // TODO: enqueue crawler tasks here w/ worker function that takes in domain_carousel as argument

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

        // TODO: determine lock order acquisition if needed
        std::lock_guard<std::mutex> lock(index_mutex);

        if (index_chunk.size() >= MAX_CHUNK_SIZE) {
            cv.notify_one();
        }
    }
}

// occasionally checks for persist opportunities and persists frontier, urlstore, and index if so
void persister(IndexChunk& index_chunk, UrlStore& url_store, Frontier& frontier) {
    while (true) {
        // no need to reset frontier or urlstore
        Frontier old_frontier(WORKER_NUMBER);
        UrlStoreState old_urlstore;
        IndexChunk old_chunk;
        
        {
            std::unique_lock<std::mutex> lock(index_mutex);
            cv.wait(lock, [&] { return index_chunk.size() >= MAX_CHUNK_SIZE; });

            old_chunk = std::move(index_chunk);
            index_chunk = IndexChunk();
        }

        old_chunk.persist();

        {
            std::lock_guard<std::mutex> lock(frontier_mutex);
            old_frontier = frontier;
        }

        {
            std::lock_guard<std::mutex> lock(urlstore_mutex);
            old_urlstore = url_store.extractState();
        }

        old_urlstore.persist();
        old_frontier.persist();
        std::this_thread::sleep_for(std::chrono::seconds(10));
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
