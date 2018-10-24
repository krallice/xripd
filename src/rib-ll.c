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

// Evaluate in_entry against our current RIB
// Potentially return ins_route and/or del_route as return rib_entry_t types
// which are used to add/delete desired routes from the kernel table:
int rib_ll_add_to_rib(int *route_ret, rib_entry_t *in_entry, rib_entry_t *ins_route, rib_entry_t *del_route) {

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
			copy_rib_entry(in_entry, ins_route);
			*route_ret = RIB_RET_INSTALL_NEW;
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
						*route_ret = RIB_RET_NO_ACTION;
						return 0;

					case LL_CMP_SAME_METRIC_DIFF_NEIGH:
#if XRIPD_DEBUG == 1
						fprintf(stderr, "[l-list]: Node:%p Different neighbour, same metric. NOT installing.\n", cur);
#endif
						*route_ret = RIB_RET_NO_ACTION;
						return 0;

					case LL_CMP_SAME_METRIC_SAME_NEIGH:
#if XRIPD_DEBUG == 1
						fprintf(stderr, "[l-list]: Node:%p Same neighbour, same metric. Updating recv_time\n", cur);
#endif
						cur->entry.recv_time = in_entry->recv_time;
						*route_ret = RIB_RET_NO_ACTION;
						return 0;

					case LL_CMP_BETTER_METRIC:
#if XRIPD_DEBUG == 1
						fprintf(stderr, "[l-list]: Node:%p Better route, INSTALLING.\n", cur);
#endif
						// Better metric, let's replace existing rib entry:
						memcpy(&(cur->entry), in_entry, sizeof(rib_entry_t));
						*route_ret = RIB_RET_REPLACE;
						return 0;
				}
			}

			// If we reach this branch, there was no match, create new entry:
#if XRIPD_DEBUG == 1
			fprintf(stderr, "[l-list]: New Route, Appending to linked list.\n");
#endif
			rib_ll_node_t *new = NULL;
			rib_ll_new_node(new, in_entry, last);
			*route_ret = RIB_RET_INSTALL_NEW;
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
						*route_ret = RIB_RET_DELETE;
						return 0;
					// Another node?:
					} else {
#if XRIPD_DEBUG == 1
						fprintf(stderr, "[l-list]: Infinity match for node %p. Node to be removed.\n", cur);
#endif
						last->next = cur->next;
						free(cur);
						*route_ret = RIB_RET_DELETE;
						return 0;
					}
			}
		}
#if XRIPD_DEBUG == 1
		fprintf(stderr, "[l-list]: No route match for Infinity Metric Entry. Ignored.\n");
#endif
		*route_ret = RIB_RET_NO_ACTION;
		return 1;
	}
}

