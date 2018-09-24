#include "rib-ll.h"

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

int rib_ll_add_to_rib(rib_entry_t *in_entry) {

	rib_ll_node_t *cur = head;
	rib_ll_node_t *new;

	fprintf(stderr, "[l-list]: RIP Metric Infinity = %d\n", RIP_METRIC_INFINITY);
	fprintf(stderr, "[l-list]: Recieved Metric = %d\n", ntohl(in_entry->rip_msg_entry.metric));

	if ( ntohl(in_entry->rip_msg_entry.metric) < RIP_METRIC_INFINITY ) {
		// First entry in our linked list:
		if ( cur == NULL ) {
			head = (rib_ll_node_t*)malloc(sizeof(rib_ll_node_t));
			memcpy(&(head->entry), in_entry, sizeof(rib_entry_t));
			head->next = NULL;
			return 0;
			
		// Not our first entry:
		} else {
			// Fast forward to last entry of linked_list:
			while ( cur->next != NULL ) {
				cur = cur->next;
			}

			new = (rib_ll_node_t*)malloc(sizeof(rib_ll_node_t));
			memcpy(&(new->entry), in_entry, sizeof(rib_entry_t));
			cur->next = new;
			return 0;
		}
	} else {
#if XRIPD_DEBUG == 1
		fprintf(stderr, "[l-list]: Infinity Metric Route Received. Handler function not yet implemented\n");
#endif
		return 1;
	}
}

int rib_ll_dump_rib() {

	rib_ll_node_t *cur = head;

	fprintf(stderr, "[l-list]: Start RIB Dump\n");

	char ipaddr[16];
	
	while ( cur != NULL ) {
		inet_ntop(AF_INET, &(cur->entry.rip_msg_entry.ipaddr), ipaddr, sizeof(ipaddr));
		fprintf(stderr, "[l-list]: Node: %p IP: %s Metric: %02d Timestamp: %lld Next: %p\n", 
				cur, ipaddr, ntohl(cur->entry.rip_msg_entry.metric), (long long)cur->entry.recv_time, cur->next);
		cur = cur->next;
	}

	fprintf(stderr, "[l-list]: End RIB Dump\n");
	return 0;
}
