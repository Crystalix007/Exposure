#include "protocol.hpp"

#include <algorithm>
#include <capnp/common.h>
#include <capnp/list.h>
#include <capnp/message.h>
#include <capnp/serialize.h>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <kj/array.h>
#include <kj/common.h>
#include <memory>
#include <optional>
#include <sstream>
#include <utility>
#include <zmqpp/socket.hpp>

#include "algorithm.hpp"

void split_equalisation_tiff(capnp::List<capnp::Data>::Builder tiffDataBuilder,
                             const std::vector<std::uint8_t>& rawTiffData) {
	for (size_t i = 0; i < tiffDataBuilder.size(); i++) {
		const auto chunkSize = std::min(rawTiffData.size() - (i * MAX_CHUNK_SIZE), MAX_CHUNK_SIZE);

		const auto* const startIter = rawTiffData.data() + (i * MAX_CHUNK_SIZE);
		capnp::Data::Reader reader{ startIter, chunkSize };

		tiffDataBuilder.set(i, reader);
	}
}

WorkerCommand::WorkerCommand(std::string commandString)
    : command_string{ std::move(commandString) } {}

WorkerCommand::~WorkerCommand() = default;

zmqpp::message WorkerCommand::to_message() const {
	zmqpp::message msg{};
	this->add_to_message(msg);
	return msg;
}

zmqpp::message& WorkerCommand::add_to_message(zmqpp::message& msg) const {
	capnp::MallocMessageBuilder message{};
	ProtocolCommand::Builder commandBuilder = message.initRoot<ProtocolCommand>();

	commandBuilder.setCommand(command_string);
	auto dataBuilder = commandBuilder.initData();

	this->command_data(dataBuilder);

	const auto messageWords = capnp::messageToFlatArray(message);
	const auto messageChars = messageWords.asChars();

	msg.add(std::string{ messageChars.begin(), messageChars.end() });

	return msg;
}

std::unique_ptr<WorkerCommand>
WorkerCommand::from_serialised_string(const std::string& serialisedString) {
	const size_t wordCount = serialisedString.size() / sizeof(capnp::word) * sizeof(char);
	std::vector<capnp::word> wordArray(wordCount);
	std::memcpy(wordArray.data(), serialisedString.c_str(), serialisedString.size() * sizeof(char));
	const kj::ArrayPtr<capnp::word> wordArrayPtr{ wordArray.data(), wordCount };
	capnp::ReaderOptions commandReaderOptions{};
	// Raise message size limit to 4GB (somewhat reasonable per tiff image)
	commandReaderOptions.traversalLimitInWords = MAX_MESSAGE_SIZE;

	capnp::FlatArrayMessageReader messageReader{ wordArrayPtr, commandReaderOptions };

	const auto commandReader = messageReader.getRoot<ProtocolCommand>();

	const std::string command{ commandReader.getCommand() };
	const auto data = commandReader.getData();

	switch (data.which()) {
		case ProtocolCommand::Data::HELO:
			assert(command == "HELO");
			return WorkerHeloCommand::from_data(data.getHelo());
		case ProtocolCommand::Data::EHLO:
			assert(command == "EHLO");
			return WorkerEhloCommand::from_data();
		case ProtocolCommand::Data::JOB:
			assert(command == "JOB");
			return WorkerJobCommand::from_data(data.getJob());
		case ProtocolCommand::Data::RESULT:
			assert(command == "RESULT");
			return WorkerResultCommand::from_data(data.getResult());
		case ProtocolCommand::Data::HEARTBEAT:
			assert(command == "HEARTBEAT");
			return WorkerHeartbeatCommand::from_data(data.getHeartbeat());
		case ProtocolCommand::Data::BYE:
			assert(command == "BYE");
			return WorkerByeCommand::from_data();
		default:
			std::clog << "Invalid command detected\n";
			return nullptr;
	}
}

WorkerHeloCommand::WorkerHeloCommand(std::uint32_t concurrency)
    : WorkerCommand{ "HELO" }, concurrency{ concurrency } {}

