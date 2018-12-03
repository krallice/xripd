#include "rib-out.h"

// Fixed place in memory to hold a datagram:
static uint8_t rip_update_datagram[RIP_DATAGRAM_SIZE];

// Fixed place in memory to hold a copy of our standard v2 response header:
static struct rip_msg_header_t ripv2_update_header = {
	.version = RIP_SUPPORTED_VERSION,
	.command = RIP_HEADER_RESPONSE,
	.zero = 0
}; 

// Init our statically allocated global variable (rip_update_datagram):
static void init_update_datagram(void) {

	// Zeroise our statically allocated datagram:
	memset(&rip_update_datagram, 0, sizeof(rip_update_datagram));

	// Copy the update header to the header of the packet:
	memcpy(&rip_update_datagram, &ripv2_update_header, sizeof(ripv2_update_header));

	return;
}

// Given a sun_addresses_t struct, Populate our daemon and rib addresses, bind to the daemon address
// for an Abtract Unix Domain Socket
static int init_abstract_unix_socket(sun_addresses_t *s) {

	// Unix Domain Socket Addresses:
	s->sockaddr_un_rib.sun_family = AF_UNIX;
	strcpy(s->sockaddr_un_rib.sun_path, "#xripd-rib");
	s->sockaddr_un_rib.sun_path[0] = 0;

	s->sockaddr_un_daemon.sun_family = AF_UNIX;
	strcpy(s->sockaddr_un_daemon.sun_path, "#xripd-daemon");
	s->sockaddr_un_daemon.sun_path[0] = 0;

	// Spawn a Socket:
	s->socketfd = socket(AF_UNIX, SOCK_DGRAM, 0);
#if XRIPD_DEBUG == 1
	fprintf(stderr, "[xripd-out]: Spawning UNIX Domain Socket.\n");
#endif
	if ( s->socketfd == 0 ) {
		goto failed_socket_init;
	}

	// Bind to the socket:
#if XRIPD_DEBUG == 1
	fprintf(stderr, "[xripd-out]: Binding to Abstract UNIX Domain Socket: \\0xripd-daemon.\n");
#endif
	if ( bind(s->socketfd, (struct sockaddr *) &(s->sockaddr_un_daemon), sizeof(struct sockaddr_un)) < 0 ) {
		goto failed_bind;
	}

	// Successful exit:
#if XRIPD_DEBUG == 1
	fprintf(stderr, "[xripd-out]: Successfully bound to Abstract UNIX Domain Socket: \\0xripd-daemon.\n");
#endif
	return 0;

failed_bind:
	close(s->socketfd);
	
failed_socket_init:
	return 1;
}

// Send datagram onto the network:
static int fire_ripv2_update_datagram(const xripd_settings_t *xripd_settings, const int n) {

	int ret = 0;

	// Calculate size of datagram
	// Header + n * message entries:
	int size = (sizeof(rip_msg_header_t)) + (n * sizeof(rip_msg_entry_t));

	// Declare destination address:
	struct sockaddr_in dest;

	// Format address:
	memset(&dest, 0, sizeof(dest));
	dest.sin_family = AF_INET;
	dest.sin_addr.s_addr = inet_addr(RIP_MCAST_IP);
	dest.sin_port = htons(RIP_UDP_PORT);

	ret = sendto(xripd_settings->sd, rip_update_datagram, size, 0, (struct sockaddr *) &dest, sizeof(dest));
#if XRIPD_DEBUG == 1
	fprintf(stderr, "[xripd-out]: Sent RIPv2 UPDATE Message to %s (%d bytes).\n", RIP_MCAST_IP, ret);
#endif

	// Reset our init datagram:
	init_update_datagram();
	return 0;
}

// Increment the metric of the route before advertising over the network:
static void increment_rip_msg_entry_metric(rib_entry_t *r) { 

	uint32_t m = ntohl(r->rip_msg_entry.metric);
	if ( m < RIP_METRIC_INFINITY ) {
		m++;
	} else {
		m = RIP_METRIC_INFINITY;
	}
	r->rip_msg_entry.metric = htonl(m);
}

