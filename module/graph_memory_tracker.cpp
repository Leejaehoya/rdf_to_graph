#include "graph_memory_tracker.hpp"

std::atomic<std::size_t> GraphMemoryTracker::bytes_{0};

void GraphMemoryTracker::add(std::size_t bytes) {
    bytes_ += bytes;
}

void GraphMemoryTracker::sub(std::size_t bytes) {
    bytes_ -= bytes;
}

std::size_t GraphMemoryTracker::current() {
    return bytes_.load();
}

void GraphMemoryTracker::reset() {
    bytes_.store(0);
}


