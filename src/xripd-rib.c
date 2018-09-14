#include "xripd.h"
#include "xripd-rib.h"

// Structure to pass into the rib:
typedef struct rip_rib_entry_t {
	struct sockaddr_in recv_from;
	rip_msg_entry_t rip_entry;
} rip_rib_entry_t;
