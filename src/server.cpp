#include "server.hpp"

#include "config.hpp"
#include "protocol.hpp"

#include <future>

#include <zmqpp/message.hpp>

Server::Server(zmqpp::context& context) : work_socket{ context, zmqpp::socket_type::router } {
	work_socket.bind("tcp://*:" + std::to_string(WORK_PORT));

	assert(work_socket);
}

void Server::serve_work(const std::filesystem::path& serve_path) {
	assert(std::filesystem::exists(serve_path));
	assert(std::filesystem::is_directory(serve_path));

	for (const auto& file : std::filesystem::directory_iterator{ serve_path }) {
		if (!std::filesystem::is_regular_file(file)) {
			continue;
		}

		enqueued_work.push(file.path());
	}

	std::future<std::map<std::string, Histogram>> receive_work_job =
	    std::async(std::launch::async, &Server::receive_work, this, enqueued_work.size());

	return receive_work_job.wait();
}

void Server::transmit_work(const std::string& worker) {
	std::unique_lock<std::recursive_mutex> work_lock{ work_mutex };
	std::unique_lock<std::recursive_mutex> worker_lock{ worker_mutex };

	// Only add more work if under threshold
	assert(worker_queues.at(worker).size() < MAX_WORKER_QUEUE);

	while (worker_queues.at(worker).size() < MAX_WORKER_QUEUE) {
		const std::string work_item = enqueued_work.front();
		zmqpp::message message{};
		message.add(worker);
		message << work_item;
		enqueued_work.pop();
		work_socket.send(message);
		worker_queues.at(worker).push_back(work_item);
	}
}

std::map<std::string, Histogram> Server::receive_work(size_t total_work_samples) {
	std::map<std::string, Histogram> work_results{};

	while (work_results.size() < total_work_samples) {
		zmqpp::message message{};
		work_socket.receive(message);

		std::string identity = message.get(0);

		std::unique_lock<std::recursive_mutex> worker_lock{ worker_mutex };

		if (message.get(1) == "HELO") {
			worker_queues.insert(std::make_pair(identity, std::vector<std::string>{}));
		} else {
			std::vector<std::string>& queue = worker_queues.at(identity);
			const auto result = decodeHistogramResult(message.get(1));
			work_results.insert(result);

			queue.erase(std::find(queue.begin(), queue.end(), result.first));
		}
		transmit_work(identity);
	}

	return work_results;
}
