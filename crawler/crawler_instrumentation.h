#pragma once

#include "../lib/vector.h"
#include "../lib/deque.h"
#include "lib/consts.h"
#include <cstddef>
#include <cstdint>
#include <atomic>
#include <mutex>
#include <thread>
#include <condition_variable>


enum class MetricType : uint8_t {
    DOCUMENTS_CRAWLED,
    PAGE_LENGTH,
    PAGE_PRIORITY,
};

struct MetricUpdate {
    MetricType type;
    double num;                 // numerator quantity
    int den;                    // denominator quantity (for averaging/batching)
};


class CrawlerInstrumentation {
public:
    CrawlerInstrumentation(size_t num_workers)
        : queues(num_workers), locks(num_workers) {}

    ~CrawlerInstrumentation() {
        running.store(false, std::memory_order_relaxed);
        shutdown_cv.notify_all();
        if (drain_thread.joinable()) drain_thread.join();
    }

    void start() {
        drain_thread = std::thread(&CrawlerInstrumentation::drain_worker, this);
    }

    void submit(size_t worker_id, MetricUpdate update) {
        std::lock_guard<std::mutex> lock(locks[worker_id]);
        queues[worker_id].push_back(update);
    }

private:
    vector<deque<MetricUpdate>> queues;
    vector<std::mutex> locks;

    uint64_t documents_crawled = 0;
    double total_page_length = 0;
    uint64_t page_length_count = 0;
    double total_page_priority = 0;
    uint64_t page_priority_count = 0;

    std::atomic<bool> running{true};
    std::mutex shutdown_mutex;
    std::condition_variable shutdown_cv;
    std::thread drain_thread;

    void process_metric_updates() {
        for (size_t i = 0; i < queues.size(); i++) {
            // Pointer swap
            deque<MetricUpdate> local;
            {
                std::lock_guard<std::mutex> lock(locks[i]);
                local = static_cast<deque<MetricUpdate>&&>(queues[i]);
            }

            // Switch handler per update type
            while (!local.empty()) {
                MetricUpdate update = local.front();
                local.pop_front();

                switch (update.type) {
                    case MetricType::DOCUMENTS_CRAWLED:
                        documents_crawled += static_cast<uint64_t>(update.num);
                        break;
                    case MetricType::PAGE_LENGTH:
                        total_page_length += update.num;
                        page_length_count += update.den;
                        break;
                    case MetricType::PAGE_PRIORITY:
                        total_page_priority += update.num;
                        page_priority_count += update.den;
                        break;
                }
            }
        }
    }

    void drain_worker() {
        while (running) {
            std::unique_lock<std::mutex> lock(shutdown_mutex);
            shutdown_cv.wait_for(lock, std::chrono::seconds(CRAWLER_INSTRUMENTATION_INTERVAL_SEC));
            if (!running) break;
            process_metric_updates();
        }
    }
};
