#ifndef XRIPD_RIB_NULL_H
#define XRIPD_RIB_NULL_H

#include "xripd.h"
#include "xripd-rib.h"

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

int rib_null_add_to_rib(rip_rib_entry_t *in_entry);

#endif
