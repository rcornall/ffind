#include <ncurses.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <unistd.h>
#include <poll.h>
#include <sys/wait.h>

#include <tui.h>
#include <list.h>

#include <termios.h>


/*
 * I want to store first rg contents.
 *
 * +-----------------------+
 * | rg_results[max_lines] |
 * +-----------------------+
 *
 * apply filter on this result list:
 *
 * hit enter to start new filtering on the filtered list:
 *
 * +------------------------------------+
 * | struct.filtered_rg_results[max_lines]     | <- tmp_filtered_results (copy, or move ).
 * +------------------------------------+
 *
 * filter again,
 *
 * hit enter to start new filtering on the filtered list:
 * At this point we could overwrite the last filtered_results buffer,
 * or we can allow for jumping back (undo) by allocate new one.
 *
 * +---------------------------------------------+
 * | tmp = malloc(strut)
 * | struct->next = tmp;
 * | struct->next.filtered_rg_results[max_lines] | <- tmp_filtered_results (copy, or move ).
 * +---------------------------------------------+
 *
 * undo pops the last one off.
 *
 * Or just store filters in a bitmask
 *
 * lines[lines][line_size + filter_bitmask_size]
 * struct.lines[line][bitmask] |= 0x1; // matched
 *
 * while filtering, only show lines with bitmask set. can be chained with multiple filters.
 *
 * what operations.
 *	- select a line in the results, to open it or preview it.
 *	- hit g to open git blame next to it.
 * 	- preview selected entry.
 * 	- execute arbitrary cmd on all in list. (xargs).
 * 	- put the list into a file
 * 	- select lines: open them in vim?
	 * 	- open vim, but allow to close it back to the list afterwards. possibly in a pad floating window.
 * 	- rename all instances (sed)
 *
 * +
 */

enum { PREVIEW_BELOW, PREVIEW_SIDE, PREVIEW_OFF };

static struct {
	struct tui_window area; // screen area split between results and preview
	struct tui_window *preview;
	int preview_mode;
} env;

static struct termios orig_termios;

static void restore_term(void)
{
	tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
}

static void render_view(struct tui_window *t1, struct list *l)
{
	int viewport_h = t1->y2 - t1->y1;

	for (int row = 1; row <= viewport_h; row++) {
		int idx = l->view_top + row - 1;
		if (idx > l->visible_lines)
			tui_clear_line(t1, row, -1);
		else
			tui_write_result_line(t1, l->buf[l->map_filtered_to_line[idx]], row, -1, idx == l->sel_line);
	}
}

/* split "file:line:col:text" result. returns malloc'd file name or NULL. */
static char* parse_result(const char *res, int *line_number)
{
	char *file = strdup(res);
	char *tmp = file ? strchr(file, ':') : NULL;
	if (tmp == NULL) {
		free(file);
		return NULL;
	}
	*tmp = '\0';
	*line_number = atoi(tmp+1);
	return file;
}

/* show file from line `first` in the preview with a file:line header. */
static int preview_file(char *file, int line, int first)
{
	char header[256];
	snprintf(header, sizeof(header), "%s:%d", file, line);
	tui_write_line(env.preview, header, 0, -1, true);
	return tui_write_file(env.preview, file, first < 1 ? 1 : first, line, 1);
}

static void update_preview(struct list *l)
{
	if (env.preview_mode == PREVIEW_OFF)
		return;

	int line_number;
	char *file = NULL;
	if (l->visible_lines > 0)
		file = parse_result(l->buf[l->map_filtered_to_line[l->sel_line]], &line_number);
	if (file == NULL) {
		tui_clear(env.preview);
		return;
	}

	int rows = env.preview->y2 - env.preview->y1;
	preview_file(file, line_number, line_number - rows / 2);
	free(file);
}

/* box around the screen area, with a line between results and preview. */
static void draw_border(struct tui_window *t1)
{
	struct tui_window *a = &env.area;
	int top = a->y1 - 1, left = a->x1 - 2, bottom = a->y2 + 1, right = a->x2 + 2;

	mvhline(top, left, ACS_HLINE, right - left);
	mvhline(bottom, left, ACS_HLINE, right - left);
	mvvline(top, left, ACS_VLINE, bottom - top);
	mvvline(top, right, ACS_VLINE, bottom - top);
	mvaddch(top, left, ACS_ULCORNER);
	mvaddch(top, right, ACS_URCORNER);
	mvaddch(bottom, left, ACS_LLCORNER);
	mvaddch(bottom, right, ACS_LRCORNER);

	if (env.preview_mode == PREVIEW_BELOW) {
		int y = t1->y2 + 1;
		mvhline(y, left, ACS_HLINE, right - left);
		mvaddch(y, left, ACS_LTEE);
		mvaddch(y, right, ACS_RTEE);
	} else if (env.preview_mode == PREVIEW_SIDE) {
		int x = t1->x2 + 2;
		mvvline(top, x, ACS_VLINE, bottom - top);
		mvaddch(top, x, ACS_TTEE);
		mvaddch(bottom, x, ACS_BTEE);
	}
}

