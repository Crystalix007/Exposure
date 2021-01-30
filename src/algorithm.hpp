#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "config.hpp"

using Histogram = std::array<float, histogramSegments>;

std::optional<Histogram> image_get_histogram(const std::string& filename);
std::vector<std::uint8_t> image_equalise(const std::string& filename, const float shadowOffset,
                                         const float midOffset, const float highlightOffset);