// Given an input of a rib entry, and an entry_num, place the entry into a rip update datagram
// Use the memory space allocated out of the static/global area of the exe
// Place up to XRIPD_ENTRIES_PER_UPDATE entries into a single datagram
// If force_send is set, fire off the datagram immediately
static int format_ripv2_update_datagram(const xripd_settings_t *xripd_settings, const int entry_num, rib_entry_t *rib_entry, int force_send) {

	// Offset for packing rip_msg_entry_t's into the rip datagram:
	int offset = 0;
	if ( entry_num > 0 ) {
		offset = (entry_num - 1) % XRIPD_ENTRIES_PER_UPDATE;
	}
#if XRIPD_DEBUG == 1
	fprintf(stderr, "[xripd-out]: Packing RIP Message #%d into Datagram Position %d/%d\n", entry_num, offset + 1, XRIPD_ENTRIES_PER_UPDATE);
#endif
	// If the caller has *NOT* told us to force send the datagram:
	if ( force_send == 0 ) {

		// Increment metric safely:
		increment_rip_msg_entry_metric(rib_entry);

		// Pointer magic
		// Copy rib_entry into the appropriate space within the datagram
		uint8_t *update = rip_update_datagram;
		memcpy((update + ((sizeof(rip_msg_header_t) + (offset * sizeof(rip_msg_entry_t))))), 
				&(rib_entry->rip_msg_entry), sizeof(rip_msg_entry_t));

		// Pack up to XRIPD_ENTRIES_PER_UPDATE into a single datagram, if we have hit the limit, fire the packet
		// onto the network:
		if ( (entry_num != 0 ) && (entry_num % XRIPD_ENTRIES_PER_UPDATE == 0) ) {
#if XRIPD_DEBUG == 1
			fprintf(stderr, "[xripd-out]: Datagram full of entries (%d/%d). Preparing to send RIPv2 UPDATE Message.\n", offset + 1, XRIPD_ENTRIES_PER_UPDATE);
#endif
			fire_ripv2_update_datagram(xripd_settings, offset + 1);
		}

	// Caller has asked us to sent the datagram onto the network regardless, send it:
	} else if ( force_send == 1 ) {

		if ( entry_num != 1 ) {
#if XRIPD_DEBUG == 1
			fprintf(stderr, "[xripd-out]: Datagram not full (%d/%d). Force sending RIPv2 UPDATE Message.\n", offset + 1, XRIPD_ENTRIES_PER_UPDATE);
#endif
			fire_ripv2_update_datagram(xripd_settings, offset + 1);
		}
	}
	return 0;
}

