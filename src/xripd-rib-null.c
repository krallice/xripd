#include "xripd-rib-null.h"

int rib_null_add_to_rib(void) {
#if XRIPD_DEBUG == 1
#endif
	fprintf(stderr, "null-rib: Add to RIB Success\n");
	return 0;
}
