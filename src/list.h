/**
 * list management
 *
 * +------+  +-----_+
 * | list |  | list |
 * | next -> |
 */

#define MAX_LINES 1024

struct string_buf {
	char **buf;
	int n;
	int m;
};

struct list {
	struct list* next;
	struct string_buf s;
	char buf[MAX_LINES][256]; // TODO dynamic.
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
