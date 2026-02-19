#pragma once

#include "string.h"
#include "vector.h"
#include "rpc_common.h"
#include <cstdint>


// Request sent to end server hosting the (sharded) dynamic URL data when a new URL is encountered
// This request is capable of being batched (per URL)
// Distance from seed list is never something that needs to be updated, so it is not included in this RPC
struct URLStoreUpdateRequest {
    string url;                         // URL: primary identifier
    vector<string> anchor_text;         // Vector of anchor text strings used to refer to this URL since its last update (potentially size >1)
    uint32_t num_encountered;           // Number of additional times this URL has been encountered since its last update
};


// Serialize a URLStoreUpdateRequest into a buffer and send it
