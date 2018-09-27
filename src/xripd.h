#ifndef XRIPD_H
#define XRIPD_H

// Standard Includes:
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>

// Network Specific:
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/if_packet.h>
#include <linux/if_ether.h>
#include <linux/if_arp.h>
#include <arpa/inet.h>

// XRIPD Defines:
#define XRIPD_PASSIVE_IFACE "enp0s8"
#define XRIPD_DEBUG 0x01

// RIP Protocol Defines:
#define RIP_MCAST_IP "224.0.0.9"
#define RIP_UDP_PORT 520

#define RIP_SUPPORTED_VERSION 2

#define RIP_DATAGRAM_SIZE 512
#define RIP_ENTRY_SIZE 20

#define RIP_METRIC_INFINITY 16

// RIP Header Defines:
#define RIP_HEADER_REQUEST 1
#define RIP_HEADER_RESPONSE 2

// RIP Entry Defines:
#define RIP_AFI_INET 2

// RIP Variables
#define RIP_ROUTE_TIMEOUT 5

// Daemon Settings Structure:
typedef struct xripd_settings_t {
	int sd; 			// Socket Descriptor
	char iface_name[IFNAMSIZ]; 	// Human String for an interface, ie. "eth3" or "enp0s3"
	int iface_index; 		// Kernel index id for interface
	struct xripd_rib_t *xripd_rib;		// Pointer to RIB
	int p_rib_in[2];		// Pipe for Listener -> RIB
} xripd_settings_t;

// https://tools.ietf.org/html/rfc2453
// RIP Message Format:
// RIP Header:
typedef struct rip_msg_header_t {
	uint8_t command;
	uint8_t version;
	uint16_t zero;
} rip_msg_header_t;

// Each RIP Message may include 1-25 RIP Entries (RTEs):
typedef struct rip_msg_entry_t {
	uint16_t afi;
	uint16_t tag;
	uint32_t ipaddr;
	uint32_t subnet;
	uint32_t nexthop;
	uint32_t metric;
} rip_msg_entry_t;

#endif
