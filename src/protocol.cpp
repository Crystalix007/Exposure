#include "protocol.hpp"
#include "algorithm.hpp"

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

WorkerJobCommand::WorkerJobCommand(const std::string& job_type)
    : WorkerCommand{ "JOB" }, job_type{ job_type } {}

void WorkerJobCommand::commandData(ProtocolCommand::Data::Builder& data_builder) const {
	auto job_builder = data_builder.initJob();
	job_builder.setType(this->job_type);

	auto job_data_builder = job_builder.initData();
	this->commandData(job_data_builder);
}

std::unique_ptr<WorkerJobCommand> WorkerJobCommand::fromData(const ProtocolJob::Reader reader) {
	const std::string decoded_type{ reader.getType() };
	const auto data = reader.getData();

	switch (data.which()) {
		case ProtocolJob::Data::HISTOGRAM:
			assert(decoded_type == "HISTOGRAM");
			return WorkerHistogramJobCommand::fromData(data.getHistogram());
		case ProtocolJob::Data::EQUILISATION:
			assert(decoded_type == "EQUILISATION");
			return WorkerEqualisationJobCommand::fromData(data.getEquilisation());
	}
}

WorkerHistogramJobCommand::WorkerHistogramJobCommand(const std::string& filename)
    : WorkerJobCommand{ "HISTOGRAM" }, filename{ filename } {}

std::unique_ptr<WorkerHistogramJobCommand>
WorkerHistogramJobCommand::fromData(const HistogramJob::Reader reader) {
	const std::string filename{ reader.getFilename() };

	return std::make_unique<WorkerHistogramJobCommand>(filename);
}

void WorkerHistogramJobCommand::commandData(ProtocolJob::Data::Builder& data_builder) const {
	data_builder.initHistogram().setFilename(this->filename);
}

std::string WorkerHistogramJobCommand::getFilename() const {
	return this->filename;
}

void WorkerHistogramJobCommand::visit(CommandVisitor& visitor) const {
	return visitor.visitHistogramJob(*this);
}

WorkerEqualisationJobCommand::WorkerEqualisationJobCommand(const std::string& filename,
                                                           float shadowOffset, float midOffset,
                                                           float highlightOffset)
    : WorkerJobCommand{ "EQUALISATION" }, filename{ filename },
      shadowOffset{ shadowOffset }, midOffset{ midOffset }, highlightOffset{ highlightOffset } {}

std::unique_ptr<WorkerEqualisationJobCommand>
WorkerEqualisationJobCommand::fromData(const EqualisationJob::Reader reader) {
	const std::string filename{ reader.getFilename() };
	const float shadowOffset = reader.getShadowOffset();
	const float midOffset = reader.getMidOffset();
	const float highlightOffset = reader.getHighlightOffset();

	return std::make_unique<WorkerEqualisationJobCommand>(filename, shadowOffset, midOffset,
	                                                      highlightOffset);
}

void WorkerEqualisationJobCommand::commandData(ProtocolJob::Data::Builder& data_builder) const {
	auto equilisation_job = data_builder.initEquilisation();

	equilisation_job.setFilename(this->filename);
	equilisation_job.setShadowOffset(this->shadowOffset);
	equilisation_job.setMidOffset(this->midOffset);
	equilisation_job.setHighlightOffset(this->highlightOffset);
}

std::string WorkerEqualisationJobCommand::getFilename() const {
	return this->filename;
}

float WorkerEqualisationJobCommand::getShadowOffset() const {
	return this->shadowOffset;
}

float WorkerEqualisationJobCommand::getMidOffset() const {
	return this->midOffset;
}

float WorkerEqualisationJobCommand::getHighlightOffset() const {
	return this->highlightOffset;
}

void WorkerEqualisationJobCommand::visit(CommandVisitor& visitor) const {
	return visitor.visitEqualisationJob(*this);
}

WorkerResultCommand::WorkerResultCommand(const std::string& result_type)
    : WorkerCommand{ "RESULT" }, result_type{ result_type } {}

void WorkerResultCommand::commandData(ProtocolCommand::Data::Builder& data_builder) const {
	auto result_builder = data_builder.initResult();
	result_builder.setType(this->result_type);

	auto result_data_builder = result_builder.initData();
	this->commandData(result_data_builder);
}

std::unique_ptr<WorkerResultCommand>
WorkerResultCommand::fromData(const ProtocolResult::Reader reader) {
	const std::string decoded_type{ reader.getType() };
	const auto data = reader.getData();

	switch (data.which()) {
		case ProtocolResult::Data::HISTOGRAM:
			assert(decoded_type == "HISTOGRAM");
			return WorkerHistogramResultCommand::fromData(data.getHistogram());
		case ProtocolResult::Data::EQUALISATION:
			assert(decoded_type == "EQUALISATION");
			return WorkerEqualisationResultCommand::fromData(data.getEqualisation());
	}
}

