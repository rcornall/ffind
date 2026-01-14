/**
 * @file
 * @brief Terminal UI support api.
 */

#include <ncurses.h>

struct tui_window {
	WINDOW* w;
	/* current row and col to display from internal window lines.
	 * col will be zero typically. row will be staring line number. */
	int curr_row;
	int curr_col;
	/* coords of window. */
	int x1;
	int y1;
	int x2;
	int y2;
};

/*
 * init tui window with autosized windows or specify cols and rows sizing.
 */
struct tui_window* tui_init(bool autosize, int cols, int rows,
			    int x1, int x2, int y1, int y2);
/*
 * destroy tui instance.
 */
void tui_destroy(struct tui_window* t);

/**
 * write line to the window and refresh from `start` line.
 */
void tui_write_line(struct tui_window *t, char *line, int n, int start, bool highlight);

/**
 * write `n` lines from `lines` to the window and refresh from `start` line.
 */
void tui_write_lines(struct tui_window *t, char *lines, int line_width, size_t n, int offset, int start);

/** 
 * scroll up or down `count` lines in a window `w`.
 */
void tui_scroll_up(struct tui_window *t, int count);
void tui_scroll_down(struct tui_window *t, int count);

/**
 * highlight a line
 */
void tui_highlight_line(struct tui_window *t, int line);
