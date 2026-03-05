#pragma once
#include "string.h"
#include "vector.h"
#include "rpc_common.h"
#include <cstring>
#include <optional>


// Encapsulation of data that we track as we crawl
struct CrawlTarget {
    string domain;          // Just the domain (stripped from full URL and free of `http://www` or `https://www.`
    string url;             // Entire URL
    uint16_t seed_distance; // URL hops from seed list
    uint16_t domain_dist;   // Domain hops from seed list
};


// Layout: [4: domain_len] [domain] [4: url_len] [url] [2: seed_distance] [2: domain_dist]

// Returns the total buffer size needed to serialize a CrawlTarget
inline size_t crawl_target_serialized_size(const CrawlTarget& ct) {
    return sizeof(uint32_t) + ct.domain.size()
         + sizeof(uint32_t) + ct.url.size()
         + sizeof(uint16_t) * 2;
}

// Serializes a CrawlTarget into buf, returns pointer past the written bytes
inline char* serialize_crawl_target(char* buf, const CrawlTarget& ct) {
    // domain (length-prefixed)
    uint32_t domain_len = static_cast<uint32_t>(ct.domain.size());
    std::memcpy(buf, &domain_len, sizeof(uint32_t));
    buf += sizeof(uint32_t);
    std::memcpy(buf, ct.domain.data(), ct.domain.size());
    buf += ct.domain.size();

    // url (length-prefixed)
    uint32_t url_len = static_cast<uint32_t>(ct.url.size());
    std::memcpy(buf, &url_len, sizeof(uint32_t));
    buf += sizeof(uint32_t);
    std::memcpy(buf, ct.url.data(), ct.url.size());
    buf += ct.url.size();

    // seed_distance
    std::memcpy(buf, &ct.seed_distance, sizeof(uint16_t));
    buf += sizeof(uint16_t);

    // domain_dist
    std::memcpy(buf, &ct.domain_dist, sizeof(uint16_t));
    buf += sizeof(uint16_t);

    return buf;
}

// Deserializes a CrawlTarget from buf, returns pointer past the read bytes
// Returns nullptr on failure (if remaining < expected)
inline const char* deserialize_crawl_target(const char* buf, size_t remaining, CrawlTarget& ct) {
    // domain
    if (remaining < sizeof(uint32_t)) return nullptr;
    uint32_t domain_len;
    std::memcpy(&domain_len, buf, sizeof(uint32_t));
    buf += sizeof(uint32_t);
    remaining -= sizeof(uint32_t);

    if (remaining < domain_len) return nullptr;
    ct.domain = string(buf, domain_len);
    buf += domain_len;
    remaining -= domain_len;

    // url
    if (remaining < sizeof(uint32_t)) return nullptr;
    uint32_t url_len;
    std::memcpy(&url_len, buf, sizeof(uint32_t));
    buf += sizeof(uint32_t);
    remaining -= sizeof(uint32_t);

    if (remaining < url_len) return nullptr;
    ct.url = string(buf, url_len);
    buf += url_len;
    remaining -= url_len;

    // seed_distance + domain_dist
    if (remaining < sizeof(uint16_t) * 2) return nullptr;
    std::memcpy(&ct.seed_distance, buf, sizeof(uint16_t));
    buf += sizeof(uint16_t);
    std::memcpy(&ct.domain_dist, buf, sizeof(uint16_t));
    buf += sizeof(uint16_t);

    return buf;
}


struct BatchCrawlTargetRequest {
    vector<CrawlTarget> targets;
};


// Helper to serialize + send a BatchCrawlTargetRequest over network
inline bool send_batch_crawl_target_request(const string& host, uint16_t port, const BatchCrawlTargetRequest& batch) {
    // 4 bytes for count, per target: 4 + domain.size() + 4 + url.size() + 2 + 2
    size_t total = sizeof(uint32_t);
    for (size_t i = 0; i < batch.targets.size(); i++) {
        total += crawl_target_serialized_size(batch.targets[i]);
    }

    char* buf = new char[total];
    size_t off = 0;

    uint32_t count = htonl(static_cast<uint32_t>(batch.targets.size()));
    std::memcpy(buf + off, &count, sizeof(uint32_t));
    off += sizeof(uint32_t);

    for (size_t i = 0; i < batch.targets.size(); i++) {
        const auto& ct = batch.targets[i];

        // domain (length-prefixed, network byte order)
        uint32_t domain_len = htonl(static_cast<uint32_t>(ct.domain.size()));
        std::memcpy(buf + off, &domain_len, sizeof(uint32_t));
        off += sizeof(uint32_t);
        std::memcpy(buf + off, ct.domain.data(), ct.domain.size());
        off += ct.domain.size();

        // url (length-prefixed, network byte order)
        uint32_t url_len = htonl(static_cast<uint32_t>(ct.url.size()));
        std::memcpy(buf + off, &url_len, sizeof(uint32_t));
        off += sizeof(uint32_t);
        std::memcpy(buf + off, ct.url.data(), ct.url.size());
        off += ct.url.size();

        // seed_distance
        uint16_t sd = htons(ct.seed_distance);
        std::memcpy(buf + off, &sd, sizeof(uint16_t));
        off += sizeof(uint16_t);

        // domain_dist
        uint16_t dd = htons(ct.domain_dist);
        std::memcpy(buf + off, &dd, sizeof(uint16_t));
        off += sizeof(uint16_t);
    }

    bool ok = send_buffer(host, port, buf, total);
    delete[] buf;
    return ok;
}


// Helper to deserialize a BatchCrawlTargetRequest from a socket fd
inline std::optional<BatchCrawlTargetRequest> recv_batch_crawl_target_request(int fd) {
    uint32_t count;
    if (!recv_u32(fd, count)) return std::nullopt;

    BatchCrawlTargetRequest batch;
    for (uint32_t i = 0; i < count; i++) {
        auto domain = recv_string(fd);
        if (!domain) return std::nullopt;

        auto url = recv_string(fd);
        if (!url) return std::nullopt;

        uint16_t seed_distance;
        if (!recv_u16(fd, seed_distance)) return std::nullopt;

        uint16_t domain_dist;
        if (!recv_u16(fd, domain_dist)) return std::nullopt;

        batch.targets.push_back(CrawlTarget{std::move(*domain), std::move(*url), seed_distance, domain_dist});
    }

    return std::optional<BatchCrawlTargetRequest>(std::move(batch));
}
