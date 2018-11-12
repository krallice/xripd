#ifndef RIB_CTL_H
#define RIB_CTL_H

#include <stdint.h>

// For extensibility:
#define RIB_CTL_HDR_VERSION_1 0x01

// Daemon REQUEST routing table dump from rib:
#define RIB_CTL_HDR_MSGTYPE_REQUEST 0x01

// In reponse to REQUEST, rib will return REPLY messages
// ENDREPLY messages will signify end of response stream
#define RIB_CTL_HDR_MSGTYPE_REPLY 0x02
#define RIB_CTL_HDR_MSGTYPE_ENDREPLY 0x03

// Used for Pre-emptive route changes, advertised gratituiously from 
// rib to daemon. ENDPREEMPT will signify end of stream
#define RIB_CTL_HDR_MSGTYPE_PREEMPT 0x12
#define RIB_CTL_HDR_MSGTYPE_ENDPREEMPT 0x13

typedef struct rib_ctl_hdr_t {
	uint8_t version;
	uint8_t msgtype;
} rib_ctl_hdr_t;

#endif