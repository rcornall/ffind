/**
 * list management
 */

#include <list.h>
#include <unistd.h>
#include <stdlib.h>

struct list* list_init(void)
{
	struct list *l = malloc(sizeof(struct list));
	if (l == NULL)
		return NULL;

	l->next = NULL;

	return l;
}

struct list* list_push(struct list* l)
{
	struct list *new = list_init();

	struct list *tmp = l;
	while (tmp->next) {
		tmp = tmp->next;
	}

	tmp->next = new;

	return new;
}

void list_drop(struct list* l)
{
	struct list* tmp = l;

	if (tmp->next == NULL) {
		free (tmp);
		l = NULL;
		return;
	}

	struct list *last;
	while (tmp->next) {
		last = tmp;
		tmp = tmp->next;
	}

	free(tmp);
	last->next = NULL;
}

void list_destroy(struct list* l)
{
	// todo
}
