#include <array>
#include <cstring>
#include <iostream>

#include "network.hpp"
#include <Magick++.h>

const constexpr uint32_t histogramSegments = 1 << 6ull;

int process_image(const std::string filename);

Magick::Image get_lightness_channel(const std::string filename);
std::array<float, histogramSegments> get_histogram(const Magick::Image& lightness_channel);

int main(int argc, char* argv[]) {
	if (argc <= 1) {
		std::cerr << "Usage: " << argv[0] << " [options] <image/to/process> ...\n";
		return -1;
	}

	Magick::InitializeMagick(*argv);

	auto mdns_service = start_mdns_service();

	int r = process_image(argv[1]);

	stop_mdns_service(std::move(mdns_service));

	return r;
}

int process_image(const std::string filename) {
	Magick::Image lightness_channel{};

	try {
		lightness_channel = get_lightness_channel(filename);
	} catch (Magick::Exception& error) {
		return 1;
	}

	const auto histogram = get_histogram(lightness_channel);

	for (size_t i = 0; i < histogramSegments; i++) {
		// Compute midpoint of each segment
		std::cout << (i + 0.5) / histogramSegments << ": ";
		std::cout << histogram[i] << "\n";
	}

	return 0;
}

Magick::Image get_lightness_channel(const std::string filename) {
	Magick::Image image{};

	try {
		image.read(filename);

		image.colorSpace(Magick::LabColorspace);
		image.channel(Magick::ChannelType::LChannel);
		// image.write("lightness.tiff");
	} catch (Magick::Exception& error) {
		std::cerr << "Error loading input: " << error.what() << std::endl;
		throw;
	}

	return image;
}

std::array<float, histogramSegments> get_histogram(const Magick::Image& lightness_channel) {
	const Magick::Quantum* pixels =
	    lightness_channel.getConstPixels(0, 0, lightness_channel.columns(), lightness_channel.rows());
	std::array<uint64_t, histogramSegments> histogram{};

	for (size_t y = 0; y < lightness_channel.rows(); y++) {
		for (size_t x = 0; x < lightness_channel.columns(); x++) {
			const float pixel = pixels[x + y * lightness_channel.columns()] * QuantumScale;
			const float decimal_bucket = pixel * (histogramSegments - 1);
			histogram[static_cast<uint32_t>(std::round(decimal_bucket))]++;
		}
	}

	std::array<float, histogramSegments> proportional_histogram{};
	const double pixel_count =
	    static_cast<uint64_t>(lightness_channel.rows()) * lightness_channel.columns();

	for (size_t i = 0; i < histogramSegments; i++) {
		proportional_histogram[i] = histogram[i] / pixel_count;
	}

	return proportional_histogram;
}
