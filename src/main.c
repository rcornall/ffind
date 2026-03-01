#include <ncurses.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <tui.h>
#include <list.h>

#include <termios.h>

#define PREVIEW_HEIGHT 10
#define PREVIEW_WIDTH 200

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

struct {
} env;

static void move_sel(struct tui_window *t1, struct list *l, int delta)
{
	int new_sel = l->sel_line + delta;
	new_sel = new_sel < 1 ? 1 : new_sel > l->visible_lines ? l->visible_lines : new_sel;
	if (new_sel == l->sel_line) return;
	tui_write_result_line(t1, l->buf[l->map_filtered_to_line[l->sel_line]], l->sel_line, -1, false);
	l->sel_line = new_sel;
	tui_write_result_line(t1, l->buf[l->map_filtered_to_line[l->sel_line]], l->sel_line, -1, true);
}

/* simple subsequent fuzzy matching */
bool fuzzy_match(const char *text, const char *pat) {
	while (*text && *pat) {
		if (*text == *pat)
			pat++;
		text++;
	}
	return *pat == '\0';
}

/* match all space-separated tokens against text (AND logic) */
bool fuzzy_match_all(const char *text, const char *pat) {
	char tokens[100];
	strncpy(tokens, pat, sizeof(tokens) - 1);
	tokens[sizeof(tokens) - 1] = '\0';

	char *saveptr;
	char *tok = strtok_r(tokens, " ", &saveptr);
	if (!tok) return true;
	while (tok) {
		if (!fuzzy_match(text, tok))
			return false;
		tok = strtok_r(NULL, " ", &saveptr);
	}
	return true;
}

void debug(struct tui_window *t1, const char *fmt, ...)
{
	/* print to last line in tui window or LINES row */
	va_list args;
	va_start(args, fmt);
	/* tui_write_line(t1, "                                                                                ", */
			   /* LINES - 4, -1, false); */
	char buf[256];
	vsnprintf(buf, sizeof(buf), fmt, args);
	tui_write_line(t1, buf, LINES - 4, -1, false);
	va_end(args);
}

char* interactive_filter(struct tui_window *t1, struct list *l, int total_lines)
{
	static char scratch[MAX_LINE_LEN];
	char *file = NULL;
	bool found = 0;
	unsigned char ch;
	if (l->visible_lines == 0)
		l->visible_lines = total_lines;

	while ((found == false) && (read(STDIN_FILENO, &ch, 1) == 1)) {
		switch (ch) {
		// let user scroll lines and select one:

			// escape sequences: arrows, page-up/down
			case '\x1b': {
				unsigned char seq[2] = {0};
				if (read(STDIN_FILENO, &seq[0], 1) != 1) break;
				if (seq[0] != '[' && seq[0] != 'O') break; // handle both normal and application cursor key mode
				if (read(STDIN_FILENO, &seq[1], 1) != 1) break;

				if (seq[1] == 'A')       move_sel(t1, l, -1);   // up arrow
				else if (seq[1] == 'B')  move_sel(t1, l, +1);   // down arrow
				else if (seq[0] == '[' && (seq[1] == '5' || seq[1] == '6')) {
					unsigned char tmp; read(STDIN_FILENO, &tmp, 1); // consume ~
					move_sel(t1, l, seq[1] == '5' ? -10 : +10);
				}
			} break;

			// ctrl-j / ctrl-k
			case '\n': move_sel(t1, l, +1); break;

			// ctrl-k
			case '': {
				move_sel(t1, l, -1); } break;

			// enter
			case '\r':{
				// copy into scratch so the original buffer is not modified
				strncpy(scratch, l->buf[l->map_filtered_to_line[l->sel_line]], MAX_LINE_LEN - 1);
				scratch[MAX_LINE_LEN - 1] = '\0';
				char *tmp = strchr(scratch, ':');
				tmp = strchr(tmp+1, ':');
				scratch[tmp - scratch] = '\0';
				file = scratch;
				found=true;
			} break;

			// let user enter fuzzy filter om lines
			// let user enter to takes current filtered results as the new search list. so to make further searches on this list.
			default:
				if (ch >= 32 && ch <= 127) {
					if (ch == 127 || ch == 8) { // backspace
						if (l->filter_len > (sizeof("filter: ")-1)) {
							l->filter_len--;
							l->filter[l->filter_len] = '\0';
						}
					} else {
						// append to filter string
						size_t len = strnlen(l->filter, sizeof(l->filter));
						if (len < sizeof(l->filter) - 1) {
							l->filter[l->filter_len] = ch;
							l->filter[l->filter_len + 1] = '\0';
							l->filter_len++;
						}
					}
					// print filter string
					tui_clear_line(t1, 0, -1);
					tui_write_line(t1, l->filter, 0, -1, false);

					// filter the list
					int line_no = 1;
					int i = 0;
					while (i < total_lines) {
						if (fuzzy_match_all(l->buf[i], &l->filter[sizeof("filter: ")-1])) {
							l->buf[i][MAX_LINE_LEN] |= 0x1; // mark as matched
							// print matched line
							tui_write_result_line(t1, l->buf[i], line_no, -1, (line_no == l->sel_line ? true : false));
							l->map_filtered_to_line[line_no] = i;
							line_no++;
						} else {
							l->buf[i][MAX_LINE_LEN] &= ~0x1; // unmark
						}
						i++;
					}
					l->visible_lines = line_no - 1;
					if (l->sel_line > l->visible_lines) l->sel_line = l->visible_lines > 0 ? l->visible_lines : 1;
					// clear remaining lines
					for (int i = line_no; i < total_lines+1; i++) {
						tui_clear_line(t1, i, -1);
					}
				}
				break;
		}
	}

	return file;
}

