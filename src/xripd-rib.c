#include "xripd-rib.h"
#include "xripd-rib-ll.h"
#include "xripd-rib-null.h"

int init_rib(xripd_settings_t *xripd_settings, uint8_t rib_datastore) {

	// Init and Zeroise:
	xripd_rib_t *xripd_rib = (xripd_rib_t*)malloc(sizeof(xripd_rib_t));
	memset(xripd_rib, 0, sizeof(xripd_rib_t));

	xripd_rib->rib_datastore = rib_datastore;

	if ( rib_datastore == XRIPD_RIB_DATASTORE_NULL ) {
		printf("MATCH\n");
		xripd_rib->add_to_rib = &rib_null_add_to_rib;
		printf("z - %p\n", &rib_null_add_to_rib);
		printf("z - %p\n", &rib_null_add_to_rib);
		printf("zz - %p\n", xripd_rib->add_to_rib);

		xripd_settings->xripd_rib = xripd_rib;
		return 0;
	}

	// Error Out:
	return 1;
}

void rib_main_loop(xripd_settings_t *xripd_settings) {

#if XRIPD_DEBUG == 1
		fprintf(stderr, "RIB Main Loop Started\n");
#endif

	while (1) {
		(*xripd_settings->xripd_rib->add_to_rib)();
		sleep(1);
	}

}
