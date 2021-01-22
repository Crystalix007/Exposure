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

	void send_message(const std::string& worker, zmqpp::message message);

	friend ServerCommandVisitor;
};

class ServerCommandVisitor : public CommandVisitor {
public:
	ServerCommandVisitor(Server& server, std::string worker_identity,
	                     std::map<std::string, Histogram>& work_results);

	void visitHelo(const WorkerHeloCommand& heloCommand) override;
	void visitEhlo(const WorkerEhloCommand& ehloCommand) override;
	void visitJob(const WorkerJobCommand& jobCommand) override;
	void visitResult(const WorkerResultCommand& resultCommand) override;
	void visitHeartbeat(const WorkerHeartbeatCommand& heartbeatCommand) override;

protected:
	Server& server;
	std::string worker_identity;
	std::map<std::string, Histogram>& work_results;
};
