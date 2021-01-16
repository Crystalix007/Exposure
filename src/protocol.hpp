#pragma once

#include "algorithm.hpp"

std::string encodeHistogramResult(const std::string& filename, const Histogram& histogram);
std::pair<std::string, Histogram> decodeHistogramResult(const std::string serialised_string);
