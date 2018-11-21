#include "rib-ll.h"

// Comparison return values:
#define LL_CMP_NO_MATCH 0x00
#define LL_CMP_WORSE_METRIC 0x01
#define LL_CMP_BETTER_METRIC 0x02
#define LL_CMP_SAME_METRIC_SAME_NEIGH 0x03
#define LL_CMP_SAME_METRIC_DIFF_NEIGH 0x04
#define LL_CMP_INFINITY_MATCH 0x05

// Wrap the rib_entry_t data into a singularly linked list struct:
// 
//  +-+-+-+-+-+-+-+-+-+-+       +-+-+-+-+-+-+-+-+-+-+
//  | rib_entry | *next | --->  | rib_entry | *next |
//  +-+-+-+-+-+-+-+-+-+-+       +-+-+-+-+-+-+-+-+-+-+

typedef struct rib_ll_node_t {
	rib_entry_t entry;
	struct rib_ll_node_t *next;
} rib_ll_node_t;

// Global pointer to the head of the list
rib_ll_node_t *head;

// Not really needed?
int rib_ll_init() {
	head = NULL;
	return 0;
}

// Responsible for the creation of a brand new rib_ll_node
// Join onto the end of the *last node:
static int rib_ll_new_node(rib_ll_node_t *new, const rib_entry_t *in_entry, rib_ll_node_t *last) {

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

void rib_ll_destroy_rib() { 

	rib_ll_node_t *cur = head;
	rib_ll_node_t *next = cur->next;

	// Iterate over linked list:
	while ( cur != NULL ) {
		free(cur);
		cur = next;
		next = cur->next;
	}
}

static int rib_ll_node_compare(const rib_entry_t *in_entry, const rib_ll_node_t *cur) {

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

// Given pointer to character buffer with a size (count * rib_entry_t)
// Dump our rib into the buffer
int rib_ll_serialise_rib(char *buf, const uint32_t *count) {

#if XRIPD_DEBUG == 1
	fprintf(stderr, "[l-list]: Recieved request to serialise RIB into byte sequence.\n");
#endif

	rib_ll_node_t *cur = head;
	rib_entry_t *cur_rib;
	int index = 0;

	// Ensure no overflow:
	while ( (index <= *count) && cur != NULL  ) {

		cur_rib = (rib_entry_t*)(buf + (sizeof(rib_entry_t) * index));
		memcpy(cur_rib, &(cur->entry), sizeof(rib_entry_t));

		index++;
		cur = cur->next;
	}
	return index;
}

// Evaluate in_entry against our current RIB
// Potentially return ins_route and/or del_route as return rib_entry_t types
// which are used to add/delete desired routes from the kernel table:
int rib_ll_add_to_rib(int *route_ret, const rib_entry_t *in_entry, rib_entry_t *ins_route, rib_entry_t *del_route, int *rib_inc) {

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
			// Prepare ins_route, and return:
			// copy_rib_entry(in_entry, ins_route);
			memcpy(ins_route, in_entry, sizeof(rib_entry_t));
			(*rib_inc)++;
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
						// Edge case, if metric is INFINITY, then technically we need to add a new route, not replace
						if (ntohl(cur->entry.rip_msg_entry.metric) == RIP_METRIC_INFINITY) {
							*route_ret = RIB_RET_INSTALL_NEW;
						} else {
							*route_ret = RIB_RET_REPLACE;
						}

						memcpy(&(cur->entry), in_entry, sizeof(rib_entry_t));

						// Return ins_route as our route to replace:
						memcpy(ins_route, in_entry, sizeof(rib_entry_t));
						return 0;
				}
			}

			// If we reach this branch, there was no match, create new entry:
#if XRIPD_DEBUG == 1
			fprintf(stderr, "[l-list]: New Route, Appending to linked list.\n");
#endif
			rib_ll_node_t *new = NULL;
			rib_ll_new_node(new, in_entry, last);
			// Prepare ins_route, and return:
			//copy_rib_entry(in_entry, ins_route);
			memcpy(ins_route, in_entry, sizeof(rib_entry_t));
			// Route RIB Size increase by 1:
			(*rib_inc)++;
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
#if XRIPD_DEBUG == 1
					fprintf(stderr, "[l-list]: Route has been invalidated. \n");
#endif
					// Replace the entry in the rib with our invalidated in_entry
					memcpy(&(cur->entry), in_entry, sizeof(rib_entry_t));

					// Return with our invalidated route, ready to process:
					memcpy(del_route, in_entry, sizeof(rib_entry_t));
					*route_ret = RIB_RET_INVALIDATE;
					return 0;
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
int rib_ll_remove_expired_entries(const rip_timers_t *timers, int *delcount) {

	time_t now = time(NULL);
	time_t expiration_time = now - timers->route_invalid;
	time_t gc_time = now - timers->route_flush;
	rib_ll_node_t *cur = head;
	rib_ll_node_t *last = head;
	rib_ll_node_t *delnode;

	// Iterate over our linked list:
	while ( cur != NULL ) {

		// Do we need to INVALIDATE (Set metric to RIP_METRIC_INFINITY) the route, ie:
		// 
		// Is the route learnt remotely, but we have not received a recent advertisement for it within the timeout period?
		// AND the route is not already invalidated
		// BUT it's not yet time to purge it out of the table (RIP_ROUTE_GC_TIMEOUT):
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

		// OR Do we need to delete the route completely out of the RIB:
		//
		// IE. Remote OR Local route, if we are definitely at RIP_METRIC_INFINITY
		// and we've hit the garbage collection timeout, let's delete the route:
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
				(*delcount)++;
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
				(*delcount)++;
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

// Traverse datastructure for RIB_ORIGIN_LOCAL routes
// which have a recv_time timestamp NOT EQUAL to the last netlink run
// This means that the local route does not exist in the local kernel table anymore
// Set metric to infinity so that it can be deleted eventually.
// Return 1 if any routes were invalidated:
int rib_ll_invalidate_expired_local_routes(time_t last_run) {

	rib_ll_node_t *cur = head;
	int ret = 0;

#if XRIPD_DEBUG == 1
	char ipaddr[16];
	fprintf(stderr, "[l-list]: Looking to invalidate any expired routes.\n");
#endif

	// Traverse our linkedlist from head to end
	while (cur != NULL) {

		// If it's a local route that's recv_time looks out of date
		// and is not already expired:
		if (cur->entry.origin == RIB_ORIGIN_LOCAL &&
		cur->entry.recv_time != last_run &&
		cur->entry.rip_msg_entry.metric == 0 ) {
#if XRIPD_DEBUG == 1
			inet_ntop(AF_INET, &(cur->entry.rip_msg_entry.ipaddr), ipaddr, sizeof(ipaddr));
			fprintf(stderr, "[l-list]: Expired Local Route for %s.\n", ipaddr);
#endif
			// Invalidate:
			cur->entry.rip_msg_entry.metric = htonl(RIP_METRIC_INFINITY);
			ret = 1;
		}

		cur = cur->next;
	}

	return ret;
}

// Dump our rib into stderr for debugging purposes:
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
