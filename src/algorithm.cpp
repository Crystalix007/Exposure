#include "algorithm.hpp"
#include "MagickCore/magick-config.h"

#include <cmath>
#include <iostream>

#include <Magick++.h>

Magick::Image get_lightness_channel(const std::string filename);
Histogram compute_lightness_histogram(const Magick::Image& lightness_channel);

std::optional<Histogram> image_get_histogram(const std::string& filename) {
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

constexpr Magick::Quantum lerp(Magick::Quantum a, Magick::Quantum b, Magick::Quantum t) noexcept {
	return a + t * (b - a);
}

Magick::Quantum linear_map(Magick::Quantum value, const Magick::Quantum shadowOffset,
                           const Magick::Quantum midOffset, const Magick::Quantum highlightOffset) {
	const Magick::Quantum input_midpoint = QuantumRange / 2;

	if (value < input_midpoint) {
		return value + lerp(shadowOffset, midOffset, value / input_midpoint);
	} else {
		return value + lerp(midOffset, highlightOffset, (value - input_midpoint) / input_midpoint);
	}
}

std::vector<std::uint8_t> image_equalise(const std::string& filename, const float shadowOffset,
                                         const float midOffset, const float highlightOffset) {
	Magick::Image image{};

	try {
		image.read(filename);

		image.colorSpace(Magick::LabColorspace);
	} catch (Magick::Exception& error) {
		std::cerr << "Error loading input: " << error.what() << std::endl;
		throw;
	}

	image.modifyImage();

	Magick::Pixels view{ image };

	Magick::Quantum* pixels = view.get(0, 0, view.columns(), view.rows());

	for (ssize_t row = 0; row < view.rows(); row++) {
		*pixels = linear_map(*pixels, shadowOffset, midOffset, highlightOffset);
		pixels += 3; // Move forward by the three channels in image
	}

	view.sync();

	Magick::Blob blob{};

	image.magick("TIFF");
	image.write(&blob);

	const auto blob_data = static_cast<const std::uint8_t*>(blob.data());
	const size_t blob_length = blob.length();

	return std::vector<std::uint8_t>{ blob_data, blob_data + blob_length };
}
