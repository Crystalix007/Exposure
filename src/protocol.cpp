#include "protocol.hpp"

#include <capnp/message.h>
#include <capnp/serialize.h>

#include <cstring>
#include <memory>
#include <sstream>

std::string encodeHistogramResult(const std::string& filename, const Histogram& histogram) {
	capnp::MallocMessageBuilder message{};

	HistogramResult::Builder resultBuilder = message.initRoot<HistogramResult>();
	resultBuilder.setFilename(filename);
	auto serializableHistogram = resultBuilder.initHistogram(histogram.size());

	for (size_t i = 0; i < histogram.size(); i++) {
		serializableHistogram.set(i, histogram[i]);
	}

	const auto byte_array = capnp::messageToFlatArray(message);
	const auto char_array = byte_array.asChars();

	return std::string{ char_array.begin(), char_array.end() };
}

WorkerCommand::WorkerCommand(const std::string_view& command_string)
    : command_string{ command_string } {}

WorkerCommand::~WorkerCommand() {}

zmqpp::message WorkerCommand::toMessage() const {
	zmqpp::message msg{};
	this->addToMessage(msg);
	return msg;
}

zmqpp::message& WorkerCommand::addToMessage(zmqpp::message& msg) const {
	capnp::MallocMessageBuilder message{};
	ProtocolCommand::Builder command_builder = message.initRoot<ProtocolCommand>();

	command_builder.setCommand(command_string);
	auto data_builder = command_builder.initData();

	this->commandData(data_builder);

	const auto message_words = capnp::messageToFlatArray(message);
	const auto message_chars = message_words.asChars();

	msg.add(std::string{ message_chars.begin(), message_chars.end() });

	return msg;
}

std::unique_ptr<WorkerCommand>
WorkerCommand::fromSerialisedString(const std::string& serialised_string) {
	const size_t word_count = serialised_string.size() / sizeof(capnp::word) * sizeof(char);
	std::unique_ptr<capnp::word[]> word_array{ new capnp::word[word_count] };
	std::memcpy(word_array.get(), serialised_string.c_str(), serialised_string.size() * sizeof(char));
	const kj::ArrayPtr<capnp::word> word_array_ptr{ word_array.get(), word_count };
	capnp::FlatArrayMessageReader message_reader{ word_array_ptr };

	const auto commandReader = message_reader.getRoot<ProtocolCommand>();

	const std::string command{ commandReader.getCommand() };
	const auto data = commandReader.getData();

	switch (data.which()) {
		case ProtocolCommand::Data::HELO:
			assert(command == "HELO");
			return WorkerHeloCommand::fromData();
		case ProtocolCommand::Data::EHLO:
			assert(command == "EHLO");
			return WorkerEhloCommand::fromData();
		case ProtocolCommand::Data::JOB:
			assert(command == "JOB");
			return WorkerJobCommand::fromData(data.getJob());
		case ProtocolCommand::Data::RESULT:
			assert(command == "RESULT");
			return WorkerResultCommand::fromData(data.getResult());
		case ProtocolCommand::Data::HEATBEAT:
			assert(command == "HEARTBEAT");
			return WorkerHeartbeatCommand::fromData(data.getHeatbeat());
		default:
			std::clog << "Invalid command detected\n";
			return nullptr;
	}
}

WorkerHeloCommand::WorkerHeloCommand() : WorkerCommand{ "HELO" } {}

std::unique_ptr<WorkerHeloCommand> WorkerHeloCommand::fromData() {
	return std::make_unique<WorkerHeloCommand>();
}

void WorkerHeloCommand::commandData(ProtocolCommand::Data::Builder& data_builder) const {
	data_builder.setHelo();
}

void WorkerHeloCommand::visit(CommandVisitor& visitor) const {
	return visitor.visitHelo(*this);
}

WorkerEhloCommand::WorkerEhloCommand() : WorkerCommand{ "EHLO" } {}

std::unique_ptr<WorkerEhloCommand> WorkerEhloCommand::fromData() {
	return std::make_unique<WorkerEhloCommand>();
}

void WorkerEhloCommand::commandData(ProtocolCommand::Data::Builder& data_builder) const {
	data_builder.setEhlo();
}

void WorkerEhloCommand::visit(CommandVisitor& visitor) const {
	return visitor.visitEhlo(*this);
}

WorkerJobCommand::WorkerJobCommand(const std::string& filename)
    : WorkerCommand{ "JOB" }, filename{ filename } {}

std::unique_ptr<WorkerJobCommand> WorkerJobCommand::fromData(const capnp::Text::Reader reader) {
	const std::string filename{ reader };

	return std::make_unique<WorkerJobCommand>(filename);
}

void WorkerJobCommand::commandData(ProtocolCommand::Data::Builder& data_builder) const {
	data_builder.setJob(this->filename);
}

std::string WorkerJobCommand::getFilename() const {
	return this->filename;
}

void WorkerJobCommand::visit(CommandVisitor& visitor) const {
	return visitor.visitJob(*this);
}

