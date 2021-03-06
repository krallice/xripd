#ifndef XRIPD_RIB_NULL_H
#define XRIPD_RIB_NULL_H

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

int rib_null_add_to_rib(int *route_ret, const rib_entry_t *in_entry, rib_entry_t *ins_route, rib_entry_t *del_route, int *rib_inc);
int rib_null_invalidate_expired_local_routes(time_t last_run);
int rib_null_remove_expired_entries(const rip_timers_t *timeers, int *delroute);

int rib_null_serialise_rib(char *buf, const uint32_t *count);
int rib_null_dump_rib();

void rib_null_destroy_rib();
#endif
