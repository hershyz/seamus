#include "crawler_node.h"

int main(int argc, char* argv[]) {
    // Initialize crawler node with config (e.g. worker id, frontier snapshot interval, etc.)
    CrawlerNode node(config);

    // Start crawler node
    node.start();

    // Wait for shutdown signal (e.g. SIGINT)
    wait_for_shutdown_signal();
}