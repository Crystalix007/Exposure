#pragma once

#include "config.hpp"
#include "semaphore.hpp"

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <zmqpp/zmqpp.hpp>

class WorkerJobCommand;
class WorkerResultCommand;

namespace zmqpp {
	class context;
	class socket;
} // namespace zmqpp

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

	ServerConnection(const std::string& name, const std::string& address, uint16_t port);
	ServerConnection(ServerConnection&& other) noexcept;
	explicit ServerConnection(ServerDetails serverDetails);

	virtual ~ServerConnection();

	void connect(zmqpp::context& context);
	void disconnect();

	bool operator==(const ServerConnection& other) const noexcept;
	bool operator==(const ServerDetails& other) const noexcept;
	bool connected() const;

	void run();

	zmqpp::endpoint_t work_endpoint() const;

	ServerConnection::State state() const;
	std::optional<std::unique_ptr<WorkerJobCommand>> pop_job();

	ServerConnection::State transition_state(ServerConnection::State nextState);
	static ServerConnection::State transition_state(ServerConnection::State currentState,
	                                                ServerConnection::State nextState);

	void send_message(zmqpp::message message) const;
	void schedule_job(std::unique_ptr<WorkerJobCommand> job);
	void notify_dying();
	void notify_job();

protected:
	const std::uint32_t THREAD_COUNT = LIBRARY_PARALLELISM ? 1U : std::thread::hardware_concurrency();

	// A single thread, capable of doing one task at a time
	void background_task();

	// Spawn hardware_concurrency() threads of background_task()
	void background_tasks();

	ServerDetails serverDetails;
	std::unique_ptr<zmqpp::socket> workSocket;
	mutable std::mutex workSocketMutex;
	ServerConnection::State currentState;
	mutable std::mutex currentStateMutex;
	std::binary_semaphore finishedSemaphore;
	std::vector<std::unique_ptr<WorkerJobCommand>> jobs;
	std::mutex jobsMutex;

	// Can use std C++ semaphores if your implementation correctly implements semaphore wake semantics
	// std::counting_semaphore<MAX_HARDWARE_CONCURRENCY> jobsSemaphore;
	POSIXSemaphore jobsSemaphore;
	std::thread backgroundThread;
};

class Worker {
public:
	Worker();

	void add_server(const std::string& name, const std::string& address, uint16_t port);
	void remove_server(const std::string& name, const std::string& address, uint16_t port);
	void run_jobs(zmqpp::context context);

	bool has_jobs() const;
	std::optional<ServerConnection> pop_connection();

protected:
	std::map<ServerDetails, ServerConnection> connections;
	mutable std::recursive_mutex connectionsMutex;
};
