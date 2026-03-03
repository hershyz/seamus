#include "crawler_node.h"

// TODO: This needs to be defined globally on bootup (defining here for now)
const uint32_t NUM_THREADS = 16;
const uint32_t WORKER_NUMBER = 0;

int main(int argc, char* argv[]) {
    // Initialize crawler node with config (e.g. worker id, frontier snapshot interval, etc.)
    CrawlerNode node(WORKER_NUMBER, NUM_THREADS);

    // spin up network listeners for new URL content from other machines

    // Start crawler node
    node.start();

    // any other logic

    node.stop();
}