/* split the screen area between results and preview, then redraw everything. */
static void layout(struct tui_window *t1, struct list *l)
{
	struct tui_window *t2 = env.preview;
	struct tui_window *a = &env.area;

	t1->x1 = t2->x1 = a->x1;
	t1->y1 = t2->y1 = a->y1;
	t1->x2 = t2->x2 = a->x2;
	t1->y2 = t2->y2 = a->y2;

	if (env.preview_mode == PREVIEW_BELOW) {
		int mid = a->y1 + (a->y2 - a->y1) / 2;
		t1->y2 = mid;
		t2->y1 = mid + 2;
	} else if (env.preview_mode == PREVIEW_SIDE) {
		int mid = a->x1 + (a->x2 - a->x1) / 2;
		t1->x2 = mid - 2;
		t2->x1 = mid + 2;
	}

	// keep selection inside the resized viewport
	int viewport_h = t1->y2 - t1->y1;
	if (l->sel_line >= l->view_top + viewport_h)
		l->view_top = l->sel_line - viewport_h + 1;

	erase();
	draw_border(t1);
	refresh();
	tui_write_line(t1, l->filter, 0, -1, false);
	render_view(t1, l);
	update_preview(l);
}

static void move_sel(struct tui_window *t1, struct list *l, int delta)
{
	if (l->visible_lines == 0) return;

	int new_sel = l->sel_line + delta;
	new_sel = new_sel < 1 ? 1 : new_sel > l->visible_lines ? l->visible_lines : new_sel;
	if (new_sel == l->sel_line) return;

	int old_sel = l->sel_line;
	l->sel_line = new_sel;

	int viewport_h = t1->y2 - t1->y1;
	// If cursor moves outside of the viewport render entire view, else just redraw the new line and old line.
	if (l->sel_line < l->view_top || l->sel_line >= l->view_top + viewport_h) {
		l->view_top = (l->sel_line < l->view_top) ? l->sel_line : l->sel_line - viewport_h + 1;
		render_view(t1, l);
	} else {
		tui_write_result_line(t1, l->buf[l->map_filtered_to_line[old_sel]], old_sel - l->view_top + 1, -1, false);
		tui_write_result_line(t1, l->buf[l->map_filtered_to_line[l->sel_line]], l->sel_line - l->view_top + 1, -1, true);
	}
	update_preview(l);
}

static bool has_upper(const char *s)
{
	for (; *s; s++)
		if (isupper((unsigned char)*s))
			return true;
	return false;
}

static char fold(char c, bool icase)
{
	return icase ? tolower((unsigned char)c) : c;
}

static bool word_start(const char *text, const char *t)
{
	return t == text || !isalnum((unsigned char)t[-1]) ||
	       (isupper((unsigned char)t[0]) && islower((unsigned char)t[-1]));
}

/*
 * subsequent fuzzy matching. smartcase: ignore case unless pat has uppercase.
 * score favours tight, consecutive matches starting on word boundaries.
 */
static bool fuzzy_match(const char *text, const char *pat, int *score) {
	bool icase = !has_upper(pat);
	int plen = strlen(pat);

	// forward: find where the first full match ends.
	const char *t, *end = NULL;
	int p = 0;
	for (t = text; *t; t++) {
		if (fold(*t, icase) == pat[p] && ++p == plen) {
			end = t;
			break;
		}
	}
	if (end == NULL)
		return false;

	// backward from the end: latest start gives the tightest window.
	const char *start = end;
	p = plen - 1;
	for (t = end; t >= text; t--) {
		if (fold(*t, icase) == pat[p] && --p < 0) {
			start = t;
			break;
		}
	}

	const char *prev = NULL;
	*score = 0;
	p = 0;
	for (t = start; t <= end; t++) {
		if (fold(*t, icase) == pat[p]) {
			*score += 16;
			if (prev && t == prev + 1)
				*score += 12;
			else if (word_start(text, t))
				*score += 8;
			prev = t;
			p++;
		} else {
			*score -= 3;
		}
	}
	return true;
}

/* plain substring matching, smartcase. */
static bool substr_match(const char *text, const char *pat) {
	bool icase = !has_upper(pat);
	for (; *text; text++) {
		const char *t = text, *p = pat;
		while (*t && *p && (icase ? tolower((unsigned char)*t) : *t) == *p) {
			t++;
			p++;
		}
		if (*p == '\0')
			return true;
	}
	return false;
}

