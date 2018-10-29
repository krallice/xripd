#ifndef XRIPD_RIB_LL_H
#define XRIPD_RIB_LL_H

#include "xripd.h"
#include "rib.h"

// Standard Includes:
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>

// Network Specific:
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/if_packet.h>
#include <linux/if_ether.h>
#include <linux/if_arp.h>
#include <arpa/inet.h>

int rib_ll_init();
int rib_ll_add_to_rib(int *route_ret, rib_entry_t *in_entry, rib_entry_t *ins_route, rib_entry_t *del_route);
int rib_ll_remove_expired_entries();
int rib_ll_invalidate_expired_local_routes();
int rib_ll_dump_rib();

#endif
