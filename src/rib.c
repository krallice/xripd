#include "rib.h"
#include "route.h"
#include "rib-ll.h"
#include "rib-null.h"

#define RIB_SELECT_TIMEOUT 1
#define RIB_MAX_READ_IN 15

// Init our xripd_rib_t structure.
// xripd_rib_t is an abstraction of function pointers which at init time
// are referenced to underlying implementations (called 'datastores'):
int init_rib(xripd_settings_t *xripd_settings, uint8_t rib_datastore) {

	// Init and Zeroise:
	xripd_rib_t *xripd_rib = (xripd_rib_t*)malloc(sizeof(xripd_rib_t));
	memset(xripd_rib, 0, sizeof(xripd_rib_t));

	xripd_rib->rib_datastore = rib_datastore;

	if ( rib_datastore == XRIPD_RIB_DATASTORE_NULL ) {
		xripd_rib->add_to_rib = &rib_null_add_to_rib;
		xripd_rib->dump_rib = &rib_null_dump_rib;
		xripd_rib->remove_expired_entries = &rib_null_remove_expired_entries;
		xripd_settings->xripd_rib = xripd_rib;
		return 0;
	} else if ( rib_datastore == XRIPD_RIB_DATASTORE_LINKEDLIST ) {
		xripd_rib->add_to_rib = &rib_ll_add_to_rib;
		xripd_rib->dump_rib = &rib_ll_dump_rib;
		xripd_rib->remove_expired_entries = &rib_ll_remove_expired_entries;
		xripd_settings->xripd_rib = xripd_rib;
		rib_ll_init();
		return 0;
	}

	// Error Out:
	return 1;
}

// Debug function to print the route recieved via the rib process:
void rib_route_print(rib_entry_t *in_entry) {

	char ipaddr[16];
	char subnet[16];
	char nexthop[16];

	inet_ntop(AF_INET, &(in_entry->rip_msg_entry.ipaddr), ipaddr, sizeof(ipaddr));
	inet_ntop(AF_INET, &(in_entry->rip_msg_entry.subnet), subnet, sizeof(subnet));
	inet_ntop(AF_INET, &(in_entry->rip_msg_entry.nexthop), nexthop, sizeof(nexthop));
	fprintf(stderr, "[rib]: Route Received: IP: %s %s Next-Hop: %s Metric: %02d Timestamp %lld, Adding to RIB\n", 
			ipaddr, subnet, nexthop, ntohl(in_entry->rip_msg_entry.metric), (long long)in_entry->recv_time);

}

// Post-fork() entry, our process enters into this function
// This is our main execution loop
void rib_main_loop(xripd_settings_t *xripd_settings) {

#if XRIPD_DEBUG == 1
	fprintf(stderr, "[rib]: RIB Main Loop Started\n");
#endif
	// Our RIB struct that the daemon passes to us:
	rib_entry_t in_entry;

	// select() variables:
	fd_set readfds; // Set of file descriptors (in our case, only one) for select() to watch for
	struct timeval timeout; // Time to wait for data in our select()ed socket
	int sret; // select() return value

	// Amount of rip msg entries we can process before forcing execution to the timeout triggered path.
	// This is to prevent a DoS due to a datagram flood. These variables deal with rate limiting our select loop:
	int entry_count = 0;
	int dump_count = 1;

	// Variables to handle the add_to_rib function:
	int add_rib_ret = 0; // result of our add_to_rib function:
	rib_entry_t ins_route; // route to add to our kernel table (if any?)
	rib_entry_t del_route; // route to delete from our kernel table (if any?)

	// Main loop:
	while (1) {

		// Zeroise our return values for route table manipulations:
		add_rib_ret = RIB_RET_NO_ACTION;
		memset(&ins_route, 0, sizeof(ins_route));
		memset(&del_route, 0, sizeof(del_route));

		// Read up to RIB_MAX_READ_IN RIP Message Entries at a time:
		while ( entry_count < RIB_MAX_READ_IN ) {

			// Wipe our set of fds, and monitor our input pipe descriptor:
			FD_ZERO(&readfds);
			FD_SET(xripd_settings->p_rib_in[0], &readfds);

			// Timeout value; (how often to poll)
			timeout.tv_sec = RIB_SELECT_TIMEOUT;
			timeout.tv_usec = 0;

			// Wait up to a second for a msg entry to come in
			sret = select(xripd_settings->p_rib_in[0] + 1, &readfds, NULL, NULL, &timeout);

			// Error:
			if (sret < 0) {
				fprintf(stderr, "[rib]: Unable to select() on pipe.\n");
				return;

			// Pipe sd is ready to be read:
			} else if (sret) { 

				// Read struct sent from listening daemon over anon pipe:
				// This is a blocking function, not an issue as we have select()ed on the 
				// fdset containing the socket beforehand:
				read(xripd_settings->p_rib_in[0], &in_entry, sizeof(rib_entry_t));
				++entry_count;
#if XRIPD_DEBUG == 1
				rib_route_print(&in_entry);
#endif
				// Add our route recieved from the daemon to our rib.
				// Determine what action we then need to apply to our routing table:
				(*xripd_settings->xripd_rib->add_to_rib)(&add_rib_ret, &in_entry, &ins_route, &del_route);
				switch (add_rib_ret) {
					case RIB_RET_NO_ACTION:
#if XRIPD_DEBUG == 1
						fprintf(stderr, "[rib]: add_to_rib result: NO_ACTION. Not installing route.\n");
#endif
						break;
					case RIB_RET_INSTALL_NEW:
#if XRIPD_DEBUG == 1
						fprintf(stderr, "[rib]: add_to_rib result: INSTALL_NEW. Installing new route.\n");
#endif
						netlink_install_new_route(xripd_settings, &ins_route);
						break;

					// Not yet implemented:
					case RIB_RET_REPLACE:
#if XRIPD_DEBUG == 1
						fprintf(stderr, "[rib]: add_to_rib result: REPLACE. Replacing route with another.\n");
#endif
						break;
					case RIB_RET_DELETE:
#if XRIPD_DEBUG == 1
						fprintf(stderr, "[rib]: add_to_rib result: DELETE. Deleting route.\n");
#endif
						break;

					default:
#if XRIPD_DEBUG == 1
						fprintf(stderr, "[rib]: Undefined result from add_to_rib. Not installing route.\n");
#endif
						break;
				}

			// Select Timeout triggered, break out of loop:
			} else {
				break;
			}
		}

		(*xripd_settings->xripd_rib->remove_expired_entries)();

		if ( (dump_count % 5) == 0 ) {
			(*xripd_settings->xripd_rib->dump_rib)();
			dump_count = 1;
		} else {
			++dump_count;
		}

		entry_count = 0;

	}
}

// Copy RIB Entry from SRC to DST:
void copy_rib_entry(rib_entry_t *src, rib_entry_t *dst) {

	memcpy(src, dst, sizeof(rib_entry_t));
	return;
}
