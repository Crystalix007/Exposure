#pragma once

#include <future>

#include "worker.hpp"

class Worker;

// DNS-SD services (i.e. automatic network discovery of clients and servers)

std::future<void> start_mdns_service();
void stop_mdns_service(std::future<void> mdnsService);

void mdns_find_server(Worker& worker);

// Work Server

void serve_work();
