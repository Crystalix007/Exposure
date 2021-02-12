#include "algorithm.hpp"

#include <Magick++.h>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <numeric>
#include <stdexcept>

Magick::Image get_lightness_channel(const std::string& filename);
Histogram compute_lightness_histogram(const Magick::Image& lightnessChannel);

std::optional<Histogram> image_get_histogram(const std::string& filename) {
	Magick::Image lightnessChannel{};

	try {
		lightnessChannel = get_lightness_channel(filename);
	} catch (Magick::Exception& error) {
		return std::nullopt;
	}

	return compute_lightness_histogram(lightnessChannel);
}

Magick::Image get_lightness_channel(const std::string& filename) {
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

std::array<float, HISTOGRAM_SEGMENTS>
compute_lightness_histogram(const Magick::Image& lightnessChannel) {
	const Magick::Quantum* pixels =
	    lightnessChannel.getConstPixels(0, 0, lightnessChannel.columns(), lightnessChannel.rows());
	std::array<uint64_t, HISTOGRAM_SEGMENTS> histogram{};

	for (size_t y = 0; y < lightnessChannel.rows(); y++) {
		for (size_t x = 0; x < lightnessChannel.columns(); x++) {
			const float pixel = pixels[x + y * lightnessChannel.columns()] / QuantumRange;
			const float bucket = std::round(pixel * (HISTOGRAM_SEGMENTS - 1));
			histogram[static_cast<uint32_t>(bucket)]++;
		}
	}

	std::array<float, HISTOGRAM_SEGMENTS> proportionalHistogram{};
	const double pixelCount =
	    static_cast<double>(lightnessChannel.rows()) * lightnessChannel.columns();

	for (size_t i = 0; i < HISTOGRAM_SEGMENTS; i++) {
		proportionalHistogram[i] = histogram[i] / pixelCount;
	}

	return proportionalHistogram;
}

struct EqualisationBand {
	float level;
	float cumulativeProportionLess;
	const float targetProportionLess;

	EqualisationBand(const float targetProportion)
	    : level{ 0.F }, cumulativeProportionLess{ 0.F }, targetProportionLess{ targetProportion } {}

	float operator-(const EqualisationBand& other) const {
		return this->level - other.level;
	}

	[[nodiscard]] bool is_new_proportion_closer(const float newProportion) const {
		return abs(cumulativeProportionLess - targetProportionLess) >
		       abs(newProportion - targetProportionLess);
	}

	void assign_best_proportion(const float newProportionLess, const float level) {
		if (this->is_new_proportion_closer(newProportionLess)) {
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
	double cumulativePreviousHistogram = 0.0;
	double cumulativeCurrentHistogram = 0.0;
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

Magick::Quantum linear_map(Magick::Quantum value, const EqualisationHistogramMapping& mapping) {
	const uint32_t histogramBin =
	    static_cast<size_t>(round((static_cast<double>(value) / static_cast<double>(QuantumRange)) *
	                              static_cast<double>(mapping.size() - 1)));

	return static_cast<Magick::Quantum>(
	    (static_cast<double>(mapping[histogramBin] * static_cast<double>(QuantumRange)) /
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

	const auto* const blobData = static_cast<const std::uint8_t*>(blob.data());
	const size_t blobLength = blob.length();

	return std::vector<std::uint8_t>{ blobData, blobData + blobLength };
}
