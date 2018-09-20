#include "rib.h"
#include "rib-ll.h"
#include "rib-null.h"

int init_rib(xripd_settings_t *xripd_settings, uint8_t rib_datastore) {

	// Init and Zeroise:
	xripd_rib_t *xripd_rib = (xripd_rib_t*)malloc(sizeof(xripd_rib_t));
	memset(xripd_rib, 0, sizeof(xripd_rib_t));

	xripd_rib->rib_datastore = rib_datastore;

	if ( rib_datastore == XRIPD_RIB_DATASTORE_NULL ) {
		xripd_rib->add_to_rib = &rib_null_add_to_rib;
		xripd_rib->dump_rib = &rib_null_dump_rib;
		xripd_settings->xripd_rib = xripd_rib;
		return 0;
	} else if ( rib_datastore == XRIPD_RIB_DATASTORE_LINKEDLIST ) {
		xripd_rib->add_to_rib = &rib_ll_add_to_rib;
		xripd_rib->dump_rib = &rib_ll_dump_rib;
		xripd_settings->xripd_rib = xripd_rib;
		rib_ll_init();
		return 0;
	}

	// Error Out:
	return 1;
}

void rib_main_loop(xripd_settings_t *xripd_settings) {

#if XRIPD_DEBUG == 1
	fprintf(stderr, "[rib]: RIB Main Loop Started\n");
#endif
	rib_entry_t in_entry;
	while (1) {

		read(xripd_settings->p_rib_in[0], &in_entry, sizeof(rib_entry_t));
#if XRIPD_DEBUG == 1
		char ipaddr[16];
		char subnet[16];
		char nexthop[16];
		inet_ntop(AF_INET, &in_entry.rip_msg_entry.ipaddr, ipaddr, sizeof(ipaddr));
		inet_ntop(AF_INET, &in_entry.rip_msg_entry.subnet, subnet, sizeof(subnet));
		inet_ntop(AF_INET, &in_entry.rip_msg_entry.nexthop, nexthop, sizeof(nexthop));
		fprintf(stderr, "[rib]: Route Received: IP: %s %s Next-Hop: %s Metric: %02d, Adding to RIB\n", ipaddr, subnet, nexthop, ntohl(in_entry.rip_msg_entry.metric));
#endif
		(*xripd_settings->xripd_rib->add_to_rib)(&in_entry);
		(*xripd_settings->xripd_rib->dump_rib)();
		//sleep(1);
	}

}