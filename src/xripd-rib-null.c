#include "xripd-rib-null.h"

int rib_null_add_to_rib(rip_rib_entry_t *in_entry) {
#if XRIPD_DEBUG == 1
	fprintf(stderr, "[null]: Add to RIB Success\n");
#endif
	return 0;
}
