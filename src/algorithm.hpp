#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "config.hpp"

using Histogram = std::array<float, histogramSegments>;

struct EqualisationParameters {
	float shadowOffset;
	float midOffset;
	float highlightOffset;
};

std::optional<Histogram> image_get_histogram(const std::string& filename);
EqualisationParameters get_equalisation_parameters(const Histogram& previousHistogram,
                                                   const Histogram& currentHistogram);
std::vector<std::uint8_t> image_equalise(const std::string& filename, const float shadowOffset,
                                         const float midOffset, const float highlightOffset);
