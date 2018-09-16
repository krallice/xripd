#ifndef XRIPD_RIB_H
#define XRIPD_RIB_H

#include "xripd.h"
#include "xripd-rib-null.h"
#include "xripd-rib-ll.h"

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

#define XRIPD_RIB_DATASTORE_NULL 0x00
#define XRIPD_RIB_DATASTORE_LINKEDLIST 0x01

typedef struct xripd_rib_t {
	uint8_t rib_datastore;
	int (* add_to_rib)();
} xripd_rib_t;

// Structure to pass into the rib:
//typedef struct rip_rib_entry_t {
	//struct sockaddr_in recv_from;
	//rip_msg_entry_t rip_entry;
//} rip_rib_entry_t;

//int init_rib(xripd_settings_t *xripd_settings, uint8_t rib_datastore);
int init_rib(uint8_t rib_datastore);

#endif
