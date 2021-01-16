#include "server.hpp"

#include "config.hpp"
#include "protocol.hpp"

#include <future>

#include <zmqpp/message.hpp>

Server::Server(zmqpp::context& context)
    : push_socket{ context, zmqpp::socket_type::push }, pull_socket{ context,
	                                                                   zmqpp::socket_type::pull } {
	push_socket.bind("tcp://*:" + std::to_string(WORK_PORT));
	pull_socket.bind("tcp://*:" + std::to_string(RESULT_PORT));

	push_socket.set(zmqpp::socket_option::send_high_water_mark, 1);

	assert(push_socket);
}

void Server::serve_work(const std::filesystem::path& serve_path) {
	assert(std::filesystem::exists(serve_path));
	assert(std::filesystem::is_directory(serve_path));

	std::vector<std::string> files{};

	for (const auto& file : std::filesystem::directory_iterator{ serve_path }) {
		if (!std::filesystem::is_regular_file(file)) {
			continue;
		}

		files.push_back(file.path());
	}

	std::future<void> transmit_work_job =
	    std::async(std::launch::async, &Server::transmit_work, this, files);
	std::future<std::map<std::string, Histogram>> receive_work_job =
	    std::async(std::launch::async, &Server::receive_work, this, files.size());

	transmit_work_job.wait();
	return receive_work_job.wait();
}

void Server::transmit_work(const std::vector<std::string>& files) {
	for (const auto& file : files) {
		zmqpp::message message{};
		message << file;
		push_socket.send(message);
	}
}

std::map<std::string, Histogram> Server::receive_work(size_t total_work_samples) {
	std::map<std::string, Histogram> work_results{};

	while (work_results.size() < total_work_samples) {
		zmqpp::message message{};
		pull_socket.receive(message);

		const auto result = decodeHistogramResult(message.get(0));
		work_results.insert(result);
	}

	return work_results;
}
