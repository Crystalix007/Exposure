#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <vector>
#include <zmqpp/context.hpp>
#include <zmqpp/socket.hpp>

#include "algorithm.hpp"
#include "protocol.hpp"

class ServerCommandVisitor;
class ServerHistogramCommandVisitor;
class ServerEqualisationCommandVisitor;

namespace zmqpp {
	class context;
	class message;
} // namespace zmqpp

using WorkPtr = std::unique_ptr<WorkerJobCommand>;
using Timestamp = std::chrono::time_point<std::chrono::system_clock>;

struct WorkerData {
	std::vector<WorkPtr> work;
	Timestamp last_heartbeat_request;
	bool heartbeat_reply_received;
	std::uint32_t concurrency;
};

class Server {
public:
	Server(zmqpp::context& context);

	void serve_work(const std::filesystem::path& servePath);

protected:
	zmqpp::socket work_socket;

	std::queue<WorkPtr> enqueued_work;
	std::recursive_mutex work_mutex;

	std::map<std::string, WorkerData> worker_queues{};
	std::recursive_mutex worker_mutex;

	[[nodiscard]] std::map<std::string, Histogram> receive_histograms(size_t totalWorkSamples);
	void receive_equalised(size_t totalWorkSamples);
	void transmit_work(const std::string& worker);

	void send_message(const std::string& worker, zmqpp::message message);

	// Dismisses worker and reallocates work onto the queue
	void dismiss_worker(const std::string& worker);
	void dismiss_workers();
	void send_heartbeats();

	friend ServerCommandVisitor;
	friend ServerHistogramCommandVisitor;
	friend ServerEqualisationCommandVisitor;
};

class ServerCommandVisitor : public CommandVisitor {
public:
	ServerCommandVisitor(Server& server, const std::string& workerIdentity);
	virtual ~ServerCommandVisitor() = default;

	void visit_helo(const WorkerHeloCommand& heloCommand) override;
	void visit_ehlo(const WorkerEhloCommand& ehloCommand) override;
	void visit_histogram_job(const WorkerHistogramJobCommand& jobCommand) override;
	void visit_equalisation_job(const WorkerEqualisationJobCommand& jobCommand) override;
	void visit_heartbeat(const WorkerHeartbeatCommand& heartbeatCommand) override;
	void visit_bye(const WorkerByeCommand& byeCommand) override;

protected:
	Server& server;
	const std::string& worker_identity;
};

class ServerHistogramCommandVisitor : public ServerCommandVisitor {
public:
	ServerHistogramCommandVisitor(Server& server, const std::string& workerIdentity,
	                              std::map<std::string, Histogram>& workResults);

	void visit_histogram_result(const WorkerHistogramResultCommand& resultCommand) override;
	void visit_equalisation_result(const WorkerEqualisationResultCommand& resultCommand) override;

protected:
	std::map<std::string, Histogram>& histogram_results;
};

class ServerEqualisationCommandVisitor : public ServerCommandVisitor {
public:
	ServerEqualisationCommandVisitor(Server& server, const std::string& workerIdentity,
	                                 size_t& equalisedCount);

	void visit_histogram_result(const WorkerHistogramResultCommand& resultCommand) override;
	void visit_equalisation_result(const WorkerEqualisationResultCommand& resultCommand) override;

protected:
	size_t& equalised_count;
};
