#pragma once

#include "algorithm.hpp"

#include <zmqpp/message.hpp>

std::string encodeHistogramResult(const std::string& filename, const Histogram& histogram);
std::pair<std::string, Histogram> decodeHistogramResult(const std::string serialised_string);

// ZeroMQ management section

zmqpp::message zmq_worker_helo();
