#pragma once

#include "../lib/vector.h"
#include "../lib/string.h"
#include "lib/consts.h"
#include <cassert>


class BucketManager {
public:

    BucketManager(vector<string> bucket_files_in) : bucket_files(static_cast<vector<string>&&>(bucket_files_in)) {
        assert(bucket_files.size() == PRIORITY_BUCKETS);
    }

    // todo(hershey): write helper to load disk buckets into in-memory buckets
    // This should run ad-hoc as needed if in-memory buckets are empty

    // todo(hershey): write helper to persist in-memory buckets into disk buckets
    // This should run in a detached thread on an interval (PERSIST_INTERVAL_SEC in consts.h)


private:
  
    // bucket_files[priority] = file to serialized queue of urls
    vector<string> bucket_files;
};
