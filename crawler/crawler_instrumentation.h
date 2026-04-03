#pragma once

#include "../lib/vector.h"
#include "../lib/deque.h"
#include <cstddef>
#include <cstdint>
#include <mutex>


enum class MetricType : uint8_t {
    DOCUMENTS_CRAWLED,
    PAGE_LENGTH,
    PAGE_PRIORITY,
};

struct MetricUpdate {
    MetricType type;
    double value;
};


class CrawlerInstrumentation {
public:
    CrawlerInstrumentation(size_t num_workers)
        : queues(num_workers), locks(num_workers) {}

    void submit(size_t worker_id, MetricUpdate update) {
        std::lock_guard<std::mutex> lock(locks[worker_id]);
        queues[worker_id].push_back(update);
    }

private:
    vector<deque<MetricUpdate>> queues;
    vector<std::mutex> locks;
};