WorkerHistogramResultCommand::WorkerHistogramResultCommand(const std::string& filename,
                                                           const Histogram& histogram)
    : WorkerResultCommand{ "HISTOGRAM" }, filename{ filename }, histogram{ histogram } {}

std::unique_ptr<WorkerHistogramResultCommand>
WorkerHistogramResultCommand::fromData(const HistogramResult::Reader histogram_reader) {
	const std::string filename{ histogram_reader.getFilename() };
	const auto encoded_histogram{ histogram_reader.getHistogram() };
	Histogram histogram{};

	assert(histogram_reader.getHistogram().size() == histogram.size());

	for (size_t i = 0; i < encoded_histogram.size(); i++) {
		histogram[i] = encoded_histogram[i];
	}

	return std::make_unique<WorkerHistogramResultCommand>(filename, histogram);
}

void WorkerHistogramResultCommand::commandData(ProtocolResult::Data::Builder& data_builder) const {
	HistogramResult::Builder histogram_builder = data_builder.initHistogram();
	histogram_builder.setFilename(filename);
	auto serializableHistogram = histogram_builder.initHistogram(histogram.size());

	for (size_t i = 0; i < histogram.size(); i++) {
		serializableHistogram.set(i, histogram[i]);
	}
}

void WorkerHistogramResultCommand::visit(CommandVisitor& visitor) const {
	return visitor.visitHistogramResult(*this);
}

std::string WorkerHistogramResultCommand::getFilename() const {
	return this->filename;
}

Histogram WorkerHistogramResultCommand::getHistogram() const {
	return this->histogram;
}

WorkerEqualisationResultCommand::WorkerEqualisationResultCommand(
    const std::string& filename, const std::vector<std::uint8_t>& tiff_data)
    : WorkerResultCommand{ "EQUALISATION" }, filename{ filename }, tiff_data{ tiff_data } {}

std::unique_ptr<WorkerEqualisationResultCommand>
WorkerEqualisationResultCommand::fromData(const EqualisationResult::Reader equalisation_reader) {
	const std::string filename{ equalisation_reader.getFilename() };
	const auto& tiff_data = equalisation_reader.getTiffResult().asBytes();

	return std::make_unique<WorkerEqualisationResultCommand>(
	    filename, std::vector<std::uint8_t>{ tiff_data.begin(), tiff_data.end() });
}

void WorkerEqualisationResultCommand::commandData(
    ProtocolResult::Data::Builder& data_builder) const {
	HistogramResult::Builder equalisation_builder = data_builder.getHistogram();
	equalisation_builder.setFilename(filename);
}

void WorkerEqualisationResultCommand::visit(CommandVisitor& visitor) const {
	return visitor.visitEqualisationResult(*this);
}

std::string WorkerEqualisationResultCommand::getFilename() const {
	return this->filename;
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
void CommandVisitor::visitHistogramJob(const WorkerHistogramJobCommand& jobCommand) {}
void CommandVisitor::visitEqualisationJob(const WorkerEqualisationJobCommand& jobCommand) {}
void CommandVisitor::visitHistogramResult(const WorkerHistogramResultCommand& resultCommand) {}
void CommandVisitor::visitEqualisationResult(const WorkerEqualisationResultCommand& resultCommand) {
}
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

void RunningWorkerCommandVisitor::visitHistogramJob(const WorkerHistogramJobCommand& jobCommand) {
	/* Run job. */
	DEBUG_NETWORK("Visited server Histogram Job\n");
	std::optional<Histogram> histogram = image_get_histogram(jobCommand.getFilename());

	assert(histogram);

	zmqpp::message response{
		WorkerHistogramResultCommand{ jobCommand.getFilename(), *histogram }.toMessage()
	};

	this->socket.send(response);
}

void RunningWorkerCommandVisitor::visitEqualisationJob(
    const WorkerEqualisationJobCommand& jobCommand) {
	/* Run job. */
	DEBUG_NETWORK("Visited server Equalisation Job\n");
	std::vector<std::uint8_t> tiff_file =
	    image_equalise(jobCommand.getFilename(), jobCommand.getShadowOffset(),
	                   jobCommand.getMidOffset(), jobCommand.getHighlightOffset());
}

void RunningWorkerCommandVisitor::visitHistogramResult(
    const WorkerHistogramResultCommand& resultCommand) {
	/* Ignore unexpected message. */
	DEBUG_NETWORK("Visited server Result (unexpected)\n");
}

void RunningWorkerCommandVisitor::visitEqualisationResult(
    const WorkerEqualisationResultCommand& resultCommand) {
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
