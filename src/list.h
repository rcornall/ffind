#ifndef LIST_H
#define LIST_H

/**
 * list management
 *
 * +------+  +-----_+
 * | list |  | list |
 * | next -> |
 */

#include <stdbool.h>

#define FILTER_PREFIX "filter: "
#define FILTER_PREFIX_LEN ((int)sizeof(FILTER_PREFIX) - 1)

struct list {
	struct list* next;
	char **buf;
	int total_lines;
	int cap;
	int *map_filtered_to_line;
	int *score; // filter score per line, higher is better
	// selected line during filtering
	int sel_line;
	int visible_lines;
	int view_top;  // first result index (1-based) currently shown at pad row 1
	char filter[100];
	int filter_len;
};

/**
 * create new list to hold array of strings.
 */
struct list* list_init(void);

/**
 * add another list to the given list.
 */
struct list* list_push(struct list* l);

/**
 * drop the last list.
 */
void list_drop(struct list* l);

/**
 * Run argv (without a shell) and append its stdoutput to list buf.
 * `quiet` drops its stderr, e.g. while the tui is up.
 */
int list_run(struct list *l, char *const argv[], bool quiet);

/**
 * drop all lines, keeping the filter.
 */
void list_clear(struct list *l);

/**
 * destroy list and all lists pushed onto it.
 */
void list_destroy(struct list* l);

/*
 *
 * l = list_init
 *
 * // create new list
 * l2 = l.push()
 *
 * // use it
 * ...
 *
 * l.drop()
 *
 *
 */

#endif
