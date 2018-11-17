#include "rib-out.h"

typedef struct sun_addresses_t {
	int socketfd;
	struct sockaddr_un sockaddr_un_daemon;
	struct sockaddr_un sockaddr_un_rib;
} sun_addresses_t;

static int init_abstract_unix_socket(sun_addresses_t *s) {

	s->sockaddr_un_rib.sun_family = AF_UNIX;
	strcpy(s->sockaddr_un_rib.sun_path, "#xripd-rib");
	s->sockaddr_un_rib.sun_path[0] = 0;

	s->sockaddr_un_daemon.sun_family = AF_UNIX;
	strcpy(s->sockaddr_un_daemon.sun_path, "#xripd-daemon");
	s->sockaddr_un_daemon.sun_path[0] = 0;

	s->socketfd = socket(AF_UNIX, SOCK_DGRAM, 0);
#if XRIPD_DEBUG == 1
	fprintf(stderr, "[xripd-out]: Spawning UNIX Domain Socket.\n");
#endif
	if ( s->socketfd == 0 ) {
		goto failed_socket_init;
	}

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

	// int sender = 77;
	// int ret = 0;

	// while (1) {
		// fprintf(stderr, "[xripd-out]: SENDING BYTES\n");
		// ret = sendto(s->socketfd, &sender, sizeof(sender), 0, (struct sockaddr *) &(s->sockaddr_un_rib), sizeof(struct sockaddr_un));
		// fprintf(stderr, "[xripd-out]: SENT %d bytes\n", ret);
		// sleep(1);
	// }

failed_bind:
	close(s->socketfd);
	
failed_socket_init:
	return 1;
}

static void send_rip_update(const xripd_settings_t *xripd_settings, const sun_addresses_t *sun_addresses) {

	rib_ctl_hdr_t rib_control_header;
	rib_control_header.version = RIB_CTL_HDR_VERSION_1;
	rib_control_header.msgtype = RIB_CTL_HDR_MSGTYPE_REQUEST;

	fprintf(stderr, "[xripd-out]: Sending RIB_CTRL_MSGTYPE_REQUEST to xripd-rib.\n");
	sendto(sun_addresses->socketfd, &rib_control_header, sizeof(rib_control_header), 
		0, (struct sockaddr *) &(sun_addresses->sockaddr_un_rib), sizeof(struct sockaddr_un));
}

static void parse_rib_ctl_msgs(const xripd_settings_t *xripd_settings, const sun_addresses_t *sun_addresses){

	int len = 0;
	int i = 0;

	struct rib_ctl_reply {
		rib_ctl_hdr_t header;
		rib_entry_t entry;
	} ctl_reply;

	char *buf = (char *)malloc(sizeof(ctl_reply));
	memset(buf, 0, sizeof(ctl_reply));

	struct rib_ctl_hdr_t *header;

	len = read(sun_addresses->socketfd, buf, sizeof(ctl_reply));

	int retry_count = 0;
	int max_retries = 3;

	// Parse our header:
	if ( len >= sizeof(rib_ctl_hdr_t) ) {
		header = (rib_ctl_hdr_t *)buf;

		// If not version 1, bomb out:
		if ( header->version != RIB_CTL_HDR_VERSION_1 ) {          
			fprintf(stderr, "[xripd-out]: Received Unsupported Version.\n"); 
			goto error_unsupported;
		} 

		// Parse the msg type:
		switch (header->msgtype) {

			// First packet recieved was a start of REPLY stream:
			case RIB_CTL_HDR_MSGTYPE_REPLY:

				while ( retry_count != max_retries ) {
					// We can start to use ctl_reply now as we are expecting header + rib entries:
					while ( len >= sizeof(ctl_reply) && 
						header->msgtype == RIB_CTL_HDR_MSGTYPE_REPLY ) {
						i++;
						fprintf(stderr, "[xripd-out]: Received RIB_CTRL_MSGTYPE_REPLY No# %d\n", i);
						len = read(sun_addresses->socketfd, buf, sizeof(ctl_reply));
						header = (rib_ctl_hdr_t *)buf;
					}
					// ENDREPLY message recieved to signify end of stream:
					if ( len >= sizeof(rib_ctl_hdr_t) && header->msgtype == RIB_CTL_HDR_MSGTYPE_ENDREPLY ) {
						fprintf(stderr, "[xripd-out]: Successfully received RIB_CTRL_MSGTYPE_ENDREPLY\n");
						fprintf(stderr, "[xripd-out]: Route Count Received: %d\n", i);
						retry_count = max_retries;
					// Msg to be aborted:
					} else {
						fprintf(stderr, "[xripd-out]: ENDREPLY not Recieved, Ignoring packet.\n");
						fprintf(stderr, "[xripd-out]: Waiting further.%d\n", i);
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

// Main control loop of the secondary thread in the daemon process:
static void main_loop(const xripd_settings_t *xripd_settings, const sun_addresses_t *sun_addresses){

	//uint8_t msgtype_request_sent = 0;
	time_t next_update_time = 0;

	// select() variables:
	fd_set readfds; // Set of file descriptors (in our case, only one) for select() to watch for
	struct timeval timeout; // Time to wait for data in our select()ed socket
	int sret; // select() return value

	while (1) {

		send_rip_update(xripd_settings, sun_addresses);
		next_update_time = time(NULL) + xripd_settings->rip_timers.route_update;

		while (time(NULL) < next_update_time) {

                        // Wipe our set of fds, and monitor our input pipe descriptor:
                        FD_ZERO(&readfds); 
                        FD_SET(sun_addresses->socketfd, &readfds); 

                        // Timeout value; (how often to poll)
                        timeout.tv_sec = 1;
                        timeout.tv_usec = 0;

                        // Wait up to a second for a msg entry to come in
                        sret = select(sun_addresses->socketfd + 1, &readfds, NULL, NULL, &timeout);
	
			// Got a message, now let's parse it:
			if (sret > 0) {
				parse_rib_ctl_msgs(xripd_settings, sun_addresses);
			}
		}
	}
}

// Spawn and setup our secondary thread in the daemon process
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

	main_loop(xripd_settings, &sun_addresses);

failed_socket:
	return NULL;
}
