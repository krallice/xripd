#include "xripd.h"

// xripd Defines:
#define XRIPD_PASSIVE_IFACE "enp0s8"
#define XRIPD_DEBUG 0x01

// RIP Protocol Defines:
#define RIP_MCAST_IP 224.0.0.9
#define RIP_UDP_PORT 520

#define RIP_SUPPORTED_VERSION 2

#define RIP_DATAGRAM_SIZE 512
#define RIP_ENTRY_SIZE 20

// RIP Header Defines:
#define RIP_HEADER_REQUEST 1
#define RIP_HEADER_RESPONSE 2

// RIP Entry Defines:
#define RIP_AFI_INET 2

// Daemon Settings Structure:
typedef struct xripd_settings_t {
	int sd; 			// Socket Descriptor
	char iface_name[IFNAMSIZ]; 	// Human String for an interface, ie. "eth3" or "enp0s3"
	int iface_index; 		// Kernel index id for interface
} xripd_settings_t;

// RIP Message Format
// https://tools.ietf.org/html/rfc2453
// RIP Header:
typedef struct rip_msg_header_t {
	uint8_t command;
	uint8_t version;
	uint16_t zero;
} rip_msg_header_t;

// Each RIP Message may include 1-25 RIP Entries (RTEs):
typedef struct rip_msg_entry_t {
	uint16_t afi;
	uint16_t tag;
	uint32_t ipaddr;
	uint32_t subnet;
	uint32_t nexthop;
	uint32_t metric;
} rip_msg_entry_t;

int get_iface_index(xripd_settings_t *xripd_settings, struct ifreq *ifrq) {

	// Attempt to find interface index number of xripd_settings->iface_name
	// If successful, interface index in ifrq->ifr_ifindex:
	strcpy(ifrq->ifr_name, xripd_settings->iface_name);
	if ( ioctl(xripd_settings->sd, SIOCGIFINDEX, ifrq) == -1) {
		return 1;
	} else {
		xripd_settings->iface_index = ifrq->ifr_ifindex;
		return 0;
	}
}

int init_socket(xripd_settings_t *xripd_settings) {

	// Interface request:
	struct ifreq ifrq;

	// Socket address vars:
	uint16_t bind_port = RIP_UDP_PORT;
	struct sockaddr_in bind_address;

	struct ip_mreq mcast_group;

	// Initiate Socket:
	xripd_settings->sd = socket(AF_INET, SOCK_DGRAM, 0);
	if ( xripd_settings->sd <= 0 ) {
		close(xripd_settings->sd);
		fprintf(stderr, "Error: Unable to open Datagram AF_INET socket.\n");
		return 1;
	}

	// Xlate iface name to ifindex:
	if ( get_iface_index(xripd_settings, &ifrq) != 0 ) {
		close(xripd_settings->sd);
		fprintf(stderr, "Error: Unable to identify interface by given string %s\n", xripd_settings->iface_name);
		return 1;
	}

	// Set SO_REUSEADDR onto the mcast socket:
	int reuse = 1;
	if ( setsockopt(xripd_settings->sd, SOL_SOCKET, SO_REUSEADDR, (char *) &reuse, sizeof(reuse)) == -1 ){
		close(xripd_settings->sd);
		fprintf(stderr, "Error: Unable to set SO_REUSEADDR on socket\n");
		return 1;
	}

	// Acquire IP Address:
	if ( ioctl(xripd_settings->sd, SIOCGIFADDR, &ifrq)  == -1 ){
		close(xripd_settings->sd);
		fprintf(stderr, "Error: Unable to get IP Address of %s\n", xripd_settings->iface_name);
		return 1;
	}

	// Convert the presentation string for RIP_MCAST_IP into a network object:
	// Format our bind address struct:

	// bind_address.sin_addr.s_addr = ((struct sockaddr_in *)&ifrq.ifr_addr)->sin_addr.s_addr;
	bind_address.sin_addr.s_addr = inet_addr("224.0.0.9");
	bind_address.sin_family = AF_INET;
	bind_address.sin_port = htons(bind_port);

	if ( bind(xripd_settings->sd, (const struct sockaddr *)(&bind_address), sizeof(bind_address)) == -1 ){
		close(xripd_settings->sd);
		fprintf(stderr, "Error: Unable to bind the RIPv2 MCAST IP + UDP Port to socket\n");
		return 1;
	}

	printf("Bound IP: %s\n", inet_ntoa(bind_address.sin_addr));

	// Populate our mcast group ips:
	mcast_group.imr_multiaddr.s_addr = inet_addr("224.0.0.9");
	mcast_group.imr_interface.s_addr = ((struct sockaddr_in *)&ifrq.ifr_addr)->sin_addr.s_addr;

	// Add mcast membership for our RIPv2 MCAST group:
	int ret = setsockopt(xripd_settings->sd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *) &mcast_group, sizeof(mcast_group));
	if ( ret == -1 ) {
		perror("setsockopt");
		close(xripd_settings->sd);
		fprintf(stderr, "Error: Unable to set socket option IP_ADD_MEMBERSHIP on socket\n");
		return 1;
	}

	printf("Added Membership to MCAST IP: %s\n", inet_ntoa(mcast_group.imr_multiaddr));

	return 0;
}

