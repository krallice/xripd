#include "rib.h"
#include "route.h"
#include "rib-ll.h"
#include "rib-null.h"

// Time to wait on reading the pipe from the daemon process, before proceeding with main loop:
#define RIB_SELECT_TIMEOUT 1
// Max amount of routes to read from the daemon process before proceeding with main loop:
#define RIB_MAX_READ_IN 15

// Init our xripd_rib_t structure.
// xripd_rib_t is an abstraction of function pointers which at init time
// are referenced to underlying implementations (called 'datastores'):
int init_rib(xripd_settings_t *xripd_settings, uint8_t rib_datastore) {

	// Init and Zeroise:
	xripd_rib_t *xripd_rib = (xripd_rib_t*)malloc(sizeof(*xripd_rib));
	memset(xripd_rib, 0, sizeof(*xripd_rib));

	// Init our filter:
	xripd_rib->filter = init_filter(XRIPD_FILTER_MODE_BLACKLIST);

	// Assign our datastore function pointers
	// (Implementation of our interface):
	xripd_rib->rib_datastore = rib_datastore;
	if ( rib_datastore == XRIPD_RIB_DATASTORE_NULL ) {

		xripd_rib->add_to_rib = &rib_null_add_to_rib;
		xripd_rib->dump_rib = &rib_null_dump_rib;
		xripd_rib->remove_expired_entries = &rib_null_remove_expired_entries;
		xripd_rib->invalidate_expired_local_routes = &rib_null_invalidate_expired_local_routes;
		xripd_settings->xripd_rib = xripd_rib;

		return 0;
	} else if ( rib_datastore == XRIPD_RIB_DATASTORE_LINKEDLIST ) {

		xripd_rib->add_to_rib = &rib_ll_add_to_rib;
		xripd_rib->dump_rib = &rib_ll_dump_rib;
		xripd_rib->remove_expired_entries = &rib_ll_remove_expired_entries;
		xripd_rib->invalidate_expired_local_routes = &rib_ll_invalidate_expired_local_routes;
		xripd_settings->xripd_rib = xripd_rib;

		// We can call a function on initialisation:
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

// Handler function:
// Recieves rib_entry_t as in_entry, and returns a add_rib_ret ret value depending on next action required re: kernel table:
//	Pass in_entry to RIB
//	Return value is add_rib_ret
//	Depending on value of add_rib_ret, 
//	Optional: ins_route will contain rib_entry_t for route to be installed into kernel's table
// 	Optional: del_route will contain rib_entry_t for route to be deleted from the kernel's table
void add_entry_to_rib(xripd_settings_t *xripd_settings, int *add_rib_ret, rib_entry_t *in_entry, rib_entry_t *ins_route, rib_entry_t *del_route) {

	// Pass argument pointers straight through to the add_to_rib function:
	(*xripd_settings->xripd_rib->add_to_rib)(add_rib_ret, in_entry, ins_route, del_route);

	// Switch on the RIB's return behaviour
	switch (*add_rib_ret) {

		case RIB_RET_NO_ACTION:
#if XRIPD_DEBUG == 1
			fprintf(stderr, "[rib]: add_to_rib result: NO_ACTION. Not installing route.\n");
#endif
			break;

		case RIB_RET_INSTALL_NEW:
#if XRIPD_DEBUG == 1
			fprintf(stderr, "[rib]: add_to_rib result: INSTALL_NEW. Installing new route.\n");
#endif
			// If the route was learnt via network/RIP, install into routing table:
			if ( ins_route->origin == RIB_ORIGIN_REMOTE ) {
				netlink_install_new_route(xripd_settings, ins_route);
			} else {
#if XRIPD_DEBUG == 1
				fprintf(stderr, "[rib]: Route origin not remote. No need to Netlink install.\n");
#endif
			}
			break;

		case RIB_RET_REPLACE:
#if XRIPD_DEBUG == 1
			fprintf(stderr, "[rib]: add_to_rib result: REPLACE. Replacing route with another.\n");
#endif
			// If the route was learnt remotely, let's blow it out of our kernel's table:
			if ( ins_route->origin == RIB_ORIGIN_REMOTE ) {
				netlink_replace_new_route(xripd_settings, ins_route);
			} else {
#if XRIPD_DEBUG == 1
				fprintf(stderr, "[rib]: Route origin not remote. No need to Netlink replace.\n");
#endif
			}
			break;

		case RIB_RET_INVALIDATE:
#if XRIPD_DEBUG == 1
			fprintf(stderr, "[rib]: add_to_rib result: INVALIDATE. Deleting route.\n");
#endif
			// If the route was learnt remotely, let's blow it out of our kernel's table:
			if ( del_route->origin == RIB_ORIGIN_REMOTE ) {
				netlink_delete_new_route(xripd_settings, del_route);
			} else {
#if XRIPD_DEBUG == 1
				fprintf(stderr, "[rib]: Route origin not remote. No need to Netlink delete.\n");
#endif
			}
			break;

		default:
#if XRIPD_DEBUG == 1
			fprintf(stderr, "[rib]: Undefined result from add_to_rib. Not installing route.\n");
#endif
			break;
	}

}

// Function called on each successive iteration of route returned from kernel via netlink.
// Parses the netlink message, converts to a rib_entry_t struct, and passes control to the add_entry_to_rib function (above)
int add_local_route_to_rib(xripd_settings_t *xripd_settings, struct nlmsghdr *nlhdr) {

	// Pointer to our rtmsg. Each rtmsg may contain multiple attributes:
	struct rtmsg *route_entry;
	struct rtattr *route_attribute;

	// Attribute length
	int len = 0;

	rib_entry_t in_entry;
	memset(&in_entry, 0, sizeof(rib_entry_t));

	// For our add_entry_to_rib call:
	int add_rib_ret = 0;
	rib_entry_t ins_route;
	rib_entry_t del_route;

	memset(&ins_route, 0, sizeof(ins_route));
	memset(&del_route, 0, sizeof(del_route));

	// NLMSG_DATA()
	// Return a pointer to the payload associated with the passed
	// nlmsghdr.
	route_entry = (struct rtmsg *)NLMSG_DATA(nlhdr);

	// Make sure we're only looking at the main table routes, no special cases:
	if (route_entry->rtm_table != RT_TABLE_MAIN) {
		return 1;
	}

	// Ignore routes that we've installed ourselves (stop circular route installation)
	if (route_entry->rtm_protocol == RTPROT_XRIPD ) {
		return 1;
	}

	// Get our attribute that sits within the entry message:
	// RTM_RTA(r), IFA_RTA(r), NDA_RTA(r), IFLA_RTA(r) and TCA_RTA(r):
	// return a pointer to the start of the attributes of the respective RTNETLINK operation given the header of the RTNETLINK message (r):
	route_attribute = (struct rtattr *)RTM_RTA(route_entry);

	// RTM_PAYLOAD(n), IFA_PAYLOAD(n), NDA_PAYLOAD(n), IFLA_PAYLOAD(n) and TCA_PAYLOAD(n):
	// return the total length of the attributes that follow the RTNETLINK operation header given the pointer to the NETLINK header (n).
	len = RTM_PAYLOAD(nlhdr);

        // RTA_OK(rta, attrlen) returns true if rta points to a valid routing
        // attribute; attrlen is the running length of the attribute buffer.
        // When not true then you must assume there are no more attributes in
        // the message, even if attrlen is nonzero.
	while ( RTA_OK(route_attribute, len) ) {

		switch (route_attribute->rta_type) {
			case RTA_DST:
				memcpy(&(in_entry.rip_msg_entry.ipaddr), RTA_DATA(route_attribute), sizeof(in_entry.rip_msg_entry.ipaddr));
				break;
			case RTA_GATEWAY:
				memcpy(&(in_entry.rip_msg_entry.nexthop), RTA_DATA(route_attribute), sizeof(in_entry.rip_msg_entry.nexthop));
				break;
		}

		// Iterate over out attributes:
		route_attribute = RTA_NEXT(route_attribute, len);
	}

	in_entry.rip_msg_entry.afi = AF_INET;
	in_entry.rip_msg_entry.metric = 0;
	in_entry.recv_from.sin_addr.s_addr = 0; 
	in_entry.recv_time = (*xripd_settings->xripd_rib).last_local_poll;
	in_entry.origin = RIB_ORIGIN_LOCAL;

	// Process netmask (Convert from dstlen cidr to an actual netmask:
	in_entry.rip_msg_entry.subnet = cidr_to_netmask_netorder(route_entry->rtm_dst_len);

	add_entry_to_rib(xripd_settings, &add_rib_ret, &in_entry, &ins_route, &del_route);
	return 0;
}

// Scan through our local routes, and find anything in our RIB that no longer matches 
// local routes. If that's the case, invalidate our routes in the RIB as required
void refresh_local_routes_into_rib(xripd_settings_t *xripd_settings) {

	// Set our last_local_poll_time to the current time:
	(*xripd_settings->xripd_rib).last_local_poll = time(NULL);

	// Poll the linux kernel using netlink to get ALL local routes
	// Attempt to install each of these into our RIB:
	if ( netlink_add_local_routes_to_rib(xripd_settings) == 0 ) {
#if XRIPD_DEBUG == 1
		fprintf(stderr, "[rib]: Added routes from Kernel to RIB.\n");
		(*xripd_settings->xripd_rib->dump_rib)();
#endif
	}

	// At this point, all kernel routes are in the RIB, however
	// the RIB may contain outdated local routes that are no longer in the local
	// kernel table anymore (local interfaces have been disabled or have failed)
	(*xripd_settings->xripd_rib->invalidate_expired_local_routes)((*xripd_settings->xripd_rib).last_local_poll);
	
	return;
}


// Post-fork() entry, our process enters into this function
// This is our main execution loop
void rib_main_loop(xripd_settings_t *xripd_settings) {

	// Our RIB struct that the daemon passes to us:
	rib_entry_t in_entry;
	memset(&in_entry, 0, sizeof(in_entry));

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

	// To start with, add local routes to our RIB:
#if XRIPD_DEBUG == 1
	fprintf(stderr, "[rib]: Adding Local Kernel Routes to RIB\n");
#endif
	(*xripd_settings->xripd_rib).last_local_poll = time(NULL);
	if ( netlink_add_local_routes_to_rib(xripd_settings) == 0 ) {
#if XRIPD_DEBUG == 1
		fprintf(stderr, "[rib]: Added routes from Kernel to RIB.\n");
		(*xripd_settings->xripd_rib->dump_rib)();
#endif
	}
#if XRIPD_DEBUG == 1
	fprintf(stderr, "[rib]: RIB Main Loop Started\n");
#endif
	// Main loop.
	// Start recieving routes from xripd-daemon picked up over the network:
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
				// Compare in_entry to existing rib & add/delete from kernel if required:
				add_entry_to_rib(xripd_settings, &add_rib_ret, &in_entry, &ins_route, &del_route);

			// Select Timeout triggered, break out of loop:
			} else {
				break;
			}
		}

		// Refresh RIB's view of local routes. Invalidate any routes that are no longer local in the RIB:
		refresh_local_routes_into_rib(xripd_settings);
		
		// Set Metric = 16 for routes that have exceeded their time to live
		// TODO: Actually implement a deletion function:
		(*xripd_settings->xripd_rib->remove_expired_entries)();

		// Dump our RIB only every 5th loop:
		if ( (dump_count % 5) == 0 ) {
			(*xripd_settings->xripd_rib->dump_rib)();
			dump_count = 1;
		} else {
			++dump_count;
		}
		entry_count = 0;
	}
}
