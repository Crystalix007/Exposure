#pragma once

#include <future>

std::future<void> start_mdns_service();
void stop_mdns_service(std::future<void> mdns_service);