std::unique_ptr<WorkerHeloCommand> WorkerHeloCommand::from_data(ProtocolHelo::Reader reader) {
	const auto concurrency{ reader.getConcurrency() };
	return std::make_unique<WorkerHeloCommand>(concurrency);
}

void WorkerHeloCommand::command_data(ProtocolCommand::Data::Builder& dataBuilder) const {
	auto helo = dataBuilder.initHelo();

	helo.setConcurrency(this->concurrency);
}

void WorkerHeloCommand::visit(CommandVisitor& visitor) const {
	return visitor.visit_helo(*this);
}

std::uint32_t WorkerHeloCommand::get_concurrency() const {
	return this->concurrency;
}

WorkerEhloCommand::WorkerEhloCommand() : WorkerCommand{ "EHLO" } {}

std::unique_ptr<WorkerEhloCommand> WorkerEhloCommand::from_data() {
	return std::make_unique<WorkerEhloCommand>();
}

void WorkerEhloCommand::command_data(ProtocolCommand::Data::Builder& dataBuilder) const {
	dataBuilder.setEhlo();
}

void WorkerEhloCommand::visit(CommandVisitor& visitor) const {
	return visitor.visit_ehlo(*this);
}

WorkerJobCommand::WorkerJobCommand(std::string jobType)
    : WorkerCommand{ "JOB" }, job_type{ std::move(jobType) } {}

void WorkerJobCommand::command_data(ProtocolCommand::Data::Builder& dataBuilder) const {
	auto jobBuilder = dataBuilder.initJob();
	jobBuilder.setType(this->job_type);

	auto jobDataBuilder = jobBuilder.initData();
	this->command_data(jobDataBuilder);
}

std::unique_ptr<WorkerJobCommand> WorkerJobCommand::from_data(const ProtocolJob::Reader reader) {
	const std::string decodedType{ reader.getType() };
	const auto data = reader.getData();

	switch (data.which()) {
		case ProtocolJob::Data::HISTOGRAM:
			assert(decodedType == "HISTOGRAM");
			return WorkerHistogramJobCommand::from_data(data.getHistogram());
		case ProtocolJob::Data::EQUALISATION:
			assert(decodedType == "EQUALISATION");
			return WorkerEqualisationJobCommand::from_data(data.getEqualisation());
		default:
			return nullptr;
	}
}

bool WorkerJobCommand::operator==(const WorkerJobCommand& other) const {
	return this->job_type == other.job_type;
}

bool WorkerJobCommand::operator==(const WorkerResultCommand& other) const {
	return this->job_type == other.result_type;
}

WorkerHistogramJobCommand::WorkerHistogramJobCommand(std::string filename)
    : WorkerJobCommand{ "HISTOGRAM" }, filename{ std::move(filename) } {}

std::unique_ptr<WorkerHistogramJobCommand>
WorkerHistogramJobCommand::from_data(const HistogramJob::Reader reader) {
	const std::string filename{ reader.getFilename() };

	return std::make_unique<WorkerHistogramJobCommand>(filename);
}

void WorkerHistogramJobCommand::command_data(ProtocolJob::Data::Builder& dataBuilder) const {
	dataBuilder.initHistogram().setFilename(this->filename);
}

std::string WorkerHistogramJobCommand::get_filename() const {
	return this->filename;
}

void WorkerHistogramJobCommand::visit(CommandVisitor& visitor) const {
	return visitor.visit_histogram_job(*this);
}

bool WorkerHistogramJobCommand::operator==(const WorkerJobCommand& other) const {
	if (!WorkerJobCommand::operator==(other)) {
		return false;
	}

	const auto& otherHistogramJob = dynamic_cast<const WorkerHistogramJobCommand&>(other);

	return this->filename == otherHistogramJob.filename;
}

bool WorkerHistogramJobCommand::operator==(const WorkerResultCommand& other) const {
	if (!WorkerJobCommand::operator==(other)) {
		return false;
	}

	const auto& otherHistogramJob = dynamic_cast<const WorkerHistogramResultCommand&>(other);

	return this->filename == otherHistogramJob.filename;
}

