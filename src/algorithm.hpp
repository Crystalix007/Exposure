#pragma once

#include <array>
#include <optional>
#include <string>

#include "config.hpp"

using Histogram = std::array<float, histogramSegments>;

std::optional<Histogram> image_get_histogram(const std::string filename);