// Initialise our xripd_settings_t struct:
xripd_settings_t *init_xripd_settings() {

	// Init and Zeroise:
	xripd_settings_t *xripd_settings = (xripd_settings_t*)malloc(sizeof(xripd_settings_t));
	memset(xripd_settings, 0, sizeof(xripd_settings_t));

	// Check interface string length, and copy if safe:
	if ( strlen(XRIPD_PASSIVE_IFACE) <= IFNAMSIZ ) {
		strcpy(xripd_settings->iface_name, XRIPD_PASSIVE_IFACE);
	} else {
		fprintf(stderr, "Error: Interface string %s is too long, exceeding IFNAMSIZ\n", xripd_settings->iface_name);
		return NULL;
	}	

	return xripd_settings;
}

int xripd_listen_loop(xripd_settings_t *xripd_settings) {

	int len = 0;
	char receive_buffer[RIP_DATAGRAM_SIZE];
	memset(&receive_buffer, 0, RIP_DATAGRAM_SIZE);

	struct sockaddr_in source_address;
	uint32_t source_address_len = sizeof(source_address);

	while(1) {
#if XRIPD_DEBUG == 1
		fprintf(stderr, "Starting Listen Loop\n");
		fflush(stderr);
#endif
		if ((len = recvfrom(xripd_settings->sd, receive_buffer, RIP_DATAGRAM_SIZE, 0, (struct sockaddr *)&source_address, &source_address_len)) == -1) {
			perror("recv");
		} else {

			rip_msg_header_t *msg_header = (rip_msg_header_t *)receive_buffer;

			if ( msg_header->version == RIP_SUPPORTED_VERSION ) {
				// RESPONSE is the only supported command at the moment:
				if ( msg_header->command == RIP_HEADER_RESPONSE ) {

					char source_address_p[16];
					inet_ntop(AF_INET, &source_address.sin_addr, source_address_p, sizeof(source_address_p));

					// Progressively scan through our buffer at interfaves of RIP_MESSAGE_SIZE
					int len_remaining = len - sizeof(rip_msg_header_t);
					int i = 0;
#if XRIPD_DEBUG == 1
					fprintf(stderr, "Received RIPv2 RESPONSE Message (Command: %02X) from %s Total Message Size: %d Entry(ies) Size: %d\n", msg_header->command, source_address_p, len, len_remaining);
#endif
					while (i <= (len_remaining - RIP_ENTRY_SIZE)) {
						rip_msg_entry_t *rip_entry = (rip_msg_entry_t *)(receive_buffer + sizeof(rip_msg_header_t) + i);
#if XRIPD_DEBUG == 1
						char ipaddr[16];
						char subnet[16];
						char nexthop[16];
						inet_ntop(AF_INET, &rip_entry->ipaddr, ipaddr, sizeof(ipaddr));
						inet_ntop(AF_INET, &rip_entry->subnet, subnet, sizeof(subnet));
						inet_ntop(AF_INET, &rip_entry->nexthop, nexthop, sizeof(nexthop));

						fprintf(stderr, "\tRIPv2 Entry AFI: %02X IP: %s %s Next-Hop: %s Metric: %02d\n", ntohs(rip_entry->afi), ipaddr, subnet, nexthop, ntohl(rip_entry->metric));
#endif
						i += RIP_ENTRY_SIZE;
					}
				} else {
#if XRIPD_DEBUG == 1
				fprintf(stderr, "Received unsupported RIP Command: %02X\n", msg_header->command);
#endif
				}
			} else {
#if XRIPD_DEBUG == 1
				fprintf(stderr, "Received unsupported RIP Version: %02X Message\n", msg_header->version);
#endif
			}
		}
	}
	return 0;
}

int main(void) {

	xripd_settings_t *xripd_settings = init_xripd_settings();
	if ( xripd_settings == NULL ) {
		return 1;
	}

	if ( init_socket(xripd_settings) != 0)
		return 1;

	xripd_listen_loop(xripd_settings);

	return 0;
}
