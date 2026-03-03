#pragma once

#include "lib/thread_pool.h"
#include "lib/vector.h"
#include "url_store/url_store.h"
#include "frontier/Frontier.h"
#include "index/IndexChunkManager.h"
#include "crawler/domain_carousel.h"
#include <atomic>
#include <thread>
#include <condition_variable>

// TODO: implement batch structs for index writes, frontier updates, UrlStore updates
struct ParsedPageBatch { /* ... */ };
struct UrlBatch { /* ... */ };

class CrawlerNode {
private:
    const uint32_t num_threads_;
    const uint32_t worker_id_;
    const size_t flush_threshold_ = 10 * 1024 * 1024; // 1 GB max size per index chunk

    IndexChunkManager index_manager_;
    UrlStore url_store_;
    Frontier frontier_;
    DomainCarousel domain_carousel_;

    // TODO(charlie): implement thread-safe queues for chunk persist/frontier write requests
    ThreadSafeQueue<ParsedPageBatch> chunk_write_queue_;
    ThreadSafeQueue<UrlBatch> frontier_queue_;

    std::atomic<bool> is_running_{false};
    ThreadPool crawler_pool_;
    std::thread writer_thread_;
    std::thread persister_thread_;
    std::thread domain_feeder_thread_;
    std::thread frontier_manager_thread_;
    std::condition_variable persist_cv;

public:
    CrawlerNode(const uint32_t worker_id, const uint32_t num_threads = 16);
    
    ~CrawlerNode();

    void start();
    
    void stop();

    void rpc_receive_urls(const UrlBatch& urls);

    void rpc_receive_url_store_update(const BatchURLStoreUpdateRequest& updates);

private:
    void worker_loop();
    
    void writer_loop();
    
    void persister_loop();
    
    void domain_feeder_loop();
    
    void frontier_manager_loop();
};