#include "xripd.h"

// xripd Defines:
#define XRIPD_PASSIVE_IFACE "enp0s8"

// Protocol Defines:
#define RIP_MCAST_IP 224.0.0.9
#define RIP_UDP_PORT 520

typedef struct xripd_settings_t {
	int sd; 			// Socket Descriptor
	char iface_name[IFNAMSIZ]; 	// Human String for an interface, ie. "eth3" or "enp0s3"
	int iface_index; 		// Kernel index id for interface
} xripd_settings_t;

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
	//bind_address.sin_addr.s_addr = mcast_ip.s_addr;
	bind_address.sin_addr.s_addr = ((struct sockaddr_in *)&ifrq.ifr_addr)->sin_addr.s_addr;
	bind_address.sin_family = AF_INET;
	bind_address.sin_port = htons(bind_port);

	if ( bind(xripd_settings->sd, (const struct sockaddr *)(&bind_address), sizeof(bind_address)) == -1 ){
		close(xripd_settings->sd);
		fprintf(stderr, "Error: Unable to bind the RIPv2 MCAST IP + UDP Port to socket\n");
		return 1;
	}

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

int main(void) {

	xripd_settings_t *xripd_settings = init_xripd_settings();
	if ( xripd_settings == NULL ) {
		return 1;
	}

	if ( init_socket(xripd_settings) != 0)
		return 1;

	while(1) {
		sleep(10);
	}

	return 0;
}
