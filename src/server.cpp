#include "server.hpp"

#include "config.hpp"
#include "protocol.hpp"

#include <fstream>
#include <future>

#include <zmqpp/message.hpp>

Server::Server(zmqpp::context& context) : work_socket{ context, zmqpp::socket_type::router } {
	work_socket.bind("tcp://*:" + std::to_string(WORK_PORT));
	work_socket.set(zmqpp::socket_option::router_mandatory, true);
	work_socket.set(zmqpp::socket_option::immediate, true);

	assert(work_socket);
}

/* Cannot be run on multiple threads! */
void Server::serve_work(const std::filesystem::path& serve_path) {
	assert(std::filesystem::exists(serve_path));
	assert(std::filesystem::is_directory(serve_path));

	for (const auto& file : std::filesystem::directory_iterator{ serve_path }) {
		if (!std::filesystem::is_regular_file(file)) {
			continue;
		}

		enqueued_work.push(std::make_unique<WorkerHistogramJobCommand>(file.path()));
	}

	const size_t jobCount = enqueued_work.size();

#if DEBUG_SERVICE_DISCOVERY
	std::clog << "Serving histogram jobs for " << jobCount << " files.\n";
#endif

	std::future<std::map<std::string, Histogram>> receive_histograms_work_job =
	    std::async(std::launch::async, &Server::receive_histograms, this, jobCount);

	const auto histograms = receive_histograms_work_job.get();

	assert(enqueued_work.size() == 0);

	auto prev_histogram_pointer = histograms.begin();
	auto curr_histogram_pointer = std::next(prev_histogram_pointer);

	std::clog << "Calculating brightness variations over " << histograms.size() << " histograms.\n";

	enqueued_work.push(
	    std::make_unique<WorkerEqualisationJobCommand>(prev_histogram_pointer->first, 0.f, 0.f, 0.f));

	while (curr_histogram_pointer != histograms.end()) {
		const auto eqParams =
		    get_equalisation_parameters(prev_histogram_pointer->second, curr_histogram_pointer->second);
		const auto currFilename = curr_histogram_pointer->first;
		enqueued_work.push(std::make_unique<WorkerEqualisationJobCommand>(
		    currFilename, eqParams.shadowOffset, eqParams.midOffset, eqParams.highlightOffset));

		prev_histogram_pointer = curr_histogram_pointer;
		curr_histogram_pointer++;
	}

	std::clog << "Equalising brightness\n";

	assert(enqueued_work.size() == jobCount);

	const std::future<void> receive_images_work_job =
	    std::async(std::launch::async, &Server::receive_equalised, this, jobCount);

	receive_images_work_job.wait();
}

void Server::transmit_work(const std::string& worker) {
	std::unique_lock<std::recursive_mutex> work_lock{ work_mutex };
	std::unique_lock<std::recursive_mutex> worker_lock{ worker_mutex };

	assert(enqueued_work.size() > 0);

	// Only add more work if under threshold
	assert(worker_queues.at(worker).size() < MAX_WORKER_QUEUE);

	while (worker_queues.at(worker).size() < MAX_WORKER_QUEUE && enqueued_work.size() > 0) {
		WorkPtr work_item = std::move(enqueued_work.front());
		zmqpp::message message{};

		work_item->addToMessage(message);
		enqueued_work.pop();
		send_message(worker, std::move(message));
		worker_queues.at(worker).push_back(std::move(work_item));
	}
}

std::map<std::string, Histogram> Server::receive_histograms(size_t total_work_samples) {
	std::map<std::string, Histogram> work_results{};

	while (work_results.size() < total_work_samples) {
		zmqpp::message message{};
		work_socket.receive(message);

		std::string identity = message.get(0);

		std::unique_lock<std::recursive_mutex> worker_lock{ worker_mutex };

		ServerHistogramCommandVisitor commandVisitor{ *this, identity, work_results };

		std::unique_ptr<WorkerCommand> command = WorkerCommand::fromSerialisedString(message.get(1));
		command->visit(commandVisitor);
	}

	return work_results;
}

