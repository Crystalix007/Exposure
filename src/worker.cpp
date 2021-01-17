#include "worker.hpp"
#include <cassert>
#include <utility>

#include "algorithm.hpp"
#include "protocol.hpp"

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
    : serverDetails{ name, address, port }, work_socket{}, currentState{
	      ServerConnection::State::Unconnected
      } {
	assert(address.length() == 4 || address.length() == 16);
}

ServerConnection::ServerConnection(const ServerDetails serverDetails)
    : serverDetails{ serverDetails }, work_socket{}, currentState{
	      ServerConnection::State::Unconnected
      } {}

ServerConnection::ServerConnection(ServerConnection&& other)
    : serverDetails{ other.serverDetails }, work_socket{ std::move(other.work_socket) },
      currentState{ other.currentState.load() } {}

ServerConnection::~ServerConnection() {
	this->disconnect();
}

void ServerConnection::connect(zmqpp::context& context) {
	assert(!this->connected());
	assert(this->currentState != ServerConnection::State::Connected);

	if (this->currentState != ServerConnection::State::Dying) {
		this->currentState = transitionState(ServerConnection::State::Connected);
		work_socket = std::make_unique<zmqpp::socket>(context, zmqpp::socket_type::dealer);

		work_socket->set(zmqpp::socket_option::receive_high_water_mark, 1);
		work_socket->set(zmqpp::socket_option::conflate, true);

		const auto& endpoint = this->work_endpoint();
		std::clog << "Worker connecting to " << endpoint << "\n";
		work_socket->connect(this->work_endpoint());

		auto helo = zmq_worker_helo();
		work_socket->send(helo);

		assert(work_socket);
	}
}

void ServerConnection::disconnect() {
	if (this->currentState != ServerConnection::State::Unconnected) {
		this->currentState = transitionState(ServerConnection::State::Dying);
		std::unique_lock<std::mutex> lock{ this->running_mutex };
		running_condition.wait(lock);
		work_socket->unbind(this->work_endpoint());
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
		// Do analysis
		std::string message;
		work_socket->receive(message);

		std::cout << message << std::endl;

		std::optional<Histogram> histogram = image_get_histogram(message);

		assert(histogram);

		zmqpp::message response{};
		response << encodeHistogramResult(message, histogram.value());

		work_socket->send(response);
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

zmqpp::endpoint_t ServerConnection::work_endpoint() const {
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
