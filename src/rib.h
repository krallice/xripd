#ifndef XRIPD_RIB_H
#define XRIPD_RIB_H

#include "xripd.h"

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

// Select():
#include <sys/select.h>

// Datastore implementation indexes
// uint8_t value, 256 possible datastores:
#define XRIPD_RIB_DATASTORE_NULL 0x00
#define XRIPD_RIB_DATASTORE_LINKEDLIST 0x01

// Return values that our rib backing store may return
// which drive the rib core logic to modify routes
// in the kernel table
#define RIB_RET_NO_ACTION 0x00 // RIB has determined that there is no change to RIB, can ignore route.
#define RIB_RET_INSTALL_NEW 0x01 // Brand new route was installed in the RIB.
#define RIB_RET_REPLACE 0x02 // Parameters for a prefix (metric/nethop) have changed.
#define RIB_RET_INVALIDATE 0x03 // Route has been invalidated (Metric = INFINITY)

// Where did our route originate from:
#define RIB_ORIGIN_LOCAL 0x00 // Locally originated from local interface
#define RIB_ORIGIN_REMOTE 0x01 // Remotely learnt

// The Rib is comprised of a logical ordering of rib_entry_t's
// The raw data from a rip msg is held in rip_msg_entry and
// related useful information is also packed in:
typedef struct rib_entry_t {
	struct sockaddr_in recv_from;
	time_t recv_time;
	rip_msg_entry_t rip_msg_entry;
	uint8_t origin;
} rib_entry_t;

// Abstraction, comprised of function pointers to underlying
// implementations relating to a 'datastore':
typedef struct xripd_rib_t {
	uint8_t rib_datastore;
	time_t last_local_poll; // Time of our last netlink poll. Used to sync our rib with our local routes (determined through netlink).
	int (*add_to_rib)(int*, rib_entry_t*, rib_entry_t*, rib_entry_t*);
	int (*remove_expired_entries)();
	int (*invalidate_expired_local_routes)(); // Metric = 16 for old local routes that are no longer in the kernel table
	int (*dump_rib)();
} xripd_rib_t;

// Create our rib datastructure, which is essentially an interface to a concrete implementation, represented by rib_datastore.
// Link this into the settings struct:
int init_rib(xripd_settings_t *xripd_settings, uint8_t rib_datastore);

// Main loop that the child process (xripd-rib) loops upon. Essentially the entry point for the child:
void rib_main_loop(xripd_settings_t *xripd_settings);

// Copy function for rib_entry_t:
void copy_rib_entry(rib_entry_t *src, rib_entry_t *dst);

// Add a local route pointed to by nlmsghdr to the local rib:
int add_local_route_to_rib(xripd_settings_t *xripd_settings, struct nlmsghdr *nlhdr);

// Temporary header for debugging. Should only be a local function
void rib_route_print(rib_entry_t *in_entry);
#endif
