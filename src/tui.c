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

struct tui_window* tui_init(bool autosize, int rows, int cols,
			    int x1, int y1, int x2, int y2)
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

	// 1. Create a Pad instead of a Window
	// A pad is (Height, Width). We make it tall enough for the file.
	int pad_rows = (rows > 0) ? rows : LINES;
	WINDOW *pad = newpad(pad_rows, COLS);
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

void tui_write_lines(struct tui_window *t, char *lines, int line_width, int n, int offset, int start)
{
	for (int i=0; i< n; i++) {
		mvwprintw(t->w, i+offset, 0, "%s", &lines[i * line_width]);
	}
	if (start >= 0)
		t->curr_row = start;
	prefresh(t->w, t->curr_row, t->curr_col, t->y1, t->x1, t->y2, t->x2);
}

int tui_write_file(struct tui_window *t, char *file)
{
	FILE* fp = fopen(file, "r");

	char line[256];
	int total_lines = 0;
	if (fp) {
		while (fgets(line, sizeof(line), fp) && total_lines < getmaxy(t->w)) {
			mvwprintw(t->w, total_lines, 0, "%s", line);
			total_lines++;
		}
	} else {
		wprintw(t->w, "File not found.");
		return 0;
	}

	fclose(fp);
	fp = NULL;

	prefresh(t->w, t->curr_row, t->curr_col, t->y1, t->x1, t->y2, t->x2);
	return total_lines;
}

void tui_scroll_up(struct tui_window *t, int count)
{
	// tui_write_line(t, results[sel_line], sel_line, -1, true);
}

void tui_scroll_down(struct tui_window *t, int count)
{
}

void tui_highlight_line(struct tui_window *t, int line)
{
}
