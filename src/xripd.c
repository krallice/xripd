#include "xripd.h"
#include "rib.h"

// Given an interface name string, find our interface index #, and
// populate our xripd_settings_t struct with this index value:
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

// Create our AF_INET SOCK_DGRAM listening socket:
// (aka listen for UDP 520 inbound to the mcast rip ip):
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
		fprintf(stderr, "[daemon]: Error, Unable to open Datagram AF_INET socket.\n");
		return 1;
	}

	// Xlate iface name to ifindex:
	if ( get_iface_index(xripd_settings, &ifrq) != 0 ) {
		close(xripd_settings->sd);
		fprintf(stderr, "[daemon]: Error, Unable to identify interface by given string %s\n", xripd_settings->iface_name);
		return 1;
	}

	// Set SO_REUSEADDR onto the mcast socket:
	int reuse = 1;
	if ( setsockopt(xripd_settings->sd, SOL_SOCKET, SO_REUSEADDR, (char *) &reuse, sizeof(reuse)) == -1 ){
		close(xripd_settings->sd);
		fprintf(stderr, "[daemon]: Error, Unable to set SO_REUSEADDR on socket\n");
		return 1;
	}

	// Acquire IP Address:
	if ( ioctl(xripd_settings->sd, SIOCGIFADDR, &ifrq)  == -1 ){
		close(xripd_settings->sd);
		fprintf(stderr, "[daemon]: Error, Unable to get IP Address of %s\n", xripd_settings->iface_name);
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
		fprintf(stderr, "[daemon]: Error, Unable to bind the RIPv2 MCAST IP + UDP Port to socket\n");
		return 1;
	}

	printf("[daemon]: Successfully Bound to IP: %s\n", inet_ntoa(bind_address.sin_addr));

	// Populate our mcast group ips:
	mcast_group.imr_multiaddr.s_addr = inet_addr("224.0.0.9");
	mcast_group.imr_interface.s_addr = ((struct sockaddr_in *)&ifrq.ifr_addr)->sin_addr.s_addr;

	// Add mcast membership for our RIPv2 MCAST group:
	int ret = setsockopt(xripd_settings->sd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *) &mcast_group, sizeof(mcast_group));
	if ( ret == -1 ) {
		perror("setsockopt");
		close(xripd_settings->sd);
		fprintf(stderr, "[daemon]: Error, Unable to set socket option IP_ADD_MEMBERSHIP on socket\n");
		return 1;
	}

	printf("[daemon]: Successfully Added Membership to MCAST IP: %s\n", inet_ntoa(mcast_group.imr_multiaddr));

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
		fprintf(stderr, "[daemon] Error, Interface string %s is too long, exceeding IFNAMSIZ\n", xripd_settings->iface_name);
		return NULL;
	}	

	return xripd_settings;
}

int send_to_rib(xripd_settings_t *xripd_settings, rip_msg_entry_t *rip_entry, struct sockaddr_in recv_from) {

	rib_entry_t entry;
	memcpy(&(entry.recv_from), &recv_from, sizeof(struct sockaddr_in));
	memcpy(&(entry.rip_entry), rip_entry, sizeof(rip_msg_entry_t));
#if XRIPD_DEBUG == 1
	fprintf(stderr, "[daemon]:\t\tSending RIP Entry to RIB\n");
#endif
	write(xripd_settings->p_rib_in[1], &entry, sizeof(rib_entry_t));
#if XRIPD_DEBUG == 1
	fprintf(stderr, "[daemon]:\t\tSent RIP Entry\n");
#endif
	return 0;
}

