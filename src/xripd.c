#include "xripd.h"
#include "xripd-out.h"
#include "rib.h"
#include "route.h"

// Given an interface name string, find and set our interface number (as indexed by the kernel).
// Populate our xripd_settings_t struct with this index value
static int get_iface_index(xripd_settings_t *xripd_settings, struct ifreq *ifrq) {

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
static int init_socket(xripd_settings_t *xripd_settings) {

	// Interface Request:
	struct ifreq ifrq;

	// Socket address vars:
	uint16_t bind_port = RIP_UDP_PORT;
	struct sockaddr_in bind_address;

	// Multicast Membership Request:
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
	bind_address.sin_addr.s_addr = inet_addr(RIP_MCAST_IP);
	bind_address.sin_family = AF_INET;
	bind_address.sin_port = htons(bind_port);

	if ( bind(xripd_settings->sd, (const struct sockaddr *)(&bind_address), sizeof(bind_address)) == -1 ){
		close(xripd_settings->sd);
		fprintf(stderr, "[daemon]: Error, Unable to bind the RIPv2 MCAST IP + UDP Port to socket\n");
		return 1;
	}

	printf("[daemon]: Successfully Bound to IP: %s\n", inet_ntoa(bind_address.sin_addr));

	// Populate our mcast group ips:
	mcast_group.imr_multiaddr.s_addr = inet_addr(RIP_MCAST_IP);
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
static xripd_settings_t *init_xripd_settings() {

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

	// Set Timers:
	xripd_settings->rip_timers.route_update = RIP_TIMER_UPDATE_DEFAULT;
	xripd_settings->rip_timers.route_invalid = RIP_TIMER_INVALID_DEFAULT;
	xripd_settings->rip_timers.route_holddown = RIP_TIMER_HOLDDOWN_DEFAULT; // NOT YET IMPLEMENTED
	xripd_settings->rip_timers.route_flush = RIP_TIMER_FLUSH_DEFAULT;
	
	xripd_settings->xripd_rib = NULL;
	xripd_settings->filter_mode = XRIPD_FILTER_MODE_NULL; // Default Value

	return xripd_settings;
}

// Encapsulate the raw rip_msg_entry_t from the datagram into a
// rib_entry_t, and send to the rib process via the p_rib_in anon pipe:
static int send_to_rib(xripd_settings_t *xripd_settings, rip_msg_entry_t *rip_entry, struct sockaddr_in recv_from) {

	// Create our rib_entry:
	rib_entry_t entry;
	memcpy(&(entry.recv_from), &recv_from, sizeof(struct sockaddr_in));
	memcpy(&(entry.rip_msg_entry), rip_entry, sizeof(rip_msg_entry_t));

	// Set our current time:
	entry.recv_time = time(NULL);

	// Remotely learnt route:
	entry.origin = RIB_ORIGIN_REMOTE;

#if XRIPD_DEBUG == 1
	fprintf(stderr, "[daemon]:\t\tSending RIP Entry to RIB\n");
#endif
	// Send through our anon pipe to the rib process:
	write(xripd_settings->p_rib_in[1], &entry, sizeof(rib_entry_t));
#if XRIPD_DEBUG == 1
	fprintf(stderr, "[daemon]:\t\tSent RIP Entry\n");
#endif
	return 0;
}

// Listen on our DGRAM socket, and parse messages recieved:
static int xripd_listen_loop(xripd_settings_t *xripd_settings) {

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

// Print usage and pass exit status on:
static void print_usage(int ret) {

	fprintf(stderr, "usage: xripd [-h] [-bw <filename>] [-p] -i <interface>\n");

	fprintf(stderr, "params:\n");
       	fprintf(stderr, "\t-i <interface>\t Bind RIP daemon to network interface\n");
       	fprintf(stderr, "\t-b\t\t Read Blacklist from <filename>\n");
       	fprintf(stderr, "\t-w\t\t Read Whielist from <filename>\n");
       	fprintf(stderr, "\t-p\t\t Enable Passive Mode (Don't generate RIPv2 Messages onto the network)\n");
       	fprintf(stderr, "\t-h\t\t Display this help message\n");
	fprintf(stderr, "filter:\n");
       	fprintf(stderr, "\t - filter file may contain zero or more routes to be white/blacklisted from the RIB\n");
       	fprintf(stderr, "\t - 1 route per line in file, in the format of x.x.x.x y.y.y.y\n");
	fprintf(stderr, "\n");
	exit(ret);
}

// Function to parse command line arguments
static int parse_args(xripd_settings_t *xripd_settings, int *argc, char **argv) {

	int option_index = 0;
	int index_count = 0;

	while ((option_index = getopt(*argc, argv, "i:b:w:hp")) != -1) {
		switch(option_index) {
			case 'i':
				strcpy(xripd_settings->iface_name, optarg);
				break;
			case 'b':
				xripd_settings->filter_mode = XRIPD_FILTER_MODE_BLACKLIST;
				strcpy(xripd_settings->filter_file, optarg);
				break;
			case 'w':
				xripd_settings->filter_mode = XRIPD_FILTER_MODE_WHITELIST;
				strcpy(xripd_settings->filter_file, optarg);
				break;
			case 'p':
				xripd_settings->passive_mode = XRIPD_PASSIVE_MODE_ENABLE;
				break;
			case 'h':
				print_usage(0);
			default:
				print_usage(0);
		}
		index_count++;
	}

	// No arguments were given, print usage to help the user
	// and bomb out:
	if (index_count == 0) {
		print_usage(1);

	}

	return 0;
}

int destroy_xripd_settings(xripd_settings_t *xripd_settings) {

	if ( xripd_settings != NULL ) {
		if ( xripd_settings->xripd_rib != NULL ) {
			destroy_rib(xripd_settings);
		}

		close(xripd_settings->p_rib_in[0]);
		close(xripd_settings->p_rib_in[1]);

		free(xripd_settings);
	}
	return 0;
}

// Shut our parent down:
static void shutdown_process(xripd_settings_t *xripd_settings, int ret) {
	destroy_xripd_settings(xripd_settings);
	exit(ret);
}

int main(int argc, char **argv) {


	// Init our settings:
	xripd_settings_t *xripd_settings = init_xripd_settings();
	if ( xripd_settings == NULL ) {
		shutdown_process(xripd_settings, 1);
	}

	if ( parse_args(xripd_settings, &argc, argv) != 0 ) {
		shutdown_process(xripd_settings, 1);
	}

	// Create pipe, used for sending rip message entries from the
	// listening daemon to the rib process:
	if (pipe(xripd_settings->p_rib_in) == -1) {
		fprintf(stderr, "[daemon]: Unable to create rib_in pipe\n");
		shutdown_process(xripd_settings, 1);
	}

	// Init our RIB with a specific datastore:
	if ( init_rib(xripd_settings, XRIPD_RIB_DATASTORE_LINKEDLIST) != 0) {
		shutdown_process(xripd_settings, 1);
	}

	// Fork:
	pid_t rib_f = fork();

	// Parent (xripd listener):
	if (rib_f > 0) {

		char proc_name[] = "xripd-daemon";
		strncpy(argv[0], proc_name, sizeof(proc_name));

		// Close reading end of rib_in pipe:
		close(xripd_settings->p_rib_in[0]);
		
		// Our listening socket for inbound RIPv2 packets:
		if ( init_socket(xripd_settings) != 0) {
			kill(rib_f, SIGINT);
			shutdown_process(xripd_settings, 1);
		}

		// Spawn our secondary thread that:
		// 	+ Listens on an Abstract Unix Domain Socket 
		// 	+ Is responsible for sending RIPv2 Messages on the wire
		if ( xripd_settings->passive_mode == XRIPD_PASSIVE_MODE_DISABLE ) {
			pthread_t xripd_out_thread;
			pthread_create(&xripd_out_thread, NULL, &xripd_out_spawn, (void *)xripd_settings);
		} else {
#if XRIPD_DEBUG == 1
			fprintf(stderr, "[daemon]: Passive Mode Enabled. No advertisements will be made onto the network.\n");
#endif
		}

		// Main Listening Loop
		xripd_listen_loop(xripd_settings);

		// SHOULD NEVER REACH:
		// xripd_listen_loop, should never return, but if it does:
		kill(rib_f, SIGINT);
		shutdown_process(xripd_settings, 1);

	// Child (xripd rib):
	} else if (rib_f == 0) {
#if XRIPD_DEBUG == 1
		fprintf(stderr, "[rib]: RIB Process Started\n");
#endif

		char proc_name[] = "xripd-rib";
		strncpy(argv[0], proc_name, sizeof(proc_name));

		// Close writing end of rib_in pipe:
		close(xripd_settings->p_rib_in[1]);

		if ( init_netlink(xripd_settings) != 0) {
			shutdown_process(xripd_settings, 1);
		}
		
		// Main loop for the RIB:
		rib_main_loop(xripd_settings);

		// SHOULD NEVER REACH:
		shutdown_process(xripd_settings, 1);

	// Failed to fork():
	} else if (rib_f < 0) {
		fprintf(stderr, "[daemon]: Failed to Fork RIB Process\n");
		shutdown_process(xripd_settings, 1);
	}

	return 1;
	destroy_xripd_settings(xripd_settings);
}
