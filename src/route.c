#include "route.h"

// Function to create our netlink interface (used to install routes etc..):
int init_netlink(xripd_settings_t *xripd_settings) {

	// Spawn our NETLINK_ROUTE AF_NETLINK socket
	xripd_settings->nlsd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
	if ( xripd_settings->nlsd <= 0 ) {
		fprintf(stderr, "[route]: Error, Unable to open AF_NETLINK Socket..\n");
		close(xripd_settings->nlsd);
		return 1;
	} else {
#if XRIPD_DEBUG == 1
		fprintf(stderr, "[route]: AF_NETLINK Socket Opened.\n");
#endif
	}

// Success:
	return 0;
}

// Delete netlink socket:
int del_netlink(xripd_settings_t *xripd_settings) {
	close(xripd_settings->nlsd);
	xripd_settings->nlsd = 0;
	return 0;
}

// Translate the rib_entry_t struct to a rtmsg that can then be sent through the NETLINK socket
// to the kernel, to add/dete routes:
int xlate_rib_entry_to_rtmsg(rib_entry_t *entry, struct rtmsg *route) {

	route->rtm_family = AF_INET;
	route->rtm_table = RT_TABLE_MAIN; // Global Routing Table
	
	route->rtm_protocol = RTPROT_STATIC; // Static Route
	// Distance to destination, Universe = Global Route
	route->rtm_scope = RT_SCOPE_UNIVERSE;
	route->rtm_type = RTN_UNICAST; // Standard Unicast Route
	
	// Success
	return 0;
}

// Install new route into the table:
int netlink_install_new_route(xripd_settings_t *xripd_settings, rib_entry_t *install_rib) {

	// Netlink header and RouteMsg Structs:
	struct nlmsghdr nl_header;
	struct rtmsg new_route;
	memset(&nl_header, 0, sizeof(nl_header));
	memset(&new_route, 0, sizeof(new_route));

	// Start to format our header:
	// Datagram orientated REQUEST message, and please CREATE a new object:
	nl_header.nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE;
	// Message type is RTM_NEWROUTE:
	nl_header.nlmsg_type = RTM_NEWROUTE;
	// Set length of msg in header:
	nl_header.nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg));

	// Translate our RIB entry that needs to go into the routing table
	// into a struct rtmsg (&new_route):
	xlate_rib_entry_to_rtmsg(install_rib, &new_route);

	return 0;
}
