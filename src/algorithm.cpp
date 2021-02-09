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

EqualisationParameters get_equalisation_parameters(const Histogram& previousHistogram,
                                                   const Histogram& currentHistogram) {
	EqualisationBand previousShadows{ .25f }, currentShadows{ .25f };
	EqualisationBand previousMidpoint{ .5f }, currentMidpoint{ .5f };
	EqualisationBand previousHighlights{ .75f }, currentHighlights{ .75f };

	double cumulativePreviousProportion = 0.0;
	double cumulativeCurrentProportion = 0.0;

	for (size_t i = 0; i < histogramSegments; i++) {
		const float currentLevel = static_cast<float>(i) / static_cast<float>(histogramSegments);
		cumulativePreviousProportion += previousHistogram[i];
		cumulativeCurrentProportion += currentHistogram[i];

		previousShadows.assignBestProportion(cumulativePreviousProportion, currentLevel);
		previousMidpoint.assignBestProportion(cumulativePreviousProportion, currentLevel);
		previousHighlights.assignBestProportion(cumulativePreviousProportion, currentLevel);

		currentShadows.assignBestProportion(cumulativeCurrentProportion, currentLevel);
		currentMidpoint.assignBestProportion(cumulativeCurrentProportion, currentLevel);
		currentHighlights.assignBestProportion(cumulativeCurrentProportion, currentLevel);
	}

	EqualisationParameters eqParams{
		(previousShadows.level - currentShadows.level) * QuantumRange,
		(previousMidpoint.level - currentMidpoint.level) * QuantumRange,
		(previousHighlights.level - currentHighlights.level) * QuantumRange,
	};

	return eqParams;
}

constexpr Magick::Quantum lerp(Magick::Quantum a, Magick::Quantum b, Magick::Quantum t) noexcept {
	return a + t * (b - a);
}

Magick::Quantum linear_map(Magick::Quantum value, const Magick::Quantum shadowOffset,
                           const Magick::Quantum midOffset, const Magick::Quantum highlightOffset) {
	const Magick::Quantum midpoint = QuantumRange / 2;
	const Magick::Quantum lower_tail = QuantumRange * 0.25;
	const Magick::Quantum upper_tail = QuantumRange - lower_tail;

	if (value < lower_tail) {
		return value;
	} else if (value < midpoint) {
		return value + lerp(shadowOffset, midOffset, (value - lower_tail) / (midpoint - lower_tail));
	} else if (value < upper_tail) {
		return value + lerp(midOffset, highlightOffset, (value - midpoint) / (upper_tail - midpoint));
	} else {
		return value;
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

	Magick::Quantum* pixels = image.getPixels(0, 0, image.columns(), image.rows());

	for (size_t row = 0; row < image.rows(); row++) {
		for (size_t col = 0; col < image.columns(); col++) {
			*pixels = linear_map(*pixels, shadowOffset, midOffset, highlightOffset);
			pixels += 3; // Move forward by the three channels in image
		}
	}

	image.syncPixels();

	Magick::Blob blob{};

	image.magick("TIFF");
	image.colorSpace(Magick::sRGBColorspace);
	image.write(&blob);

	const auto blob_data = static_cast<const std::uint8_t*>(blob.data());
	const size_t blob_length = blob.length();

	return std::vector<std::uint8_t>{ blob_data, blob_data + blob_length };
}
