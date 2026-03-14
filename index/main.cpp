#include "Index.h"
#include <thread>

int main(int argc, char argv[]) {
    unsigned int n_cores = std::thread::hardware_concurrency();

    if (argc != 2) {
        perror("Usage: ./index <worker number>");
        exit(1);
    }

    // Initialize the index
    WORKER_NUMBER = argv[1];
    init_index();

    // TODO: Spawn n_cores threads to pull from files queue and parse with index_file
    // To use our thread_pool, might have to rework IndexChunk format a bit

    return 0;
}