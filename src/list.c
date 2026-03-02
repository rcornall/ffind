/**
 * list management
 */

#include <list.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

struct list* list_init(void)
{
	struct list *l = malloc(sizeof(struct list));
	if (l == NULL)
		return NULL;

	memset(l, 0, sizeof(struct list));
	// line 0 is filter line.
	l->sel_line = 1;
	l->visible_lines = 0;
	l->view_top = 1;
	l->filter_len = sizeof("filter: ") - 1;
	strncpy(l->filter, "filter: ", sizeof("filter: "));

	for (int i = 0; i < MAX_LINES-1; i++) {
		l->buf[i][MAX_LINE_LEN] |= 0x1;
		l->map_filtered_to_line[i+1] = i;
	}

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

int list_popen(struct list *l, char *cmd)
{
	FILE *fp = popen(cmd, "r");
	if (fp == NULL) {
		printf("Failed to run: %s: %s\n", cmd, strerror(errno));
		return -1;
	}

	char line[MAX_LINE_LEN];
	int total_lines = 0;
	while (fgets(line, sizeof(line), fp) && total_lines < MAX_LINES) {
		strncpy(l->buf[total_lines], line, sizeof(line));
		total_lines++;
	}
	fclose(fp);
	fp = NULL;

	return total_lines;
}

void list_destroy(struct list* l)
{
	// todo
}
