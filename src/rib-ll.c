#include "rib-ll.h"

// Comparison return values:
#define LL_CMP_NO_MATCH 0x00
#define LL_CMP_WORSE_METRIC 0x01
#define LL_CMP_BETTER_METRIC 0x02
#define LL_CMP_SAME_METRIC_SAME_NEIGH 0x03
#define LL_CMP_SAME_METRIC_DIFF_NEIGH 0x04
#define LL_CMP_INFINITY_MATCH 0x05

typedef struct rib_ll_node_t {
	rib_entry_t entry;
	struct rib_ll_node_t *next;
} rib_ll_node_t;

rib_ll_node_t *head;

int rib_ll_init() {
	//head = (rib_ll_node_t*)malloc(sizeof(rib_ll_node_t));
	//memset(head, 0, sizeof(rib_ll_node_t));
	//head->next = NULL;

	head = NULL;
	return 0;
}

int rib_ll_new_node(rib_ll_node_t *new, rib_entry_t *in_entry, rib_ll_node_t *last) {

	new = (rib_ll_node_t*)malloc(sizeof(rib_ll_node_t));
	memset(new, 0, sizeof(rib_ll_node_t));
	memcpy(&(new->entry), in_entry, sizeof(rib_entry_t));
	new->next = NULL;

	// If input is NOT head:
	if ( last != NULL ) {
		last->next = new;
	}
	return 0;
}

int rib_ll_node_compare(rib_entry_t *in_entry, rib_ll_node_t *cur) {

	// Check for IP/Subnet First:
	if ( (in_entry->rip_msg_entry.ipaddr == cur->entry.rip_msg_entry.ipaddr) &&
		(in_entry->rip_msg_entry.subnet == cur->entry.rip_msg_entry.subnet) ) {

		// Check for metric value:
		if ( ntohl(in_entry->rip_msg_entry.metric) < RIP_METRIC_INFINITY ) {

			// Worse (Higher) metric:
			if ( ntohl(in_entry->rip_msg_entry.metric) > ntohl(cur->entry.rip_msg_entry.metric) ) {
				return LL_CMP_WORSE_METRIC;

			// Equal metric:
			} else if ( ntohl(in_entry->rip_msg_entry.metric) == ntohl(cur->entry.rip_msg_entry.metric) ) {
				
				// Advertised from the same neighbour:
				if ( in_entry->recv_from.sin_addr.s_addr == cur->entry.recv_from.sin_addr.s_addr ) {					
					return LL_CMP_SAME_METRIC_SAME_NEIGH;
				} else {
					return LL_CMP_SAME_METRIC_DIFF_NEIGH;
				}

			// Better (Lower) metric:
			} else {
				return LL_CMP_BETTER_METRIC;
			}

		// Infinity:
		} else {
			if ( in_entry->recv_from.sin_addr.s_addr == cur->entry.recv_from.sin_addr.s_addr ) {					
				return LL_CMP_INFINITY_MATCH;
			} else {
				return LL_CMP_NO_MATCH;
			}
		}
	} else {
		return LL_CMP_NO_MATCH;
	}
}

