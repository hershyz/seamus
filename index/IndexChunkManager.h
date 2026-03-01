#include <mutex>
#include <condition_variable>
#include "Index.h"

class IndexChunkManager {
    private:
        IndexChunk chunk_;
        std::mutex m_;
        std::condition_variable cv_;
        static const uint64_t MAX_CHUNK_SIZE = 100ull * 1024 * 1024 * 1024; // 100GB
    
    public:

        // method for workers to add content from crawled page to current chunk_
        // TODO(charlie): figure out details for add_data
        void add_data( ... ) {

            // does some work w/ input to update chunk_

            if (chunk_.size() >= MAX_CHUNK_SIZE) cv_.notify_one();
        }

        // waits until curr chunk_ is full, copies into local copy and resets chunk_ if so
        IndexChunk persist_reset() {
            IndexChunk old_chunk;
            
            std::unique_lock<std::mutex> lock(m_);
            cv_.wait(lock, [this] { return chunk_.size() >= MAX_CHUNK_SIZE; });

            old_chunk = std::move(chunk_);
            chunk_ = IndexChunk();

            return old_chunk;
        };
};