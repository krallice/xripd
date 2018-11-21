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

#include <getopt.h>

// Network Specific:
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/if_packet.h>
#include <linux/if_ether.h>
#include <linux/if_arp.h>
#include <arpa/inet.h>

#include <pthread.h>

// XRIPD Defines:
#define XRIPD_PASSIVE_IFACE "enp0s8"
#define XRIPD_DEBUG 0x01

#define XRIPD_ENTRIES_PER_UPDATE 4

#define XRIPD_PASSIVE_MODE_DISABLE 0x00
#define XRIPD_PASSIVE_MODE_ENABLE 0x01

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
#define RIP_ROUTE_TIMEOUT 45
#define RIP_ROUTE_GC_TIMEOUT (int)(RIP_ROUTE_TIMEOUT + 120)

// #define RIP_TIMER_UPDATE_DEFAULT 30
#define RIP_TIMER_UPDATE_DEFAULT 5 
#define RIP_TIMER_INVALID_DEFAULT 180 
#define RIP_TIMER_HOLDDOWN_DEFAULT 180 
#define RIP_TIMER_FLUSH_DEFAULT 200 

typedef struct rip_timers_t {
	
	// Update: Rate at which updates are sent:
	uint16_t route_update;
	
	// Invalid: Interval at which the route is marked at Invalid (Metric == Infinity). Kept in RIB, but removed from Kernel table:
	// Default: 180s Should be 3x Update
	uint16_t route_invalid;

	// Holddown TODO Implement: Amount of time the router will refuse to learn about the route from other addresses, only from the current router
	// Default: 180s Should be 3x Update
	uint16_t route_holddown;

	// Flush: Amount of time that must pass before the route is removed completely from the routing table:
	// Default 240s
	uint16_t route_flush;
} rip_timers_t;

// Daemon Settings Structure:
typedef struct xripd_settings_t {
	
	// Sockets:
	uint8_t sd; 			// Socket Descriptor (for inbound RIP Packets)
	uint8_t nlsd;			// Netlink Socket Descriptor (for route table manipulation)

	uint8_t passive_mode;		// Enable Passive Flag (aka do not advertise on net)
	struct sockaddr_in self_ip;	// Self IP of interface daemon is bound to. Do not accept inbound rip updates when source = self_ip (loop avoidance)
	
	// Interfaces:
	char iface_name[IFNAMSIZ]; 	// Human String for an interface, ie. "eth3" or "enp0s3"
	uint8_t iface_index; 		// Kernel index id for interface

	// RIB:
	struct xripd_rib_t *xripd_rib;		// Pointer to RIB
	int p_rib_in[2];		// Pipe for Listener -> RIB
	
	// Filter:
	char filter_file[64];		// Filename for the filterfile
	uint8_t filter_mode;		// Whitelist or Blacklist?

	// Timers:
	rip_timers_t rip_timers;
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
