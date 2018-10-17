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

void dump_rtm_newroute(xripd_settings_t *xripd_settings, struct nlmsghdr *nlhdr) {

	struct rtmsg *route_entry;

	struct rtattr *route_attribute;
	int len = 0;

	uint8_t netmask = 0;

	// Presentation Strings for Destination/Gateways:
	char dst[32];
	char gw[32];

	route_entry = (struct rtmsg *)NLMSG_DATA(nlhdr);

	// Make sure we're only looking at the main table routes, no special cases:
	if (route_entry->rtm_table != RT_TABLE_MAIN) {
		return;
	}

	// Get our attribute that sits within the entry message:
	route_attribute = (struct rtattr *)RTM_RTA(route_entry);
	len = RTM_PAYLOAD(nlhdr);

	netmask = route_entry->rtm_dst_len;

	while ( RTA_OK(route_attribute, len) ) {

		switch (route_attribute->rta_type) {
			case RTA_DST:
				inet_ntop(AF_INET, RTA_DATA(route_attribute), dst, sizeof(dst));		
				break;
			case RTA_GATEWAY:
				inet_ntop(AF_INET, RTA_DATA(route_attribute), gw, sizeof(gw));		
				break;
		}

		route_attribute = RTA_NEXT(route_attribute, len);
	}

	fprintf(stderr, "[route]: %s/%d via %s.\n", dst, netmask, gw);
}


int netlink_read_local_routes(xripd_settings_t *xripd_settings, rib_entry_t *install_rib) {

	struct {
		struct nlmsghdr nl;
		struct rtgenmsg rt_gen;
		char buf[8192];
	} req;

	char buf[8192];

	struct sockaddr_nl kernel_address;
	
	// Sending datastructures:
	struct msghdr rtnl_msghdr;
	struct iovec io_vec;

	// Reply datastructures:
	struct msghdr rtnl_msghdr_reply;
	struct iovec io_vec_reply;

	struct nlmsghdr *msg_ptr;
	
	int end_parse = 0;

	memset(&kernel_address, 0, sizeof(kernel_address));
	memset(&rtnl_msghdr, 0, sizeof(rtnl_msghdr));
	memset(&io_vec, 0, sizeof(io_vec));

	memset(&req, 0, sizeof(req));

#if XRIPD_DEBUG == 1
	fprintf(stderr, "[route]: Sending NLM_F_DUMP request to kernel.\n");
#endif

	// Kernel's address for NETLINK sockets:
	kernel_address.nl_family = AF_NETLINK;
	kernel_address.nl_groups = 0;

	req.nl.nlmsg_len = NLMSG_LENGTH(sizeof(struct rtgenmsg));
	req.nl.nlmsg_type = RTM_GETROUTE;
	req.nl.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
	req.nl.nlmsg_seq = 1;
	req.nl.nlmsg_pid = getpid();
	req.rt_gen.rtgen_family = AF_INET;

	io_vec.iov_base = &req;
	io_vec.iov_len = req.nl.nlmsg_len;

	rtnl_msghdr.msg_iov = &io_vec;
	rtnl_msghdr.msg_iovlen = 1;
	rtnl_msghdr.msg_name = &kernel_address;
	rtnl_msghdr.msg_namelen = sizeof(kernel_address);

	sendmsg(xripd_settings->nlsd, (struct msghdr *) &rtnl_msghdr, 0);

	while(!end_parse) {

		int len;

		memset(&rtnl_msghdr_reply, 0, sizeof(rtnl_msghdr_reply));
		memset(&io_vec_reply, 0, sizeof(io_vec_reply));
	
		io_vec_reply.iov_base = buf;
		io_vec_reply.iov_len = sizeof(buf);

		rtnl_msghdr_reply.msg_iov = &io_vec_reply;
		rtnl_msghdr_reply.msg_iovlen = 1;
		rtnl_msghdr_reply.msg_name = &kernel_address;
		rtnl_msghdr_reply.msg_namelen = sizeof(kernel_address);

		len = recvmsg(xripd_settings->nlsd, &rtnl_msghdr_reply, 0);
		if (len) {
			for (msg_ptr = (struct nlmsghdr *) buf; NLMSG_OK(msg_ptr, len); msg_ptr = NLMSG_NEXT(msg_ptr, len)) {

				switch (msg_ptr->nlmsg_type) {

					case NLMSG_DONE:
						end_parse = 1;
						break;

					case RTM_NEWROUTE:
#if XRIPD_DEBUG == 1
						fprintf(stderr, "[route]: Route (Type: RTM_NEWROUTE / 24) received from kernel.\n");
						dump_rtm_newroute(xripd_settings, msg_ptr);
#endif
						break;

					default:
						break;
				}
			}
		} else {
#if XRIPD_DEBUG == 1
			fprintf(stderr, "[route]: No AF_NETLINK reply received. Indicates bad socket.\n");
#endif
			return 0;
		}
	}

	return 0;
}
