#include "rib-out.h"

// Given a sun_addresses_t struct, Populate our daemon and rib addresses, bind to the rib address
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
	fprintf(stderr, "[rib-out]: Spawning UNIX Domain Socket.\n");
#endif
	if ( s->socketfd == 0 ) {
		goto failed_socket_init;
	}

	// Bind to the socket:
#if XRIPD_DEBUG == 1
	fprintf(stderr, "[rib-out]: Binding to Abstract UNIX Domain Socket: \\0xripd-rib.\n");
#endif
	if ( bind(s->socketfd, (struct sockaddr *) &(s->sockaddr_un_rib), sizeof(struct sockaddr_un)) < 0 ) {
		goto failed_bind;
	}

	// Successful exit:
#if XRIPD_DEBUG == 1
	fprintf(stderr, "[rib-out]: Successfully bound to Abstract UNIX Domain Socket: \\0xripd-rib.\n");
#endif
	return 0;

failed_bind:
	close(s->socketfd);
	
failed_socket_init:
	return 1;
}

// Send a serialise requeset to our rib, to return our routes into a buffer,
// Send these via the 'rib_ctl' buffer back to the daemon via a reply message:
static void send_rib_ctl_reply(xripd_settings_t *xripd_settings, const sun_addresses_t *sun_addresses) {

	int len = 0;
	int retval = 0;

	int msgnum = 0;

	// Used for sending the route through our filter:
	uint32_t ip = 0;
	uint32_t netmask = 0;

	// rib_ctl_reply struct:
	struct rib_ctl_reply {
		rib_ctl_hdr_t header;
		rib_entry_t entry;
	} ctl_reply;

	// Format header:
	ctl_reply.header.version = RIB_CTL_HDR_VERSION_1;
	ctl_reply.header.msgtype = RIB_CTL_HDR_MSGTYPE_REPLY;

	// Allocate enough memory on the heap for the entire RIB:
	char *buf = (char *)malloc(xripd_settings->xripd_rib->size * (sizeof(rib_entry_t)));

	// Populate buffer with our rib in a serialised format, in a block of rib_entry_t's:
	pthread_mutex_lock(&(xripd_settings->rib_shared.mutex_rib_lock));
	len = xripd_settings->xripd_rib->serialise_rib(buf, &(xripd_settings->xripd_rib->size));
	pthread_mutex_unlock(&(xripd_settings->rib_shared.mutex_rib_lock));
	
	// If we have a positive amount of rib entries (aka, there is some data within the rib)
	if ( len != 0 ) {

		// Iterate over the buffer:
		for ( int i = 0; i < len; i++ ) {

			// Pass route through our filter (if it is configured):
			if ( xripd_settings->filter_mode != XRIPD_FILTER_MODE_NULL ) {

				// Pointer magic:
				ip = ((rib_entry_t *)(buf + (i * sizeof(rib_entry_t))))->rip_msg_entry.ipaddr;
				netmask = ((rib_entry_t *)(buf + (i * sizeof(rib_entry_t))))->rip_msg_entry.subnet;

				// If the route does not pass through the filter, continue onto the next route:
				if ( filter_route(xripd_settings->xripd_rib->filter, ip, 
					netmask) != XRIPD_FILTER_RESULT_ALLOW ) {
#if XRIPD_DEBUG == 1
					fprintf(stderr, "[rib-out]: Filtered route from being sent via RIB_CTL_HDR_MSGTYPE_REPLY\n");
#endif
					continue;
				}
			}

			// Add entry to reply struct:
			memcpy(&(ctl_reply.entry), (rib_entry_t*)(buf + (i * sizeof(rib_entry_t))), sizeof(rib_entry_t));
			
			// Send reply via socket back to the daemon:
			retval = sendto(sun_addresses->socketfd, &ctl_reply, sizeof(ctl_reply), 
					0, (struct sockaddr *) &(sun_addresses->sockaddr_un_daemon), sizeof(struct sockaddr_un));
#if XRIPD_DEBUG == 1
			msgnum++;
			fprintf(stderr, "[rib-out]: Sent %d bytes in RIB_CTL_HDR_MSGTYPE_REPLY Msg No: %d\n", retval, msgnum);
#endif
		}

		// Format header for ENDREPLY, this it to let the daemon know we have reached the end of our datagram stream:
		ctl_reply.header.version = RIB_CTL_HDR_VERSION_1;
		ctl_reply.header.msgtype = RIB_CTL_HDR_MSGTYPE_ENDREPLY;
		
		// Send endreply via socket to the daemon:
		retval = sendto(sun_addresses->socketfd, &(ctl_reply.header), sizeof(ctl_reply.header), 
				0, (struct sockaddr *) &(sun_addresses->sockaddr_un_daemon), sizeof(struct sockaddr_un));
#if XRIPD_DEBUG == 1
		fprintf(stderr, "[rib-out]: Sent %d bytes in RIB_CTL_HDR_MSGTYPE_ENDREPLY.\n", retval);
#endif

	}
	// Free up the heap:
	free(buf);
}

// Main Listening Loop
// Wait on the Unix Socket, parse the message type, and then dispatch appropriately:
static void listen_loop(xripd_settings_t *xripd_settings, const sun_addresses_t *sun_addresses) {

	// Create a buffer that fits atleast 1 rib_ctl header and 1 rib entry:
	int len = 0;
	char *buf = (char *)malloc((sizeof(rib_ctl_hdr_t)) + sizeof(rib_entry_t));
	memset(buf, 0, (sizeof(rib_ctl_hdr_t) + sizeof(rib_entry_t)));

	// Pointer used to parse our Header:
	struct rib_ctl_hdr_t *rib_control_header;

	// Listen Loop:
	while (1) {

		// Read bytes from UNIX Socket, placing into buf:
		len = read(sun_addresses->socketfd, buf, sizeof(buf));
		
		// If our datagram is incompletely formed, move along:
		if ( len < sizeof(rib_ctl_hdr_t) ) {
			break;
		}
		
		// Cast our raw recieved bytes to retrieve our header:
		rib_control_header = (rib_ctl_hdr_t *)buf;

		// Only support VERSION_1 for now:
		if ( rib_control_header->version != RIB_CTL_HDR_VERSION_1 ) {
			fprintf(stderr, "[rib-out]: Received Unsupported Version.\n");
			break;
		}

		// Parse our header:
		switch (rib_control_header->msgtype) {
			case RIB_CTL_HDR_MSGTYPE_REQUEST:
				fprintf(stderr, "[rib-out]: Received RIB_CTRL_MSGTYPE_REQUEST from xripd-daemon.\n");
				send_rib_ctl_reply(xripd_settings, sun_addresses);
				break;
			default:
				break;
		}
	}
	free(buf);
}

// Entry point for the rib-out thread. Spawned from the main rib.c
// Responsible for setting everything up, and then moving onto our listening loop
void *rib_out_spawn(void *xripd_settings) {

	// Initialse our addresses struct on the stack:
	sun_addresses_t sun_addresses;
	memset(&sun_addresses, 0, sizeof(sun_addresses));

	// Create our abstract UNIX Domain Socket:
	if ( init_abstract_unix_socket(&sun_addresses) != 0 ) {
		fprintf(stderr, "[rib-out]: Failed to bind to Abstract UNIX Domain Socket: \\0xripd-rib.\n");
		fprintf(stderr, "[rib-out]: Killing Thread.\n");
		goto failed_socket;
	}

	listen_loop(xripd_settings, &sun_addresses);

	// Should never hit here:
	return NULL;

failed_socket:
	return NULL;
}
