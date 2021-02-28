#pragma once

#include <capnp/blob.h>
#include <capnp/list.h>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>
#include <zmqpp/message.hpp>
#include <zmqpp/socket.hpp>

#include "algorithm.hpp"
#include "commands.capnp.h"
#include "config.hpp"
#include "worker.hpp"

namespace zmqpp {
	class socket;
} // namespace zmqpp

#if DEBUG_NETWORK_REQUESTS // NOLINTNEXTLINE(bugprone-macro-parentheses)
#	define DEBUG_NETWORK(x) do { std::clog << x; } while (0)
#else
#	define DEBUG_NETWORK(x)
#endif

class CommandVisitor;

class WorkerCommand {
public:
	WorkerCommand(std::string commandString);
	virtual ~WorkerCommand();

	virtual void command_data(ProtocolCommand::Data::Builder& dataBuilder) const = 0;

	virtual void visit(CommandVisitor& visitor) const = 0;

	[[nodiscard]] zmqpp::message to_message() const;
	zmqpp::message& add_to_message(zmqpp::message& msg) const;

	static std::unique_ptr<WorkerCommand> from_serialised_string(const std::string& serialisedString);

private:
	const std::string command_string;
};

class WorkerHeloCommand : public WorkerCommand {
public:
	WorkerHeloCommand(std::uint32_t concurrency);
	~WorkerHeloCommand() override = default;

	void command_data(ProtocolCommand::Data::Builder& dataBuilder) const override;
	void visit(CommandVisitor& visitor) const override;

	[[nodiscard]] std::uint32_t get_concurrency() const;

	static std::unique_ptr<WorkerHeloCommand> from_data(ProtocolHelo::Reader reader);

protected:
	std::uint32_t concurrency;
};

class WorkerEhloCommand : public WorkerCommand {
public:
	WorkerEhloCommand();
	~WorkerEhloCommand() override = default;

	void command_data(ProtocolCommand::Data::Builder& dataBuilder) const override;
	void visit(CommandVisitor& visitor) const override;

	static std::unique_ptr<WorkerEhloCommand> from_data();
};

class WorkerResultCommand;
class WorkerHistogramResultCommand;
class WorkerEqualisationResultCommand;

class WorkerJobCommand : public WorkerCommand {
public:
	WorkerJobCommand(std::string jobType);
	~WorkerJobCommand() override = default;

	void command_data(ProtocolCommand::Data::Builder& dataBuilder) const override;

	// Require child classes to be able to build the job component of command's data
	virtual void command_data(ProtocolJob::Data::Builder& dataBuilder) const = 0;

	static std::unique_ptr<WorkerJobCommand> from_data(ProtocolJob::Reader reader);

	virtual bool operator==(const WorkerJobCommand& other) const;
	virtual bool operator==(const WorkerResultCommand& other) const;

protected:
	std::string job_type;

	friend WorkerResultCommand;
};

class WorkerHistogramJobCommand : public WorkerJobCommand {
public:
	WorkerHistogramJobCommand(std::string filename);

	void command_data(ProtocolJob::Data::Builder& dataBuilder) const override;
	void visit(CommandVisitor& visitor) const override;

	[[nodiscard]] std::string get_filename() const;

	static std::unique_ptr<WorkerHistogramJobCommand> from_data(HistogramJob::Reader reader);

	bool operator==(const WorkerJobCommand& other) const override;
	bool operator==(const WorkerResultCommand& other) const override;

protected:
	std::string filename;

	friend WorkerHistogramResultCommand;
};

class WorkerEqualisationJobCommand : public WorkerJobCommand {
public:
	WorkerEqualisationJobCommand(std::string filename,
	                             const EqualisationHistogramMapping& histogramMapping);

	void command_data(ProtocolJob::Data::Builder& dataBuilder) const override;
	void visit(CommandVisitor& visitor) const override;

	[[nodiscard]] std::string get_filename() const;
	[[nodiscard]] EqualisationHistogramMapping get_histogram_mapping() const;

	static std::unique_ptr<WorkerEqualisationJobCommand> from_data(EqualisationJob::Reader reader);

	bool operator==(const WorkerJobCommand& other) const override;
	bool operator==(const WorkerResultCommand& other) const override;

protected:
	std::string filename;
	EqualisationHistogramMapping histogramMapping;

