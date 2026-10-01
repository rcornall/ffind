/**
 * @brief Terminal UI support api.
 */

#include <tui.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct local_env {
	bool init;
	int highlight_color;
	int filename_color;
};

static struct local_env env;

struct tui_window* tui_init(void)
{
	if (env.init == false) {
		initscr();
		raw(); // ctrl-c is handled as a key so the terminal gets restored
		noecho();
		curs_set(0);
		env.highlight_color = 1;
		env.filename_color = 2;

		start_color();
		init_pair(env.highlight_color, COLOR_WHITE, COLOR_BLUE);
		init_pair(env.filename_color, COLOR_CYAN, COLOR_BLACK);
		env.init = true;
	}

	struct tui_window* t = malloc(sizeof(struct tui_window));
	if (t == NULL)
		return NULL;

	WINDOW *pad = newpad(LINES, COLS);
	if (pad == NULL) {
		free(t);
		return NULL;
	}

	// getch does implicit refresh which messes with pad.
	// refresh here so getch doesnt clear pad window.
	refresh();

	t->w = pad;
	t->curr_row = 0;
	t->curr_col = 0;
	t->x1 = 2;
	t->y1 = 2;
	t->x2 = COLS-6;
	t->y2 = LINES-6;

	return t;
}

void tui_destroy(struct tui_window* t)
{
	if (t) {
		delwin(t->w);

		free(t);
		t=NULL;
	}

	// refresh?
}

void tui_end(void)
{
	endwin();
}

void tui_suspend(void)
{
	def_prog_mode();
	endwin();
}

void tui_resume(void)
{
	reset_prog_mode();
	refresh();
}

void tui_set_area(struct tui_window *t, int x1, int y1, int x2, int y2)
{
	t->x1 = x1;
	t->y1 = y1;
	t->x2 = x2;
	t->y2 = y2;
}

int tui_rows(struct tui_window *t)
{
	return t->y2 - t->y1 + 1;
}

void tui_draw_border(int x1, int y1, int x2, int y2, int split_row, int split_col)
{
	// one col of padding inside the border.
	int top = y1 - 1, left = x1 - 2, bottom = y2 + 1, right = x2 + 2;

	erase();
	mvhline(top, left, ACS_HLINE, right - left);
	mvhline(bottom, left, ACS_HLINE, right - left);
	mvvline(top, left, ACS_VLINE, bottom - top);
	mvvline(top, right, ACS_VLINE, bottom - top);
	mvaddch(top, left, ACS_ULCORNER);
	mvaddch(top, right, ACS_URCORNER);
	mvaddch(bottom, left, ACS_LLCORNER);
	mvaddch(bottom, right, ACS_LRCORNER);

	if (split_row >= 0) {
		mvhline(split_row, left, ACS_HLINE, right - left);
		mvaddch(split_row, left, ACS_LTEE);
		mvaddch(split_row, right, ACS_RTEE);
	}
	if (split_col >= 0) {
		mvvline(top, split_col, ACS_VLINE, bottom - top);
		mvaddch(top, split_col, ACS_TTEE);
		mvaddch(bottom, split_col, ACS_BTEE);
	}
	refresh();
}

void tui_refresh(struct tui_window *t)
{
	touchwin(t->w);
	prefresh(t->w, t->curr_row, t->curr_col, t->y1, t->x1, t->y2, t->x2);
}

/* write at most `len` chars (all if < 0), stopping at `max_x` so lines never wrap. */
static void put_clipped(WINDOW *w, const char *s, int len, int max_x)
{
	for (int i = 0; s[i] && (len < 0 || i < len) && getcurx(w) < max_x; i++)
		waddch(w, (unsigned char)s[i]);
}

void tui_write_line(struct tui_window *t, char *line, int n, int start, bool highlight)
{
	if (highlight)
		wattron(t->w, COLOR_PAIR(env.highlight_color));

	wmove(t->w, n, 0);
	wclrtoeol(t->w);
	put_clipped(t->w, line, -1, t->x2 - t->x1 + 1);

	if (highlight)
		wattroff(t->w, COLOR_PAIR(env.highlight_color));

	if (start >= 0)
		t->curr_row = start;
	prefresh(t->w, t->curr_row, t->curr_col, t->y1, t->x1, t->y2, t->x2);
}

void tui_write_result_line(struct tui_window *t, char *line, int n, int start, bool highlight)
{
	int max_x = t->x2 - t->x1 + 1;

	wmove(t->w, n, 0);
	wclrtoeol(t->w);

	if (highlight) {
		wattron(t->w, COLOR_PAIR(env.highlight_color));
		put_clipped(t->w, line, -1, max_x);
		wattroff(t->w, COLOR_PAIR(env.highlight_color));
	} else {
		/* color filename:linenum: prefix, then write the rest normally */
		char *p = strchr(line, ':');
		if (p) p = strchr(p + 1, ':');
		if (p) p = strchr(p + 1, ':');
		if (p) {
			int prefix_len = (p + 1) - line;
			wattron(t->w, COLOR_PAIR(env.filename_color));
			put_clipped(t->w, line, prefix_len, max_x);
			wattroff(t->w, COLOR_PAIR(env.filename_color));
			put_clipped(t->w, p + 1, -1, max_x);
		} else {
			put_clipped(t->w, line, -1, max_x);
		}
	}

	if (start >= 0)
		t->curr_row = start;
	prefresh(t->w, t->curr_row, t->curr_col, t->y1, t->x1, t->y2, t->x2);
}

void tui_clear_line(struct tui_window *t, int n, int start)
{
	wmove(t->w, n, 0);
	wclrtoeol(t->w);

	if (start >= 0)
		t->curr_row = start;

	prefresh(t->w, t->curr_row, t->curr_col, t->y1, t->x1, t->y2, t->x2);
}

void tui_clear(struct tui_window *t)
{
	werase(t->w);
	prefresh(t->w, t->curr_row, t->curr_col, t->y1, t->x1, t->y2, t->x2);
}

int tui_write_file(struct tui_window *t, char *file, int first, int line, int offset)
{
	int rows = t->y2 - t->y1 + 1 - offset;
	int max_x = t->x2 - t->x1 + 1;

	for (int row = offset; row < offset + rows; row++) {
		wmove(t->w, row, 0);
		wclrtoeol(t->w);
	}

	FILE* fp = fopen(file, "r");
	if (fp == NULL) {
		mvwprintw(t->w, offset, 0, "File not found.");
		prefresh(t->w, t->curr_row, t->curr_col, t->y1, t->x1, t->y2, t->x2);
		return 0;
	}

	char *buf = NULL;
	size_t sz = 0;
	ssize_t len;
	int n = 0;
	int row = offset;
	while (row < offset + rows && (len = getline(&buf, &sz, fp)) != -1) {
		n++;
		if (n < first)
			continue;

		while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
			buf[--len] = '\0';

		wmove(t->w, row, 0);
		if (n == line)
			wattron(t->w, COLOR_PAIR(env.highlight_color));
		put_clipped(t->w, buf, -1, max_x);
		if (n == line)
			wattroff(t->w, COLOR_PAIR(env.highlight_color));
		row++;
	}
	free(buf);
	fclose(fp);

	prefresh(t->w, t->curr_row, t->curr_col, t->y1, t->x1, t->y2, t->x2);
	return row - offset;
}
