#pragma once

#include <cstdint>

#define DEBUG_SERVICE_DISCOVERY 0
#define DEBUG_NETWORK_REQUESTS 0

const constexpr std::uint16_t WORK_PORT = 42069u;
const constexpr std::uint32_t MAX_WORKER_QUEUE = 2u;
const constexpr uint32_t histogramSegments = 1ull << 10ull;