// If we've got here, we've recieved some data on our sun_addresses->socketfd, time to parse this data:
static void parse_rib_ctl_msgs(const xripd_settings_t *xripd_settings, const sun_addresses_t *sun_addresses){

	int len = 0; // Length of data: 
	int recv_count = 0; // Count of recieved messages:
	int send_count = 0; // Count of messages to send onto network

	struct rib_ctl_reply {
		rib_ctl_hdr_t header;
		rib_entry_t entry;
	} ctl_reply;

	// Number of times we've tried to read from our socket to complete a stream
	// This is to account for out-of-order packets (with interspersed PREEMPT messages)
	int retry_count = 0;
	// Amount of times we will retry to read the stream correctly before giving up.
	const int max_retries = 3; 

	// Create buffer for a single datagram
	char *buf = (char *)malloc(sizeof(ctl_reply));
	memset(buf, 0, sizeof(ctl_reply));

	// Pointer to a ctl header, used for parsing our recieved data:
	struct rib_ctl_hdr_t *header;

	// Read in a full reply's worth of data:
	len = read(sun_addresses->socketfd, buf, sizeof(ctl_reply));

	// Parse our header, make sure it's not malformed:
	if ( len >= sizeof(rib_ctl_hdr_t) ) {

		// Let's first look at the header:
		header = (rib_ctl_hdr_t *)buf;

		// If not version 1, bomb out:
		if ( header->version != RIB_CTL_HDR_VERSION_1 ) {          
			fprintf(stderr, "[xripd-out]: Received Unsupported Version.\n"); 
			goto error_unsupported;
		} 

		// Parse the msg type:
		switch (header->msgtype) {

			// First packet recieved was a start of REPLY stream, let's continue to read until we
			// hit a RIB_CTL_HDR_MSGTYPE_ENDREPLY message:
			case RIB_CTL_HDR_MSGTYPE_REPLY:

				// Retry a few times until we abandon parsing:
				while ( retry_count != max_retries ) {
					
					// Ensure that the datagram that we recieved first is the correct size and that
					// it is a reply message (redundant on first pass, but important for secondary passes):
					while ( len >= sizeof(ctl_reply) && 
						header->msgtype == RIB_CTL_HDR_MSGTYPE_REPLY ) {

						// Increment our received count:
						recv_count++;
#if XRIPD_DEBUG == 1
						fprintf(stderr, "[xripd-out]: Received RIB_CTRL_MSGTYPE_REPLY No# %d\n", recv_count);
#endif
						// Cast our buffer as a ctl_reply, and send the rib_entry_t to the
						// handler function to pack it into our static rip_datagram variable
						// Function may or may not place the packet onto the wire
						ctl_reply = (*(struct rib_ctl_reply *)buf);

						// If Split Horizon logic is enabled, only advertise ORIGIN_LOCAL routes
						// We can make this assumption based on the logic that xripd only supports 1 interface
						// If it is ever extended to support 1+ interfaces, actual split horizon logic
						// will need to be implemented:
						if ( RIP_SPLIT_HORIZON_ENABLE ) {
							if ( ctl_reply.entry.origin == RIB_ORIGIN_LOCAL ) {
								send_count++;
								format_ripv2_update_datagram(xripd_settings, recv_count, &(ctl_reply.entry), 0);
							}
						// If split horizon isnt enabled, place the route onto the network anyway:
						} else {
							send_count++;
							format_ripv2_update_datagram(xripd_settings, recv_count, &(ctl_reply.entry), 0);
						}
				
						// Read next packet, look at the header, and increment our count:
						len = read(sun_addresses->socketfd, buf, sizeof(ctl_reply));
						header = (rib_ctl_hdr_t *)buf;
					}

					// ENDREPLY message recieved to signify end of stream:
					if ( len >= sizeof(rib_ctl_hdr_t) && header->msgtype == RIB_CTL_HDR_MSGTYPE_ENDREPLY ) {
#if XRIPD_DEBUG == 1
						fprintf(stderr, "[xripd-out]: Successfully received RIB_CTRL_MSGTYPE_ENDREPLY\n");
						fprintf(stderr, "[xripd-out]: Route Count Received: %d\n", recv_count);
#endif
						format_ripv2_update_datagram(xripd_settings, send_count, NULL, 1);
						retry_count = max_retries;

					// We've recieved something unexpected, let's try again to either read more REPLY messages
					// or an ENDREPLY:
					} else {
#if XRIPD_DEBUG == 1
						fprintf(stderr, "[xripd-out]: ENDREPLY not Recieved, Ignoring packet.\n");
						fprintf(stderr, "[xripd-out]: Waiting further.%d\n", send_count);
#endif
						len = read(sun_addresses->socketfd, buf, sizeof(ctl_reply));
						retry_count++;
					}
				}
			default:
				break;
		}
	}

error_unsupported:
	free(buf);
}

