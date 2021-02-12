#include "server.hpp"

#include <algorithm>
#include <array>
#include <bits/exception.h>
#include <cassert>
#include <cxxabi.h>
#include <fstream>
#include <future>
#include <iterator>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <zmqpp/message.hpp>
#include <zmqpp/socket_options.hpp>
#include <zmqpp/socket_types.hpp>

#include "config.hpp"
#include "protocol.hpp"

namespace zmqpp {
	class context;
} // namespace zmqpp

Server::Server(zmqpp::context& context) : work_socket{ context, zmqpp::socket_type::router } {
	work_socket.bind("tcp://*:" + std::to_string(WORK_PORT));
	work_socket.set(zmqpp::socket_option::router_mandatory, true);
	work_socket.set(zmqpp::socket_option::immediate, true);

	assert(work_socket);
}

/* Cannot be run on multiple threads! */
void Server::serve_work(const std::filesystem::path& servePath) {
	assert(std::filesystem::exists(servePath));
	assert(std::filesystem::is_directory(servePath));

	for (const auto& file : std::filesystem::directory_iterator{ servePath }) {
		if (!std::filesystem::is_regular_file(file)) {
			continue;
		}

		enqueued_work.push(std::make_unique<WorkerHistogramJobCommand>(file.path()));
	}

	const size_t jobCount = enqueued_work.size();

#if DEBUG_SERVICE_DISCOVERY
	std::clog << "Serving histogram jobs for " << jobCount << " files.\n";
#endif

	std::future<std::map<std::string, Histogram>> receiveHistogramsWorkJob =
	    std::async(std::launch::async, &Server::receive_histograms, this, jobCount);

	const auto histograms = receiveHistogramsWorkJob.get();

	assert(enqueued_work.empty());

	auto prevHistogramPointer = histograms.begin();
	auto currHistogramPointer = std::next(prevHistogramPointer);

	std::clog << "Calculating brightness variations over " << histograms.size() << " histograms.\n";

	enqueued_work.push(std::make_unique<WorkerEqualisationJobCommand>(
	    prevHistogramPointer->first, identity_equalisation_histogram_mapping()));

	while (currHistogramPointer != histograms.end()) {
		const auto eqParams =
		    get_equalisation_parameters(prevHistogramPointer->second, currHistogramPointer->second);
		const auto currFilename = currHistogramPointer->first;
		enqueued_work.push(std::make_unique<WorkerEqualisationJobCommand>(currFilename, eqParams));

		prevHistogramPointer = currHistogramPointer;
		currHistogramPointer++;
	}

	std::clog << "Equalising brightness\n";

	assert(enqueued_work.size() == jobCount);

	const std::future<void> receiveImagesWorkJob =
	    std::async(std::launch::async, &Server::receive_equalised, this, jobCount);

	receiveImagesWorkJob.wait();

	this->dismiss_workers();
}

void Server::transmit_work(const std::string& worker) {
	std::unique_lock<std::recursive_mutex> workLock{ work_mutex };
	std::unique_lock<std::recursive_mutex> workerLock{ worker_mutex };

	assert(!enqueued_work.empty());

	// Only add more work if under threshold
	assert(worker_queues.at(worker).size() < MAX_WORKER_QUEUE);

	while (worker_queues.at(worker).size() < MAX_WORKER_QUEUE && !enqueued_work.empty()) {
		WorkPtr workItem = std::move(enqueued_work.front());
		zmqpp::message message{};

		workItem->add_to_message(message);
		enqueued_work.pop();
		send_message(worker, std::move(message));
		worker_queues.at(worker).push_back(std::move(workItem));
	}
}

std::map<std::string, Histogram> Server::receive_histograms(size_t totalWorkSamples) {
	std::map<std::string, Histogram> workResults{};

	while (workResults.size() < totalWorkSamples) {
		zmqpp::message message{};
		work_socket.receive(message);

		std::string identity = message.get(0);

		std::unique_lock<std::recursive_mutex> workerLock{ worker_mutex };

		ServerHistogramCommandVisitor commandVisitor{ *this, identity, workResults };

		std::unique_ptr<WorkerCommand> command = WorkerCommand::from_serialised_string(message.get(1));
		command->visit(commandVisitor);
	}

	return workResults;
}