/*
 * match all space-separated tokens against text (AND logic). !token excludes
 * lines containing token. score is the sum of token scores.
 */
static bool fuzzy_match_all(const char *text, const char *pat, int *score) {
	char tokens[100];
	strncpy(tokens, pat, sizeof(tokens) - 1);
	tokens[sizeof(tokens) - 1] = '\0';

	*score = 0;
	char *saveptr;
	char *tok = strtok_r(tokens, " ", &saveptr);
	while (tok) {
		int s = 0;
		if (tok[0] == '!') {
			if (tok[1] && substr_match(text, tok + 1))
				return false;
		} else if (!fuzzy_match(text, tok, &s)) {
			return false;
		}
		*score += s;
		tok = strtok_r(NULL, " ", &saveptr);
	}
	return true;
}

static void __attribute__((unused)) debug(struct tui_window *t1, const char *fmt, ...)
{
	/* print to last visible row of the tui window */
	va_list args;
	va_start(args, fmt);
	char buf[256];
	vsnprintf(buf, sizeof(buf), fmt, args);
	tui_write_line(t1, buf, t1->y2 - t1->y1, -1, false);
	va_end(args);
}

/* read one byte, giving up after `ms`. */
static bool read_timeout(unsigned char *ch, int ms)
{
	struct pollfd pfd = { .fd = STDIN_FILENO, .events = POLLIN };
	if (poll(&pfd, 1, ms) <= 0)
		return false;
	return read(STDIN_FILENO, ch, 1) == 1;
}

static int *sort_score;

/* best score first, ties keep the original rg order. */
static int cmp_score(const void *a, const void *b)
{
	int ia = *(const int *)a, ib = *(const int *)b;
	if (sort_score[ia] != sort_score[ib])
		return sort_score[ib] - sort_score[ia];
	return ia - ib;
}

/*
 * filter the list. `narrow` when the filter only got stricter, so just the
 * currently visible lines need checking.
 */
static void apply_filter(struct tui_window *t1, struct list *l, bool narrow)
{
	const char *pat = &l->filter[FILTER_PREFIX_LEN];
	tui_write_line(t1, l->filter, 0, -1, false);

	int line_no = 1;
	if (narrow) {
		// compact the map in place, line_no never passes i.
		for (int i = 1; i <= l->visible_lines; i++) {
			int idx = l->map_filtered_to_line[i];
			if (fuzzy_match_all(l->buf[idx], pat, &l->score[idx])) {
				l->map_filtered_to_line[line_no] = idx;
				line_no++;
			}
		}
	} else {
		for (int i = 0; i < l->total_lines; i++) {
			if (fuzzy_match_all(l->buf[i], pat, &l->score[i])) {
				l->map_filtered_to_line[line_no] = i;
				line_no++;
			}
		}
	}
	l->visible_lines = line_no - 1;

	sort_score = l->score;
	qsort(&l->map_filtered_to_line[1], l->visible_lines, sizeof(int), cmp_score);

	l->sel_line = 1;
	l->view_top = 1;
	render_view(t1, l);
	update_preview(l);
}

static void run_cmd(char *const argv[])
{
	pid_t pid = fork();
	if (pid == 0) {
		execvp(argv[0], argv);
		_exit(127);
	}
	if (pid > 0)
		waitpid(pid, NULL, 0);
}

static void open_vim(struct tui_window *t1, char *file, int line_number)
{
	char line_arg[32];
	snprintf(line_arg, sizeof(line_arg), "+%d", line_number);
	char *vim_argv[] = { "vim", "-c", "set noswapfile", line_arg, "-c", "normal! zz", "--", file, NULL };
	def_prog_mode();
	endwin();
	run_cmd(vim_argv);
	reset_prog_mode();
	refresh();
	tui_refresh(t1);
	if (env.preview_mode != PREVIEW_OFF)
		tui_refresh(env.preview);
}

/*
 * returns the selected result line, or NULL when the user quits.
 * `vim` is set when it should be opened in vim instead of previewed.
 */
