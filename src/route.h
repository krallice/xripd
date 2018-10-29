#ifndef XRIPD_ROUTE_H
#define XRIPD_ROUTE_H

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

#define RTPROT_XRIPD 33

// Init and bind our netlink socket:
int init_netlink(xripd_settings_t *xripd_settings);

// Delete our socket
int del_netlink(xripd_settings_t *xripd_settings);

int netlink_add_local_routes_to_rib(xripd_settings_t *xripd_settings_t);

int netlink_install_new_route(xripd_settings_t *xripd_settings, rib_entry_t *install_rib);
int netlink_delete_new_route(xripd_settings_t *xripd_settings, rib_entry_t *del_entry);

// Helper translation functions between netmask and cidr:
uint32_t cidr_to_netmask_netorder(int cidr);
int netmask_to_cidr(uint32_t netmask);

#endif
