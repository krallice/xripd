#ifndef XRIPD_RIB_OUT_H
#define XRIPD_RIB_OUT_H

#include "xripd.h"
#include "rib.h"
#include "rib-ctl.h"

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

// Unix Domain Sockets:
#include <sys/socket.h>
#include <sys/un.h>

// Select():
#include <sys/select.h>
#include <pthread.h>

// Entry point for our rib-out thread
// Responsible for retrieving all routes from the rib
// and sending them via a Unix Domain socket to the daemon process
void *rib_out_spawn(void *xripd_settings);

#endif