// Expire out old entries out of the rib:
int rib_ll_remove_expired_entries() {

	time_t now = time(NULL);
	time_t expiration_time = now - RIP_ROUTE_TIMEOUT;
	time_t gc_time = now - RIP_ROUTE_GC_TIMEOUT;
	rib_ll_node_t *cur = head;
	rib_ll_node_t *last = head;
	rib_ll_node_t *delnode;

	// Iterate over our linked list:
	while ( cur != NULL ) {

		// We have not received a recent RIP msg entry for node, we have passed the expiration time
		// but not yet the gc time. Route will be removed from routing table, but will persist in the rib:
		if ( (cur->entry.recv_time < expiration_time) && 
				(cur->entry.recv_time > gc_time) && 
				(ntohl(cur->entry.rip_msg_entry.metric) < RIP_METRIC_INFINITY) &&
				(cur->entry.origin != RIB_ORIGIN_LOCAL) ) {
#if XRIPD_DEBUG == 1
			char ipaddr[16];
			char subnet[16];
			char nexthop[16];
			inet_ntop(AF_INET, &(cur->entry.rip_msg_entry.ipaddr), ipaddr, sizeof(ipaddr));
			inet_ntop(AF_INET, &(cur->entry.rip_msg_entry.subnet), subnet, sizeof(subnet));
			inet_ntop(AF_INET, &(cur->entry.recv_from.sin_addr.s_addr), nexthop, sizeof(nexthop));
			cur->entry.rip_msg_entry.metric = htonl(RIP_METRIC_INFINITY);
			fprintf(stderr, "[l-list]: Node Expired (Metric set to %d): %p IP: %s %s NH: %s Metric: %02d Timestamp: %lld Next: %p -- Current Time: %lld Expiration Time: %lld\n",
					RIP_METRIC_INFINITY, cur, ipaddr, subnet, nexthop, 
					ntohl(cur->entry.rip_msg_entry.metric), (long long)(cur->entry.recv_time), cur->next, (long long)now, (long long)expiration_time);
#endif
			// Increment:
			last = cur;
			cur = cur->next;

		// Need to delete:
		} else if ( cur->entry.recv_time < gc_time && 
				ntohl(cur->entry.rip_msg_entry.metric) >= RIP_METRIC_INFINITY) { 
#if XRIPD_DEBUG == 1
			char ipaddr[16];
			char subnet[16];
			char nexthop[16];
			inet_ntop(AF_INET, &(cur->entry.rip_msg_entry.ipaddr), ipaddr, sizeof(ipaddr));
			inet_ntop(AF_INET, &(cur->entry.rip_msg_entry.subnet), subnet, sizeof(subnet));
			inet_ntop(AF_INET, &(cur->entry.recv_from.sin_addr.s_addr), nexthop, sizeof(nexthop));
			fprintf(stderr, "[l-list]: Node Expired (Deleting from RIB): %p IP: %s %s NH: %s Metric: %02d Timestamp: %lld Next: %p -- Current Time: %lld Expiration Time: %lld\n",
					cur, ipaddr, subnet, nexthop, ntohl(cur->entry.rip_msg_entry.metric), (long long)(cur->entry.recv_time), cur->next, (long long)now, (long long)expiration_time);
#endif
			// Deleting our head?
			if ( cur == head ) {
				// Increment head pointer:
				head = cur->next;

				// Free our current node for deletion, and reset current and last to new head node:
				free(cur);
				cur = head;
				last = head;

			// Deleting a middle node:
			} else {
				// Link our last node to bypass the current node for deletion:
				last->next = cur->next;

				// New pointer for deletion:
				delnode = cur;
				// increment cur:
				cur = cur->next;
				// Delete from memory
				free(delnode);
			}

		// Time is still valid, Do not delete node, just increment:
		} else {
			// Increment:
			last = cur;
			cur = cur->next;
		}
	}
	return 0;
}

int rib_ll_dump_rib() {

	rib_ll_node_t *cur = head;

	fprintf(stderr, "[l-list]: Start RIB Dump\n");

	char ipaddr[16];
	char subnet[16];
	char nexthop[16];

	char origin_string[64];
	
	while ( cur != NULL ) {
		inet_ntop(AF_INET, &(cur->entry.rip_msg_entry.ipaddr), ipaddr, sizeof(ipaddr));
		inet_ntop(AF_INET, &(cur->entry.rip_msg_entry.subnet), subnet, sizeof(subnet));
		inet_ntop(AF_INET, &(cur->entry.recv_from.sin_addr.s_addr), nexthop, sizeof(nexthop));
		if ( cur->entry.origin == RIB_ORIGIN_LOCAL ) {
			strcpy(origin_string, "LOC");
		} else {
			strcpy(origin_string, "REM");
		}
		fprintf(stderr, "[l-list]: RIB Dump: Node: %p IP: %s %s NH: %s Metric: %02d Origin: %s Timestamp: %lld Next: %p\n", 
				cur, ipaddr, subnet, nexthop, ntohl(cur->entry.rip_msg_entry.metric), origin_string, (long long)cur->entry.recv_time, cur->next);
		cur = cur->next;
	}

	fprintf(stderr, "[l-list]: End RIB Dump\n");
	return 0;
}
