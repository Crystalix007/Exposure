#pragma once

#include <zmqpp/context.hpp>
#include <zmqpp/socket.hpp>

#include <filesystem>
#include <map>
#include <mutex>
#include <queue>
#include <string>

#include "algorithm.hpp"
#include "protocol.hpp"

class ServerCommandVisitor;
class ServerHistogramCommandVisitor;

class Server {
public:
	Server(zmqpp::context& context);

	void serve_work(const std::filesystem::path& serve_path);

protected:
	zmqpp::socket work_socket;

	using WorkPtr = std::unique_ptr<WorkerJobCommand>;

	std::queue<WorkPtr> enqueued_work;
	std::recursive_mutex work_mutex;

	std::map<std::string, std::vector<WorkPtr>> worker_queues{};
	std::recursive_mutex worker_mutex;

	std::map<std::string, Histogram> receive_histograms(size_t total_work_samples);
	void receive_equalised(size_t total_work_samples);
	void transmit_work(const std::string& worker);

	void send_message(const std::string& worker, zmqpp::message message);

	friend ServerCommandVisitor;
	friend ServerHistogramCommandVisitor;
};

class ServerCommandVisitor : public CommandVisitor {
public:
	ServerCommandVisitor(Server& server, const std::string& worker_identity);
	virtual ~ServerCommandVisitor() = default;

	void visitHelo(const WorkerHeloCommand& heloCommand) override;
	void visitEhlo(const WorkerEhloCommand& ehloCommand) override;
	void visitHistogramJob(const WorkerHistogramJobCommand& jobCommand) override;
	void visitEqualisationJob(const WorkerEqualisationJobCommand& jobCommand) override;
	void visitHeartbeat(const WorkerHeartbeatCommand& heartbeatCommand) override;

protected:
	Server& server;
	const std::string& worker_identity;
};

class ServerHistogramCommandVisitor : public ServerCommandVisitor {
public:
	ServerHistogramCommandVisitor(Server& server, const std::string& worker_identity,
	                              std::map<std::string, Histogram>& work_results);

	void visitHistogramResult(const WorkerHistogramResultCommand& resultCommand) override;
	void visitEqualisationResult(const WorkerEqualisationResultCommand& resultCommand) override;

protected:
	std::map<std::string, Histogram>& histogram_results;
};

class ServerEqualisationCommandVisitor : public ServerCommandVisitor {
public:
	ServerEqualisationCommandVisitor(Server& server, const std::string& worker_identity,
	                                 size_t& equalised_count);

	void visitHistogramResult(const WorkerHistogramResultCommand& resultCommand) override;
	void visitEqualisationResult(const WorkerEqualisationResultCommand& resultCommand) override;

protected:
	size_t& equalised_count;
};
