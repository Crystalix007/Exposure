#pragma once

#include <cstdint>

#define DEBUG_SERVICE_DISCOVERY 1

const constexpr std::uint16_t WORK_PORT = 42069;
const constexpr std::uint16_t RESULT_PORT = WORK_PORT + 1;
const constexpr uint32_t histogramSegments = 1 << 10ull;
