#include "route.h"

typedef struct req_t {
	struct nlmsghdr nl;
	struct rtmsg rt;
	char buf[1024];
} req_t;

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
	netlink_address.nl_pid = getpid();
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

// This is the utility function for adding the parameters to the packet. 
static int addattr_l(struct nlmsghdr *n, int maxlen, int type, void *data, int alen) { 

	int len = RTA_LENGTH(alen); 
	struct rtattr *rta; 

	if (NLMSG_ALIGN(n->nlmsg_len) + len > maxlen) 
		return -1; 
	
	rta = (struct rtattr*)(((char*)n) + NLMSG_ALIGN(n->nlmsg_len)); 
	rta->rta_type = type; 
	rta->rta_len = len; 

	memcpy(RTA_DATA(rta), data, alen); 
	n->nlmsg_len = NLMSG_ALIGN(n->nlmsg_len) + len; 

	return 0; 
}

// Given an input of a netmask (255.255.255.0), return a cidr value (/24):
int netmask_to_cidr(uint32_t netmask) {

	int cidr = 0;
	if (netmask) {
		while (netmask) {
			cidr++;
			netmask &= (netmask - 1);
		}
		return cidr;
	} else {
		return cidr;
	}
}

// Given a cidr (/24), convert to netmask (255.255.255.0):
uint32_t cidr_to_netmask_netorder(int cidr) {

	if (cidr) {
		return htonl(0xFFFFFFFF << (32 - cidr));
	} else {
		return 0;
	}
}

// Given a reference to req, prepare the netlink header
// nlmsg_type (ex. RTM_NEWROUTE) must be provided
static void prepare_req_nlhdr_rtm(req_t *req, int nlmsg_type, rib_entry_t *entry, int replace_flag) {

	// Format our Netlink header:
	// Datagram orientated REQUEST message, and request to CREATE a new object:
	req->nl.nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg)); // Set length of msg in header:

	// Replace or append:
	if ( replace_flag ) {
		fprintf(stderr, "REEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEPLACE\n");
		req->nl.nlmsg_flags = NLM_F_REQUEST | NLM_F_REPLACE;
	} else {
		req->nl.nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE;
	}
	
	// Assign our type:
	req->nl.nlmsg_type = nlmsg_type;

	// Route Msg Headers
	// Translate our RIB entry that needs to go into the routing table
	req->rt.rtm_family = AF_INET;
	req->rt.rtm_table = RT_TABLE_MAIN; // Global Routing Table
	req->rt.rtm_scope = RT_SCOPE_UNIVERSE; // Distance to destination, Universe = Global Route
	// Custom Route Protocol for XRIPD. Defined in route.h.
	// Used to differentiate locally learnt/added routes from routes added via our daemon
	req->rt.rtm_protocol = RTPROT_XRIPD; 	
	req->rt.rtm_type = RTN_UNICAST; // Standard Unicast Route

	// Required to convert netmask(which rip uses) to cidr(which netlink uses):
	req->rt.rtm_dst_len = netmask_to_cidr(entry->rip_msg_entry.subnet);

	return;
}

// Prepare the RTAs for a RTM_NEWROUTE message:
static void prepare_req_rtm_newroute_rtas(req_t *req, xripd_settings_t *xripd_settings, rib_entry_t *entry) {
	
	// Attribute Variables:
	int index = 0;
	uint8_t dst[4];
	uint8_t gw[4];

	// Format and copy attributes into our message:
	index = xripd_settings->iface_index;
	memcpy(dst, &(entry->rip_msg_entry.ipaddr), 4);
	memcpy(gw, &(entry->recv_from.sin_addr.s_addr), 4);

	addattr_l(&req->nl, sizeof(*req), RTA_OIF, &index, sizeof(index));
	addattr_l(&req->nl, sizeof(*req), RTA_DST, dst, 4);
	addattr_l(&req->nl, sizeof(*req), RTA_GATEWAY, gw, 4);
}

// Format and prepare msghdr (which is used in the sendmsg abi):
static void prepare_msghdr(struct msghdr *rtnl_msghdr, struct iovec *io_vec, struct req_t *req, struct sockaddr_nl *kernel_address) {

	// Address of our destination (the kernel):
	kernel_address->nl_family = AF_NETLINK;
	kernel_address->nl_pid = 0; // Destined for kernel
	kernel_address->nl_groups = 0; // MCAST not a requirement

	// Format for sendmsg:
	io_vec->iov_base = &(req->nl);
	io_vec->iov_len = req->nl.nlmsg_len;

	rtnl_msghdr->msg_name = kernel_address;
	rtnl_msghdr->msg_namelen = sizeof(*kernel_address);

	rtnl_msghdr->msg_iovlen = 1; // Only one buffer to send
	rtnl_msghdr->msg_iov = io_vec;

	return;
}

