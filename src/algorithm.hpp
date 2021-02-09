#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "config.hpp"

using Histogram = std::array<float, histogramSegments>;
using EqualisationHistogramMapping = std::array<uint32_t, histogramSegments>;

std::optional<Histogram> image_get_histogram(const std::string& filename);
EqualisationHistogramMapping identity_equalisation_histogram_mapping();
EqualisationHistogramMapping get_equalisation_parameters(const Histogram& previousHistogram,
                                                         const Histogram& currentHistogram);
std::vector<std::uint8_t> image_equalise(const std::string& filename,
                                         const EqualisationHistogramMapping& mapping);
