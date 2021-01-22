#pragma once

#include "algorithm.hpp"
#include "config.hpp"
#include "worker.hpp"

#include <string>
#include <string_view>

#include <capnp/list.h>
#include <zmqpp/message.hpp>
#include <zmqpp/socket.hpp>

#include <mutex>

#include "commands.capnp.h"

#if DEBUG_NETWORK_REQUESTS
#	define DEBUG_NETWORK(x)                                                                         \
		do {                                                                                           \
			std::clog << x;                                                                              \
		} while (0)
#else
#	define DEBUG_NETWORK(x)
#endif

class CommandVisitor;

class WorkerCommand {
public:
	WorkerCommand(const std::string_view& command_string);
	virtual ~WorkerCommand();

	virtual void commandData(ProtocolCommand::Data::Builder& data_builder) const = 0;

	virtual void visit(CommandVisitor& visitor) const = 0;

	zmqpp::message toMessage() const;
	zmqpp::message& addToMessage(zmqpp::message& msg) const;

	static std::unique_ptr<WorkerCommand> fromSerialisedString(const std::string& message);

private:
	std::string command_string;
};

class WorkerHeloCommand : public WorkerCommand {
public:
	WorkerHeloCommand();
	virtual ~WorkerHeloCommand() = default;

	virtual void commandData(ProtocolCommand::Data::Builder& data_builder) const override;
	void visit(CommandVisitor& visitor) const override;

	static std::unique_ptr<WorkerHeloCommand> fromData();
};

class WorkerEhloCommand : public WorkerCommand {
public:
	WorkerEhloCommand();
	virtual ~WorkerEhloCommand() = default;

	virtual void commandData(ProtocolCommand::Data::Builder& data_builder) const override;
	void visit(CommandVisitor& visitor) const override;

	static std::unique_ptr<WorkerEhloCommand> fromData();
};

class WorkerJobCommand : public WorkerCommand {
public:
	WorkerJobCommand(const std::string& filename);
	virtual ~WorkerJobCommand() = default;

	virtual void commandData(ProtocolCommand::Data::Builder& data_builder) const override;
	void visit(CommandVisitor& visitor) const override;

	std::string getFilename() const;

	static std::unique_ptr<WorkerJobCommand> fromData(const capnp::Text::Reader filename);

protected:
	std::string filename;
};

class WorkerResultCommand : public WorkerCommand {
public:
	WorkerResultCommand(const std::string& filename, const Histogram& histogram);
	virtual ~WorkerResultCommand() = default;

	virtual void commandData(ProtocolCommand::Data::Builder& data_builder) const override;
	void visit(CommandVisitor& visitor) const override;

	static std::unique_ptr<WorkerResultCommand>
	fromData(const HistogramResult::Reader histogram_result);

	std::string getFilename() const;
	Histogram getHistogram() const;

protected:
	std::string filename;
	Histogram histogram;
};

class WorkerHeartbeatCommand : public WorkerCommand {
public:
	WorkerHeartbeatCommand(const std::string& peer_name);
	virtual ~WorkerHeartbeatCommand() = default;

	virtual void commandData(ProtocolCommand::Data::Builder& data_builder) const override;
	void visit(CommandVisitor& visitor) const override;

	std::string getPeerName() const;

	static std::unique_ptr<WorkerHeartbeatCommand> fromData(const capnp::Text::Reader peer_name);

protected:
	std::string peer_name;
};

class CommandVisitor {
public:
	CommandVisitor() = default;

	virtual void visitHelo(const WorkerHeloCommand& heloCommand);
	virtual void visitEhlo(const WorkerEhloCommand& ehloCommand);
	virtual void visitJob(const WorkerJobCommand& jobCommand);
	virtual void visitResult(const WorkerResultCommand& resultCommand);
	virtual void visitHeartbeat(const WorkerHeartbeatCommand& heartbeatCommand);
};

class ConnectingWorkerCommandVisitor : public CommandVisitor {
public:
	ConnectingWorkerCommandVisitor(ServerConnection::State& connectionState);

	void visitEhlo(const WorkerEhloCommand& ehloCommand) override;

protected:
	ServerConnection::State& connectionState;
};

class RunningWorkerCommandVisitor : public CommandVisitor {
public:
	RunningWorkerCommandVisitor(zmqpp::socket& socket);

	void visitHelo(const WorkerHeloCommand& heloCommand) override;
	void visitEhlo(const WorkerEhloCommand& ehloCommand) override;
	void visitJob(const WorkerJobCommand& jobCommand) override;
	void visitResult(const WorkerResultCommand& resultCommand) override;
	void visitHeartbeat(const WorkerHeartbeatCommand& heartbeatCommand) override;

protected:
	zmqpp::socket& socket;
};