void Server::receive_equalised(size_t totalWorkSamples) {
	size_t cumulativeWorkSamples = 0;

	std::clog << "Serving jobs to " << worker_queues.size() << " existing workers.\n";

	for (const auto& [worker, _] : worker_queues) {
		if (!enqueued_work.empty()) {
			this->transmit_work(worker);
		} else {
			break;
		}
	}

	while (cumulativeWorkSamples < totalWorkSamples) {
		zmqpp::message message{};
		work_socket.receive(message);

		std::string identity = message.get(0);

		std::unique_lock<std::recursive_mutex> workerLock{ worker_mutex };

		ServerEqualisationCommandVisitor commandVisitor{ *this, identity, cumulativeWorkSamples };

		try {
			std::unique_ptr<WorkerCommand> command =
			    WorkerCommand::from_serialised_string(message.get(1));
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

void Server::dismiss_workers() {
	std::unique_lock<std::recursive_mutex> workLock{ this->worker_mutex };
	std::unique_lock<std::recursive_mutex> workerLock{ this->work_mutex };

	for (const auto& [worker, _] : worker_queues) {
		this->send_message(worker, WorkerByeCommand{}.to_message());
	}

	// Mark workers as dismissed in local state
	worker_queues.clear();
}

ServerCommandVisitor::ServerCommandVisitor(Server& server, const std::string& workerIdentity)
    : server{ server }, worker_identity{ workerIdentity } {}

void ServerCommandVisitor::visit_helo(const WorkerHeloCommand& heloCommand) {
	/* Mark connection as successful / connected. */
	DEBUG_NETWORK("Visited Worker Helo\n");
	server.worker_queues.insert(std::make_pair(worker_identity, std::vector<Server::WorkPtr>{}));
	server.send_message(worker_identity, WorkerEhloCommand{}.to_message());

	server.transmit_work(worker_identity);
}

void ServerCommandVisitor::visit_ehlo(const WorkerEhloCommand& ehloCommand) {
	/* Ignore unexpected message. */
	DEBUG_NETWORK("Visited Worker Ehlo (unexpected)\n");
}

void ServerCommandVisitor::visit_histogram_job(const WorkerHistogramJobCommand& jobCommand) {
	/* Ignore unexpected message. */
	DEBUG_NETWORK("Visited Worker Job (unexpected)\n");
}

void ServerCommandVisitor::visit_equalisation_job(const WorkerEqualisationJobCommand& jobCommand) {
	/* Ignore unexpected message. */
	DEBUG_NETWORK("Visited Worker Job (unexpected)\n");
}

void ServerCommandVisitor::visit_heartbeat(const WorkerHeartbeatCommand& heartbeatCommand) {
	DEBUG_NETWORK("Visited Worker Heartbeat\n");

	zmqpp::message commandMessage{
		WorkerHeartbeatCommand{ heartbeatCommand.get_peer_name() }.to_message()
	};

	server.send_message(heartbeatCommand.get_peer_name(), std::move(commandMessage));
}

void ServerCommandVisitor::visit_bye(const WorkerByeCommand& byeCommand) {
	DEBUG_NETWORK("Visited Worker Bye\n");

	std::unique_lock<std::recursive_mutex> workLock{ this->server.work_mutex };
	std::unique_lock<std::recursive_mutex> workerLock{ this->server.worker_mutex };

	const auto workerJobsIter = this->server.worker_queues.find(worker_identity);

	if (workerJobsIter != this->server.worker_queues.end()) {
		auto& workerJobs = workerJobsIter->second;

		while (!workerJobs.empty()) {
			auto&& job = std::move(workerJobs.back());
			this->server.enqueued_work.push(std::move(job));
			workerJobs.pop_back();
		}
	}

	this->server.worker_queues.erase(workerJobsIter);
}

ServerHistogramCommandVisitor::ServerHistogramCommandVisitor(
    Server& server, const std::string& workerIdentity,
    std::map<std::string, Histogram>& workResults)
    : ServerCommandVisitor{ server, workerIdentity }, histogram_results{ workResults } {}

void ServerHistogramCommandVisitor::visit_histogram_result(
    const WorkerHistogramResultCommand& resultCommand) {
	/* Add result to list of histogram results. */
	DEBUG_NETWORK("Visited Worker Histogram Result\n");

	try {
		std::vector<Server::WorkPtr>& queue = server.worker_queues.at(worker_identity);
		histogram_results.insert(
		    std::make_pair(resultCommand.get_filename(), resultCommand.get_histogram()));

		queue.erase(
		    std::find_if(queue.begin(), queue.end(), [&resultCommand](const Server::WorkPtr& work) {
			    return *work.get() == resultCommand;
		    }));

		if (!server.enqueued_work.empty()) {
			server.transmit_work(worker_identity);
		}
	} catch (std::out_of_range& exception) {
		std::clog << "Invalid result from unknown (unregistered) worker: " << worker_identity << "\n";
	}
}

void ServerHistogramCommandVisitor::visit_equalisation_result(
    const WorkerEqualisationResultCommand& resultCommand) {
	DEBUG_NETWORK("Visited (Unexpected) Worker Equalisation Result\n");
}

ServerEqualisationCommandVisitor::ServerEqualisationCommandVisitor(
    Server& server, const std::string& workerIdentity, size_t& equalisedCount)
    : ServerCommandVisitor{ server, workerIdentity }, equalised_count{ equalisedCount } {}

void ServerEqualisationCommandVisitor::visit_histogram_result(
    const WorkerHistogramResultCommand& resultCommand) {
	DEBUG_NETWORK("Visited (Unexpected) Worker Histogram Result. Should have completed all "
	              "histogram results already!\n");
}

void ServerEqualisationCommandVisitor::visit_equalisation_result(
    const WorkerEqualisationResultCommand& resultCommand) {
	DEBUG_NETWORK("Visited Worker Equalisation Result\n");

	std::ofstream resultOutput{ resultCommand.get_filename() + ".tiff",
		                          std::ios_base::binary | std::ios_base::out };
	const auto tiffData = resultCommand.get_tiff_data();

	resultOutput.write(reinterpret_cast<const char*>(tiffData.data()), tiffData.size());

	this->equalised_count++;
}
