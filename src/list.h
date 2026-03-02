/**
 * list management
 *
 * +------+  +-----_+
 * | list |  | list |
 * | next -> |
 */

#define MAX_LINES 395000
#define MAX_LINE_LEN 200
#define MAX_FILTERS 16

struct string_buf {
	char **buf;
	int n;
	int m;
};

struct list {
	struct list* next;
	struct string_buf s;
	char buf[MAX_LINES][MAX_LINE_LEN + MAX_FILTERS]; // TODO dynamic.
	int map_filtered_to_line[MAX_LINES];
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
 * Run cmd and fill list buf with stdoutput.
 */
int list_popen(struct list *l, char *cmd);

/**
 * destroy list.
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
