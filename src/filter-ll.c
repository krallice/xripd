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
static void destroy_filter_list(filter_list_t *fl) {

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
static filter_node_t *init_filter_node(uint32_t addr, uint32_t mask) {

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

static int filter_route_blacklist(filter_t *f, uint32_t *addr, uint32_t *mask) {
	
	// Assign cur iterator:
	filter_list_t *fl = f->filter_list;
	filter_node_t *cur = fl->head;

#if XRIPD_DEBUG == 1
	char ipaddr[16];
	char subnet[16];
	memset(ipaddr, 0, 16);
	memset(subnet, 0, 16);

	inet_ntop(AF_INET, addr, ipaddr, sizeof(ipaddr));
	inet_ntop(AF_INET, mask, subnet, sizeof(ipaddr));
	fprintf(stderr, "[filter]: Running %s %s through filter.\n", ipaddr, subnet);
#endif

	// Iterate over filter:
	while (cur != NULL) {
		// We have a match?
		if (cur->ipaddr == *addr) {
			if (cur->netmask == *mask) {
#if XRIPD_DEBUG == 1
				fprintf(stderr, "[filter]: Filter match for %s %s. ROUTE DENIED.\n", ipaddr, subnet);
#endif
				return XRIPD_FILTER_RESULT_DENY;
			}
		}
		// Iterate:
		cur = cur->next;
	}

#if XRIPD_DEBUG == 1
	fprintf(stderr, "[filter]: No Match for %s %s. Route Allowed.\n", ipaddr, subnet);
#endif
	return XRIPD_FILTER_RESULT_ALLOW;
}

static int filter_route_whitelist(filter_t *f, uint32_t *addr, uint32_t *mask) {
	
	// Assign cur iterator:
	filter_list_t *fl = f->filter_list;
	filter_node_t *cur = fl->head;

#if XRIPD_DEBUG == 1
	char ipaddr[16];
	char subnet[16];
	memset(ipaddr, 0, 16);
	memset(subnet, 0, 16);

	inet_ntop(AF_INET, addr, ipaddr, sizeof(ipaddr));
	inet_ntop(AF_INET, mask, subnet, sizeof(ipaddr));
	fprintf(stderr, "[filter]: Running %s %s through filter.\n", ipaddr, subnet);
#endif

	// Iterate over filter:
	while (cur != NULL) {
		// We have a match?
		if (cur->ipaddr == *addr) {
			if (cur->netmask == *mask) {
#if XRIPD_DEBUG == 1
				fprintf(stderr, "[filter]: Filter match for %s %s. Route Allowed.\n", ipaddr, subnet);
#endif
				return XRIPD_FILTER_RESULT_ALLOW;;
			}
		}
		// Iterate:
		cur = cur->next;
	}

#if XRIPD_DEBUG == 1
	fprintf(stderr, "[filter]: No Match for %s %s. ROUTE DENIED.\n", ipaddr, subnet);
#endif
	return XRIPD_FILTER_RESULT_DENY;
}

// Run our route past our filter
// This is a simple conditional function call
// depending on which 'mode' the filter is running in:
int filter_route(filter_t *f, uint32_t addr, uint32_t mask) {

	int res;

	if (f->filter_mode == XRIPD_FILTER_MODE_BLACKLIST) {
		res = filter_route_blacklist(f, &addr, &mask);
	} else if (f->filter_mode == XRIPD_FILTER_MODE_WHITELIST) {
		res = filter_route_whitelist(f, &addr, &mask);
	} else {
		res = XRIPD_FILTER_RESULT_DENY;
	}

	return res;
}

// Import filter from filename:
int import_filter_from_file(filter_t *f, const char *filename) {

	FILE *fp;
	char *line = NULL;
	size_t len = 0;
	size_t read = 0;

	uint32_t uaddr = 0;
	uint32_t umask = 0;

	// Open file or bomb out:
	fp = fopen(filename, "r");
	if (fp == NULL) {
		fprintf(stderr, "[filter]: No file found by name %s\n", filename);
		return 1;
	}
	
#if XRIPD_DEBUG == 1
	fprintf(stderr, "[filter]: Loading filter from filter file: %s\n", filename);
#endif
	// Read each line:
	while ((read = getline(&line, &len, fp)) != -1) {
		char *token;
		// token = address
		token = strtok(line, " ");
		while (token != NULL) {

#if XRIPD_DEBUG == 1
			fprintf(stderr, "[filter]: Filter Rule Address: %s\n", token);
#endif
			inet_pton(AF_INET, token, &uaddr);

			// token == subnet?
			token = strtok(NULL, " \n");
			if ( token != NULL ) {
#if XRIPD_DEBUG == 1
				fprintf(stderr, "[filter]: Filter Rule Mask: %s\n", token);
				
#endif
				// If we get to here, we are good to create a filter list:
				inet_pton(AF_INET, token, &umask);
				append_to_filter_list(f, uaddr, umask);

				// Loop (token = NULL):
				token = strtok(NULL, " ");
			} else {
				fprintf(stderr, "[filter]: Error with filter file format.\n");
				fclose(fp);
				return 1;
			}
		}
	}

	fclose(fp);
	if (line) {
		free(line);
	}

	return 0;
}
