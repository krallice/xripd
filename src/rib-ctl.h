#ifndef RIB_CTL_H
#define RIB_CTL_H

#include <stdint.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "xripd.h"
#include "rib.h"

// For extensibility:
#define RIB_CTL_HDR_VERSION_1 0x01

// Daemon REQUEST routing table dump from rib:
#define RIB_CTL_HDR_MSGTYPE_REQUEST 0x11

// In reponse to REQUEST, rib will return REPLY messages
// ENDREPLY messages will signify end of response stream
#define RIB_CTL_HDR_MSGTYPE_REPLY 0x22
#define RIB_CTL_HDR_MSGTYPE_ENDREPLY 0x23

// Used for Pre-emptive route changes, advertised gratituiously from 
// rib to daemon. ENDPREEMPT will signify end of stream
#define RIB_CTL_HDR_MSGTYPE_UNSOLICITED 0x32
#define RIB_CTL_HDR_MSGTYPE_ENDUNSOLICITED 0x33

// Maximum amount of rib_entry_t messages to carry in a single stream:
#define RIB_CTL_MAX_BUFFER 8

typedef struct rib_ctl_hdr_t {
	uint8_t version;
	uint8_t msgtype;
} rib_ctl_hdr_t;

typedef struct sun_addresses_t {
	int socketfd;
	struct sockaddr_un sockaddr_un_daemon;
	struct sockaddr_un sockaddr_un_rib;
} sun_addresses_t;

#endif