char* interactive_filter(struct tui_window *t1, struct list *l, bool *vim)
{
	unsigned char ch;
	while (read(STDIN_FILENO, &ch, 1) == 1) {
		switch (ch) {
		// let user scroll lines and select one:

			// escape sequences: arrows, page-up/down. lone escape quits.
			case '\x1b': {
				unsigned char seq[2] = {0};
				if (!read_timeout(&seq[0], 50)) return NULL;
				if (seq[0] != '[' && seq[0] != 'O') break; // handle both normal and application cursor key mode
				if (!read_timeout(&seq[1], 50)) break;

				if (seq[1] == 'A')       move_sel(t1, l, -1);   // up arrow
				else if (seq[1] == 'B')  move_sel(t1, l, +1);   // down arrow
				else if (seq[0] == '[' && (seq[1] == '5' || seq[1] == '6')) {
					unsigned char tmp; read_timeout(&tmp, 50); // consume ~
					move_sel(t1, l, seq[1] == '5' ? -10 : +10);
				}
			} break;

			// ctrl-c
			case '\x03':
				return NULL;

			// ctrl-p: cycle preview below / side / off
			case '\x10':
				env.preview_mode = (env.preview_mode + 1) % 3;
				layout(t1, l);
				break;

			// ctrl-j / ctrl-k
			case '\n': move_sel(t1, l, +1); break;

			// ctrl-k
			case '\x0b': {
				move_sel(t1, l, -1); } break;

			// enter: scroll preview, ctrl-o: open in vim
			case '\r':
			case '\x0f':
				if (l->visible_lines > 0) {
					*vim = ch == '\x0f';
					return l->buf[l->map_filtered_to_line[l->sel_line]];
				}
				break;

			// backspace
			case 127:
			case '\b':
				if (l->filter_len > FILTER_PREFIX_LEN) {
					l->filter_len--;
					l->filter[l->filter_len] = '\0';
					apply_filter(t1, l, false);
				}
				break;

			// let user enter fuzzy filter om lines
			// let user enter to takes current filtered results as the new search list. so to make further searches on this list.
			default:
				if (ch >= 32 && ch < 127 && l->filter_len < (int)sizeof(l->filter) - 1) {
					l->filter[l->filter_len] = ch;
					l->filter[l->filter_len + 1] = '\0';
					l->filter_len++;

					// extending a !token excludes less, so needs a full rescan.
					const char *last = strrchr(&l->filter[FILTER_PREFIX_LEN], ' ');
					last = last ? last + 1 : &l->filter[FILTER_PREFIX_LEN];
					apply_filter(t1, l, !(last[0] == '!' && strlen(last) > 2));
				}
				break;
		}
	}

	return NULL;
}

int main(int argc, char *argv[])
{
	if (argc != 2) {
		fprintf(stderr, "Usage: %s <search-pattern>\n", argv[0]);
		return 1;
	}
	char *search = argv[1];

	struct list *l = list_init();
	if (!l) {
		printf("Failed to create list.\n");
		return -1;
	}

	char *rg_argv[] = { "rg", "--no-ignore", "--vimgrep", "--sortr", "path", "--", search, ".", NULL };
	if (list_run(l, rg_argv) < 0) {
		list_destroy(l);
		return -1;
	}

	// setup terminal raw, restored on exit.
	tcgetattr(STDIN_FILENO, &orig_termios);
	atexit(restore_term);
	struct termios raw = orig_termios;
	raw.c_lflag &= ~(ICANON | ECHO | ISIG);
	raw.c_iflag &= ~(ICRNL); // allow detecting enter vs ctrl-j
	tcsetattr(STDIN_FILENO, TCSANOW, &raw);

	struct tui_window *t1 = tui_init(false, 0, 0, 0,0,0,0);
	env.preview = tui_init(false, 0, 0, 0,0,0,0);
	if (t1 == NULL || env.preview == NULL) {
		endwin();
		printf("Failed to init tui\n");
		return -1;
	}
	env.area = *t1;
	layout(t1, l);

	char *sel;
	bool vim;
	while ((sel = interactive_filter(t1, l, &vim))) {
		int line_number;
		char *file = parse_result(sel, &line_number);
		if (file == NULL)
			continue;
		if (vim) {
			open_vim(t1, file, line_number);
			free(file);
			continue;
		}

		// scroll the preview, q to go back.
		if (env.preview_mode == PREVIEW_OFF) {
			env.preview_mode = PREVIEW_BELOW;
			layout(t1, l);
		}
		int rows = env.preview->y2 - env.preview->y1;
		int first = line_number - rows / 2;
		if (first < 1) first = 1;
		int shown = preview_file(file, line_number, first);
		unsigned char ch = 0;

		while ((read(STDIN_FILENO, &ch, 1) == 1) && ch != 'q' && ch != '\x03') {
			switch (ch) {
				case 'j': // Vim down
					if (shown == rows) first++;
					break;
				case 'k': // Vim up
					if (first > 1) first--;
					break;
				case 'd': // Vim down more
					if (shown == rows) first += 20;
					break;
				case 'u': // Vim up more
					first = first > 20 ? first - 20 : 1;
					break;
				case '\x0f': // ctrl-o
					open_vim(t1, file, line_number);
					break;
				default:
					break;
			}
			shown = preview_file(file, line_number, first);
		}
		// back to interactive filter
		update_preview(l);
		free(file);
	}

	tui_destroy(env.preview);
	tui_destroy(t1);
	endwin();
	list_destroy(l);
	return 0;
}
