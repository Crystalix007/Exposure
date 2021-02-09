#include "algorithm.hpp"
#include "MagickCore/magick-config.h"

#include <cmath>
#include <iostream>
#include <numeric>

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
			const float pixel = pixels[x + y * lightness_channel.columns()] / QuantumRange;
			const float bucket = std::round(pixel * (histogramSegments - 1));
			histogram[static_cast<uint32_t>(bucket)]++;
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

struct EqualisationBand {
	float level;
	float cumulativeProportionLess;
	const float targetProportionLess;

	EqualisationBand(const float targetProportion)
	    : level{ 0.f }, cumulativeProportionLess{ 0.f }, targetProportionLess{ targetProportion } {}

	float operator-(const EqualisationBand& other) {
		return this->level - other.level;
	}

	bool isNewProportionCloser(const float newProportion) {
		return abs(cumulativeProportionLess - targetProportionLess) >
		       abs(newProportion - targetProportionLess);
	}

	void assignBestProportion(const float newProportionLess, const float level) {
		if (this->isNewProportionCloser(newProportionLess)) {
			this->cumulativeProportionLess = newProportionLess;
			this->level = level;
		}
	}
};

EqualisationHistogramMapping identity_equalisation_histogram_mapping() {
	EqualisationHistogramMapping mapping{};
	std::iota(mapping.begin(), mapping.end(), 0);
	return mapping;
}

EqualisationHistogramMapping get_equalisation_parameters(const Histogram& previousHistogram,
                                                         const Histogram& currentHistogram) {
	double cumulativePreviousHistogram = 0.0, cumulativeCurrentHistogram = 0.0;
	uint32_t previousHistogramBin = 0;

	EqualisationHistogramMapping mapping{};

	for (uint32_t currentHistogramBin = 0; currentHistogramBin != currentHistogram.size() - 1 ||
	                                       previousHistogramBin != previousHistogram.size() - 1;) {
		mapping[currentHistogramBin] = previousHistogramBin;

		if (cumulativeCurrentHistogram < cumulativePreviousHistogram) {
			if (currentHistogramBin < currentHistogram.size() - 1) {
				cumulativeCurrentHistogram += currentHistogram[currentHistogramBin];
				currentHistogramBin++;
			} else {
				cumulativePreviousHistogram += previousHistogram[previousHistogramBin];
				previousHistogramBin++;
			}
		} else if (cumulativeCurrentHistogram > cumulativePreviousHistogram) {
			if (previousHistogramBin < previousHistogram.size() - 1) {
				cumulativePreviousHistogram += previousHistogram[previousHistogramBin];
				previousHistogramBin++;
			} else {
				cumulativeCurrentHistogram += currentHistogram[currentHistogramBin];
				currentHistogramBin++;
			}
		} else if (cumulativeCurrentHistogram == cumulativePreviousHistogram) {
			if (currentHistogramBin < currentHistogram.size() - 1) {
				cumulativeCurrentHistogram += currentHistogram[currentHistogramBin];
				currentHistogramBin++;
			}

			if (previousHistogramBin < previousHistogram.size() - 1) {
				cumulativePreviousHistogram += previousHistogram[previousHistogramBin];
				previousHistogramBin++;
			}
		} else {
			throw std::logic_error{ "Floating comparison failed" };
		}
	}

	return mapping;
}

constexpr Magick::Quantum lerp(Magick::Quantum a, Magick::Quantum b, Magick::Quantum t) noexcept {
	return a + t * (b - a);
}

Magick::Quantum linear_map(Magick::Quantum value, const EqualisationHistogramMapping& mapping) {
	const uint32_t histogram_bin =
	    static_cast<size_t>(round((static_cast<double>(value) / static_cast<double>(QuantumRange)) *
	                              static_cast<double>(mapping.size() - 1)));

	return static_cast<Magick::Quantum>(
	    (static_cast<double>(mapping[histogram_bin] * static_cast<double>(QuantumRange)) /
	     static_cast<double>(mapping.size() - 1)));
}

std::vector<std::uint8_t> image_equalise(const std::string& filename,
                                         const EqualisationHistogramMapping& mapping) {
	Magick::Image image{};

	try {
		image.read(filename);

		image.colorSpace(Magick::LabColorspace);
	} catch (Magick::Exception& error) {
		std::cerr << "Error loading input: " << error.what() << std::endl;
		throw;
	}

	image.modifyImage();

	Magick::Quantum* pixels = image.getPixels(0, 0, image.columns(), image.rows());

	for (size_t row = 0; row < image.rows(); row++) {
		for (size_t col = 0; col < image.columns(); col++) {
			*pixels = linear_map(*pixels, mapping);
			pixels += 3; // Move forward by the three channels in image
		}
	}

	image.syncPixels();

	Magick::Blob blob{};

	image.colorSpace(Magick::sRGBColorspace);
	image.magick("TIFF");
	image.write(&blob);

	const auto blob_data = static_cast<const std::uint8_t*>(blob.data());
	const size_t blob_length = blob.length();

	return std::vector<std::uint8_t>{ blob_data, blob_data + blob_length };
}
