#include "rib-ll.h"

typedef struct rib_ll_node_t {
	rib_entry_t entry;
	struct rib_ll_node_t *next;
} rib_ll_node_t;

rib_ll_node_t *head;

int rib_ll_init() {
	head = (rib_ll_node_t*)malloc(sizeof(rib_ll_node_t));
	memset(head, 0, sizeof(rib_ll_node_t));

	head->next = NULL;
	return 0;
}

int rib_ll_add_to_rib(rib_entry_t *in_entry) {

	rib_ll_node_t *cur = head;
	rib_ll_node_t *new;

	if ( cur == NULL ) {
		new = (rib_ll_node_t*)malloc(sizeof(rib_ll_node_t));
		memcpy(&(new->entry), in_entry, sizeof(rib_entry_t));
		cur->next = new;
		return 0;
	} else {
		// Fast forward to end of linked_list:
		while ( cur->next != NULL ) {
			cur = cur->next;
		}

		new = (rib_ll_node_t*)malloc(sizeof(rib_ll_node_t));
		memcpy(&(new->entry), in_entry, sizeof(rib_entry_t));
		cur->next = new;
		return 0;
	}
	return 1;
}

int rib_ll_dump_rib() {

	rib_ll_node_t *cur = head;

	fprintf(stderr, "[l-list]: Start RIB Dump\n");

	char ipaddr[16];
	inet_ntop(AF_INET, &(cur->entry.rip_msg_entry.ipaddr), ipaddr, sizeof(ipaddr));
	fprintf(stderr, "[l-list]: Node: %p IP: %s Metric: %02d Next: %p\n", cur, ipaddr, ntohl(cur->entry.rip_msg_entry.metric), cur->next);
	cur = cur->next;

	while ( cur != NULL ) {
		inet_ntop(AF_INET, &(cur->entry.rip_msg_entry.ipaddr), ipaddr, sizeof(ipaddr));
		fprintf(stderr, "[l-list]: Node: %p IP: %s Metric: %02d Next: %p\n", cur, ipaddr, ntohl(cur->entry.rip_msg_entry.metric), cur->next);
		cur = cur->next;
	}

	fprintf(stderr, "[l-list]: End RIB Dump\n");
	return 0;
}
