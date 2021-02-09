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
	virtual void visit(CommandVisitor& visitor) const override;

	static std::unique_ptr<WorkerHeloCommand> fromData();
};

class WorkerEhloCommand : public WorkerCommand {
public:
	WorkerEhloCommand();
	virtual ~WorkerEhloCommand() = default;

	virtual void commandData(ProtocolCommand::Data::Builder& data_builder) const override;
	virtual void visit(CommandVisitor& visitor) const override;

	static std::unique_ptr<WorkerEhloCommand> fromData();
};

class WorkerResultCommand;
class WorkerHistogramResultCommand;
class WorkerEqualisationResultCommand;

class WorkerJobCommand : public WorkerCommand {
public:
	WorkerJobCommand(const std::string& job_type);
	virtual ~WorkerJobCommand() = default;

	virtual void commandData(ProtocolCommand::Data::Builder& data_builder) const override;

	// Require child classes to be able to build the job component of command's data
	virtual void commandData(ProtocolJob::Data::Builder& data_builder) const = 0;

	static std::unique_ptr<WorkerJobCommand> fromData(const ProtocolJob::Reader reader);

	virtual bool operator==(const WorkerJobCommand& other) const;
	virtual bool operator==(const WorkerResultCommand& resultCommand) const;

protected:
	std::string job_type;

	friend WorkerResultCommand;
};

class WorkerHistogramJobCommand : public WorkerJobCommand {
public:
	WorkerHistogramJobCommand(const std::string& filename);

	virtual void commandData(ProtocolJob::Data::Builder& data_builder) const override;
	virtual void visit(CommandVisitor& visitor) const override;

	std::string getFilename() const;

	static std::unique_ptr<WorkerHistogramJobCommand> fromData(const HistogramJob::Reader reader);

	virtual bool operator==(const WorkerJobCommand& other) const override;
	virtual bool operator==(const WorkerResultCommand& other) const override;

protected:
	std::string filename;

	friend WorkerHistogramResultCommand;
};

class WorkerEqualisationJobCommand : public WorkerJobCommand {
public:
	WorkerEqualisationJobCommand(const std::string& filename, float shadowOffset, float midOffset,
	                             float highlightOffset);

	virtual void commandData(ProtocolJob::Data::Builder& data_builder) const override;
	virtual void visit(CommandVisitor& visitor) const override;

	std::string getFilename() const;
	float getShadowOffset() const;
	float getMidOffset() const;
	float getHighlightOffset() const;

	static std::unique_ptr<WorkerEqualisationJobCommand>
	fromData(const EqualisationJob::Reader reader);

	virtual bool operator==(const WorkerJobCommand& other) const override;
	virtual bool operator==(const WorkerResultCommand& other) const override;

protected:
	std::string filename;
	float shadowOffset;
	float midOffset;
	float highlightOffset;

	friend WorkerEqualisationResultCommand;
};

class WorkerResultCommand : public WorkerCommand {
public:
	WorkerResultCommand(const std::string& result_type);
	virtual ~WorkerResultCommand() = default;

	virtual void commandData(ProtocolCommand::Data::Builder& data_builder) const override;

	// Require child classes to be able to build the result component of command's data
	virtual void commandData(ProtocolResult::Data::Builder& data_builder) const = 0;

	static std::unique_ptr<WorkerResultCommand> fromData(const ProtocolResult::Reader reader);

	virtual bool operator==(const WorkerJobCommand& jobCommand) const;
	virtual bool operator==(const WorkerResultCommand& other) const;

protected:
	std::string result_type;

	friend WorkerJobCommand;
};

class WorkerHistogramResultCommand : public WorkerResultCommand {
public:
	WorkerHistogramResultCommand(const std::string& filename, const Histogram& histogram);
	virtual ~WorkerHistogramResultCommand() = default;

	virtual void commandData(ProtocolResult::Data::Builder& data_builder) const override;
	virtual void visit(CommandVisitor& visitor) const override;

	static std::unique_ptr<WorkerHistogramResultCommand>
	fromData(const HistogramResult::Reader histogram_result);

	std::string getFilename() const;
	Histogram getHistogram() const;

	virtual bool operator==(const WorkerJobCommand& jobCommand) const override;
	virtual bool operator==(const WorkerResultCommand& other) const override;

protected:
	std::string filename;
	Histogram histogram;

	friend WorkerHistogramJobCommand;
};

class WorkerEqualisationResultCommand : public WorkerResultCommand {
public:
	WorkerEqualisationResultCommand(const std::string& filename,
	                                const std::vector<std::uint8_t>& tiff_data);
	virtual ~WorkerEqualisationResultCommand() = default;

	virtual void commandData(ProtocolResult::Data::Builder& data_builder) const override;
	virtual void visit(CommandVisitor& visitor) const override;

	static std::unique_ptr<WorkerEqualisationResultCommand>
	fromData(const EqualisationResult::Reader equalisation_result);

	std::string getFilename() const;
	std::vector<std::uint8_t> getTiffData() const;

	virtual bool operator==(const WorkerJobCommand& jobCommand) const override;
	virtual bool operator==(const WorkerResultCommand& other) const override;

protected:
	std::string filename;
	std::vector<std::uint8_t> tiff_data;

	friend WorkerEqualisationJobCommand;
};

class WorkerHeartbeatCommand : public WorkerCommand {
public:
	WorkerHeartbeatCommand(const std::string& peer_name);
	virtual ~WorkerHeartbeatCommand() = default;

	virtual void commandData(ProtocolCommand::Data::Builder& data_builder) const override;
	virtual void visit(CommandVisitor& visitor) const override;

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
	virtual void visitHistogramJob(const WorkerHistogramJobCommand& jobCommand);
	virtual void visitEqualisationJob(const WorkerEqualisationJobCommand& jobCommand);
	virtual void visitHistogramResult(const WorkerHistogramResultCommand& resultCommand);
	virtual void visitEqualisationResult(const WorkerEqualisationResultCommand& resultCommand);
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
	void visitHistogramJob(const WorkerHistogramJobCommand& jobCommand) override;
	void visitEqualisationJob(const WorkerEqualisationJobCommand& jobCommand) override;
	void visitHistogramResult(const WorkerHistogramResultCommand& resultCommand) override;
	void visitEqualisationResult(const WorkerEqualisationResultCommand& resultCommand) override;
	void visitHeartbeat(const WorkerHeartbeatCommand& heartbeatCommand) override;

protected:
	zmqpp::socket& socket;
};
