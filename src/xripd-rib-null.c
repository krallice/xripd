#include "xripd-rib-null.h"

int rib_null_add_to_rib(rib_entry_t *in_entry) {
#if XRIPD_DEBUG == 1
	fprintf(stderr, "[null]: Add to RIB Success\n");
#endif
	return 0;
}

int rib_null_dump_rib() {
#if XRIPD_DEBUG == 1
	fprintf(stderr, "[null]: Dumping RIB, Empty no surprise ...\n");
#endif
	return 0;
}
