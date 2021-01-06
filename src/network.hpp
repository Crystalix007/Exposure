#pragma once

#include <future>

// DNS-SD services (i.e. automatic network discovery of clients and servers)

std::future<void> start_mdns_service();
void stop_mdns_service(std::future<void> mdns_service);

void mdns_find_server();

// Work Server

void serve_work();
