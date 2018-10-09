#include "route.h"

// Function to create our netlink interface (used to install routes etc..):
int init_netlink(xripd_settings_t *xripd_settings) {

	// Spawn our NETLINK_ROUTE AF_NETLINK socket
	xripd_settings->nlsd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
	if ( xripd_settings->nlsd <= 0 ) {
		fprintf(stderr, "[route]: Error, Unable to open AF_NETLINK Socket..\n");
		close(xripd_settings->nlsd);
		return 1;
	} else {
#if XRIPD_DEBUG == 1
		fprintf(stderr, "[route]: RIB Process Started\n");
#endif
	}

// Success:
	return 0;
}

// Delete netlink socket:
int del_netlink(xripd_settings_t *xripd_settings) {
	close(xripd_settings->nlsd);
	xripd_settings->nlsd = 0;
	return 0;
}