// Given a new route (install_rib), install this into the routing table:
int netlink_install_new_route(xripd_settings_t *xripd_settings, rib_entry_t *install_rib) {

	// rtmsg struct with netlink message header:
	req_t req;

	int len = 0;
	
	// Netlink socket address for the kernel:
	struct sockaddr_nl kernel_address;

	// Structs for sendmsg()
	// Netlink packets get packed/referenced into these:
	struct msghdr rtnl_msghdr;
	struct iovec io_vec;

	// Wipe buffer and associated structs:
	memset(&kernel_address, 0, sizeof(kernel_address));
	memset(&rtnl_msghdr, 0, sizeof(rtnl_msghdr));
	memset(&io_vec, 0, sizeof(io_vec));
	memset(&req, 0, sizeof(req));

	// Prepare the netlink header contained in req:
	prepare_req_nlhdr_rtm(&req, RTM_NEWROUTE, install_rib, 0);

	// Prepare our RTAs given install_rib:
	prepare_req_rtm_newroute_rtas(&req, xripd_settings, install_rib);

	// Prepare our msgheader used for sendmsg:
	prepare_msghdr(&rtnl_msghdr, &io_vec, &req, &kernel_address);

	// Send to the kernel
#if XRIPD_DEBUG == 1
	char ipaddr[32];
	char subnet[16];
	inet_ntop(AF_INET, &(install_rib->rip_msg_entry.ipaddr), ipaddr, sizeof(ipaddr));
	inet_ntop(AF_INET, &(install_rib->rip_msg_entry.subnet), subnet, sizeof(subnet));
	fprintf(stderr, "[route]: Sending NLM_F_CREATE Request to Kernel for %s %s.\n", ipaddr, subnet);
#endif
	len = sendmsg(xripd_settings->nlsd, &rtnl_msghdr, 0);
#if XRIPD_DEBUG == 1
	fprintf(stderr, "[route]: sendmsg len was %d.\n", len);
#endif
	return 0;
}

// Given a del_entry, delete route from kernel table:
int netlink_delete_new_route(xripd_settings_t *xripd_settings, rib_entry_t *del_entry) {

	// rtmsg struct with netlink message header:
	req_t req;

	int len = 0;
	
	// Netlink socket address for the kernel:
	struct sockaddr_nl kernel_address;

	// Structs for sendmsg()
	// Netlink packets get packed/referenced into these:
	struct msghdr rtnl_msghdr;
	struct iovec io_vec;

	// Wipe buffer and associated structs:
	memset(&kernel_address, 0, sizeof(kernel_address));
	memset(&rtnl_msghdr, 0, sizeof(rtnl_msghdr));
	memset(&io_vec, 0, sizeof(io_vec));
	memset(&req, 0, sizeof(req));

	// Prepare the netlink header contained in req:
	prepare_req_nlhdr_rtm(&req, RTM_DELROUTE, del_entry, 0);

	// Prepare our RTAs given del_entry:
	prepare_req_rtm_newroute_rtas(&req, xripd_settings, del_entry);

	// Prepare our msgheader used for sendmsg:
	prepare_msghdr(&rtnl_msghdr, &io_vec, &req, &kernel_address);

	// Send to the kernel
#if XRIPD_DEBUG == 1
	char ipaddr[32];
	char subnet[16];
	inet_ntop(AF_INET, &(del_entry->rip_msg_entry.ipaddr), ipaddr, sizeof(ipaddr));
	inet_ntop(AF_INET, &(del_entry->rip_msg_entry.subnet), subnet, sizeof(subnet));
	fprintf(stderr, "[route]: Sending NLM_F_CREATE Request to Kernel for %s %s.\n", ipaddr, subnet);
#endif
	len = sendmsg(xripd_settings->nlsd, &rtnl_msghdr, 0);
#if XRIPD_DEBUG == 1
	fprintf(stderr, "[route]: sendmsg len was %d.\n", len);
#endif
	return 0;
}

// Given a new route (install_rib), install this into the routing table:
int netlink_replace_new_route(xripd_settings_t *xripd_settings, rib_entry_t *install_rib) {

	// rtmsg struct with netlink message header:
	req_t req;

	int len = 0;
	
	// Netlink socket address for the kernel:
	struct sockaddr_nl kernel_address;

	// Structs for sendmsg()
	// Netlink packets get packed/referenced into these:
	struct msghdr rtnl_msghdr;
	struct iovec io_vec;

	// Wipe buffer and associated structs:
	memset(&kernel_address, 0, sizeof(kernel_address));
	memset(&rtnl_msghdr, 0, sizeof(rtnl_msghdr));
	memset(&io_vec, 0, sizeof(io_vec));
	memset(&req, 0, sizeof(req));

	// Prepare the netlink header contained in req:
	prepare_req_nlhdr_rtm(&req, RTM_NEWROUTE, install_rib, 1);

	// Prepare our RTAs given install_rib:
	prepare_req_rtm_newroute_rtas(&req, xripd_settings, install_rib);

	// Prepare our msgheader used for sendmsg:
	prepare_msghdr(&rtnl_msghdr, &io_vec, &req, &kernel_address);

	// Send to the kernel
#if XRIPD_DEBUG == 1
	char ipaddr[32];
	char subnet[16];
	inet_ntop(AF_INET, &(install_rib->rip_msg_entry.ipaddr), ipaddr, sizeof(ipaddr));
	inet_ntop(AF_INET, &(install_rib->rip_msg_entry.subnet), subnet, sizeof(subnet));
	fprintf(stderr, "[route]: Sending NLM_F_CREATE Request to Kernel for %s %s.\n", ipaddr, subnet);
#endif
	len = sendmsg(xripd_settings->nlsd, &rtnl_msghdr, 0);
#if XRIPD_DEBUG == 1
	fprintf(stderr, "[route]: sendmsg len was %d.\n", len);
#endif
	return 0;
}

static void dump_rtm_newroute(xripd_settings_t *xripd_settings, struct nlmsghdr *nlhdr) {

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

// Dump our entire local routing table, and
// for each route that we discover, attempt to add to our local routing table (by calling add_local_route_to_rib):
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
