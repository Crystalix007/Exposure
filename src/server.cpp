#include "server.hpp"

#include "config.hpp"

#include <zmqpp/message.hpp>

Server::Server(zmqpp::context& context) : push_socket{ context, zmqpp::socket_type::push } {
	push_socket.bind("tcp://*:" + std::to_string(SERVICE_PORT));

	assert(push_socket);
}

void Server::serve_work(const std::filesystem::path& serve_path) {
	assert(std::filesystem::exists(serve_path));
	assert(std::filesystem::is_directory(serve_path));

	for (const auto& file : std::filesystem::directory_iterator{ serve_path }) {
		if (!std::filesystem::is_regular_file(file)) {
			continue;
		}

		zmqpp::message message{};
		message << file.path();
		push_socket.send(message);
	}
}
