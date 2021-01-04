#include "network.hpp"

#include <iostream>

#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#include <avahi-common/alternative.h>
#include <avahi-common/defs.h>
#include <avahi-common/error.h>
#include <avahi-common/malloc.h>
#include <avahi-common/simple-watch.h>

static AvahiSimplePoll* simple_poll{};
static AvahiEntryGroup* group{};
static char* name{};

void create_avahi_services(AvahiClient* c);
void avahi_client_callback(AvahiClient* c, AvahiClientState state, void* userdata);
void avahi_entry_group_callback(AvahiEntryGroup* g, AvahiEntryGroupState state, void*);

std::future<void> start_mdns_service() {
	AvahiClient* client{};

	if (!(simple_poll = avahi_simple_poll_new())) {
		std::cerr << "Failed to create Avahi simple poll object.\n";
		return {};
	}

	name = avahi_strdup("ImageHistogramServer");

	int error;
	client =
	    avahi_client_new(avahi_simple_poll_get(simple_poll), AvahiClientFlags::AVAHI_CLIENT_NO_FAIL,
	                     avahi_client_callback, nullptr, &error);

	if (!client) {
		std::cerr << "Failed to create Avahi client object: " << avahi_strerror(error) << ".\n";
		return {};
	}

	const auto run_mdns_service = [client]() {
		avahi_simple_poll_loop(simple_poll);

		if (client) {
			avahi_client_free(client);
		}

		if (simple_poll) {
			avahi_simple_poll_free(simple_poll);
		}

		avahi_free(name);
	};

	return std::async(std::launch::async, run_mdns_service);
}

void stop_mdns_service(std::future<void> mdns_service) {
	avahi_simple_poll_quit(simple_poll);

	mdns_service.wait();
}

void avahi_client_callback(AvahiClient* c, AvahiClientState state, void*) {
	assert(c);

	switch (state) {
		case AVAHI_CLIENT_S_RUNNING:
			return create_avahi_services(c);
		case AVAHI_CLIENT_FAILURE:
			std::cerr << "Avahi client failure: " << avahi_strerror(avahi_client_errno(c)) << "\n";
			avahi_simple_poll_quit(simple_poll);
			return;
		case AVAHI_CLIENT_S_COLLISION:
			/* Let's drop our registered services. When the server is back
			 * in AVAHI_SERVER_RUNNING state we will register them
			 * again with the new host name. */
		case AVAHI_CLIENT_S_REGISTERING:
			/* The server records are now being established. This
			 * might be caused by a host name change. We need to wait
			 * for our own records to register until the host name is
			 * properly esatblished. */
			if (group)
				avahi_entry_group_reset(group);
			break;
		case AVAHI_CLIENT_CONNECTING:;
	}
}

void create_avahi_services(AvahiClient* c) {
	if (!group) {
		if (!(group = avahi_entry_group_new(c, avahi_entry_group_callback, nullptr))) {
			std::cerr << "Failed to create Avahi entry group: " << avahi_strerror(avahi_client_errno(c))
			          << "\n";
			return;
		}
	}

	int error = 0;

	if (avahi_entry_group_is_empty(group)) {
		std::clog << "Adding service '" << name << "'\n";

		if ((error = avahi_entry_group_add_service(group, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC,
		                                           AvahiPublishFlags::AVAHI_PUBLISH_USE_MULTICAST, name,
		                                           "_image_histogram._tcp", nullptr, nullptr, 42069,
		                                           nullptr))) {
			std::cerr << "Failed to register Avahi service with entry group: " << avahi_strerror(error)
			          << "\n";
			return;
		}

		if ((error = avahi_entry_group_commit(group)) < 0) {
			std::cerr << "Failed to commit entry group: " << avahi_strerror(error) << "\n";
			return;
		}
	}
}

void avahi_entry_group_callback(AvahiEntryGroup* g, AvahiEntryGroupState state, void*) {
	assert(g == group || !group);
	group = g;

	switch (state) {
		case AVAHI_ENTRY_GROUP_ESTABLISHED:
			std::cerr << "Service '" << name << "%s' successfully established.\n";
			break;
		case AVAHI_ENTRY_GROUP_COLLISION: {
			char* n;
			n = avahi_alternative_service_name(name);
			avahi_free(name);
			name = n;
			std::cerr << "Service name collision, renaming service to '" << name << "'\n";
			create_avahi_services(avahi_entry_group_get_client(g));
			break;
		}
		case AVAHI_ENTRY_GROUP_FAILURE:
			std::cerr << "Entry group failure: "
			          << avahi_strerror(avahi_client_errno(avahi_entry_group_get_client(g))) << "\n";
			avahi_simple_poll_quit(simple_poll);
			break;
		case AVAHI_ENTRY_GROUP_UNCOMMITED:
		case AVAHI_ENTRY_GROUP_REGISTERING:;
	}
}