// Generate and send rib ctl REQUEST message to rib process via Abstract Unix Domain Socket:
static void send_ctl_request(const xripd_settings_t *xripd_settings, const sun_addresses_t *sun_addresses) {

	// Create Header on Stack:
	rib_ctl_hdr_t rib_control_header;
	rib_control_header.version = RIB_CTL_HDR_VERSION_1;
	rib_control_header.msgtype = RIB_CTL_HDR_MSGTYPE_REQUEST;

	// Fire off request to rib process:
#if XRIPD_DEBUG == 1
	fprintf(stderr, "[xripd-out]: Sending RIB_CTRL_MSGTYPE_REQUEST to xripd-rib.\n");
#endif
	sendto(sun_addresses->socketfd, &rib_control_header, sizeof(rib_control_header), 
		0, (struct sockaddr *) &(sun_addresses->sockaddr_un_rib), sizeof(struct sockaddr_un));
}

// Main control loop:
static void main_loop(xripd_settings_t *xripd_settings, const sun_addresses_t *sun_addresses){

	// Used to calculate when to generate the next rib ctl REQUEST message:
	time_t next_request_time = 0;

	// select() variables:
	fd_set readfds; // Set of file descriptors (in our case, only one) for select() to watch for
	struct timeval timeout; // Time to wait for data in our select()ed socket
	int sret; // select() return value

	// Init our statically allocated datagram:
	init_update_datagram();

	while (1) {

		// Generate and send rib ctl REQUEST message to rib process:
		send_ctl_request(xripd_settings, sun_addresses);

		// Calculate time to send next request:
		next_request_time  = time(NULL) + xripd_settings->rip_timers.route_update;

		// While we dont need to send a new request:
		while (time(NULL) < next_request_time) {

                        // Wipe our set of fds, and monitor our input pipe descriptor:
                        FD_ZERO(&readfds); 
                        FD_SET(sun_addresses->socketfd, &readfds); 

			// Sleep for a second:
                        timeout.tv_sec = 1;
                        timeout.tv_usec = 0;

                        // Wait up to a second for a msg entry to come in
                        sret = select(sun_addresses->socketfd + 1, &readfds, NULL, NULL, &timeout);
	
			// Got a message, now let's parse it:
			if (sret > 0) {
				parse_rib_ctl_msgs(xripd_settings, sun_addresses);
			}

			// Access shared memory from parent (daemon) thread. If it's recently received a 
			// RIPv2 REQUEST Message, it will set the flag to 1.
			// If the valuie is one, send a rib_ctl message to the daemon to do a full routing table
			// dump, and then reset back to zero
			pthread_mutex_lock(&(xripd_settings->daemon_shared.mutex_request_flag));
			if ( xripd_settings->daemon_shared.request_flag == 1 ) {
#if XRIPD_DEBUG == 1
				fprintf(stderr, "[xripd-out]: request_flag was set to 1 by daemon. Dumping Rib ...\n");
#endif
				send_ctl_request(xripd_settings, sun_addresses);
				xripd_settings->daemon_shared.request_flag = 0;
			}
			pthread_mutex_unlock(&(xripd_settings->daemon_shared.mutex_request_flag));
		}
	}
}

// Entry point for the daemon-out thread, responsible for communicating with the rib
// via Unix Domain Sockets.
// Responsible for initialising our Abstract Unix Domain Socket, and then entering our main loop:
void *xripd_out_spawn(void *xripd_settings) {

	// Initialse our addresses struct on the stack:
	sun_addresses_t sun_addresses;
	memset(&sun_addresses, 0, sizeof(sun_addresses));

	// Create our abstract UNIX Domain Socket:
	if ( init_abstract_unix_socket(&sun_addresses) != 0 ) {
		fprintf(stderr, "[xripd-out]: Failed to bind to Abstract UNIX Domain Socket: \\0xripd-daemon.\n");
		fprintf(stderr, "[xripd-out]: Killing Thread.\n");
		goto failed_socket;
	}

	// Enter main loop:
	main_loop(xripd_settings, &sun_addresses);

failed_socket:
	return NULL;
}
