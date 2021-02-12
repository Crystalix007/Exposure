#include "network.hpp"

#include <avahi-client/client.h>
#include <avahi-client/lookup.h>
#include <avahi-client/publish.h>
#include <avahi-common/address.h>
#include <avahi-common/alternative.h>
#include <avahi-common/defs.h>
#include <avahi-common/error.h>
#include <avahi-common/malloc.h>
#include <avahi-common/simple-watch.h>
#include <avahi-common/strlst.h>
#include <cassert>
#include <cstdint>
#include <cxxabi.h>
#include <iostream>
#include <netinet/in.h>
#include <string>
#include <system_error>

#include "config.hpp"
#include "worker.hpp"

static AvahiSimplePoll* simplePoll{};
static AvahiEntryGroup* group{};
static char* name{};

struct MdnsContext {
	AvahiClient* client;
	Worker& worker;
};

// Server-related functions
void create_avahi_services(AvahiClient* c);
void avahi_server_callback(AvahiClient* c, AvahiClientState state, void* userdata);
void avahi_entry_group_callback(AvahiEntryGroup* g, AvahiEntryGroupState state, void* /*unused*/);

// Client-related functions
void avahi_client_callback(AvahiClient* c, AvahiClientState state, void* userdata);
void avahi_client_browse_callback(AvahiServiceBrowser* b, AvahiIfIndex interface,
                                  AvahiProtocol protocol, AvahiBrowserEvent event, const char* name,
                                  const char* type, const char* domain,
                                  AvahiLookupResultFlags flags, void* userdata);
void avahi_client_resolve_callback(AvahiServiceResolver* r, AvahiIfIndex /*interface*/,
                                   AvahiProtocol /*ipVersion*/, AvahiResolverEvent event,
                                   const char* name, const char* type, const char* domain,
                                   const char* hostName, const AvahiAddress* address, uint16_t port,
                                   AvahiStringList* txt, AvahiLookupResultFlags flags,
                                   void* userdata);

static char serviceName[] = "_image_histogram._tcp";

std::future<void> start_mdns_service() {
	AvahiClient* client{};

	if ((simplePoll = avahi_simple_poll_new()) == nullptr) {
		std::cerr << "Failed to create Avahi simple server poll object.\n";
		return {};
	}

	name = avahi_strdup("ImageHistogramServer");

	int error;
	client =
	    avahi_client_new(avahi_simple_poll_get(simplePoll), AvahiClientFlags::AVAHI_CLIENT_NO_FAIL,
	                     avahi_server_callback, nullptr, &error);

	if (client == nullptr) {
		std::cerr << "Failed to create Avahi client object: " << avahi_strerror(error) << ".\n";
		return {};
	}

	const auto runMdnsService = [client]() {
		avahi_simple_poll_loop(simplePoll);

		if (client != nullptr) {
			avahi_client_free(client);
		}

		if (simplePoll != nullptr) {
			avahi_simple_poll_free(simplePoll);
		}

		avahi_free(name);
	};

	return std::async(std::launch::async, runMdnsService);
}

void stop_mdns_service(std::future<void> mdnsService) {
	avahi_simple_poll_quit(simplePoll);

	mdnsService.wait();
}

void avahi_server_callback(AvahiClient* c, AvahiClientState state, void* /*unused*/) {
	assert(c);

	switch (state) {
		case AVAHI_CLIENT_S_RUNNING:
			return create_avahi_services(c);
		case AVAHI_CLIENT_FAILURE:
			std::cerr << "Avahi client failure: " << avahi_strerror(avahi_client_errno(c)) << "\n";
			avahi_simple_poll_quit(simplePoll);
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
			if (group != nullptr) {
				avahi_entry_group_reset(group);
			}
			break;
		case AVAHI_CLIENT_CONNECTING:;
	}
}

