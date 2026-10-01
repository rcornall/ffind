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

#define PREVIEW_HEIGHT 10
#define PREVIEW_WIDTH 200
//#define VIM

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
}

/* simple subsequent fuzzy matching */
static bool fuzzy_match(const char *text, const char *pat, bool icase) {
	while (*text && *pat) {
		char c = icase ? tolower((unsigned char)*text) : *text;
		if (c == *pat)
			pat++;
		text++;
	}
	return *pat == '\0';
}

/* match all space-separated tokens against text (AND logic). smartcase. */
static bool fuzzy_match_all(const char *text, const char *pat) {
	char tokens[100];
	strncpy(tokens, pat, sizeof(tokens) - 1);
	tokens[sizeof(tokens) - 1] = '\0';

	bool icase = true;
	for (const char *p = pat; *p; p++)
		if (isupper((unsigned char)*p))
			icase = false;

	char *saveptr;
	char *tok = strtok_r(tokens, " ", &saveptr);
	if (!tok) return true;
	while (tok) {
		if (!fuzzy_match(text, tok, icase))
			return false;
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

static void apply_filter(struct tui_window *t1, struct list *l)
{
	tui_write_line(t1, l->filter, 0, -1, false);

	int line_no = 1;
	for (int i = 0; i < l->total_lines; i++) {
		if (fuzzy_match_all(l->buf[i], &l->filter[FILTER_PREFIX_LEN])) {
			l->map_filtered_to_line[line_no] = i;
			line_no++;
		}
	}
	l->visible_lines = line_no - 1;
	l->sel_line = 1;
	l->view_top = 1;
	render_view(t1, l);
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

/*
 * returns the selected result line, or NULL when the user quits.
 */
char* interactive_filter(struct tui_window *t1, struct list *l)
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

			// ctrl-j / ctrl-k
			case '\n': move_sel(t1, l, +1); break;

			// ctrl-k
			case '\x0b': {
				move_sel(t1, l, -1); } break;

			// enter
			case '\r':
				if (l->visible_lines > 0)
					return l->buf[l->map_filtered_to_line[l->sel_line]];
				break;

			// backspace
			case 127:
			case '\b':
				if (l->filter_len > FILTER_PREFIX_LEN) {
					l->filter_len--;
					l->filter[l->filter_len] = '\0';
					apply_filter(t1, l);
				}
				break;

			// let user enter fuzzy filter om lines
			// let user enter to takes current filtered results as the new search list. so to make further searches on this list.
			default:
				if (ch >= 32 && ch < 127 && l->filter_len < (int)sizeof(l->filter) - 1) {
					l->filter[l->filter_len] = ch;
					l->filter[l->filter_len + 1] = '\0';
					l->filter_len++;
					apply_filter(t1, l);
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
	if (t1 == NULL) {
		printf("Failed to init tui\n");
		return -1;
	}

	tui_write_line(t1, l->filter, 0, -1, false);
	render_view(t1, l);

	char *sel;
	while ((sel = interactive_filter(t1, l))) {
		// split "file:line:col:text"
		char *file = strdup(sel);
		char *tmp = file ? strchr(file, ':') : NULL;
		if (tmp == NULL) {
			free(file);
			continue;
		}
		*tmp = '\0';
		int line_number = atoi(tmp+1);
#ifdef VIM
		// open file in vim
		char line_arg[32];
		snprintf(line_arg, sizeof(line_arg), "+%d", line_number);
		char *vim_argv[] = { "vim", "-c", "set noswapfile", line_arg, "-c", "normal! zz", "--", file, NULL };
		def_prog_mode();
		endwin();
		run_cmd(vim_argv);
		reset_prog_mode();
		refresh();
		tui_refresh(t1);
#else
		// add new window
		struct tui_window *t2 = tui_init(false, 23000, PREVIEW_WIDTH,
						 0,0,0,0);
		if (t2 == NULL) {
			free(file);
			continue;
		}

		// 2. Load file into the Pad
		int file_lines = tui_write_file(t2, file);
		int current_line = line_number > 1 ? line_number - 1 : 0;
		unsigned char ch = 0;
		prefresh(t2->w, current_line, 0, t2->y1, t2->x1, t2->y2, t2->x2);

		// 3. Event Loop for Scrolling
		while ((read(STDIN_FILENO, &ch, 1) == 1) && ch != 'q') {
			switch (ch) {
				case 'j': // Vim down
					if (current_line < file_lines-2) current_line++;
					break;
				case 'k': // Vim up
					if (current_line > 0) current_line--;
					break;
				case 'd': // Vim down more
				{
					const int down_lines = 20;
					if ((current_line + down_lines) < file_lines - 2)
							current_line += down_lines;
					else
						current_line = file_lines - 2;
				} break;
				  //
				case 'u': // Vim up more
				{
					const int up_lines = 20;
					if ((current_line - up_lines) > 0)
						current_line -= up_lines;
					else
						current_line = 0;
				} break;
				default:
					break;
			}

			// 4. Refresh the Pad
			// prefresh(pad, pad_row, pad_col, screen_y1, screen_x1, screen_y2, screen_x2)
			prefresh(t2->w, current_line, 0, t2->y1, t2->x1, t2->y2, t2->x2);
		}
		// back to interactive filter
		tui_destroy(t2);
		tui_refresh(t1);
#endif
		free(file);
	}

	tui_destroy(t1);
	endwin();
	list_destroy(l);
	return 0;
}
