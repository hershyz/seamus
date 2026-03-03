#include "lib/thread_pool.h"
#include "lib/vector.h"
#include "url_store/url_store.h"
#include "frontier/Frontier.h"
#include "index/IndexChunkManager.h"
#include "crawler/domain_carousel.h"
#include <atomic>
#include <thread>

// Dummy structs to represent your data
struct ParsedPageBatch { /* ... */ };
struct ExtractedUrlBatch { /* ... */ };

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
    ThreadSafeQueue<ExtractedUrlBatch> frontier_queue_;

    // --- Thread Management ---
    std::atomic<bool> is_running_{false};
    ThreadPool crawler_pool_;
    std::thread writer_thread_;
    std::thread persister_thread_;
    std::thread domain_feeder_thread_;
    std::thread frontier_manager_thread_;
    std::condition_variable persist_cv;

public:
    CrawlerNode(uint32_t worker_id, uint32_t num_threads)
        : worker_id_(worker_id), 
          num_threads_(num_threads),
          frontier_(worker_id),
          crawler_pool_(num_threads) 
    {
        // Initialization/Recovery logic
        url_store_.readFromFile(worker_id_);
        // Seed frontier, recover index, etc.
    }

    ~CrawlerNode() {
        stop();
    }

    void start() {
        if (is_running_.exchange(true)) return; // Prevent double start

        // Spin up background services
        writer_thread_ = std::thread(&CrawlerNode::writer_loop, this);
        persister_thread_ = std::thread(&CrawlerNode::persister_loop, this);
        domain_feeder_thread_ = std::thread(&CrawlerNode::domain_feeder_loop, this);
        frontier_manager_thread_ = std::thread(&CrawlerNode::frontier_manager_loop, this);

        // Enqueue worker tasks into the thread pool
        for (size_t i = 0; i < num_threads_; ++i) {
            crawler_pool_.enqueue_task([this]() { this->worker_loop(); });
        }
    }

    void stop() {
        if (!is_running_.exchange(false)) return; // Prevent double stop
        index_manager_.shutdown();

        // shutdown threadsafe queues if needed

        if (writer_thread_.joinable()) writer_thread_.join();
        if (persister_thread_.joinable()) persister_thread_.join();
        if (domain_feeder_thread_.joinable()) domain_feeder_thread_.join();
        if (frontier_manager_thread_.joinable()) frontier_manager_thread_.join();  

        // crawler_pool_ destructor will join worker threads
    }

private:
    // --- Thread Loops ---
    void worker_loop() {
        ParsedPageBatch page_buf;
        size_t page_buf_bytes = 0;

        while (is_running_) {
            CrawlTarget target = domain_carousel_.get_target();
            
            // DNS & Network fetch
            // Parse content
            
            ExtractedUrlBatch local_batch = /* ... */;
            
            // Extract URLs and push to frontier manager queue
            for (const auto& url : local_batch) {
                // check if url belongs to our machine based off hash
                if (node_id == this->worker_id_) {
                    // Add to local batch if it belongs to this machine
                    local_batch.push_back(url);
                } else {
                    // Otherwise, send to appropriate machine (e.g. via RPC)
                    send_to_worker(url);
                }
            }
            
            if (!local_batch.empty()) {
                frontier_queue_.push(std::move(local_batch));
            }

            // Batch parsed pages
            // page_buf.add(...);
            // page_buf_bytes += ...;
            if (page_buf_bytes >= flush_threshold_) {
                chunk_write_queue_.push(std::move(page_buf));
                page_buf = ParsedPageBatch(); // reset
                page_buf_bytes = 0;
            }
        }
    }

    void writer_loop() {
        while (is_running_) {
            // Blocks until a batch is ready
            ParsedPageBatch write_req = chunk_write_queue_.pop(); 
            index_manager_.add_data(write_req);
        }
    }

    void persister_loop() {
        while (is_running_) {
            IndexChunk old_chunk = index_manager_.persist_reset();
            old_chunk.persist();
            url_store_.persist_snapshot();
            frontier_.persist_snapshot();
        }
    }

    void rpc_receive_urls(ExtractedUrlBatch& urls) {
        if (!is_running_ || urls.empty()) return;

        frontier_queue_.push(std::move(urls));
    }

    void domain_feeder_loop() {
        while (is_running_) {
            // Pull chunk from frontier and feed domain_carousel with new CrawlTargets
            // Check if domain_carousel size falls below threshold
            // Pull Chunk (100s or thousands at a time tbd) from frontier
            //      (in one step to minimize blocking)
            // Feed url one at a time to domain carousel with loop
        }
    }

    void frontier_manager_loop() {
        while (is_running_) {
            // Listen/receive URLs from other workers
            ExtractedUrlBatch urls = frontier_queue_.pop();
            // Check UrlStore + increment seen count + domain distance
            // Add to frontier if new, else discard
        }
    }
};