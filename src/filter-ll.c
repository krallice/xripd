#include "filter-ll.h"

// Create a brand new list:
filter_list_t *init_filter_list(void) {

	// Allocate on the heap:
	filter_list_t *filter_list = (filter_list_t*)malloc(sizeof(*filter_list));
	memset(filter_list, 0, sizeof(*filter_list));

	filter_list->head = NULL;
	filter_list->tail = NULL;

	return filter_list;
}

// Destroy the filter_list_t struct:
void destroy_filter_list(filter_list_t *fl) {

	// Todo: code to traverse the filter nodes
	
	filter_node_t *cur = fl->head;
	filter_node_t *next = cur;

	// De-alloc every filter_node:
	while ( cur != NULL ) {
		next = cur->next;
		free(cur);
		cur = next;
	}
}

// Destroy our filter:
void destroy_filter(filter_t *f) {
	destroy_filter_list(f->filter_list);
	free(f);
}

// Initialise our filter struct
filter_t *init_filter(uint8_t mode) {

	// Allocate on the heap:
	filter_t *filter = (filter_t*)malloc(sizeof(*filter));
	memset(filter, 0, sizeof(*filter));

	// Create our filter list and link to our struct:
	filter->filter_list = init_filter_list();
	filter->filter_mode = mode;
	return filter;
}

// Create a brand new node:
filter_node_t *init_filter_node(uint32_t addr, uint32_t mask) {

	// Init and zeroise:
	filter_node_t *n = (filter_node_t*)malloc(sizeof(*n));
	memset(n, 0, sizeof(*n));

	n->ipaddr = addr;
	n->netmask = mask;
	n->next = NULL;

	return n;
}

void dump_filter_list(filter_t *f) {

	// Assign cur iterator:
	filter_list_t *fl = f->filter_list;
	filter_node_t *cur = fl->head;

	char ipaddr[16];
	char subnet[16];
	char mode[32];

	if (f->filter_mode == XRIPD_FILTER_MODE_BLACKLIST) {
		strcpy(mode, "Blacklist Filter");
	} else if (f->filter_mode == XRIPD_FILTER_MODE_WHITELIST) {
		strcpy(mode, "Whitelist Filter");
	} else {
		strcpy(mode, "ERROR: UNKNOWN FILTER");
	}

	fprintf(stderr, "[filter]: Dumping Filter List.\n");
	fprintf(stderr, "[filter]: Filter Type: %s.\n", mode);

	while (cur != NULL) {
		inet_ntop(AF_INET, &(cur->ipaddr), ipaddr, sizeof(ipaddr));
		inet_ntop(AF_INET, &(cur->netmask), subnet, sizeof(ipaddr));
		fprintf(stderr, "[filter]: Dump: Filter Node: %p Network: %s %s\n", cur, ipaddr, subnet);
		cur = cur->next;
	}
	return;
}

// Given an input of address and mask, append to our filter list:
int append_to_filter_list(filter_t *f, uint32_t addr, uint32_t mask) {

#if XRIPD_DEBUG == 1
	char ipaddr[16];
	char subnet[16];

	inet_ntop(AF_INET, &addr, ipaddr, sizeof(ipaddr));
	inet_ntop(AF_INET, &mask, subnet, sizeof(ipaddr));
	fprintf(stderr, "[filter]: Appending to filter %s %s\n", ipaddr, subnet);
#endif

	filter_list_t *fl = f->filter_list;

	// First entry in our linked list:
	if (fl-> tail == NULL ) {
		fl->head = init_filter_node(addr, mask);
		fl->tail = fl->head;
		return 0;
	} else {

		// Allocate and assign to the last node in the list:
		fl->tail->next = init_filter_node(addr, mask);
		fl->tail = fl->tail->next;
		return 0;
	}
}
