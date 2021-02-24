#include "worker.hpp"

#include <cassert>
#include <mutex>
#include <ostream>
#include <utility>
#include <zmqpp/context.hpp>
#include <zmqpp/socket.hpp>
#include <zmqpp/socket_types.hpp>

#include "protocol.hpp"

// Command visitor to use whilst connecting to a server
class ConnectingWorkerCommandVisitor : public CommandVisitor {
public:
	ConnectingWorkerCommandVisitor(ServerConnection::State& connectionState);

	void visit_ehlo(const WorkerEhloCommand& ehloCommand) override;
	void visit_bye(const WorkerByeCommand& byeCommand) override;

protected:
	ServerConnection::State& connectionState;
};

// Command visitor to initially process
class CommunicatingWorkerCommandVisitor : public CommandVisitor {
public:
	CommunicatingWorkerCommandVisitor(ServerConnection& connection);

	void visit_helo(const WorkerHeloCommand& heloCommand) override;
	void visit_ehlo(const WorkerEhloCommand& ehloCommand) override;
	void visit_histogram_job(const WorkerHistogramJobCommand& jobCommand) override;
	void visit_equalisation_job(const WorkerEqualisationJobCommand& jobCommand) override;
	void visit_histogram_result(const WorkerHistogramResultCommand& resultCommand) override;
	void visit_equalisation_result(const WorkerEqualisationResultCommand& resultCommand) override;
	void visit_heartbeat(const WorkerHeartbeatCommand& heartbeatCommand) override;
	void visit_bye(const WorkerByeCommand& byeCommand) override;

protected:
	ServerConnection& connection;
};

// Command visitor used by running thread
class RunningWorkerCommandVisitor : public CommandVisitor {
public:
	RunningWorkerCommandVisitor(ServerConnection& connection);

	void visit_histogram_job(const WorkerHistogramJobCommand& jobCommand) override;
	void visit_equalisation_job(const WorkerEqualisationJobCommand& jobCommand) override;

protected:
	ServerConnection& connection;
};

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
    : serverDetails{ name, address, port }, workSocket{},
      currentState{ ServerConnection::State::Unconnected }, finishedSemaphore{ 0 }, jobsSemaphore{
	      0
      } {
	assert(address.length() == 4 || address.length() == 16);
}

ServerConnection::ServerConnection(ServerDetails serverDetails)
    : serverDetails{ std::move(serverDetails) }, workSocket{},
      currentState{ ServerConnection::State::Unconnected }, finishedSemaphore{ 0 }, jobsSemaphore{
	      0
      } {}

ServerConnection::ServerConnection(ServerConnection&& other) noexcept
    : serverDetails{ std::move(other.serverDetails) }, workSocket{ std::move(other.workSocket) },
      currentState{ other.currentState }, finishedSemaphore{ 0 }, jobsSemaphore{ 0 } {}

ServerConnection::~ServerConnection() {
	this->disconnect();
}

void ServerConnection::connect(zmqpp::context& context) {
	assert(!this->connected());
	assert(this->state() == ServerConnection::State::Unconnected);

	std::unique_lock<std::mutex> currentStateLock{ this->currentStateMutex };

	ConnectingWorkerCommandVisitor visitor{ this->currentState };
	workSocket = std::make_unique<zmqpp::socket>(context, zmqpp::socket_type::dealer);

	const auto& endpoint = this->work_endpoint();
	std::clog << "Worker connecting to " << endpoint << "\n";
	workSocket->connect(this->work_endpoint());

	const auto heloCommand = WorkerHeloCommand{};
	auto heloMessage = heloCommand.to_message();
	workSocket->send(heloMessage);

	std::string returnMessage{};
	workSocket->receive(returnMessage);
	const auto command = WorkerCommand::from_serialised_string(returnMessage);
	command->visit(visitor);

	assert(this->currentState == ServerConnection::State::Connected);
}

void ServerConnection::disconnect() {
	const auto previousState = this->transition_state(ServerConnection::State::Dying);

	if (previousState != ServerConnection::State::Unconnected) {
		jobsSemaphore.release();
		finishedSemaphore.acquire();
		workSocket->disconnect(this->work_endpoint());
		workSocket.reset();
	}
}

bool ServerConnection::operator==(const ServerConnection& other) const noexcept {
	return this->serverDetails == other.serverDetails;
}

bool ServerConnection::operator==(const ServerDetails& other) const noexcept {
	return this->serverDetails == other;
}

bool ServerConnection::connected() const {
	return static_cast<bool>(workSocket);
}

void ServerConnection::background_tasks() {
	assert(this->currentState != ServerConnection::State::Unconnected);
	assert(this->workSocket);

	while (this->state() != ServerConnection::State::Dying) {
		while (const auto job = this->pop_job()) {
			RunningWorkerCommandVisitor visitor{ *this };
			job.value()->visit(visitor);
		}

		this->jobsSemaphore.acquire();
	}
}

void ServerConnection::run() {
	assert(this->currentState != ServerConnection::State::Unconnected);
	assert(this->workSocket);

	backgroundThread = std::thread{ &ServerConnection::background_tasks, this };

	while (this->state() != ServerConnection::State::Dying) {
		std::string message;
		workSocket->receive(message);

		CommunicatingWorkerCommandVisitor commandVisitor{ *this };

		WorkerCommand::from_serialised_string(message)->visit(commandVisitor);
	}

	backgroundThread.join();
	this->finishedSemaphore.release();
}

ServerConnection::State ServerConnection::state() const {
	std::unique_lock<std::mutex> currentStateLock{ this->currentStateMutex };
	return this->currentState;
}

