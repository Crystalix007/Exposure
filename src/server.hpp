#pragma once

#include <zmqpp/context.hpp>
#include <zmqpp/socket.hpp>

#include <filesystem>

class Server {
public:
	Server(zmqpp::context& context);

	void serve_work(const std::filesystem::path& serve_path);

protected:
	zmqpp::socket push_socket;
};
