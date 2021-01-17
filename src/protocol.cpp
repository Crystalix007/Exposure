#include "protocol.hpp"

#include <capnp/message.h>
#include <capnp/serialize.h>

#include <cstring>
#include <memory>
#include <sstream>

#include "histogram.capnp.h"

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

std::pair<std::string, Histogram> decodeHistogramResult(const std::string serialised_string) {
	const size_t word_count = serialised_string.size() / sizeof(capnp::word) * sizeof(char);
	std::unique_ptr<capnp::word[]> word_array{ new capnp::word[word_count] };
	std::memcpy(word_array.get(), serialised_string.c_str(), serialised_string.size() * sizeof(char));
	kj::ArrayPtr<capnp::word> word_array_ptr{ word_array.get(), word_count };
	capnp::FlatArrayMessageReader message_reader{ word_array_ptr };

	const auto histogramReader = message_reader.getRoot<HistogramResult>();
	Histogram histogram{};

	for (size_t i = 0; i < histogram.size(); i++) {
		histogram[i] = histogramReader.getHistogram()[i];
	}

	return std::pair<std::string, Histogram>{ histogramReader.getFilename(), histogram };
}

zmqpp::message zmq_worker_helo() {
	zmqpp::message message{};
	message.add("HELO");

	return message;
}