WorkerEqualisationJobCommand::WorkerEqualisationJobCommand(
    std::string filename, EqualisationHistogramMapping histogramMapping)
    : WorkerJobCommand{ "EQUALISATION" }, filename{ std::move(filename) }, histogramMapping{
	      std::move(histogramMapping)
      } {}

std::unique_ptr<WorkerEqualisationJobCommand>
WorkerEqualisationJobCommand::from_data(const EqualisationJob::Reader reader) {
	const std::string filename{ reader.getFilename() };
	const auto messageHistogramOffsets = reader.getHistogramMapping();
	EqualisationHistogramMapping mapping{};

	for (size_t i = 0; i < mapping.size(); i++) {
		mapping[i] = messageHistogramOffsets[i];
	}

	return std::make_unique<WorkerEqualisationJobCommand>(filename, mapping);
}

void WorkerEqualisationJobCommand::command_data(ProtocolJob::Data::Builder& dataBuilder) const {
	auto equalisationJob = dataBuilder.initEqualisation();

	equalisationJob.setFilename(this->filename);
	auto jobHistogramOffsets = equalisationJob.initHistogramMapping(this->histogramMapping.size());

	for (size_t i = 0; i < this->histogramMapping.size(); i++) {
		jobHistogramOffsets.set(i, this->histogramMapping[i]);
	}
}

std::string WorkerEqualisationJobCommand::get_filename() const {
	return this->filename;
}

EqualisationHistogramMapping WorkerEqualisationJobCommand::get_histogram_mapping() const {
	return this->histogramMapping;
}

void WorkerEqualisationJobCommand::visit(CommandVisitor& visitor) const {
	return visitor.visit_equalisation_job(*this);
}

bool WorkerEqualisationJobCommand::operator==(const WorkerJobCommand& other) const {
	if (!WorkerJobCommand::operator==(other)) {
		return false;
	}

	const auto& otherEqualisationJob = dynamic_cast<const WorkerEqualisationJobCommand&>(other);

	return this->filename == otherEqualisationJob.filename;
}

bool WorkerEqualisationJobCommand::operator==(const WorkerResultCommand& other) const {
	if (!WorkerJobCommand::operator==(other)) {
		return false;
	}

	const auto& otherEqualisationJob = dynamic_cast<const WorkerEqualisationResultCommand&>(other);

	return this->filename == otherEqualisationJob.filename;
}

WorkerResultCommand::WorkerResultCommand(std::string resultType)
    : WorkerCommand{ "RESULT" }, result_type{ std::move(resultType) } {}

void WorkerResultCommand::command_data(ProtocolCommand::Data::Builder& dataBuilder) const {
	auto resultBuilder = dataBuilder.initResult();
	resultBuilder.setType(this->result_type);

	auto resultDataBuilder = resultBuilder.initData();
	this->command_data(resultDataBuilder);
}

std::unique_ptr<WorkerResultCommand>
WorkerResultCommand::from_data(const ProtocolResult::Reader reader) {
	const std::string decodedType{ reader.getType() };
	const auto data = reader.getData();

	switch (data.which()) {
		case ProtocolResult::Data::HISTOGRAM:
			assert(decodedType == "HISTOGRAM");
			return WorkerHistogramResultCommand::from_data(data.getHistogram());
		case ProtocolResult::Data::EQUALISATION:
			assert(decodedType == "EQUALISATION");
			return WorkerEqualisationResultCommand::from_data(data.getEqualisation());
		default:
			return nullptr;
	}
}

bool WorkerResultCommand::operator==(const WorkerJobCommand& other) const {
	return this->result_type == other.job_type;
}

bool WorkerResultCommand::operator==(const WorkerResultCommand& other) const {
	return this->result_type == other.result_type;
}

WorkerHistogramResultCommand::WorkerHistogramResultCommand(std::string filename,
                                                           const Histogram& histogram)
    : WorkerResultCommand{ "HISTOGRAM" }, filename{ std::move(filename) }, histogram{ histogram } {}

