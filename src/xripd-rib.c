#include "xripd-rib.h"

int init_rib(uint8_t rib_datastore) {

	// Init and Zeroise:
	xripd_rib_t *xripd_rib = (xripd_rib_t*)malloc(sizeof(xripd_rib_t));
	memset(xripd_rib, 0, sizeof(xripd_rib_t));

	xripd_rib->rib_datastore = rib_datastore;

	if ( rib_datastore == XRIPD_RIB_DATASTORE_NULL ) {
		xripd_rib->add_to_rib = rib_null_add_to_rib;
		return 0;
	}

	// Error Out:
	return 1;

}
