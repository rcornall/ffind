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

/* simple subsequent fuzzy matching */
bool fuzzy_match(const char *text, const char *pat) {
	while (*text && *pat) {
		if (*text == *pat)
			pat++;
		text++;
	}
	return *pat == '\0';
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
	int sel_line=0;
	char *file = NULL;
	int map_line_to_filter[MAX_LINES] = {0};
	bool found = 0;
	unsigned char ch;

	while (sel_line < total_lines) {
		// init defaults
		l->buf[sel_line][MAX_LINE_LEN] |= 0x1;
		map_line_to_filter[sel_line+1] = sel_line;
		sel_line++;
	}
	sel_line = 1;


	char filter[100] = "filter: ";
	int filter_len = strlen(filter);
	while ((found == false) && (read(STDIN_FILENO, &ch, 1) == 1)) {
		switch (ch) {
		// let user scroll lines and select one:

			// ctrl-j
			case '\n': {
				// clear previous line color first.
				int i = 0;
				int i_prev = 0;
				int match = 0;
				while (match <= sel_line) {
					if (l->buf[i][MAX_LINE_LEN] & 0x1) {
						match++;
						if (match == sel_line) {
							i_prev = i;
						} else if (match > sel_line) {
							break;
						}
					}
					i++;
				}
				debug(t1, "i: %d, i_prev: %d\n", i, i_prev);
				tui_write_line(t1, l->buf[i_prev], sel_line, -1, false);
				sel_line++;
				tui_write_line(t1, l->buf[i], sel_line, -1, true);
				// tui_scroll_down(t1, 1);
			} break;

			// ctrl-k
			case '': {
				// clear previous line color first.
				int i = 0;
				int i_prev = 0;
				int match = 0;
				while (match <= sel_line-1) {
					if (l->buf[i][MAX_LINE_LEN] & 0x1) {
						match++;
						if (match == sel_line-1) {
							i_prev = i;
						} else if (match > sel_line-1) {
							break;
						}
					}
					i++;
				}
				tui_write_line(t1, l->buf[map_line_to_filter[sel_line]], sel_line, -1, false);
				sel_line--;
				tui_write_line(t1, l->buf[map_line_to_filter[sel_line]], sel_line, -1, true);
			} break;
			case '\r':{ // enter
				// find the file and open it:
				int i = 0;
				int match = 0;
				while (match <= sel_line-1) {
					if (l->buf[i][MAX_LINE_LEN] & 0x1) {
						match++;
						if (match == sel_line) {
							break;
						}
					}
					i++;
				}
				char *tmp = strchr(l->buf[i], ':');
				tmp = strchr(tmp+1, ':');
				l->buf[i][tmp-l->buf[i]] = '\0';
				// printf("%d: %s\n", tmp-l->buf[sel_line], l->buf[sel_line]);
				found=true;
				file = l->buf[i];
			} break;

		// let user enter fuzzy filter om lines
		// let user enter to takes current filtered results as the new search list. so to make further searches on this list.
			default:
				if (ch >= 32 && ch <= 127) {
					if (ch == 127 || ch == 8) { // backspace
						if (filter_len > (sizeof("filter: ")-1)) {
							filter_len--;
							filter[filter_len] = '\0';
						}
					} else {
						// append to filter string
						size_t len = strnlen(filter, sizeof(filter));
						if (len < sizeof(filter) - 1) {
							filter[filter_len] = ch;
							filter[filter_len + 1] = '\0';
							filter_len++;
						}
					}
					// print filter string
					tui_clear_line(t1, 0, -1);
					tui_write_line(t1, filter, 0, -1, false);

					// filter the list
					int line_no = 1;
					int i = 0;
					while (i < total_lines) {
						if (fuzzy_match(l->buf[i], &filter[sizeof("filter: ")-1])) {
							l->buf[i][MAX_LINE_LEN] |= 0x1; // mark as matched
							// print matched line
							tui_write_line(t1, l->buf[i], line_no, -1, (line_no == sel_line ? true : false));
							map_line_to_filter[line_no] = i;
							line_no++;
						} else {
							l->buf[i][MAX_LINE_LEN] &= ~0x1; // unmark
						}
						i++;
					}
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

	char *search;
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
		//printf("Positional argument: %s\n", argv[optind]);
		search = argv[optind];
	}

	struct list *l = list_init();

	char cmd[200] = {0};
	snprintf(cmd, sizeof(cmd), "/bin/rg --no-ignore --vimgrep %s . 2>&1 | sort -r", search);
	int total_lines = list_popen(l, cmd);
	/* printf("Total lines from rg: %d\n", total_lines); */
	/* sleep(1); */

	struct tui_window *t1 = tui_init(false, MAX_LINES, PREVIEW_WIDTH,
					 0,0,0,0);

	tui_write_line(t1, "filter: ", 0, -1, false);
	tui_write_line(t1, l->buf[0], 1, -1, true);
	tui_write_lines(t1, (char*)(l->buf[1]), sizeof(l->buf[0]), total_lines-1, 2, -1);

	while (true) {
		char * file = interactive_filter(t1, l, total_lines);
		// get line number
		char *tmp = strchr(file, ':');
		int line_number = atoi(tmp+1);
		file[tmp-file] = '\0';

		if (file) {
#define VIM
#ifdef VIM
			// open file in vim
			char vim_cmd[256];
			snprintf(vim_cmd, sizeof(vim_cmd), "vim -c 'set noswapfile' +'%s' %s +%d", "normal! zz", file, line_number);
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

