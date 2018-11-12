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

void *xripd_out_spawn(void *arg) {

	// Initialse our addresses struct on the stack:
	sun_addresses_t sun_addresses;
	memset(&sun_addresses, 0, sizeof(sun_addresses));
	memset(&(sun_addresses.sockaddr_un_daemon), 0, sizeof(sun_addresses.sockaddr_un_daemon));
	memset(&(sun_addresses.sockaddr_un_rib), 0, sizeof(sun_addresses.sockaddr_un_rib));

	// Create our abstract UNIX Domain Socket:
	if ( init_abstract_unix_socket(&sun_addresses) != 0 ) {
		fprintf(stderr, "[xripd-out]: Failed to bind to Abstract UNIX Domain Socket: \\0xripd-daemon.\n");
		fprintf(stderr, "[xripd-out]: Killing Thread.\n");
		goto failed_socket;
	}

	int retval;
	rib_ctl_hdr_t rib_control_header;
	rib_control_header.version = RIB_CTL_HDR_VERSION_1;
	rib_control_header.msgtype = RIB_CTL_HDR_MSGTYPE_REQUEST;
	
	while (1) {
		fprintf(stderr, "[xripd-out]: Sending RIB_CTRL_MSGTYPE_REQUEST to xripd-rib.\n");
		retval = sendto(sun_addresses.socketfd, &rib_control_header, sizeof(rib_control_header), 0, (struct sockaddr *) &(sun_addresses.sockaddr_un_rib), sizeof(struct sockaddr_un));
		fprintf(stderr, "[xripd-out]: Sent %d bytes\n", retval);
		sleep(1);
	}


failed_socket:
	return NULL;
}
