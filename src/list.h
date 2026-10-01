#ifndef LIST_H
#define LIST_H

/**
 * list management
 *
 * +------+  +-----_+
 * | list |  | list |
 * | next -> |
 */

#define FILTER_PREFIX "filter: "
#define FILTER_PREFIX_LEN ((int)sizeof(FILTER_PREFIX) - 1)

struct list {
	struct list* next;
	char **buf;
	int total_lines;
	int cap;
	int *map_filtered_to_line;
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
 * Run argv (without a shell) and fill list buf with stdoutput.
 */
int list_run(struct list *l, char *const argv[]);

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
