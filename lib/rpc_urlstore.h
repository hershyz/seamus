#pragma once

#include "string.h"
#include "vector.h"
#include "rpc_common.h"
#include <cstdint>
#include <optional>


// Request sent to end server hosting the (sharded) dynamic URL data when a new URL is encountered
// This request is capable of being batched (per URL)
// Distance from seed list is never something that needs to be updated, so it is not included in this RPC
struct URLStoreUpdateRequest {
    string url;                         // URL: primary identifier
    string anchor_text;                 // Vector of anchor text strings used to refer to this URL since its last update (potentially size >1)
    uint32_t num_encountered;           // Number of additional times this URL has been encountered since its last update
    uint32_t seed_list_url_hops;        // Found distance in url hops from seed list (updated if lower than current)
    uint32_t seed_list_domain_hops;     // Found distance in domain hops from the seed list (updated if lower than current)
};

struct BatchURLStoreUpdateRequest {
    vector<URLStoreUpdateRequest> reqs;
};


// Helper to serialize + send a BatchURLStoreUpdateRequest over network
inline bool send_batch_urlstore_update(const string& host, uint16_t port, const BatchURLStoreUpdateRequest& batch) {
    // Compute total buffer size:
    // 4 bytes for request count
    // Per request: 4 + url.size() + 4 + anchor_text.size() + 4 + 4 + 4
    size_t total = sizeof(uint32_t);
    for (size_t i = 0; i < batch.reqs.size(); i++) {
        const auto& req = batch.reqs[i];
        total += sizeof(uint32_t) + req.url.size()
               + sizeof(uint32_t) + req.anchor_text.size()
               + sizeof(uint32_t) * 3;
    }

    char* buf = new char[total];
    size_t off = 0;

    // Write request count
    uint32_t count = htonl(static_cast<uint32_t>(batch.reqs.size()));
    std::memcpy(buf + off, &count, sizeof(uint32_t));
    off += sizeof(uint32_t);

    for (size_t i = 0; i < batch.reqs.size(); i++) {
        const auto& req = batch.reqs[i];

        // url (length-prefixed)
        uint32_t url_len = htonl(static_cast<uint32_t>(req.url.size()));
        std::memcpy(buf + off, &url_len, sizeof(uint32_t));
        off += sizeof(uint32_t);
        std::memcpy(buf + off, req.url.data(), req.url.size());
        off += req.url.size();

        // anchor_text (length-prefixed)
        uint32_t anchor_len = htonl(static_cast<uint32_t>(req.anchor_text.size()));
        std::memcpy(buf + off, &anchor_len, sizeof(uint32_t));
        off += sizeof(uint32_t);
        std::memcpy(buf + off, req.anchor_text.data(), req.anchor_text.size());
        off += req.anchor_text.size();

        // num_encountered
        uint32_t enc = htonl(req.num_encountered);
        std::memcpy(buf + off, &enc, sizeof(uint32_t));
        off += sizeof(uint32_t);

        // seed_list_url_hops
        uint32_t url_hops = htonl(req.seed_list_url_hops);
        std::memcpy(buf + off, &url_hops, sizeof(uint32_t));
        off += sizeof(uint32_t);

        // seed_list_domain_hops
        uint32_t domain_hops = htonl(req.seed_list_domain_hops);
        std::memcpy(buf + off, &domain_hops, sizeof(uint32_t));
        off += sizeof(uint32_t);
    }

    bool ok = send_buffer(host, port, buf, total);
    delete[] buf;
    return ok;
}


// Helper to deserialize a BatchURLStoreUpdateRequest from a socket fd
inline std::optional<BatchURLStoreUpdateRequest> recv_batch_urlstore_update(int fd) {
    uint32_t count;
    if (!recv_u32(fd, count)) return std::nullopt;

    BatchURLStoreUpdateRequest batch;
    for (uint32_t i = 0; i < count; i++) {
        auto url = recv_string(fd);
        if (!url) return std::nullopt;

        auto anchor = recv_string(fd);
        if (!anchor) return std::nullopt;

        URLStoreUpdateRequest req{std::move(*url), std::move(*anchor), 0, 0, 0};

        if (!recv_u32(fd, req.num_encountered)) return std::nullopt;
        if (!recv_u32(fd, req.seed_list_url_hops)) return std::nullopt;
        if (!recv_u32(fd, req.seed_list_domain_hops)) return std::nullopt;

        batch.reqs.push_back(std::move(req));
    }

    return std::optional<BatchURLStoreUpdateRequest>(std::move(batch));
}
