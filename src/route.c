#include "route.h"

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

	// Each Netlink datagram may contain 1+ route_attributes, followed by route data
	//
	//                       NLMSG_DATA                              NLMSG_NEXT
	//  * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	//  * |  netlink header |  rtmsg | rtattr | rtattr  | rtattr  |  netlink header |
	//  * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	struct rtattr *route_attribute;
	struct rtmsg *route_entry;
	int len = 0; // Attribute length

	// Presentation strings for dst_ip & gw_ip:
	char dst[32];
	char gw[32];
	uint8_t netmask = 0;

	// Init our presentation strings:
	memset(gw, 0, sizeof(gw));
	memset(dst, 0, sizeof(dst));

	// NLMSG_DATA()
	// Return a pointer to the payload associated with the passed
	// nlmsghdr.
	route_entry = (struct rtmsg *)NLMSG_DATA(nlhdr);

	// Make sure we're only looking at the main table routes, no special cases:
	if (route_entry->rtm_table != RT_TABLE_MAIN) {
		return;
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
				inet_ntop(AF_INET, RTA_DATA(route_attribute), dst, sizeof(dst));		
				break;
			case RTA_GATEWAY:
				inet_ntop(AF_INET, RTA_DATA(route_attribute), gw, sizeof(gw));		
				break;
		}

		// Iterate over out attributes:
		route_attribute = RTA_NEXT(route_attribute, len);
	}

	netmask = route_entry->rtm_dst_len;

	// Dump:
	fprintf(stderr, "[route]: Received route from kernel table RT_TABLE_MAIN: %s/%d via %s.\n", dst, netmask, gw);
}

int netlink_add_local_routes_to_rib(xripd_settings_t *xripd_settings) {

	struct {
		struct nlmsghdr nl;
		struct rtgenmsg rt_gen;
	} req;

	char buf[8192];

	// Netlink socket address for the kernel itself.
	// Referencing this struct in our msghdr struct allows our netlink request
	// to be delivered to kernel:
	struct sockaddr_nl kernel_address;
	
	// Used in sendmsg()
	// Our NLM_F_REQUEST will be contained in these structs:
	// Containers our rtnetlink messages:
	struct msghdr rtnl_msghdr;
	struct iovec io_vec;

	// Structs for our returned data from the kernel:
	struct msghdr rtnl_msghdr_reply;
	struct iovec io_vec_reply;

	// Pointer to netlink message header:
	struct nlmsghdr *msg_ptr;
	
	int end_parse = 0;

	// Zero out our sending datastructures for sendmsg():
	memset(&kernel_address, 0, sizeof(kernel_address));
	memset(&rtnl_msghdr, 0, sizeof(rtnl_msghdr));
	memset(&io_vec, 0, sizeof(io_vec));
	memset(&req, 0, sizeof(req));

#if XRIPD_DEBUG == 1
	fprintf(stderr, "[route]: Sending NLM_F_DUMP request to kernel.\n");
#endif
	// Kernel's address for NETLINK sockets
	// kernel_address.nl_pid = 0 is implicit:
	kernel_address.nl_family = AF_NETLINK;
	kernel_address.nl_groups = 0;

	// Our netlink header:
	/*
	 *  0		    8               16              24              32
	 *  1 2 3 4 5 6 7 8 1 2 3 4 5 6 7 8 1 2 3 4 5 6 7 8 1 2 3 4 5 6 7 8
	 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+ 
	 * |                     message length (32)			   |
	 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+ 
	 * |       type(16)                |            flags(16)          |
	 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+ 
	 * |                     sequence number (32)                      |
	 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+ 
	 * |                             pid (32)                          |
	 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+ 
	 */

	req.nl.nlmsg_len = NLMSG_LENGTH(sizeof(struct rtgenmsg)); // Header is going to carry a rtgenmsg
	req.nl.nlmsg_type = RTM_GETROUTE;
	req.nl.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP; // Dump the entire routing table
	req.nl.nlmsg_seq = 1;
	req.nl.nlmsg_pid = getpid(); // Our sending 'address'

	req.rt_gen.rtgen_family = AF_INET;

	// Pack our msgheader
	// Iovector that references our actual netlink data:
	io_vec.iov_base = &req;
	io_vec.iov_len = req.nl.nlmsg_len;

	// Reference our newly created iovector:
	rtnl_msghdr.msg_iovlen = 1; // Only one buffer to send
	rtnl_msghdr.msg_iov = &io_vec;

	// Destined for the kernel:
	rtnl_msghdr.msg_name = &kernel_address;
	rtnl_msghdr.msg_namelen = sizeof(kernel_address);

	sendmsg(xripd_settings->nlsd, (struct msghdr *) &rtnl_msghdr, 0);

	while(!end_parse) {

		int len;

		// Zeroise our reply structs:
		memset(&rtnl_msghdr_reply, 0, sizeof(rtnl_msghdr_reply));
		memset(&io_vec_reply, 0, sizeof(io_vec_reply));
	
		// Place our recieved data into buf:
		io_vec_reply.iov_base = buf;
		io_vec_reply.iov_len = sizeof(buf);

		rtnl_msghdr_reply.msg_iov = &io_vec_reply;
		rtnl_msghdr_reply.msg_iovlen = 1;

		// Receive from the kernel:
		rtnl_msghdr_reply.msg_name = &kernel_address;
		rtnl_msghdr_reply.msg_namelen = sizeof(kernel_address);

		// Give it to me:
		len = recvmsg(xripd_settings->nlsd, &rtnl_msghdr_reply, 0);

		// Got something?
		if (len) {

			msg_ptr = (struct nlmsghdr *) buf;

			// Is it safe to parse other netlink macros?
			while  (NLMSG_OK(msg_ptr, len) ) {

				switch (msg_ptr->nlmsg_type) {

					case NLMSG_DONE:
						end_parse = 1;
						break;

					// Kernel's sent us a route:
					case RTM_NEWROUTE:
#if XRIPD_DEBUG == 1
						dump_rtm_newroute(xripd_settings, msg_ptr);
#endif
						add_local_route_to_rib(xripd_settings, msg_ptr);
						break;

					default:
						break;
				}

				// Walk the chain of responses:
				// 1 Request (NLM_F_DUMP) in this instance, generates multiple responses
				// sizeof(buf) is quite large, so the sendmsg() call returns multiple messages:
				msg_ptr = NLMSG_NEXT(msg_ptr, len);
			}
		} else {
#if XRIPD_DEBUG == 1
			fprintf(stderr, "[route]: No AF_NETLINK reply received. Indicates bad socket.\n");
#endif
			return 1;
		}
	}

	return 0;
}
