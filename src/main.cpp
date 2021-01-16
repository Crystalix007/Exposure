#include <array>
#include <cmath>
#include <cstring>
#include <iostream>

#include <unistd.h>

#include "config.hpp"
#include "network.hpp"
#include "server.hpp"
#include "worker.hpp"

#include <Magick++.h>

int process_image(const std::string filename);

int main(int argc, char* argv[]) {
	if (argc <= 1) {
		std::cerr << "Usage: " << argv[0] << " [options] <image/to/process> ...\n";
		return -1;
	}

	zmqpp::context context{};

	// Enable IPv6 port communications
	context.set(zmqpp::context_option::ipv6, true);

	if (strcmp(argv[1], "--client") == 0) {
		std::clog << "Running as client only\n";
		Magick::InitializeMagick(*argv);
		Worker worker{};
		mdns_find_server(worker);

		worker.runJobs(std::move(context));
		return 0;
	}

	std::clog << "Starting server\n";
	Server server{ context };

	auto mdns_service = start_mdns_service();

	server.serve_work(std::filesystem::path{ argv[1] });

	stop_mdns_service(std::move(mdns_service));

	return 0;
}
