#ifndef XRIPD_ROUTE_H
#define XRIPD_ROUTE_H

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

int init_netlink(xripd_settings_t *xripd_settings);
int del_netlink(xripd_settings_t *xripd_settings);
int netlink_install_new_route(xripd_settings_t *xripd_settings);

#endif