int main(int argc, char *argv[])
{
	// setup terminal raw
	struct termios old;
	tcgetattr(STDIN_FILENO, &old);
	old.c_lflag &= ~(ICANON | ECHO);
	old.c_iflag &= ~(ICRNL); // allow detecting enter vs ctrl-j
	tcsetattr(STDIN_FILENO, TCSANOW, &old);

	char *search = NULL;
	int opt;
	// The option string "a:b" indicates that 'a' requires an argument, 'b' does not
	while ((opt = getopt(argc, argv, "a:b")) != -1) {
		switch (opt) {
			case 'a':
				printf("Option -a provided with value: %s\n", optarg);
				break;
			case 'b':
				printf("Option -b provided\n");
				break;
			default:
				fprintf(stderr, "Usage: %s [-a value] [-b]\n", argv[0]);
				return 1;
		}
	}
	// Process positional arguments starting from optind
	for (; optind < argc; optind++) {
		search = argv[optind];
	}

	if (!search) {
		fprintf(stderr, "Usage: %s <search-pattern>\n", argv[0]);
		return 1;
	}

	struct list *l = list_init();
	if (!l) {
		printf("Failed to create list.\n");
		return -1;
	}

	char cmd[200] = {0};
	snprintf(cmd, sizeof(cmd), "/bin/rg --no-ignore --vimgrep %s . 2>&1 | sort -r", search);
	int total_lines = list_popen(l, cmd);
	/* printf("Total lines from rg: %d\n", total_lines); */
	/* sleep(1); */

	struct tui_window *t1 = tui_init(false, MAX_LINES, PREVIEW_WIDTH,
					 0,0,0,0);

	tui_write_line(t1, "filter: ", 0, -1, false);
	for (int i = 0; i < total_lines; i++)
		tui_write_result_line(t1, l->buf[i], i + 1, -1, i == 0);

	while (true) {
		char * file = interactive_filter(t1, l, total_lines);
		if (file) {
			// get line number
			char *tmp = strchr(file, ':');
			int line_number = atoi(tmp+1);
			file[tmp-file] = '\0';
#define VIM
#ifdef VIM
			// open file in vim
			char vim_cmd[256];
			snprintf(vim_cmd, sizeof(vim_cmd), "vim -c 'set noswapfile' +%d -c 'normal! zz' %s", line_number, file);
			endwin();
			system(vim_cmd);
			initscr();
			cbreak();
			noecho();
			keypad(stdscr, TRUE); // Enable arrow keys and others
			curs_set(0);

			start_color();
			prefresh(t1->w, 0, 0, 2, 2, LINES - 3, COLS-3);


#else
			// add new window
			struct tui_window *t2 = tui_init(false, MAX_LINES, PREVIEW_WIDTH,
							 0,0,0,0);

			// 2. Load file into the Pad
			total_lines  = tui_write_file(t2, file);
			int current_line = 0;
			unsigned char ch = 0;

			// 3. Event Loop for Scrolling
			while ((read(STDIN_FILENO, &ch, 1) == 1) && ch != 'q') {
				switch (ch) {
					case 'j': // Vim down
					/* case KEY_DOWN: */
						if (current_line < total_lines-2) current_line++;
						break;
					case 'k': // Vim up
					/* case KEY_UP: */
						if (current_line > 0) current_line--;
						break;
					case 'd': // Vim down more
					{
						const int down_lines = 20;
						if ((current_line + down_lines) < total_lines - 2)
								current_line += down_lines;
						else
							current_line = total_lines - 2;
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
				prefresh(t2->w, current_line, 0, 2, 2, LINES - 3, COLS-3);
			}
			// back to interactive filter
			delwin(t2->w);
			prefresh(t1->w, 0, 0, 2, 2, LINES - 3, COLS-3);
#endif
		} else {
			printf("No file selected, exiting.\n");
		}

	}

	/* while(true) {} */
	endwin();
	return 0;
}