// Listen on our DGRAM socket, and parse messages recieved:
int xripd_listen_loop(xripd_settings_t *xripd_settings) {

	int len = 0;
	char receive_buffer[RIP_DATAGRAM_SIZE];
	memset(&receive_buffer, 0, RIP_DATAGRAM_SIZE);

	struct sockaddr_in source_address;
	uint32_t source_address_len = sizeof(source_address);

	while(1) {
#if XRIPD_DEBUG == 1
		fprintf(stderr, "[daemon]: Listening ...\n");
		fflush(stderr);
#endif
		if ((len = recvfrom(xripd_settings->sd, receive_buffer, RIP_DATAGRAM_SIZE, 0, (struct sockaddr *)&source_address, &source_address_len)) == -1) {
			perror("recv");
		} else {

			rip_msg_header_t *msg_header = (rip_msg_header_t *)receive_buffer;

			char source_address_p[16];
			inet_ntop(AF_INET, &source_address.sin_addr, source_address_p, sizeof(source_address_p));


			if ( msg_header->version == RIP_SUPPORTED_VERSION ) {
				// RESPONSE is the only supported command at the moment:
				if ( msg_header->command == RIP_HEADER_RESPONSE ) {

					// Progressively scan through our buffer at interfaves of RIP_MESSAGE_SIZE
					int len_remaining = len - sizeof(rip_msg_header_t);
					int i = 0;
#if XRIPD_DEBUG == 1
					fprintf(stderr, "[daemon]: Received RIPv2 RESPONSE Message (Command: %02X) from %s Total Message Size: %d Entry(ies) Size: %d\n", msg_header->command, source_address_p, len, len_remaining);
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

						fprintf(stderr, "[daemon]:\tRIPv2 Entry AFI: %02X IP: %s %s Next-Hop: %s Metric: %02d\n", ntohs(rip_entry->afi), ipaddr, subnet, nexthop, ntohl(rip_entry->metric));
#endif

						if (send_to_rib(xripd_settings, rip_entry, source_address) != 0) {
#if XRIPD_DEBUG == 1
							fprintf(stderr, "[daemon]: Unable to add entry to RIP-RIB!\n");
#endif
						}
						i += RIP_ENTRY_SIZE;
					}
				} else {
#if XRIPD_DEBUG == 1
				fprintf(stderr, "[daemon]: Received unsupported RIP Command: %02X from %s\n", msg_header->command, source_address_p);
#endif
				}
			} else {
#if XRIPD_DEBUG == 1
				fprintf(stderr, "[daemon]: Received unsupported RIP Version: %02X Message from %s\n", msg_header->version, source_address_p);
#endif
			}
		}
	}
	return 0;
}

int main(void) {

	// Init our settings:
	xripd_settings_t *xripd_settings = init_xripd_settings();
	if ( xripd_settings == NULL ) {
		return 1;
	}

	// Create pipe and fork:
	if (pipe(xripd_settings->p_rib_in) == -1) {
		fprintf(stderr, "[daemon]: Unable to create rib_in pipe\n");
		return 1;
	}

	// Init our RIB with a specific datastore:
	if ( init_rib(xripd_settings, XRIPD_RIB_DATASTORE_LINKEDLIST) != 0)
		return 1;

	pid_t f = fork();

	// Parent (xripd listener):
	if (f > 0) {

		// Close reading end of rib_in pipe:
		close(xripd_settings->p_rib_in[0]);
		
		// Our listening socket for inbound RIPv2 packets:
		if ( init_socket(xripd_settings) != 0) {
			kill(f, SIGKILL);
			return 1;
		}

		// Main Listening Loop
		xripd_listen_loop(xripd_settings);

		kill(f, SIGKILL);
		return 1;

	// Child (xripd rib):
	} else if (f == 0) {
#if XRIPD_DEBUG == 1
		fprintf(stderr, "[rib]: RIB Process Started\n");
#endif
		// Close writing end of rib_in pipe:
		close(xripd_settings->p_rib_in[1]);

		rib_main_loop(xripd_settings);

		return 0;

	// Failed to fork():
	} else if (f < 0) {
		fprintf(stderr, "[daemon]: Failed to Fork RIB Process\n");
		return 1;
	}
}
