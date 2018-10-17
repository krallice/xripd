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

int init_netlink(xripd_settings_t *xripd_settings);
int del_netlink(xripd_settings_t *xripd_settings);
int netlink_install_new_route(xripd_settings_t *xripd_settings, rib_entry_t *install_rib);
//int netlink_read_local_routes(xripd_settings_t *xripd_settings_t, rib_entry_t *install_rib);
int netlink_add_local_routes_to_rib(xripd_settings_t *xripd_settings_t);

#endif
