#include <Magick++.h>
#include <array>
#include <cstring>
#include <iostream>

extern "C" {
#include <microdns/microdns.h>
}

const constexpr uint32_t histogramSegments = 1 << 6ull;

bool mdns_server_stop(void*);
void mdns_server_callback(void*, const struct sockaddr* mdns_ip, const char* service,
                          enum mdns_announce_type type);

int process_image(const std::string filename);

Magick::Image get_lightness_channel(const std::string filename);
std::array<float, histogramSegments> get_histogram(const Magick::Image& lightness_channel);

int main(int argc, char* argv[]) {
	if (argc <= 1) {
		std::cerr << "Usage: " << argv[0] << " [options] <image/to/process> ...\n";
		return -1;
	}

	int r = 0;
	struct mdns_ctx* ctx = nullptr;

	if ((r = mdns_init(&ctx, nullptr, MDNS_PORT)) < 0) {
		std::cerr << "Failed to initialise mDNS service\n";
		return 2;
	}

	if (strcmp(argv[1], "--client") == 0) {
		std::clog << "Running as client only\n";
		return 5;
	}

	Magick::InitializeMagick(*argv);

	mdns_announce(ctx, RR_PTR, mdns_server_callback, ctx);

	if ((r = mdns_serve(ctx, mdns_server_stop, nullptr)) < 0) {
		std::cerr << "Failed to announce mDNS records\n";
		return 3;
	}

	r = process_image(argv[1]);

	mdns_destroy(ctx);
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

void mdns_server_callback(void* callback_data, const struct sockaddr* mdns_ip, const char* service,
                          enum mdns_announce_type type) {
	const constexpr size_t answer_count = 2;
	struct mdns_ctx* ctx = static_cast<struct mdns_ctx*>(callback_data);
	struct mdns_hdr hdr = {};
	struct rr_entry answers[answer_count] = { {} };

	hdr.flags |= FLAG_QR;
	hdr.flags |= FLAG_AA;

	hdr.num_ans_rr = answer_count;

	for (size_t i = 0; i < answer_count; i++) {
		answers[i].rr_class = RR_IN;
		answers[i].ttl = type == mdns_announce_type::MDNS_ANNOUNCE_GOODBYE ? 0 : 120;
		answers[i].msbit = 1;

		if (i + 1 < answer_count) {
			answers[i].next = &answers[i + 1];
		}
	}

	char domain_name[] = "Kekstop-PC.local";
	char service_type[] = "_image_histogram._tcp.local";
	char service_type_link[] = "Kekstop-PC Kekstop-PC._image_histogram._tcp.local";

	answers[0].type = RR_PTR;
	answers[0].name = service_type;
	answers[0].data.PTR.domain = service_type_link;

	answers[1].type = RR_SRV;
	answers[1].name = service_type_link;
	answers[1].data.SRV.port = 4200;
	answers[1].data.SRV.priority = 0;
	answers[1].data.SRV.weight = 0;
	answers[1].data.SRV.target = domain_name;

	mdns_entries_send(ctx, &hdr, answers);
}

bool mdns_server_stop(void*) {
	return false;
}
