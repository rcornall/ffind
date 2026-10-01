/**
 * list management
 */

#include <list.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/wait.h>

struct list* list_init(void)
{
	struct list *l = calloc(1, sizeof(struct list));
	if (l == NULL)
		return NULL;

	// line 0 is filter line.
	l->sel_line = 1;
	l->visible_lines = 0;
	l->view_top = 1;
	l->filter_len = FILTER_PREFIX_LEN;
	strcpy(l->filter, FILTER_PREFIX);

	l->next = NULL;

	return l;
}

struct list* list_push(struct list* l)
{
	struct list *new = list_init();
	if (new == NULL)
		return NULL;

	struct list *tmp = l;
	while (tmp->next) {
		tmp = tmp->next;
	}

	tmp->next = new;

	return new;
}

static void list_free(struct list* l)
{
	for (int i = 0; i < l->total_lines; i++)
		free(l->buf[i]);
	free(l->buf);
	free(l->map_filtered_to_line);
	free(l->score);
	free(l);
}

void list_drop(struct list* l)
{
	struct list* tmp = l;

	if (tmp->next == NULL) {
		list_free(tmp);
		return;
	}

	struct list *last;
	while (tmp->next) {
		last = tmp;
		tmp = tmp->next;
	}

	list_free(tmp);
	last->next = NULL;
}

int list_run(struct list *l, char *const argv[])
{
	int fds[2];
	if (pipe(fds) < 0) {
		fprintf(stderr, "pipe: %s\n", strerror(errno));
		return -1;
	}

	pid_t pid = fork();
	if (pid < 0) {
		fprintf(stderr, "fork: %s\n", strerror(errno));
		close(fds[0]);
		close(fds[1]);
		return -1;
	}

	if (pid == 0) {
		dup2(fds[1], STDOUT_FILENO);
		close(fds[0]);
		close(fds[1]);
		execvp(argv[0], argv);
		fprintf(stderr, "Failed to run: %s: %s\n", argv[0], strerror(errno));
		_exit(127);
	}
	close(fds[1]);

	FILE *fp = fdopen(fds[0], "r");
	if (fp == NULL) {
		close(fds[0]);
		waitpid(pid, NULL, 0);
		return -1;
	}

	char *line = NULL;
	size_t sz = 0;
	ssize_t len;
	while ((len = getline(&line, &sz, fp)) != -1) {
		if (len > 0 && line[len - 1] == '\n')
			line[len - 1] = '\0';

		if (l->total_lines == l->cap) {
			int cap = l->cap ? l->cap * 2 : 1024;
			char **buf = realloc(l->buf, cap * sizeof(char *));
			if (buf == NULL)
				break;
			l->buf = buf;
			l->cap = cap;
		}

		// hand ownership of line to the list, getline allocates a new one.
		l->buf[l->total_lines++] = line;
		line = NULL;
		sz = 0;
	}
	free(line);
	fclose(fp);
	waitpid(pid, NULL, 0);

	// index 0 is the filter line, results are 1-based.
	l->map_filtered_to_line = malloc((l->total_lines + 1) * sizeof(int));
	l->score = calloc(l->total_lines + 1, sizeof(int));
	if (l->map_filtered_to_line == NULL || l->score == NULL)
		return -1;
	for (int i = 0; i < l->total_lines; i++)
		l->map_filtered_to_line[i+1] = i;
	l->visible_lines = l->total_lines;

	return l->total_lines;
}

void list_destroy(struct list* l)
{
	while (l) {
		struct list *next = l->next;
		list_free(l);
		l = next;
	}
}
