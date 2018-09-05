#include "xripd.h"

// xripd Defines:
#define XRIPD_PASSIVE_IFACE "eth1"

// Protocol Defines:
#define RIP_MCAST_IP 224.0.0.9

typedef struct xripd_settings_t {
	int sd; // Socket Descriptor
	char iface_name[IFNAMSIZ]; // Human String for an interface, ie. "eth3" or "enp0s3"
} xripd_settings_t;

int init_socket(xripd_settings_t *xripd_settings) {
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
		return NULL;
	}	

	return xripd_settings;
}

int main(void) {

	xripd_settings_t *xripd_settings = init_xripd_settings();
	if ( xripd_settings == NULL ) {
		return 1;
	}

	if ( init_socket(xripd_settings) == 0)
		return 1;

	return 0;
}
