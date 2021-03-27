#pragma once

#include <future>

#include "worker.hpp"

class Worker;

// DNS-SD services (i.e. automatic network discovery of clients and servers)

std::future<void> start_mdns_service();
void stop_mdns_service(std::future<void> mdnsService);

/* Find server via mDNS records. */
/**
 * @brief Find server via mDNS records.
 *
 * @param worker Worker instance to update records of
 * @param persists Whether the service should persist after finding the first mDNS record
 */
void mdns_find_server(Worker& worker, bool persists = false);

// Work Server

void serve_work();