std::unique_ptr<WorkerHistogramResultCommand>
WorkerHistogramResultCommand::from_data(const HistogramResult::Reader histogramReader) {
	const std::string filename{ histogramReader.getFilename() };
	const auto encodedHistogram{ histogramReader.getHistogram() };
	Histogram histogram{};

	assert(histogramReader.getHistogram().size() == histogram.size());

	for (size_t i = 0; i < encodedHistogram.size(); i++) {
		histogram[i] = encodedHistogram[i];
	}

	return std::make_unique<WorkerHistogramResultCommand>(filename, histogram);
}

void WorkerHistogramResultCommand::command_data(ProtocolResult::Data::Builder& dataBuilder) const {
	HistogramResult::Builder histogramBuilder = dataBuilder.initHistogram();
	histogramBuilder.setFilename(filename);
	auto serializableHistogram = histogramBuilder.initHistogram(histogram.size());

	for (size_t i = 0; i < histogram.size(); i++) {
		serializableHistogram.set(i, histogram[i]);
	}
}

void WorkerHistogramResultCommand::visit(CommandVisitor& visitor) const {
	return visitor.visit_histogram_result(*this);
}

std::string WorkerHistogramResultCommand::get_filename() const {
	return this->filename;
}

Histogram WorkerHistogramResultCommand::get_histogram() const {
	return this->histogram;
}

bool WorkerHistogramResultCommand::operator==(const WorkerJobCommand& jobCommand) const {
	if (!WorkerResultCommand::operator==(jobCommand)) {
		return false;
	}

	const auto& histogramJobCommand = dynamic_cast<const WorkerHistogramJobCommand&>(jobCommand);

	return this->filename == histogramJobCommand.filename;
}

bool WorkerHistogramResultCommand::operator==(const WorkerResultCommand& jobCommand) const {
	if (!WorkerResultCommand::operator==(jobCommand)) {
		return false;
	}

	const auto& histogramJobCommand = dynamic_cast<const WorkerHistogramResultCommand&>(jobCommand);

	return this->filename == histogramJobCommand.filename;
}

WorkerEqualisationResultCommand::WorkerEqualisationResultCommand(std::string filename,
                                                                 std::vector<std::uint8_t> tiffData)
    : WorkerResultCommand{ "EQUALISATION" }, filename{ std::move(filename) }, tiff_data{ std::move(
	                                                                                tiffData) } {}

// This decoding step is complex by necessity. There is sufficient memory moving around here to make
// efficiency in doing so pretty important.
std::unique_ptr<WorkerEqualisationResultCommand>
WorkerEqualisationResultCommand::from_data(const EqualisationResult::Reader equalisationReader) {
	const std::string filename{ equalisationReader.getFilename() };
	const auto& tiffDataList = equalisationReader.getTiffResult();
	std::vector<uint8_t> tiffData{};

	/* Pre-allocate in order to avoid expensive resizes. */
	{
		size_t totalTiffDataSize = 0;

		for (const auto& tiffDataChunk : tiffDataList) {
			totalTiffDataSize += tiffDataChunk.asBytes().size();
		}

		tiffData.resize(totalTiffDataSize);
	}

	size_t cumulativeSize = 0;

	for (const auto& tiffDataChunk : tiffDataList) {
		const auto& tiffDataChunkBytes = tiffDataChunk.asBytes();
		auto* const endElem = tiffData.data() + cumulativeSize;
		cumulativeSize += tiffDataChunkBytes.size();

		// Avoid member-by-member copying a-la std::vector::insert
		std::memcpy(endElem, tiffDataChunkBytes.begin(), tiffDataChunkBytes.size());
	}

	return std::make_unique<WorkerEqualisationResultCommand>(filename, std::move(tiffData));
}

