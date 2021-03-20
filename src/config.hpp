#pragma once

#include <chrono>
#include <cstdint>

#define DEBUG_SERVICE_DISCOVERY 0
#define DEBUG_NETWORK_REQUESTS 0

const constexpr std::uint16_t MAX_PORT_STR_SIZE =
    6; // i.e. port 65535 occupies 6 characters including null sentinel

const constexpr std::uint16_t WORK_PORT = 42069U;
const constexpr std::uint16_t COMMUNICATION_PORT = WORK_PORT + 1;
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

// Whether the libraries linked with already have parallelism enabled.
// Assume no library parallelism if not defined.
const constexpr bool LIBRARY_PARALLELISM = false;

// The maximum expected hardware concurrency in threads. Used solely to define communication
// semaphore limits. With threaded ImageMagick, this is forced to be 1, as
// using only a single thread avoids some parallelism overhead.
const constexpr std::uint32_t MAX_HARDWARE_CONCURRENCY = LIBRARY_PARALLELISM ? 1U : 16384U;

// How many letters are used to uniquely identify each worker communicating across ZMQ sockets.
// Bounds how many nodes can join the compute cluster, however, with 256^5
// potential names, the chance of a birthday collision is very slim for
// reasonably sized compute clusters (~0.1% for 1 billion nodes).
const constexpr std::uint16_t WORKER_ID_LETTER_COUNT = 5;