std::optional<std::unique_ptr<WorkerJobCommand>> ServerConnection::pop_job() {
	std::unique_lock<std::mutex> jobsLock{ this->jobsMutex };

	if (this->jobs.empty()) {
		return std::nullopt;
	}

	auto job = std::move(this->jobs.back());
	this->jobs.pop_back();
	return std::move(job);
}

ServerConnection::State ServerConnection::transition_state(ServerConnection::State nextState) {
	std::unique_lock<std::mutex> currentStateLock{ this->currentStateMutex };
	const auto currentState = this->currentState;
	this->currentState = ServerConnection::transition_state(this->currentState, nextState);
	this->notify_job();
	return currentState;
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

void ServerConnection::send_message(zmqpp::message message) const {
	assert(this->connected());

	std::unique_lock<std::mutex> workSocketLock{ this->workSocketMutex };
	this->workSocket->send(message);
}

void ServerConnection::schedule_job(std::unique_ptr<WorkerJobCommand> job) {
	assert(job);

	std::unique_lock<std::mutex> jobsLock{ this->jobsMutex };
	this->jobs.push_back(std::move(job));
	this->notify_job();
}

void ServerConnection::notify_job() {
	this->jobsSemaphore.release();
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

ConnectingWorkerCommandVisitor::ConnectingWorkerCommandVisitor(
    ServerConnection::State& connectionState)
    : connectionState{ connectionState } {}

void ConnectingWorkerCommandVisitor::visit_ehlo(const WorkerEhloCommand& ehloCommand) {
	DEBUG_NETWORK("Visited server Ehlo whilst connecting\n");
	this->connectionState =
	    ServerConnection::transition_state(this->connectionState, ServerConnection::State::Connected);
}

void ConnectingWorkerCommandVisitor::visit_bye(const WorkerByeCommand& byeCommand) {
	DEBUG_NETWORK("Visited server Bye whilst connecting\n");
	this->connectionState =
	    ServerConnection::transition_state(this->connectionState, ServerConnection::State::Dying);
}

CommunicatingWorkerCommandVisitor::CommunicatingWorkerCommandVisitor(ServerConnection& connection)
    : connection{ connection } {}

void CommunicatingWorkerCommandVisitor::visit_helo(const WorkerHeloCommand& heloCommand) {
	/* Ignore unexpected message. */
	DEBUG_NETWORK("Visited server Helo (unexpected)\n");
}

void CommunicatingWorkerCommandVisitor::visit_ehlo(const WorkerEhloCommand& ehloCommand) {
	/* Mark connection as successful / connected. */
	DEBUG_NETWORK("Visited server Ehlo (unexpected once already connected)\n");
}

void CommunicatingWorkerCommandVisitor::visit_histogram_job(
    const WorkerHistogramJobCommand& jobCommand) {
	/* Schedule job. */
	DEBUG_NETWORK("Visited server Histogram Job: " << jobCommand.get_filename() << "\n");
	this->connection.schedule_job(std::make_unique<WorkerHistogramJobCommand>(jobCommand));
}

void CommunicatingWorkerCommandVisitor::visit_equalisation_job(
    const WorkerEqualisationJobCommand& jobCommand) {
	/* Schedule job. */
	DEBUG_NETWORK("Visited server Equalisation Job: " << jobCommand.get_filename() << "\n");
	this->connection.schedule_job(std::make_unique<WorkerEqualisationJobCommand>(jobCommand));
}

void CommunicatingWorkerCommandVisitor::visit_histogram_result(
    const WorkerHistogramResultCommand& resultCommand) {
	/* Ignore unexpected message. */
	DEBUG_NETWORK("Visited server Result (unexpected)\n");
}

void CommunicatingWorkerCommandVisitor::visit_equalisation_result(
    const WorkerEqualisationResultCommand& resultCommand) {
	/* Ignore unexpected message. */
	DEBUG_NETWORK("Visited server Result (unexpected)\n");
}

void CommunicatingWorkerCommandVisitor::visit_heartbeat(
    const WorkerHeartbeatCommand& heartbeatCommand) {
	/* Respond with the same heartbeat data. */
	DEBUG_NETWORK("Visited server Heartbeat\n");

	zmqpp::message commandMessage{
		WorkerHeartbeatCommand{ heartbeatCommand.get_peer_name() }.to_message()
	};

	this->connection.send_message(std::move(commandMessage));
}

void CommunicatingWorkerCommandVisitor::visit_bye(const WorkerByeCommand& byeCommand) {
	DEBUG_NETWORK("Visited server Bye\n");
	this->connection.transition_state(ServerConnection::State::Dying);
}

RunningWorkerCommandVisitor::RunningWorkerCommandVisitor(ServerConnection& connection)
    : connection{ connection } {}

void RunningWorkerCommandVisitor::visit_histogram_job(const WorkerHistogramJobCommand& jobCommand) {
	/* Run job. */
	DEBUG_NETWORK("Running Histogram Job: " << jobCommand.get_filename() << "\n");
	std::optional<Histogram> histogram = image_get_histogram(jobCommand.get_filename());

	assert(histogram);

	zmqpp::message response{
		WorkerHistogramResultCommand{ jobCommand.get_filename(), *histogram }.to_message()
	};

	this->connection.send_message(std::move(response));
}

void RunningWorkerCommandVisitor::visit_equalisation_job(
    const WorkerEqualisationJobCommand& jobCommand) {
	/* Run job. */
	DEBUG_NETWORK("Running Equalisation Job: " << jobCommand.get_filename() << "\n");
	std::vector<std::uint8_t> tiffFile =
	    image_equalise(jobCommand.get_filename(), jobCommand.get_histogram_mapping());

	zmqpp::message response{
		WorkerEqualisationResultCommand{ jobCommand.get_filename(), tiffFile }.to_message()
	};

	this->connection.send_message(std::move(response));
}