void WorkerEqualisationResultCommand::command_data(
    ProtocolResult::Data::Builder& dataBuilder) const {
	EqualisationResult::Builder equalisationBuilder = dataBuilder.initEqualisation();
	equalisationBuilder.setFilename(filename);

	const auto chunkDiv = std::lldiv(this->tiff_data.size(), MAX_CHUNK_SIZE);
	const auto chunkCount = chunkDiv.quot + ((chunkDiv.rem == 0) ? 0ULL : 1ULL);
	auto tiffResultBuilder = equalisationBuilder.initTiffResult(chunkCount);
	split_equalisation_tiff(tiffResultBuilder, this->tiff_data);
}

void WorkerEqualisationResultCommand::visit(CommandVisitor& visitor) const {
	return visitor.visit_equalisation_result(*this);
}

std::string WorkerEqualisationResultCommand::get_filename() const {
	return this->filename;
}

const std::vector<std::uint8_t>& WorkerEqualisationResultCommand::get_tiff_data() const {
	return this->tiff_data;
}

bool WorkerEqualisationResultCommand::operator==(const WorkerJobCommand& jobCommand) const {
	if (!WorkerResultCommand::operator==(jobCommand)) {
		return false;
	}

	const auto& equalisationJobCommand =
	    dynamic_cast<const WorkerEqualisationJobCommand&>(jobCommand);

	return this->filename == equalisationJobCommand.filename;
}

bool WorkerEqualisationResultCommand::operator==(const WorkerResultCommand& jobCommand) const {
	if (!WorkerResultCommand::operator==(jobCommand)) {
		return false;
	}

	const auto& equalisationJobCommand =
	    dynamic_cast<const WorkerEqualisationResultCommand&>(jobCommand);

	return this->filename == equalisationJobCommand.filename;
}

WorkerHeartbeatCommand::WorkerHeartbeatCommand(HeartbeatType heartbeatType)
    : WorkerCommand{ "HEARTBEAT" }, heartbeat_type{ heartbeatType } {}

std::unique_ptr<WorkerHeartbeatCommand>
WorkerHeartbeatCommand::from_data(ProtocolHeartbeat::Reader reader) {
	const HeartbeatType heartbeatType{ reader.getType() };

	return std::make_unique<WorkerHeartbeatCommand>(heartbeatType);
}

void WorkerHeartbeatCommand::command_data(ProtocolCommand::Data::Builder& dataBuilder) const {
	auto protocolHeartbeat = dataBuilder.initHeartbeat();
	protocolHeartbeat.setType(this->heartbeat_type);
}

HeartbeatType WorkerHeartbeatCommand::get_heartbeat_type() const {
	return this->heartbeat_type;
}

void WorkerHeartbeatCommand::visit(CommandVisitor& visitor) const {
	return visitor.visit_heartbeat(*this);
}

WorkerByeCommand::WorkerByeCommand() : WorkerCommand{ "BYE" } {}

void WorkerByeCommand::command_data(ProtocolCommand::Data::Builder& dataBuilder) const {
	dataBuilder.setBye();
}

void WorkerByeCommand::visit(CommandVisitor& visitor) const {
	visitor.visit_bye(*this);
}

std::unique_ptr<WorkerByeCommand> WorkerByeCommand::from_data() {
	return std::make_unique<WorkerByeCommand>();
}

void CommandVisitor::visit_helo(const WorkerHeloCommand& heloCommand) {}
void CommandVisitor::visit_ehlo(const WorkerEhloCommand& ehloCommand) {}
void CommandVisitor::visit_histogram_job(const WorkerHistogramJobCommand& jobCommand) {}
void CommandVisitor::visit_equalisation_job(const WorkerEqualisationJobCommand& jobCommand) {}
void CommandVisitor::visit_histogram_result(const WorkerHistogramResultCommand& resultCommand) {}
void CommandVisitor::visit_equalisation_result(
    const WorkerEqualisationResultCommand& resultCommand) {}
void CommandVisitor::visit_heartbeat(const WorkerHeartbeatCommand& heartbeatCommand) {}
void CommandVisitor::visit_bye(const WorkerByeCommand& byeCommand) {}