int rib_ll_add_to_rib(rib_entry_t *in_entry) {

	rib_ll_node_t *cur = head;
	rib_ll_node_t *last = head;

#if XRIPD_DEBUG == 1
	fprintf(stderr, "[l-list]: Recieved Metric = %d\n", ntohl(in_entry->rip_msg_entry.metric));
#endif

	// Positive metric rip message:
	if ( ntohl(in_entry->rip_msg_entry.metric) < RIP_METRIC_INFINITY ) {
		// First entry in our linked list:
		if ( cur == NULL ) {
			head = (rib_ll_node_t*)malloc(sizeof(rib_ll_node_t));
			memcpy(&(head->entry), in_entry, sizeof(rib_entry_t));
			head->next = NULL;
			return 0;
			
		// Linked List already has atleast one entry:
		} else {

			// Loop through each entry of the linked list until the end:
			while ( cur != NULL ) {

				int ret = rib_ll_node_compare(in_entry, cur);

				switch (ret) {
					// No match, iterate on linked list:
					case LL_CMP_NO_MATCH:
						last = cur;
						cur = cur->next;
						break;

					case LL_CMP_WORSE_METRIC:
#if XRIPD_DEBUG == 1
						fprintf(stderr, "[l-list]: Node:%p Worse Metric, NOT installing.\n", cur);
#endif
						return 0;

					case LL_CMP_SAME_METRIC_DIFF_NEIGH:
#if XRIPD_DEBUG == 1
						fprintf(stderr, "[l-list]: Node:%p Different neighbour, same metric. NOT installing.\n", cur);
#endif
						return 0;

					case LL_CMP_SAME_METRIC_SAME_NEIGH:
#if XRIPD_DEBUG == 1
						fprintf(stderr, "[l-list]: Node:%p Same neighbour, same metric. Updating recv_time\n", cur);
#endif
						cur->entry.recv_time = in_entry->recv_time;
						return 0;

					case LL_CMP_BETTER_METRIC:
#if XRIPD_DEBUG == 1
						fprintf(stderr, "[l-list]: Node:%p Better route, INSTALLING.\n", cur);
#endif
						// Better metric, let's replace existing rib entry:
						memcpy(&(cur->entry), in_entry, sizeof(rib_entry_t));
						return 0;
				}
			}

			// If we reach this branch, there was no match, create new entry:
#if XRIPD_DEBUG == 1
			fprintf(stderr, "[l-list]: New Route, Appending to linked list.\n");
#endif
			rib_ll_node_t *new = NULL;
			rib_ll_new_node(new, in_entry, last);
			return 0;
		}

	// Infinity metric:
	} else {
#if XRIPD_DEBUG == 1
		fprintf(stderr, "[l-list]: Infinity Metric Route Received.\n");
#endif
		while ( cur != NULL ) {

			int ret = rib_ll_node_compare(in_entry, cur);

			switch (ret) {
				// No match, iterate on linked list:
				case LL_CMP_NO_MATCH:
					last = cur;
					cur = cur->next;
					break;
				case LL_CMP_INFINITY_MATCH:
					
					// First node?:
					if ( cur == head ) {
#if XRIPD_DEBUG == 1
						fprintf(stderr, "[l-list]: Infinity match for head node. Head to be removed.\n");
#endif
						head = cur->next;
						free(cur);
						return 0;
					// Another node?:
					} else {
#if XRIPD_DEBUG == 1
						fprintf(stderr, "[l-list]: Infinity match for node %p. Node to be removed.\n", cur);
#endif
						last->next = cur->next;
						free(cur);
						return 0;
					}
			}
		}
#if XRIPD_DEBUG == 1
		fprintf(stderr, "[l-list]: No route match for Infinity Metric Entry. Ignored.\n");
#endif
		return 1;
	}
}

int rib_ll_dump_rib() {

	rib_ll_node_t *cur = head;

	fprintf(stderr, "[l-list]: Start RIB Dump\n");

	char ipaddr[16];
	char subnet[16];
	char nexthop[16];
	
	while ( cur != NULL ) {
		inet_ntop(AF_INET, &(cur->entry.rip_msg_entry.ipaddr), ipaddr, sizeof(ipaddr));
		inet_ntop(AF_INET, &(cur->entry.rip_msg_entry.subnet), subnet, sizeof(subnet));
		inet_ntop(AF_INET, &(cur->entry.recv_from.sin_addr.s_addr), nexthop, sizeof(nexthop));
		fprintf(stderr, "[l-list]: Node: %p IP: %s %s NH: %s Metric: %02d Timestamp: %lld Next: %p\n", 
				cur, ipaddr, subnet, nexthop, ntohl(cur->entry.rip_msg_entry.metric), (long long)cur->entry.recv_time, cur->next);
		cur = cur->next;
	}

	fprintf(stderr, "[l-list]: End RIB Dump\n");
	return 0;
}