void Server::receive_equalised(size_t total_work_samples) {
	size_t cumulative_work_samples = 0;

	std::clog << "Serving jobs to " << worker_queues.size() << " existing workers.\n";

	for (const auto& [worker, _] : worker_queues) {
		if (enqueued_work.size() > 0) {
			this->transmit_work(worker);
		} else {
			break;
		}
	}

	while (cumulative_work_samples < total_work_samples) {
		zmqpp::message message{};
		work_socket.receive(message);

		std::string identity = message.get(0);

		std::unique_lock<std::recursive_mutex> worker_lock{ worker_mutex };

		ServerEqualisationCommandVisitor commandVisitor{ *this, identity, cumulative_work_samples };

		try {
			std::unique_ptr<WorkerCommand> command = WorkerCommand::fromSerialisedString(message.get(1));
			command->visit(commandVisitor);
		} catch (const std::exception& e) {
			std::clog << e.what() << "\n";
			throw e;
		}
	}
}

void Server::send_message(const std::string& worker, zmqpp::message message) {
	message.push_front(worker);
	work_socket.send(message);
}

ServerCommandVisitor::ServerCommandVisitor(Server& server, const std::string& worker_identity)
    : server{ server }, worker_identity{ worker_identity } {}

void ServerCommandVisitor::visitHelo(const WorkerHeloCommand& heloCommand) {
	/* Mark connection as successful / connected. */
	DEBUG_NETWORK("Visited Worker Helo\n");
	server.worker_queues.insert(std::make_pair(worker_identity, std::vector<Server::WorkPtr>{}));
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

void ServerCommandVisitor::visitHeartbeat(const WorkerHeartbeatCommand& heartbeatCommand) {
	DEBUG_NETWORK("Visited Worker Heartbeat\n");

	zmqpp::message command_message{
		WorkerHeartbeatCommand{ heartbeatCommand.getPeerName() }.toMessage()
	};

	server.send_message(heartbeatCommand.getPeerName(), std::move(command_message));
}

ServerHistogramCommandVisitor::ServerHistogramCommandVisitor(
    Server& server, const std::string& worker_identity,
    std::map<std::string, Histogram>& work_results)
    : ServerCommandVisitor{ server, worker_identity }, histogram_results{ work_results } {}

void ServerHistogramCommandVisitor::visitHistogramResult(
    const WorkerHistogramResultCommand& resultCommand) {
	/* Add result to list of histogram results. */
	DEBUG_NETWORK("Visited Worker Histogram Result\n");

	try {
		std::vector<Server::WorkPtr>& queue = server.worker_queues.at(worker_identity);
		histogram_results.insert(
		    std::make_pair(resultCommand.getFilename(), resultCommand.getHistogram()));

		queue.erase(
		    std::find_if(queue.begin(), queue.end(), [&resultCommand](const Server::WorkPtr& work) {
			    return *work.get() == resultCommand;
		    }));

		if (server.enqueued_work.size() != 0) {
			server.transmit_work(worker_identity);
		}
	} catch (std::out_of_range& exception) {
		std::clog << "Invalid result from unknown (unregistered) worker: " << worker_identity << "\n";
	}
}

void ServerHistogramCommandVisitor::visitEqualisationResult(
    const WorkerEqualisationResultCommand& resultCommand) {
	DEBUG_NETWORK("Visited (Unexpected) Worker Equalisation Result\n");
}

ServerEqualisationCommandVisitor::ServerEqualisationCommandVisitor(
    Server& server, const std::string& worker_identity, size_t& equalised_count)
    : ServerCommandVisitor{ server, worker_identity }, equalised_count{ equalised_count } {}

void ServerEqualisationCommandVisitor::visitHistogramResult(
    const WorkerHistogramResultCommand& resultCommand) {
	DEBUG_NETWORK("Visited (Unexpected) Worker Histogram Result. Should have completed all "
	              "histogram results already!\n");
}

void ServerEqualisationCommandVisitor::visitEqualisationResult(
    const WorkerEqualisationResultCommand& resultCommand) {
	DEBUG_NETWORK("Visited Worker Equalisation Result\n");

	std::ofstream resultOutput{ resultCommand.getFilename() + ".tiff",
		                          std::ios_base::binary | std::ios_base::out };
	const auto tiffData = resultCommand.getTiffData();

	resultOutput.write(reinterpret_cast<const char*>(tiffData.data()), tiffData.size());

	this->equalised_count++;
}
