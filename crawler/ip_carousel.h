//
// Created by Aiden Mizhen on 2/23/26.
//

#pragma once
#include "lib/deque.h"
#include "lib/string.h"
#include <time.h>
#include <mutex>
#include "lib/utils.h"


struct CrawlTarget {
    uint32_t ip_address;
    string path;
};


class IPCarousel {

public:
    static constexpr size_t NUMBER_OF_QUEUES = 1024; // TODO : Switch to env?
    static constexpr uint64_t WAIT_TIME = 2'000'000'000ULL; // TODO : Switch to env?
    static constexpr timespec SLEEP_TIME{0, 10'000'000};

private:
    struct IPQueue {
        std::mutex ip_lock;
        uint64_t ready_at = 0;
        deque<CrawlTarget> targets{};
    };
    IPQueue carousel[NUMBER_OF_QUEUES]{};

    static uint32_t hash_ipv4(uint32_t ip_address) {
        ip_address ^= ip_address >> 16;
        ip_address *= 0x85ebca6b;
        ip_address ^= ip_address >> 13;
        ip_address *= 0xc2b2ae35;
        ip_address ^= ip_address >> 16;

        return ip_address;
    }

public:
    CrawlTarget get_target(uint32_t thread_num = 0) {
        size_t number_of_threads = 1000; // TODO UPDATE NUMBER OF THREADS FROM ENV OR GLOBAL VAR
        size_t start_index = NUMBER_OF_QUEUES * thread_num / number_of_threads;
        size_t ip_index = start_index;
        timespec timestamp{};

        while (true) {
            if (not carousel[ip_index].targets.empty()) {
                std::unique_lock<std::mutex> lock(carousel[ip_index].ip_lock, std::try_to_lock);
                if (lock.owns_lock()) {
                    clock_gettime(CLOCK_MONOTONIC, &timestamp);
                    uint64_t time = timestamp.tv_nsec + timestamp.tv_sec * 1'000'000'000ULL;
                    if (not carousel[ip_index].targets.empty() and
                        time >= carousel[ip_index].ready_at) {

                        carousel[ip_index].ready_at = time + WAIT_TIME;
                        CrawlTarget target = move(carousel[ip_index].targets.front());
                        carousel[ip_index].targets.pop_front();

                        return target;
                    }
                }
            }
            ip_index = (ip_index + 1) % NUMBER_OF_QUEUES;
            if (ip_index == start_index) {
                nanosleep(&SLEEP_TIME, nullptr);
            }
        }
    }

    void push_target(CrawlTarget&& target) {
        size_t ip_index = hash_ipv4(target.ip_address) % NUMBER_OF_QUEUES;
        std::lock_guard<std::mutex> lock(carousel[ip_index].ip_lock);
        carousel[ip_index].targets.push_back(move(target));
    }

};