void create_avahi_services(AvahiClient* c) {
	if (group == nullptr) {
		if ((group = avahi_entry_group_new(c, avahi_entry_group_callback, nullptr)) == nullptr) {
			std::cerr << "Failed to create Avahi entry group: " << avahi_strerror(avahi_client_errno(c))
			          << "\n";
			return;
		}
	}

	int error = 0;

	if (avahi_entry_group_is_empty(group) != 0) {
		if ((error = avahi_entry_group_add_service(group, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC,
		                                           AvahiPublishFlags::AVAHI_PUBLISH_USE_MULTICAST, name,
		                                           serviceName, nullptr, nullptr, WORK_PORT,
		                                           nullptr)) != 0) {
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

void avahi_entry_group_callback(AvahiEntryGroup* g, AvahiEntryGroupState state, void* /*unused*/) {
	assert(g == group || !group);
	group = g;

	switch (state) {
		case AVAHI_ENTRY_GROUP_ESTABLISHED:
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
			avahi_simple_poll_quit(simplePoll);
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
		avahi_simple_poll_quit(simplePoll);
	}
}

void avahi_client_resolve_callback(AvahiServiceResolver* r, AvahiIfIndex interface,
                                   AvahiProtocol ipVersion, AvahiResolverEvent event,
                                   const char* name, const char* type, const char* domain,
                                   const char* hostName, const AvahiAddress* address, uint16_t port,
                                   AvahiStringList* txt, AvahiLookupResultFlags flags,
                                   void* userdata) {
	using namespace std::string_literals;

	assert(r);
	assert(userdata);

	auto* context = static_cast<MdnsContext*>(userdata);

	/* Called whenever a service has been resolved successfully or timed out */
	switch (event) {
		case AVAHI_RESOLVER_FAILURE:
			std::cerr << "(Resolver) Failed to resolve service '" << name << "' of type '" << type
			          << "' in domain '" << domain
			          << "': " << avahi_strerror(avahi_client_errno(avahi_service_resolver_get_client(r)))
			          << "\n";
			break;
		case AVAHI_RESOLVER_FOUND: {
			char a[AVAHI_ADDRESS_STR_MAX];
			char* t;
#if DEBUG_SERVICE_DISCOVERY
			std::clog << "Service '" << name << "' of type '" << type << "' in domain '" << domain
			          << " on IP version " << ip_version << "': "
			          << "\n";
#endif
			avahi_address_snprint(a, sizeof(a), address);
			t = avahi_string_list_to_string(txt);
#if DEBUG_SERVICE_DISCOVERY
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
#endif
			avahi_free(t);

			if (ipVersion == AVAHI_PROTO_INET6 &&
			    (IN6_IS_ADDR_LINKLOCAL(address->data.data) || IN6_IS_ADDR_LOOPBACK(address->data.data))) {
				// Specify interface if needing to connect to a remote on a link-local IPv6 address
				context->worker.add_server(name, a + "%"s + std::to_string(interface), port);
			} else {
				context->worker.add_server(name, a, port);
			}
			avahi_simple_poll_quit(simplePoll);
		}
	}
	avahi_service_resolver_free(r);
}

void avahi_client_browse_callback(AvahiServiceBrowser* b, AvahiIfIndex interface,
                                  AvahiProtocol protocol, AvahiBrowserEvent event, const char* name,
                                  const char* type, const char* domain,
                                  AvahiLookupResultFlags flags, void* userdata) {
	assert(b);
	assert(userdata);
	auto* context = static_cast<MdnsContext*>(userdata);

	/* Called whenever a new services becomes available on the LAN or is removed from the LAN */
	switch (event) {
		case AVAHI_BROWSER_FAILURE:
			std::cerr << "(Browser) "
			          << avahi_strerror(avahi_client_errno(avahi_service_browser_get_client(b))) << "\n";
			avahi_simple_poll_quit(simplePoll);
			return;
		case AVAHI_BROWSER_NEW:
#if DEBUG_SERVICE_DISCOVERY
			std::clog << "(Browser) NEW: service '" << name << "' of type '" << type << "' in domain '"
			          << domain << "'\n";
#endif
			/* We ignore the returned resolver object. In the callback
			   function we free it. If the server is terminated before
			   the callback function is called the server will free
			   the resolver for us. */
			if ((avahi_service_resolver_new(context->client, interface, protocol, name, type, domain,
			                                AVAHI_PROTO_UNSPEC, static_cast<AvahiLookupFlags>(0),
			                                avahi_client_resolve_callback, context)) == nullptr) {
				std::clog << "Failed to resolve service '" << name
				          << "': " << avahi_strerror(avahi_client_errno(context->client)) << "\n";
			}
			break;
		case AVAHI_BROWSER_REMOVE:
#if DEBUG_SERVICE_DISCOVERY
			std::clog << "(Browser) REMOVE: service '" << name << "' of type '" << type << "' in domain '"
			          << domain << "'\n";
#endif
			break;
			[[fallthrough]];
		case AVAHI_BROWSER_ALL_FOR_NOW:
		case AVAHI_BROWSER_CACHE_EXHAUSTED:
#if DEBUG_SERVICE_DISCOVERY
			const auto event_description =
			    ((event == AVAHI_BROWSER_CACHE_EXHAUSTED) ? "CACHE_EXHAUSTED" : "ALL_FOR_NOW");
			std::clog << "(Browser) " << event_description << "\n";
#endif
			break;
	}
}

void mdns_find_server(Worker& worker) {
	AvahiClient* client{};
	AvahiServiceBrowser* serviceBrowser{};

	if ((simplePoll = avahi_simple_poll_new()) == nullptr) {
		std::cerr << "Failed to create Avahi simple client poll object.\n";
		return;
	}

	name = avahi_strdup("ImageHistogramClient");

	int error;
	client =
	    avahi_client_new(avahi_simple_poll_get(simplePoll), AvahiClientFlags::AVAHI_CLIENT_NO_FAIL,
	                     avahi_client_callback, nullptr, &error);

	if (client == nullptr) {
		std::cerr << "Failed to create Avahi client object: " << avahi_strerror(error) << ".\n";
		return;
	}

	MdnsContext context{
		client,
		worker,
	};

	if ((serviceBrowser = avahi_service_browser_new(
	         client, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, serviceName, nullptr,
	         static_cast<AvahiLookupFlags>(0), avahi_client_browse_callback, &context)) == nullptr) {
		std::cerr << "Failed to create service browser: " << avahi_strerror(avahi_client_errno(client))
		          << "\n";
		return;
	}

	avahi_simple_poll_loop(simplePoll);

	if (client != nullptr) {
		avahi_client_free(client);
	}

	if (simplePoll != nullptr) {
		avahi_simple_poll_free(simplePoll);
	}

	avahi_free(name);
}
