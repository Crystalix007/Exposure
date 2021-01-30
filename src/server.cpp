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

#if DEBUG_SERVICE_DISCOVERY
	std::clog << "Serving histogram jobs for " << enqueued_work.size() << " files.\n";
#endif

	std::future<std::map<std::string, Histogram>> receive_work_job =
	    std::async(std::launch::async, &Server::receive_work, this, enqueued_work.size());

	return receive_work_job.wait();
}

void Server::transmit_work(const std::string& worker) {
	std::unique_lock<std::recursive_mutex> work_lock{ work_mutex };
	std::unique_lock<std::recursive_mutex> worker_lock{ worker_mutex };

	// Only add more work if under threshold
	try {
		assert(worker_queues.at(worker).size() < MAX_WORKER_QUEUE);
	} catch (std::out_of_range& exception) {
		// Insert empty queue of work for worker.
		worker_queues.insert(std::pair{ worker, std::vector<std::string>{} });
	}

	while (worker_queues.at(worker).size() < MAX_WORKER_QUEUE) {
		const std::string work_item = enqueued_work.front();
		zmqpp::message message{};

		WorkerHistogramJobCommand{ work_item }.addToMessage(message);
		enqueued_work.pop();
		send_message(worker, std::move(message));
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

		ServerCommandVisitor commandVisitor{ *this, identity, work_results };

		std::unique_ptr<WorkerCommand> command = WorkerCommand::fromSerialisedString(message.get(1));
		command->visit(commandVisitor);
	}

	return work_results;
}

void Server::send_message(const std::string& worker, zmqpp::message message) {
	message.push_front(worker);
	work_socket.send(message);
}

ServerCommandVisitor::ServerCommandVisitor(Server& server, std::string worker_identity,
                                           std::map<std::string, Histogram>& work_results)
    : server{ server }, worker_identity{ worker_identity }, work_results{ work_results } {}

void ServerCommandVisitor::visitHelo(const WorkerHeloCommand& heloCommand) {
	/* Mark connection as successful / connected. */
	DEBUG_NETWORK("Visited Worker Helo\n");
	server.send_message(worker_identity, WorkerEhloCommand{}.toMessage());

	server.transmit_work(worker_identity);
}

void ServerCommandVisitor::visitEhlo(const WorkerEhloCommand& ehloCommand) {
	/* Ignore unexpected message. */
	DEBUG_NETWORK("Visited Worker Ehlo (unexpected)\n");
}

void ServerCommandVisitor::visitHistogramJob(const WorkerHistogramJobCommand& jobCommand) {
	/* Ignore unexpected message. */
	DEBUG_NETWORK("Visited Worker Job (unexpected)\n");
}

void ServerCommandVisitor::visitEqualisationJob(const WorkerEqualisationJobCommand& jobCommand) {
	/* Ignore unexpected message. */
	DEBUG_NETWORK("Visited Worker Job (unexpected)\n");
}

void ServerCommandVisitor::visitHistogramResult(const WorkerHistogramResultCommand& resultCommand) {
	/* Add result to list of histogram results. */
	DEBUG_NETWORK("Visited Worker Result\n");

	try {
		std::vector<std::string>& queue = server.worker_queues.at(worker_identity);
		work_results.insert(std::make_pair(resultCommand.getFilename(), resultCommand.getHistogram()));

		queue.erase(std::find(queue.begin(), queue.end(), resultCommand.getFilename()));

		server.transmit_work(worker_identity);
	} catch (std::out_of_range& exception) {
		std::clog << "Invalid result from unknown (unregistered) worker: " << worker_identity << "\n";
	}
}

void ServerCommandVisitor::visitEqualisationResult(
    const WorkerEqualisationResultCommand& resultCommand) {
	/* TODO: Implement. */
	/* Add result to list of equalisation results, and save file somewhere. */
}

void ServerCommandVisitor::visitHeartbeat(const WorkerHeartbeatCommand& heartbeatCommand) {
	DEBUG_NETWORK("Visited Worker Heartbeat\n");

	zmqpp::message command_message{
		WorkerHeartbeatCommand{ heartbeatCommand.getPeerName() }.toMessage()
	};

	server.send_message(heartbeatCommand.getPeerName(), std::move(command_message));
}
