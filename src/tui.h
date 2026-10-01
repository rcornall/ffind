#ifndef TUI_H
#define TUI_H

/**
 * Terminal UI support api.
 */

#include <ncurses.h>

struct tui_window {
	WINDOW* w;
	/* current row and col to display from internal window lines.
	 * col will be zero typically. row will be starting line number. */
	int curr_row;
	int curr_col;
	/* screen coords: x=horizontal (col), y=vertical (row) */
	int x1; /* left col */
	int y1; /* top row */
	int x2; /* right col */
	int y2; /* bottom row */
};

/*
 * init tui window covering the default screen area.
 */
struct tui_window* tui_init(void);

/*
 * destroy tui instance.
 */
void tui_destroy(struct tui_window* t);

/*
 * end the tui, restoring the terminal.
 */
void tui_end(void);

/*
 * leave the tui temporarily to run another program, and come back.
 * windows need a tui_refresh after resuming.
 */
void tui_suspend(void);
void tui_resume(void);

/**
 * set the screen area the window is shown in.
 */
void tui_set_area(struct tui_window *t, int x1, int y1, int x2, int y2);

/**
 * number of visible rows.
 */
int tui_rows(struct tui_window *t);

/**
 * clear the screen and draw a box around the area x1,y1 - x2,y2, with an
 * optional divider at `split_row` or `split_col` (-1 for none).
 */
void tui_draw_border(int x1, int y1, int x2, int y2, int split_row, int split_col);

/**
 * redraw the whole window, e.g. after returning from another program.
 */
void tui_refresh(struct tui_window *t);

/**
 * write line to the window at `n` and refresh from `start` row.
 */
void tui_write_line(struct tui_window *t, char *line, int n, int start, bool highlight);

/**
 * write a search result line, coloring the filename:linenum: prefix distinctly.
 */
void tui_write_result_line(struct tui_window *t, char *line, int n, int start, bool highlight);

void tui_clear_line(struct tui_window *t, int n, int start);

/**
 * clear the whole window.
 */
void tui_clear(struct tui_window *t);

/**
 * write file contents from line `first` (1-based) to window rows from `offset`.
 * `line` is highlighted. return lines written.
 */
int tui_write_file(struct tui_window *t, char *file, int first, int line, int offset);

#endif
