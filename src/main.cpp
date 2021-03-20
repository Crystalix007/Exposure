#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>
#include <zmqpp/context.hpp>
#include <zmqpp/context_options.hpp>

#include "Magick++/Functions.h"
#include "network.hpp"
#include "server.hpp"
#include "worker.hpp"

int process_image(std::string filename);

int main(int argc, char* argv[]) {
	if (argc <= 1) {
		std::cerr << "Usage: " << argv[0] << " [options] <image/to/process> ...\n";
		return -1;
	}

	zmqpp::context context{};

	// Enable IPv6 port communications
	context.set(zmqpp::context_option::ipv6, 1);

	if (strcmp(argv[1], "--client") == 0) {
		bool persist = (argc >= 2) && (strcmp(argv[2], "--persist") == 0);
		std::clog << "Running as client only\n";
		Magick::InitializeMagick(*argv);
		Worker worker{};
		mdns_find_server(worker);

		worker.run_jobs(std::move(context), persist);
		return 0;
	}

	std::clog << "Starting server\n";
	Server server{ context };

	auto mdnsService = start_mdns_service();

	server.serve_work(std::filesystem::path{ argv[1] });

	stop_mdns_service(std::move(mdnsService));

	return 0;
}
