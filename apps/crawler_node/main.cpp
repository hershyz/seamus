// todo(hershey): revisit the app/crawler_node structure, write memory mapping logic for the crawling priority buckets

#include "lib/thread_pool.h"
#include "lib/deque.h"
#include "url_store/url_store.h"
#include "frontier/Frontier.h"
#include "index/Index.h"
#include "index/IndexChunkManager.h"
#include "crawler/domain_carousel.h"
#include <mutex>
#include <condition_variable>



// TODO: This needs to be defined globally on bootup (defining here for now)
const uint32_t NUM_THREADS = 16;
const uint32_t WORKER_NUMBER = 0;

const size_t FLUSH_THRESHOLD = 10 * 1024 * 1024; // 10 MB of pages

// TODO(charlie): encapsulate logic into class wrapper
std::mutex m;
std::condition_variable cv;
deque<...> chunkWriteReqs;


int main(int argc, char* argv[]) {
    
    // recover index from disk if exists
    update_chunk_number();
    IndexChunkManager index_manager;

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

    // spin up writer thread

    // spin up persister thread

    return 0;
}

// TODO : Switch the frontier to be block based,
//  only push or fetch page-sized blocks of urls that are accumulated in memory
//  both on the producer and consumer end. This is because the frontier is fundamentally blocking

// Does the network call and parse
void worker(DomainCarousel& domain_carousel, IndexChunkManager& index_manager, UrlStore& url_store, Frontier& frontier) {
    vector<...> page_buf;
    size_t page_buf_bytes = 0;

    while (true) {
        // Pop from carousel
        CrawlTarget target = domain_carousel.get_target();
        
        // dns lookup (ig the os or something caches this hopefully)
        // network call
        // parse and add content to page_buf

        // add new discovered urls to frontier
            // suggestion: consider batching these to push to frontier
        
        
        // suggestion: batch parsed pages to submit to IndexChunkManager
        if (page_buf_bytes >= FLUSH_THRESHOLD) {
            // TODO(charlie): encapsulate pushing to chunkWriteReqs in class wrapper
            {
                std::unique_lock<std::mutex> lock(m);
                chunkWriteReqs.push_back(std::move(page_buf));
            }

            cv.notify_one();
            page_buf = vector<...>();
            page_buf_bytes = 0;
        }
    }
}

// responsible for taking all content from parsed pages and writing to in-memory indexChunk
void writer(IndexChunkManager& index_manager) {
    ... writeReq;
    while (true) {
        // pop from request queue
        {
            std::unique_lock<std::mutex> lock(m);
            cv.wait(lock, [] { return !chunkWriteReqs.empty(); });
            writeReq = std::move(chunkWriteReqs.front());
            chunkWriteReqs.pop_front();
        }

        // write to index_manager chunk
        index_manager.add_data(writeReq);
    }
}

// checks for persist opportunities and persists frontier, urlstore, and index if so
void persister(IndexChunkManager& index_manager, UrlStore& url_store, Frontier& frontier) {
    while (true) {
        // no need to reset frontier or urlstore
        IndexChunk old_chunk = index_manager.persist_reset();
        old_chunk.persist();
        url_store.persist_snapshot();
        frontier.persist_snapshot();
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
