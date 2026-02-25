//
// Created by Aiden Mizhen on 2/23/26.
//

#pragma once
#include "lib/deque.h"
#include "lib/string.h"
#include <ctime>
#include <mutex>
#include "lib/utils.h"


struct CrawlTarget {
    string domain;
    string path;
};


class DomainCarousel {

public:
    static constexpr size_t NUMBER_OF_QUEUES = 1024; // TODO : Switch to env?
    static constexpr uint64_t WAIT_TIME = 2'000'000'000ULL; // TODO : Switch to env? or make website specific
    static constexpr timespec SLEEP_TIME{0, 10'000'000};

private:
    struct DomainQueue {
        std::mutex domain_lock;
        uint64_t ready_at = 0;
        deque<CrawlTarget> targets{};
    };
    DomainQueue carousel[NUMBER_OF_QUEUES]{};


    static size_t hash_domain(const string& domain) {
        // These constants are specific to 64-bit FNV-1
        size_t hash = 0xcbf29ce484222325ULL;
        size_t fnv_prime = 0x100000001b3ULL;

        for (size_t i = 0; i < domain.size(); ++i) {
            // XOR the bottom byte
            hash ^= static_cast<unsigned char>(domain[i]);
            // Multiply by the prime
            hash *= fnv_prime;
        }

        return hash;
    }


public:
    CrawlTarget get_target(uint32_t thread_num = 0) {
        size_t number_of_threads = 1000; // TODO UPDATE NUMBER OF THREADS FROM ENV OR GLOBAL VAR
        const size_t start_index = NUMBER_OF_QUEUES * thread_num / number_of_threads;
        size_t domain_index = start_index;
        timespec timestamp{};

        while (true) {
            if (not carousel[domain_index].targets.empty()) {
                std::unique_lock<std::mutex> lock(carousel[domain_index].domain_lock, std::try_to_lock);
                if (lock.owns_lock()) {
                    clock_gettime(CLOCK_MONOTONIC, &timestamp);
                    uint64_t time = timestamp.tv_nsec + timestamp.tv_sec * 1'000'000'000ULL;
                    if (not carousel[domain_index].targets.empty() and
                        time >= carousel[domain_index].ready_at) {

                        carousel[domain_index].ready_at = time + WAIT_TIME;
                        CrawlTarget target = move(carousel[domain_index].targets.front());
                        carousel[domain_index].targets.pop_front();

                        return target;
                    }
                }
            }
            domain_index = (domain_index + 1) % NUMBER_OF_QUEUES;
            if (domain_index == start_index) {
                nanosleep(&SLEEP_TIME, nullptr);
            }
        }
    }

    void push_target(CrawlTarget&& target) {
        size_t domain_index = hash_domain(target.domain) % NUMBER_OF_QUEUES;
        std::lock_guard<std::mutex> lock(carousel[domain_index].domain_lock);
        carousel[domain_index].targets.push_back(move(target));
    }

};
