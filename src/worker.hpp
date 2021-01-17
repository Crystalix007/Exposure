#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include <zmqpp/zmqpp.hpp>

struct ServerDetails {
	std::string name;
	std::string address;
	std::uint16_t port;

	ServerDetails(std::string name, std::string address, std::uint16_t port);
	ServerDetails(const ServerDetails& other) noexcept = default;
	ServerDetails(ServerDetails&& other) noexcept = default;
	bool operator==(const ServerDetails& other) const noexcept;
	bool operator<(const ServerDetails& other) const noexcept;
	ServerDetails& operator=(const ServerDetails& other) noexcept = default;
	ServerDetails& operator=(ServerDetails&& other) noexcept = default;
};

class ServerConnection {
public:
	enum class State {
		Unconnected,
		Connected,
		Dying,
	};

	ServerConnection(const std::string name, const std::string address, const uint16_t port);
	ServerConnection(ServerConnection&& other);
	explicit ServerConnection(const ServerDetails serverDetails);

	virtual ~ServerConnection();

	void connect(zmqpp::context& context);
	void disconnect();

	bool operator==(const ServerConnection& other) const noexcept;
	bool operator==(const ServerDetails& other) const noexcept;
	bool connected() const;

	void run();

	zmqpp::endpoint_t work_endpoint() const;

	std::atomic<ServerConnection::State>& state();
	ServerConnection::State state() const;

	ServerConnection::State transitionState(ServerConnection::State nextState) const;

protected:
	ServerDetails serverDetails;
	std::unique_ptr<zmqpp::socket> work_socket;
	std::atomic<ServerConnection::State> currentState;
	std::condition_variable running_condition;
	std::mutex running_mutex;
};

class Worker {
public:
	Worker();

	void addServer(const std::string name, const std::string address, const uint16_t port);
	void removeServer(const std::string name, const std::string address, const uint16_t port);
	void runJobs(zmqpp::context context);

	bool hasJobs() const;
	std::optional<ServerConnection> popConnection();

protected:
	std::map<ServerDetails, ServerConnection> connections;
	mutable std::recursive_mutex connectionsMutex;
};
