#include "network.hpp"

#include <iostream>

#include <avahi-client/client.h>
#include <avahi-client/lookup.h>
#include <avahi-client/publish.h>
#include <avahi-common/alternative.h>
#include <avahi-common/defs.h>
#include <avahi-common/error.h>
#include <avahi-common/malloc.h>
#include <avahi-common/simple-watch.h>

static AvahiSimplePoll* simple_poll{};
static AvahiEntryGroup* group{};
static char* name{};

// Server-related functions
void create_avahi_services(AvahiClient* c);
void avahi_server_callback(AvahiClient* c, AvahiClientState state, void* userdata);
void avahi_entry_group_callback(AvahiEntryGroup* g, AvahiEntryGroupState state, void*);

// Client-related functions
void avahi_client_callback(AvahiClient* c, AvahiClientState state, void* userdata);
void avahi_client_browse_callback(AvahiServiceBrowser* b, AvahiIfIndex interface,
                                  AvahiProtocol protocol, AvahiBrowserEvent event, const char* name,
                                  const char* type, const char* domain,
                                  AvahiLookupResultFlags flags, void* userdata);
void avahi_client_resolve_callback(AvahiServiceResolver* r, AvahiIfIndex, AvahiProtocol,
                                   AvahiResolverEvent event, const char* name, const char* type,
                                   const char* domain, const char* host_name,
                                   const AvahiAddress* address, uint16_t port, AvahiStringList* txt,
                                   AvahiLookupResultFlags flags, void*);

static char service_name[] = "_image_histogram._tcp";

