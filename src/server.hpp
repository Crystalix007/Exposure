#pragma once

#include <zmqpp/context.hpp>
#include <zmqpp/socket.hpp>

#include <filesystem>
#include <map>
#include <mutex>
#include <queue>
#include <string>

#include "algorithm.hpp"

class Server {
public:
	Server(zmqpp::context& context);

	void serve_work(const std::filesystem::path& serve_path);

protected:
	zmqpp::socket work_socket;

	std::queue<std::string> enqueued_work;
	std::recursive_mutex work_mutex;

	std::map<std::string, std::vector<std::string>> worker_queues{};
	std::recursive_mutex worker_mutex;

	std::map<std::string, Histogram> receive_work(size_t total_work_samples);
	void transmit_work(const std::string& worker);
};