WorkerResultCommand::WorkerResultCommand(const std::string& filename, const Histogram& histogram)
    : WorkerCommand{ "RESULT" }, filename{ filename }, histogram{ histogram } {}

std::unique_ptr<WorkerResultCommand>
WorkerResultCommand::fromData(const HistogramResult::Reader histogram_reader) {
	const std::string filename{ histogram_reader.getFilename() };
	const auto encoded_histogram{ histogram_reader.getHistogram() };
	Histogram histogram{};

	assert(histogram_reader.getHistogram().size() == histogram.size());

	for (size_t i = 0; i < encoded_histogram.size(); i++) {
		histogram[i] = encoded_histogram[i];
	}

	return std::make_unique<WorkerResultCommand>(filename, histogram);
}

void WorkerResultCommand::commandData(ProtocolCommand::Data::Builder& data_builder) const {
	HistogramResult::Builder result_builder = data_builder.initResult();
	result_builder.setFilename(filename);
	auto serializableHistogram = result_builder.initHistogram(histogram.size());

	for (size_t i = 0; i < histogram.size(); i++) {
		serializableHistogram.set(i, histogram[i]);
	}
}

void WorkerResultCommand::visit(CommandVisitor& visitor) const {
	return visitor.visitResult(*this);
}

std::string WorkerResultCommand::getFilename() const {
	return this->filename;
}

Histogram WorkerResultCommand::getHistogram() const {
	return this->histogram;
}

WorkerHeartbeatCommand::WorkerHeartbeatCommand(const std::string& peer_name)
    : WorkerCommand{ "HEARTBEAT" }, peer_name{ peer_name } {}

std::unique_ptr<WorkerHeartbeatCommand>
WorkerHeartbeatCommand::fromData(const capnp::Text::Reader reader) {
	const std::string heartbeat_data{ reader };

	return std::make_unique<WorkerHeartbeatCommand>(heartbeat_data);
}

void WorkerHeartbeatCommand::commandData(ProtocolCommand::Data::Builder& data_builder) const {
	data_builder.setHeatbeat(peer_name);
}

std::string WorkerHeartbeatCommand::getPeerName() const {
	return this->peer_name;
}

void WorkerHeartbeatCommand::visit(CommandVisitor& visitor) const {
	return visitor.visitHeartbeat(*this);
}

void CommandVisitor::visitHelo(const WorkerHeloCommand& heloCommand) {}
void CommandVisitor::visitEhlo(const WorkerEhloCommand& ehloCommand) {}
void CommandVisitor::visitJob(const WorkerJobCommand& jobCommand) {}
void CommandVisitor::visitResult(const WorkerResultCommand& resultCommand) {}
void CommandVisitor::visitHeartbeat(const WorkerHeartbeatCommand& heatbeatCommand) {}

ConnectingWorkerCommandVisitor::ConnectingWorkerCommandVisitor(
    ServerConnection::State& connectionState)
    : connectionState{ connectionState } {}

void ConnectingWorkerCommandVisitor::visitEhlo(const WorkerEhloCommand& ehloCommand) {
	DEBUG_NETWORK("Visited server Ehlo whilst connecting\n");
	this->connectionState =
	    ServerConnection::transitionState(this->connectionState, ServerConnection::State::Connected);
}

RunningWorkerCommandVisitor::RunningWorkerCommandVisitor(zmqpp::socket& socket)
    : socket{ socket } {}

void RunningWorkerCommandVisitor::visitHelo(const WorkerHeloCommand& heloCommand) {
	/* Ignore unexpected message. */
	DEBUG_NETWORK("Visited server Helo (unexpected)\n");
}

void RunningWorkerCommandVisitor::visitEhlo(const WorkerEhloCommand& ehloCommand) {
	/* Mark connection as successful / connected. */
	DEBUG_NETWORK("Visited server Ehlo (unexpected once already connected)\n");
}

void RunningWorkerCommandVisitor::visitJob(const WorkerJobCommand& jobCommand) {
	/* Run job. */
	DEBUG_NETWORK("Visited server Job\n");
	std::optional<Histogram> histogram = image_get_histogram(jobCommand.getFilename());

	assert(histogram);

	zmqpp::message response{
		WorkerResultCommand{ jobCommand.getFilename(), *histogram }.toMessage()
	};

	this->socket.send(response);
}

void RunningWorkerCommandVisitor::visitResult(const WorkerResultCommand& resultCommand) {
	/* Ignore unexpected message. */
	DEBUG_NETWORK("Visited server Result (unexpected)\n");
}

void RunningWorkerCommandVisitor::visitHeartbeat(const WorkerHeartbeatCommand& heartbeatCommand) {
	/* Respond with the same heartbeat data. */
	DEBUG_NETWORK("Visited server Heartbeat\n");

	zmqpp::message command_message{
		WorkerHeartbeatCommand{ heartbeatCommand.getPeerName() }.toMessage()
	};
	socket.send(command_message);
}
