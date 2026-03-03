#pragma once

#include "../lib/vector.h"
#include "../lib/string.h"
#include "lib/consts.h"
#include <cassert>


class BucketManager {
public:

    BucketManager(vector<string> bucket_files) {
        assert(bucket_files.size() == PRIORITY_BUCKETS);
    }
};
