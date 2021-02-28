#pragma once

#include <cstdint>
#include <chrono>

#define DEBUG_SERVICE_DISCOVERY 0
#define DEBUG_NETWORK_REQUESTS 0

const constexpr std::uint16_t WORK_PORT = 42069U;
const constexpr std::uint32_t MAX_WORKER_QUEUE = 32U;
const constexpr uint32_t HISTOGRAM_SEGMENTS = 1ULL << 10ULL;

// By default, to be safe, allow 64MB chunks
// Increasing this value will decrease overhead, at the potential cost of compatibility
// Should not be increased beyond [512 MB](https://stackoverflow.com/a/48492369)
const constexpr std::uint64_t MAX_CHUNK_SIZE = 64 * 1024ULL * 1024ULL;

// By default, to be safe, allow up to 256 chunks
const constexpr std::uint64_t MAX_MESSAGE_SIZE = 256 * MAX_CHUNK_SIZE;

// Max interval between heartbeat request and responses before a peer is considered "dead"
const constexpr std::chrono::seconds MAX_HEARTBEAT_INTERVAL{ 5 };
