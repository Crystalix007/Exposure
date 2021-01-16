#include "algorithm.hpp"

#include <cmath>
#include <iostream>

#include <Magick++.h>

Magick::Image get_lightness_channel(const std::string filename);
Histogram compute_lightness_histogram(const Magick::Image& lightness_channel);

std::optional<Histogram> image_get_histogram(const std::string filename) {
	Magick::Image lightness_channel{};

	try {
		lightness_channel = get_lightness_channel(filename);
	} catch (Magick::Exception& error) {
		return std::nullopt;
	}

	return compute_lightness_histogram(lightness_channel);
}

Magick::Image get_lightness_channel(const std::string filename) {
	Magick::Image image{};

	try {
		image.read(filename);

		image.colorSpace(Magick::LabColorspace);
		image.channel(Magick::ChannelType::LChannel);
	} catch (Magick::Exception& error) {
		std::cerr << "Error loading input: " << error.what() << std::endl;
		throw;
	}

	return image;
}

std::array<float, histogramSegments>
compute_lightness_histogram(const Magick::Image& lightness_channel) {
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
