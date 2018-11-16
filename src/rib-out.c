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
	fprintf(stderr, "[rib-out]: Spawning UNIX Domain Socket.\n");
#endif
	if ( s->socketfd == 0 ) {
		goto failed_socket_init;
	}

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

static void send_rib_ctl_reply(const xripd_settings_t *xripd_settings, const sun_addresses_t *sun_addresses) {

	int len = 0;
	int retval = 0;

	struct rib_ctl_reply {
		rib_ctl_hdr_t header;
		rib_entry_t entry;
	} ctl_reply;

	// Format header:
	ctl_reply.header.version = RIB_CTL_HDR_VERSION_1;
	ctl_reply.header.msgtype = RIB_CTL_HDR_MSGTYPE_REPLY;

	// Allocate enough memory on the heap for the entire RIB:
	char *buf = (char *)malloc(xripd_settings->xripd_rib->size * (sizeof(rib_entry_t)));

	// Populate buffer with our rib:
	len = xripd_settings->xripd_rib->serialise_rib(buf, &(xripd_settings->xripd_rib->size));

	// Positive amount of rib entries:
	if ( len != 0 ) {

		// Iterate over the buffer to second last item:
		for ( int i = 0; i < len; i++ ) {
#if XRIPD_DEBUG == 1
			fprintf(stderr, "[rib-out]: Sending RIB_CTL_HDR_MSGTYPE_REPLY Msg No: %d\n", i);
#endif
			// Add entry to reply struct:
			memcpy(&(ctl_reply.entry), (rib_entry_t*)(buf + (i * sizeof(rib_entry_t))), sizeof(rib_entry_t));
			
			//ctl_reply.entry = (rib_entry_t*)(buf + (i * sizeof(rib_entry_t)));
			
			// Send reply via socket back to the daemon:
			retval = sendto(sun_addresses->socketfd, &ctl_reply, sizeof(ctl_reply), 
					0, (struct sockaddr *) &(sun_addresses->sockaddr_un_daemon), sizeof(struct sockaddr_un));
#if XRIPD_DEBUG == 1
			fprintf(stderr, "[rib-out]: Sent %d bytes in RIB_CTL_HDR_MSGTYPE_REPLY Msg No: %d\n", retval, i);
#endif

		}
	}
	
	// Format header for ENDREPLY:
	ctl_reply.header.version = RIB_CTL_HDR_VERSION_1;
	ctl_reply.header.msgtype = RIB_CTL_HDR_MSGTYPE_ENDREPLY;
	
	// Send endreply via socket to the daemon:
	retval = sendto(sun_addresses->socketfd, &(ctl_reply.header), sizeof(ctl_reply.header), 
			0, (struct sockaddr *) &(sun_addresses->sockaddr_un_daemon), sizeof(struct sockaddr_un));
#if XRIPD_DEBUG == 1
	fprintf(stderr, "[rib-out]: Sent %d bytes in RIB_CTL_HDR_MSGTYPE_ENDREPLY.\n", retval);
#endif

	// Free up the heap:
	free(buf);
}

static void listen_loop(const xripd_settings_t *xripd_settings, const sun_addresses_t *sun_addresses) {

	//int len = 0;
	char *buf = (char *)malloc((sizeof(rib_entry_t)) + sizeof(rib_ctl_hdr_t));
	memset(buf, 0, (sizeof(rib_entry_t) + sizeof(rib_ctl_hdr_t)));

	// Parsing Header:
	struct rib_ctl_hdr_t *rib_control_header;

	// Listen Loop:
	while (1) {

		// Read len bytes from UNIX Socket, placing into buf:
		//len = read(sun_addresses->socketfd, buf, sizeof(buf));
		read(sun_addresses->socketfd, buf, sizeof(buf));
		
		// Todo: Fix
		// if ( len < sizeof(rib_control_header) ) {
			// break;
		// }
		
		// Cast our raw recieved bytes to retrieve our header:
		rib_control_header = (rib_ctl_hdr_t *)buf;

		if ( rib_control_header->version != RIB_CTL_HDR_VERSION_1 ) {
			fprintf(stderr, "[rib-out]: Received Unsupported Version.\n");
			break;
		}
		switch (rib_control_header->msgtype) {
			case RIB_CTL_HDR_MSGTYPE_REQUEST:
				fprintf(stderr, "[rib-out]: Received RIB_CTRL_MSGTYPE_REQUEST!\n");
				send_rib_ctl_reply(xripd_settings, sun_addresses);
				break;
			default:
				break;
		}
	}

	free(buf);
}

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
