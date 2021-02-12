#pragma once

#include <cstdint>

#define DEBUG_SERVICE_DISCOVERY 0
#define DEBUG_NETWORK_REQUESTS 0

const constexpr std::uint16_t WORK_PORT = 42069U;
const constexpr std::uint32_t MAX_WORKER_QUEUE = 2U;
const constexpr uint32_t HISTOGRAM_SEGMENTS = 1ULL << 10ULL;
const constexpr std::uint64_t MAX_MESSAGE_SIZE = 4ULL * 1024ULL * 1024ULL * 1024ULL;
