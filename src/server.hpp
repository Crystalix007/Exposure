#pragma once

#include <zmqpp/context.hpp>
#include <zmqpp/socket.hpp>

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "algorithm.hpp"

class Server {
public:
	Server(zmqpp::context& context);

	void serve_work(const std::filesystem::path& serve_path);

protected:
	zmqpp::socket push_socket;
	zmqpp::socket pull_socket;

	void transmit_work(const std::vector<std::string>& files);
	std::map<std::string, Histogram> receive_work(size_t total_work_samples);
};