	friend WorkerEqualisationResultCommand;
};

class WorkerResultCommand : public WorkerCommand {
public:
	WorkerResultCommand(std::string resultType);
	~WorkerResultCommand() override = default;

	void command_data(ProtocolCommand::Data::Builder& dataBuilder) const override;

	// Require child classes to be able to build the result component of command's data
	virtual void command_data(ProtocolResult::Data::Builder& dataBuilder) const = 0;

	static std::unique_ptr<WorkerResultCommand> from_data(ProtocolResult::Reader reader);

	virtual bool operator==(const WorkerJobCommand& other) const;
	virtual bool operator==(const WorkerResultCommand& other) const;

protected:
	std::string result_type;

	friend WorkerJobCommand;
};

class WorkerHistogramResultCommand : public WorkerResultCommand {
public:
	WorkerHistogramResultCommand(std::string filename, const Histogram& histogram);
	~WorkerHistogramResultCommand() override = default;

	void command_data(ProtocolResult::Data::Builder& dataBuilder) const override;
	void visit(CommandVisitor& visitor) const override;

	static std::unique_ptr<WorkerHistogramResultCommand>
	from_data(HistogramResult::Reader histogramReader);

	[[nodiscard]] std::string get_filename() const;
	[[nodiscard]] Histogram get_histogram() const;

	bool operator==(const WorkerJobCommand& jobCommand) const override;
	bool operator==(const WorkerResultCommand& jobCommand) const override;

protected:
	std::string filename;
	Histogram histogram;

	friend WorkerHistogramJobCommand;
};

class WorkerEqualisationResultCommand : public WorkerResultCommand {
public:
	WorkerEqualisationResultCommand(std::string filename, std::vector<std::uint8_t> tiffData);
	~WorkerEqualisationResultCommand() override = default;

	void command_data(ProtocolResult::Data::Builder& dataBuilder) const override;
	void visit(CommandVisitor& visitor) const override;

	static std::unique_ptr<WorkerEqualisationResultCommand>
	from_data(EqualisationResult::Reader equalisationReader);

	[[nodiscard]] std::string get_filename() const;
	[[nodiscard]] std::vector<std::uint8_t> get_tiff_data() const;

	bool operator==(const WorkerJobCommand& jobCommand) const override;
	bool operator==(const WorkerResultCommand& jobCommand) const override;

protected:
	std::string filename;
	std::vector<std::uint8_t> tiff_data;

	friend WorkerEqualisationJobCommand;
};

class WorkerHeartbeatCommand : public WorkerCommand {
public:
	WorkerHeartbeatCommand(HeartbeatType heartbeatType);
	~WorkerHeartbeatCommand() override = default;

	void command_data(ProtocolCommand::Data::Builder& dataBuilder) const override;
	void visit(CommandVisitor& visitor) const override;

	[[nodiscard]] HeartbeatType get_heartbeat_type() const;

	static std::unique_ptr<WorkerHeartbeatCommand> from_data(ProtocolHeartbeat::Reader reader);

protected:
	HeartbeatType heartbeat_type;
};

class WorkerByeCommand : public WorkerCommand {
public:
	WorkerByeCommand();
	~WorkerByeCommand() override = default;

	void command_data(ProtocolCommand::Data::Builder& dataBuilder) const override;
	void visit(CommandVisitor& visitor) const override;

	static std::unique_ptr<WorkerByeCommand> from_data();
};

class CommandVisitor {
public:
	CommandVisitor() = default;

	virtual void visit_helo(const WorkerHeloCommand& heloCommand);
	virtual void visit_ehlo(const WorkerEhloCommand& ehloCommand);
	virtual void visit_histogram_job(const WorkerHistogramJobCommand& jobCommand);
	virtual void visit_equalisation_job(const WorkerEqualisationJobCommand& jobCommand);
	virtual void visit_histogram_result(const WorkerHistogramResultCommand& resultCommand);
	virtual void visit_equalisation_result(const WorkerEqualisationResultCommand& resultCommand);
	virtual void visit_heartbeat(const WorkerHeartbeatCommand& heartbeatCommand);
	virtual void visit_bye(const WorkerByeCommand& byeCommand);
};
