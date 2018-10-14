#include "route.h"

typedef struct rtnetlink_request_t {
	struct nlmsghdr rtnetlink_header;
	struct rtmsg rt_msg;
	char buf[8192];
} rtnetlink_request_t;

// Function to create our netlink interface (used to install routes etc..):
int init_netlink(xripd_settings_t *xripd_settings) {

	struct sockaddr_nl netlink_address;

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

	// Bind to our socket:
	// Netlink uses the pid as the socket address:
	memset(&netlink_address, 0, sizeof(netlink_address));
	netlink_address.nl_family = AF_NETLINK;
	netlink_address.nl_pad = 0;
	//netlink_address.nl_pid = getpid();
	netlink_address.nl_pid = 0;
	netlink_address.nl_groups = 0;

	if (bind(xripd_settings->nlsd, (struct sockaddr*) &netlink_address, sizeof(netlink_address)) < 0 ) {
		fprintf(stderr, "[route]: Error, Unable to open AF_NETLINK Socket..\n");
		close(xripd_settings->nlsd);
		return 1;
	} else {
#if XRIPD_DEBUG == 1
		fprintf(stderr, "[route]: Bound to AF_NETLINK Socket using PID address %d\n", netlink_address.nl_pid);
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

	struct {
		struct nlmsghdr nl;
		struct rtmsg rt;
		char buf[8192];
	} req;

	struct sockaddr_nl addr;
	struct rtattr *attr;
	
	int seq = 0;
	int len = 0;
	
	// Message header (used in sendmsg):
	// struct msghdr msg_header;
	// struct iovec io_vector;

	// Wipe buffer:
	memset(&req, 0, sizeof(req));
	
	// struct for our send call
	memset(&addr, 0, sizeof(struct sockaddr_nl));
	addr.nl_family = AF_NETLINK;
	addr.nl_pid = 0; // Packets are destined for the kernel
	addr.nl_groups = 0; // MCAST not a requirement

	// Start to format our Netlink header:
	// Datagram orientated REQUEST message, and request to CREATE a new object:
	req.nl.nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg)); // Set length of msg in header:
	req.nl.nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE;
	req.nl.nlmsg_type = RTM_NEWROUTE; // Message type is RTM_NEWROUTE:
	req.nl.nlmsg_pid = getpid();
	req.nl.nlmsg_seq = seq++;

	// Translate our RIB entry that needs to go into the routing table
	req.rt.rtm_family = AF_INET;
	req.rt.rtm_table = RT_TABLE_MAIN; // Global Routing Table
	req.rt.rtm_protocol = RTPROT_STATIC; // Static Route
	req.rt.rtm_scope = RT_SCOPE_UNIVERSE; // Distance to destination, Universe = Global Route
	req.rt.rtm_type = RTN_UNICAST; // Standard Unicast Route
	req.rt.rtm_dst_len = 32;

	len = sizeof(struct rtmsg);

	// Route Attribute (Carrying IP Address):
	attr = (struct rtattr *) req.buf;
	attr->rta_type = RTA_DST;
	attr->rta_len = sizeof(struct rtattr) + 4;
	memcpy(((char *)attr) + sizeof(struct rtattr), &(install_rib->rip_msg_entry.ipaddr), 4);
	len += attr->rta_len;

	// Route Attribute (Carrying Interface Index):
	attr = (struct rtattr *) (((char *)attr) + attr->rta_len);
	attr->rta_type = RTA_OIF;
	attr->rta_len = sizeof(struct rtattr) + 4;
	memcpy(((char *)attr) + sizeof(struct rtattr), &(xripd_settings->iface_index), 4);
	len += attr->rta_len;

	sendto(xripd_settings->nlsd,(void *) &req, req.nl.nlmsg_len, 0, 
			(struct sockaddr *) &addr, sizeof(struct sockaddr_nl));

	// Translate our RIB entry that needs to go into the routing table
	// into a struct rtmsg (&new_route):
	//xlate_rib_entry_to_rtmsg(install_rib, &new_route);

	return 0;
}
