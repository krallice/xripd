#include "rib.h"
#include "rib-null.h"

int rib_null_add_to_rib(int *route_ret, const rib_entry_t *in_entry, rib_entry_t *ins_route, rib_entry_t *del_route, int *rib_inc) {
#if XRIPD_DEBUG == 1
	fprintf(stderr, "[null]: Add to RIB Success\n");
#endif
	// NULLify our route pointers:
	ins_route = NULL;
	del_route = NULL;

	*route_ret = RIB_RET_NO_ACTION;

	return 0;
}

int rib_null_remove_expired_entries(const rip_timers_t *timers, int *delroute) {
#if XRIPD_DEBUG == 1
	fprintf(stderr, "[null]: Removing Expired Entries.\n");
#endif
	return 0;
}

int rib_null_invalidate_expired_local_routes(time_t last_run) {
#if XRIPD_DEBUG == 1
	fprintf(stderr, "[null]: Local routes no longer in kernel set to metric 16. Obviously none..\n");
#endif
	return 0;
}

int rib_null_dump_rib() {
#if XRIPD_DEBUG == 1
	fprintf(stderr, "[null]: Dumping RIB, Empty no surprise ...\n");
#endif
	return 0;
}

int rib_null_serialise_rib(char *buf, const uint32_t *count) {
#if XRIPD_DEBUG == 1
	fprintf(stderr, "[null]: Serialising RIB, Empty no surprise ...\n");
#endif
	return 0;
}

void rib_null_destroy_rib() {
#if XRIPD_DEBUG == 1
	fprintf(stderr, "[null]: Destroying RIB, Nothing to destroy ...\n");
#endif
	return;
}
