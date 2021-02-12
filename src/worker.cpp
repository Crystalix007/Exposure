#include "worker.hpp"

#include <cassert>
#include <ostream>
#include <utility>
#include <zmqpp/context.hpp>
#include <zmqpp/socket.hpp>
#include <zmqpp/socket_types.hpp>

#include "protocol.hpp"

ServerDetails::ServerDetails(std::string name, std::string address, std::uint16_t port)
    : name{ std::move(name) }, address{ std::move(address) }, port{ port } {}

bool ServerDetails::operator==(const ServerDetails& other) const noexcept {
	return this->name == other.name && this->address == other.address && this->port == other.port;
}

bool ServerDetails::operator<(const ServerDetails& other) const noexcept {
	return this->name < other.name || this->address < other.address;
}

ServerConnection::ServerConnection(const std::string& name, const std::string& address,
                                   const uint16_t port)
    : serverDetails{ name, address, port }, work_socket{}, currentState{
	      ServerConnection::State::Unconnected
      } {
	assert(address.length() == 4 || address.length() == 16);
}

ServerConnection::ServerConnection(ServerDetails serverDetails)
    : serverDetails{ std::move(serverDetails) }, work_socket{}, currentState{
	      ServerConnection::State::Unconnected
      } {}

ServerConnection::ServerConnection(ServerConnection&& other) noexcept
    : serverDetails{ std::move(other.serverDetails) }, work_socket{ std::move(other.work_socket) },
      currentState{ other.currentState } {}

ServerConnection::~ServerConnection() {
	this->disconnect();
}

void ServerConnection::connect(zmqpp::context& context) {
	assert(!this->connected());
	assert(this->state() != ServerConnection::State::Connected);

	std::unique_lock<std::mutex> currentStateLock{ this->currentStateMutex };

	if (this->currentState != ServerConnection::State::Dying) {
		ConnectingWorkerCommandVisitor visitor{ this->currentState };
		work_socket = std::make_unique<zmqpp::socket>(context, zmqpp::socket_type::dealer);

		const auto& endpoint = this->work_endpoint();
		std::clog << "Worker connecting to " << endpoint << "\n";
		work_socket->connect(this->work_endpoint());

		const auto heloCommand = WorkerHeloCommand{};
		auto heloMessage = heloCommand.to_message();
		work_socket->send(heloMessage);

		std::string returnMessage{};
		work_socket->receive(returnMessage);
		const auto command = WorkerCommand::from_serialised_string(returnMessage);
		command->visit(visitor);

		assert(this->currentState == ServerConnection::State::Connected);
	}
}

void ServerConnection::disconnect() {
	ServerConnection::State previousState{};

	{
		std::unique_lock<std::mutex> currentStateLock{ this->currentStateMutex };
		previousState = this->currentState;
		this->currentState = transition_state(ServerConnection::State::Dying);
	}

	if (previousState != ServerConnection::State::Unconnected) {
		std::unique_lock<std::mutex> lock{ this->running_mutex };
		running_condition.wait(lock);
		work_socket->disconnect(this->work_endpoint());
		work_socket.reset();
	}
}

bool ServerConnection::operator==(const ServerConnection& other) const noexcept {
	return this->serverDetails == other.serverDetails;
}

bool ServerConnection::operator==(const ServerDetails& other) const noexcept {
	return this->serverDetails == other;
}

bool ServerConnection::connected() const {
	return static_cast<bool>(work_socket);
}

void ServerConnection::run() {
	assert(this->currentState != ServerConnection::State::Unconnected);
	assert(this->work_socket);

	while (this->state() != ServerConnection::State::Dying) {
		std::string message;
		work_socket->receive(message);
		RunningWorkerCommandVisitor commandVisitor{ *this->work_socket };

		WorkerCommand::from_serialised_string(message)->visit(commandVisitor);
	}

	std::unique_lock<std::mutex> lock{ this->running_mutex };
	this->running_condition.notify_all();
}

ServerConnection::State ServerConnection::state() const {
	std::unique_lock<std::mutex> currentStateLock{ this->currentStateMutex };
	return this->currentState;
}

ServerConnection::State
ServerConnection::transition_state(ServerConnection::State nextState) const {
	return ServerConnection::transition_state(this->currentState, nextState);
}

ServerConnection::State ServerConnection::transition_state(ServerConnection::State currentState,
                                                           ServerConnection::State nextState) {
	switch (currentState) {
		case ServerConnection::State::Unconnected:
			return nextState;
		case ServerConnection::State::Connected:
			if (nextState != ServerConnection::State::Unconnected) {
				return nextState;
			}
			break;
		case ServerConnection::State::Dying:
			break;
	}

	return currentState;
}

zmqpp::endpoint_t ServerConnection::work_endpoint() const {
	assert(this->connected());

	return "tcp://" + serverDetails.address + ":" + std::to_string(serverDetails.port);
}

Worker::Worker() = default;

void Worker::add_server(const std::string& name, const std::string& address, const uint16_t port) {
	assert(!name.empty());

	std::lock_guard<std::recursive_mutex> connectionsLock{ connectionsMutex };
	ServerDetails details{ name, address, port };
	ServerConnection connection{ details };

	connections.insert(std::pair<ServerDetails, ServerConnection>(details, std::move(connection)));
}

void Worker::remove_server(const std::string& name, const std::string& address,
                           const uint16_t port) {
	assert(!connections.empty());

	const ServerDetails details{ name, address, port };
	std::lock_guard<std::recursive_mutex> connectionsLock{ this->connectionsMutex };
	auto connection = this->connections.find(details);

	connections.erase(connection);
}

bool Worker::has_jobs() const {
	std::unique_lock<std::recursive_mutex> lock{ this->connectionsMutex };
	return !connections.empty();
}

std::optional<ServerConnection> Worker::pop_connection() {
	std::unique_lock lock{ this->connectionsMutex };

	if (this->has_jobs()) {
		auto connectionIter = connections.begin();
		ServerConnection connection = std::move(connectionIter->second);
		connections.erase(connectionIter);
		return std::move(connection);
	}

	return std::nullopt;
}

void Worker::run_jobs(zmqpp::context context) {
	while (auto connection = this->pop_connection()) {
		connection->connect(context);
		connection->run();
	}
}
