#ifndef XRIPD_FILTER_H
#define XRIPD_FILTER_H

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

// Netlink Specific:
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

// Whitelist or Blacklist filter operation, and default value:
#define XRIPD_FILTER_MODE_WHITELIST 0x00
#define XRIPD_FILTER_MODE_BLACKLIST 0x01

#define XRIPD_FILTER_RESULT_DENY 0x00
#define XRIPD_FILTER_RESULT_ALLOW 0x01

// Individual nodes of our filter:
typedef struct filter_node_t {
	uint32_t ipaddr;
	uint32_t netmask;
	uint8_t cidr; // Maybe not required?
	struct filter_node_t *next;
} filter_node_t;

// Linked List of nodes:
typedef struct filter_list_t {
	filter_node_t *head;
	filter_node_t *tail;
} filter_list_t;

// Filter struct holding our settings and datastructure:
typedef struct filter_t {
	uint8_t filter_mode;
	filter_list_t *filter_list;
} filter_t;

// Create/Destroy our filter struct (and substructs):
filter_t *init_filter(uint8_t mode);
void destroy_filter(filter_t *f);

// Append a route to the end of our filter list:
int append_to_filter_list(filter_t *f, uint32_t addr, uint32_t mask);

int filter_route(filter_t *f, uint32_t addr, uint32_t mask);

void dump_filter_list(filter_t *f);

#endif
