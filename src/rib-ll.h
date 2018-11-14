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

// Create new datastructure:
int rib_ll_init();

// Add a new rib_entry_t (in_entry) to rib, potentially return a value ine
// ins_rouce or del_route depending on return of the function
int rib_ll_add_to_rib(int *route_ret, const rib_entry_t *in_entry, rib_entry_t *ins_route, rib_entry_t *del_route, int *rib_inc);

// Expire out old entries out of the rib:
int rib_ll_remove_expired_entries(int *delroute);

// Traverse datastructure for RIB_ORIGIN_LOCAL routes
// which have a recv_time timestamp NOT EQUAL to the last netlink run
// This means that the local route does not exist in the local kernel table anymore
// Set metric to infinity so that it can be deleted eventually.
// Return 1 if any routes were invalidated:
int rib_ll_invalidate_expired_local_routes();

// Dump rib:
int rib_ll_dump_rib();

int rib_ll_serialise_rib(char *buf, const uint32_t *count);

void rib_ll_destroy_rib();
#endif
