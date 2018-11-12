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

void *rib_out_spawn(void *arg) {

	char buf[8192];

	// Initialse our addresses struct on the stack:
	sun_addresses_t sun_addresses;
	memset(&sun_addresses, 0, sizeof(sun_addresses));
	memset(&(sun_addresses.sockaddr_un_daemon), 0, sizeof(sun_addresses.sockaddr_un_daemon));
	memset(&(sun_addresses.sockaddr_un_rib), 0, sizeof(sun_addresses.sockaddr_un_rib));

	// Create our abstract UNIX Domain Socket:
	if ( init_abstract_unix_socket(&sun_addresses) != 0 ) {
		fprintf(stderr, "[rib-out]: Failed to bind to Abstract UNIX Domain Socket: \\0xripd-rib.\n");
		fprintf(stderr, "[rib-out]: Killing Thread.\n");
		goto failed_socket;
	}

	int len = 0;
	struct rib_ctl_hdr_t rib_control_header;

	while (1) {
		len = read(sun_addresses.socketfd, buf, sizeof(buf));
		fprintf(stderr, "[rib-out]: Recieved %d Bytes.\n", len);
		// Cast our raw recieved bytes to retrieve our header:
		rib_control_header = *(rib_ctl_hdr_t *)buf;
		fprintf(stderr, "[rib-out]: Received: %d Bytes.\n", len);
		if ( rib_control_header.version != RIB_CTL_HDR_VERSION_1 ) {
			fprintf(stderr, "[rib-out]: Received Unsupported Version.\n");
			break;
		}
		if ( rib_control_header.msgtype == RIB_CTL_HDR_MSGTYPE_REQUEST ) {
			fprintf(stderr, "[rib-out]: Received RIB_CTRL_MSGTYPE_REQUEST!\n");
			fprintf(stderr, "[rib-out]: Time to dump RIB.\n");
		}
	}

failed_socket:
	return NULL;
}
