#include "rib.h"
#include "rib-ll.h"
#include "rib-null.h"

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
		xripd_settings->xripd_rib = xripd_rib;
		return 0;
	} else if ( rib_datastore == XRIPD_RIB_DATASTORE_LINKEDLIST ) {
		xripd_rib->add_to_rib = &rib_ll_add_to_rib;
		xripd_rib->dump_rib = &rib_ll_dump_rib;
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
	// Our RIB struct that gets populated by the pipe input:
	rib_entry_t in_entry;

	fd_set readfds; // Set of file descriptors (in our case, only one) for select() to watch for
	struct timeval timeout; // Time to wait for data in our select()ed socket
	int sret; // select() return value

	// Wipe our set of fds, and monitor our input pipe descriptor:
	FD_ZERO(&readfds);
	FD_SET(xripd_settings->p_rib_in[0], &readfds);

	// Timeout value 1s:
	timeout.tv_sec = 1;
	timeout.tv_usec = 0;

	while (1) {

		// Read struct sent from listening daemon over anon pipe:
		read(xripd_settings->p_rib_in[0], &in_entry, sizeof(rib_entry_t));

#if XRIPD_DEBUG == 1
		rib_route_print(&in_entry);
#endif
		(*xripd_settings->xripd_rib->add_to_rib)(&in_entry);
		(*xripd_settings->xripd_rib->dump_rib)();
	}

}
