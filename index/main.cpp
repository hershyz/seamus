#include "Index.h"
#include <thread>

int main(int argc, char argv[]) {
    unsigned int n_cores = std::thread::hardware_concurrency();

    if (argc != 2) {
        perror("Usage: ./index <worker number>");
        exit(1);
    }

    WORKER_NUMBER = argv[1];

    // TODO: Fill links queue, init index
    return 0;
}