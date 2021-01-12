#include "worker.hpp"
#include <cassert>
#include <utility>

ServerDetails::ServerDetails(std::string name, std::string address, std::uint16_t port)
    : name{ name }, address{ address }, port{ port } {}

bool ServerDetails::operator==(const ServerDetails& other) const noexcept {
	return this->name == other.name && this->address == other.address && this->port == other.port;
}

bool ServerDetails::operator<(const ServerDetails& other) const noexcept {
	return this->name < other.name || this->address < other.address;
}

ServerConnection::ServerConnection(const std::string name, const std::string address,
                                   const uint16_t port)
    : serverDetails{ name, address, port }, pull_socket{}, currentState{
	      ServerConnection::State::Unconnected
      } {
	assert(address.length() == 4 || address.length() == 16);
}

ServerConnection::ServerConnection(const ServerDetails serverDetails)
    : serverDetails{ serverDetails }, pull_socket{}, currentState{
	      ServerConnection::State::Unconnected
      } {}

ServerConnection::ServerConnection(ServerConnection&& other)
    : serverDetails{ other.serverDetails }, pull_socket{ std::move(other.pull_socket) },
      currentState{ other.currentState.load() } {}

ServerConnection::~ServerConnection() {
	this->disconnect();
}

void ServerConnection::connect(zmqpp::context& context) {
	assert(!this->connected());
	assert(this->currentState != ServerConnection::State::Connected);

	if (this->currentState != ServerConnection::State::Dying) {
		this->currentState = transitionState(ServerConnection::State::Connected);
		pull_socket = std::make_unique<zmqpp::socket>(context, zmqpp::socket_type::pull);
		const auto& endpoint = this->endpoint();
		std::clog << "Worker connecting to " << endpoint << "\n";
		pull_socket->connect(this->endpoint());

		assert(pull_socket);
	}
}

void ServerConnection::disconnect() {
	if (this->currentState != ServerConnection::State::Unconnected) {
		this->currentState = transitionState(ServerConnection::State::Dying);
		std::unique_lock<std::mutex> lock{ this->running_mutex };
		running_condition.wait(lock);
		pull_socket->unbind(this->endpoint());
		pull_socket.reset();
	}
}

bool ServerConnection::operator==(const ServerConnection& other) const noexcept {
	return this->serverDetails == other.serverDetails;
}

bool ServerConnection::operator==(const ServerDetails& other) const noexcept {
	return this->serverDetails == other;
}

bool ServerConnection::connected() const {
	return static_cast<bool>(pull_socket);
}

void ServerConnection::run() {
	assert(this->currentState != ServerConnection::State::Unconnected);
	assert(this->pull_socket);

	while (this->state() != ServerConnection::State::Dying) {
		// Do analysis
		std::string message;
		pull_socket->receive(message);

		std::cout << message << std::endl;
	}

	std::unique_lock<std::mutex> lock{ this->running_mutex };
	this->running_condition.notify_all();
}

std::atomic<ServerConnection::State>& ServerConnection::state() {
	return this->currentState;
}

ServerConnection::State ServerConnection::state() const {
	return this->currentState;
}

ServerConnection::State ServerConnection::transitionState(ServerConnection::State nextState) const {
	switch (this->currentState) {
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

	return this->currentState;
}

zmqpp::endpoint_t ServerConnection::endpoint() const {
	assert(this->connected());

	return "tcp://" + serverDetails.address + ":" + std::to_string(serverDetails.port);
}

Worker::Worker() : connections{}, connectionsMutex{} {}

void Worker::addServer(const std::string name, const std::string address, const uint16_t port) {
	assert(name != "");

	std::lock_guard<std::recursive_mutex> connectionsLock{ connectionsMutex };
	ServerDetails details{ name, address, port };
	ServerConnection connection{ details };

	connections.insert(std::pair<ServerDetails, ServerConnection>(details, std::move(connection)));
}

void Worker::removeServer(const std::string name, const std::string address, const uint16_t port) {
	assert(!connections.empty());

	const ServerDetails details{ name, address, port };
	std::lock_guard<std::recursive_mutex> connectionsLock{ this->connectionsMutex };
	auto connection = this->connections.find(details);

	connections.erase(connection);
}

bool Worker::hasJobs() const {
	std::unique_lock<std::recursive_mutex> lock{ this->connectionsMutex };
	return connections.size();
}

std::optional<ServerConnection> Worker::popConnection() {
	std::unique_lock lock{ this->connectionsMutex };

	if (this->hasJobs()) {
		auto connectionIter = connections.begin();
		ServerConnection connection = std::move(connectionIter->second);
		connections.erase(connectionIter);
		return std::move(connection);
	}

	return std::nullopt;
}

void Worker::runJobs(zmqpp::context context) {
	while (auto connection = this->popConnection()) {
		connection->connect(context);
		connection->run();
	}
}
