/**
 * @brief Terminal UI support api.
 */

#include <tui.h>
#include <stdio.h>
#include <stdlib.h>

struct local_env {
	bool init;
	int highlight_color;
};

static struct local_env env;

struct tui_window* tui_init(bool autosize, int cols, int rows,
			    int x1, int x2, int y1, int y2)
{
	if (env.init == false) {
		initscr();
		cbreak();
		noecho();
		keypad(stdscr, TRUE); // Enable arrow keys and others
		curs_set(0);
		env.highlight_color = 1;

		start_color();
		init_pair(env.highlight_color, COLOR_WHITE, COLOR_BLUE);
	}

	struct tui_window* t = malloc(sizeof(struct tui_window));
	if (t == NULL)
		return NULL;

	// 1. Create a Pad instead of a Window
	// A pad is (Height, Width). We make it tall enough for the file.
	WINDOW *pad = newpad(cols, rows);
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
	/* todo reversed */
	t->x2 = LINES-3;
	t->y2 = COLS-3;

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

void tui_write_line(struct tui_window *t, char *line, int n, int start, bool highlight)
{
	if (highlight)
		wattron(t->w, COLOR_PAIR(env.highlight_color));
	mvwprintw(t->w, n, 0, "%s", line);
	if (highlight)
		wattroff(t->w, COLOR_PAIR(env.highlight_color));

	if (start >= 0)
		t->curr_row = start;
	prefresh(t->w, t->curr_row, t->curr_col, t->x1, t->y1, t->x2, t->y2);
}

void tui_write_lines(struct tui_window *t, char *lines, int line_width, size_t n, int offset, int start)
{
	for (int i=offset; i< n; i++) {
		// printf("LINE: %s\n", &lines[i * line_width]);
		mvwprintw(t->w, i, 0, "%s", &lines[i * line_width]);
	}
	t->curr_row = start;
	prefresh(t->w, t->curr_row, t->curr_col, t->x1, t->y1, t->x2, t->y2);
}

int tui_write_file(struct tui_window *t, FILE* fp)
{
	char line[256];
	int total_lines = 0;
	if (fp) {
		while (fgets(line, sizeof(line), fp) && total_lines < t->x2) {
			mvwprintw(t->w, total_lines, 0, "%s", line);
			total_lines++;
		}
		fclose(fp);
	} else {
		wprintw(t->w, "File not found.");
		return 0;
	}

	prefresh(t->w, t->curr_row, t->curr_col, t->x1, t->y1, t->x2, t->y2);
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