std::future<void> start_mdns_service() {
	AvahiClient* client{};

	if (!(simple_poll = avahi_simple_poll_new())) {
		std::cerr << "Failed to create Avahi simple server poll object.\n";
		return {};
	}

	name = avahi_strdup("ImageHistogramServer");

	int error;
	client =
	    avahi_client_new(avahi_simple_poll_get(simple_poll), AvahiClientFlags::AVAHI_CLIENT_NO_FAIL,
	                     avahi_server_callback, nullptr, &error);

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

void avahi_server_callback(AvahiClient* c, AvahiClientState state, void*) {
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
		                                           service_name, nullptr, nullptr, 42069, nullptr))) {
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

void avahi_client_callback(AvahiClient* c, AvahiClientState state, void* userdata) {
	assert(c);

	/* Called whenever the client or server state changes */
	if (state == AVAHI_CLIENT_FAILURE) {
		std::cerr << "Server connection failure: " << avahi_strerror(avahi_client_errno(c)) << "\n";
		avahi_simple_poll_quit(simple_poll);
	}
}

void resolve_callback(AvahiServiceResolver* r, AvahiIfIndex, AvahiProtocol,
                      AvahiResolverEvent event, const char* name, const char* type,
                      const char* domain, const char* host_name, const AvahiAddress* address,
                      uint16_t port, AvahiStringList* txt, AvahiLookupResultFlags flags, void*) {
	assert(r);

	/* Called whenever a service has been resolved successfully or timed out */
	switch (event) {
		case AVAHI_RESOLVER_FAILURE:
			std::cerr << "(Resolver) Failed to resolve service '" << name << "' of type '" << type
			          << "' in domain '" << domain
			          << "': " << avahi_strerror(avahi_client_errno(avahi_service_resolver_get_client(r)))
			          << "\n";
			break;
		case AVAHI_RESOLVER_FOUND: {
			char a[AVAHI_ADDRESS_STR_MAX], *t;
			std::clog << "Service '" << name << "' of type '" << type << "' in domain '" << domain
			          << "': "
			          << "\n";
			avahi_address_snprint(a, sizeof(a), address);
			t = avahi_string_list_to_string(txt);
			fprintf(stderr,
			        "\t%s:%u (%s)\n"
			        "\tTXT=%s\n"
			        "\tcookie is %u\n"
			        "\tis_local: %i\n"
			        "\tour_own: %i\n"
			        "\twide_area: %i\n"
			        "\tmulticast: %i\n"
			        "\tcached: %i\n",
			        host_name, port, a, t, avahi_string_list_get_service_cookie(txt),
			        !!(flags & AVAHI_LOOKUP_RESULT_LOCAL), !!(flags & AVAHI_LOOKUP_RESULT_OUR_OWN),
			        !!(flags & AVAHI_LOOKUP_RESULT_WIDE_AREA), !!(flags & AVAHI_LOOKUP_RESULT_MULTICAST),
			        !!(flags & AVAHI_LOOKUP_RESULT_CACHED));
			avahi_free(t);
		}
	}
	avahi_service_resolver_free(r);
}

void avahi_client_browse_callback(AvahiServiceBrowser* b, AvahiIfIndex interface,
                                  AvahiProtocol protocol, AvahiBrowserEvent event, const char* name,
                                  const char* type, const char* domain,
                                  AvahiLookupResultFlags flags, void* userdata) {
	AvahiClient* client = static_cast<AvahiClient*>(userdata);
	assert(b);

	/* Called whenever a new services becomes available on the LAN or is removed from the LAN */
	switch (event) {
		case AVAHI_BROWSER_FAILURE:
			std::cerr << "(Browser) "
			          << avahi_strerror(avahi_client_errno(avahi_service_browser_get_client(b))) << "\n";
			avahi_simple_poll_quit(simple_poll);
			return;
		case AVAHI_BROWSER_NEW:
			std::clog << "(Browser) NEW: service '" << name << "' of type '" << type << "' in domain '"
			          << domain << "'\n";
			/* We ignore the returned resolver object. In the callback
			   function we free it. If the server is terminated before
			   the callback function is called the server will free
			   the resolver for us. */
			if (!(avahi_service_resolver_new(client, interface, protocol, name, type, domain,
			                                 AVAHI_PROTO_UNSPEC, static_cast<AvahiLookupFlags>(0),
			                                 resolve_callback, client)))
				std::clog << "Failed to resolve service '" << name
				          << "': " << avahi_strerror(avahi_client_errno(client)) << "\n";
			break;
		case AVAHI_BROWSER_REMOVE:
			std::clog << "(Browser) REMOVE: service '" << name << "' of type '" << type << "' in domain '"
			          << domain << "'\n";
			break;
		case AVAHI_BROWSER_ALL_FOR_NOW:
		case AVAHI_BROWSER_CACHE_EXHAUSTED:
			const auto event_description =
			    ((event == AVAHI_BROWSER_CACHE_EXHAUSTED) ? "CACHE_EXHAUSTED" : "ALL_FOR_NOW");
			std::clog << "(Browser) " << event_description << "\n";
			break;
	}
}

void mdns_find_server() {
	AvahiClient* client{};
	AvahiServiceBrowser* service_browser{};

	if (!(simple_poll = avahi_simple_poll_new())) {
		std::cerr << "Failed to create Avahi simple client poll object.\n";
		return;
	}

	name = avahi_strdup("ImageHistogramClient");

	int error;
	client =
	    avahi_client_new(avahi_simple_poll_get(simple_poll), AvahiClientFlags::AVAHI_CLIENT_NO_FAIL,
	                     avahi_client_callback, nullptr, &error);

	if (!client) {
		std::cerr << "Failed to create Avahi client object: " << avahi_strerror(error) << ".\n";
		return;
	}

	if (!(service_browser = avahi_service_browser_new(
	          client, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, service_name, nullptr,
	          static_cast<AvahiLookupFlags>(0), avahi_client_browse_callback, client))) {
		std::cerr << "Failed to create service browser: " << avahi_strerror(avahi_client_errno(client))
		          << "\n";
		return;
	}

	avahi_simple_poll_loop(simple_poll);

	if (client) {
		avahi_client_free(client);
	}

	if (simple_poll) {
		avahi_simple_poll_free(simple_poll);
	}

	avahi_free(name);
}
