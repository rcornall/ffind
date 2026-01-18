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

char* interactive_filter(struct tui_window *t1, struct list *l, int total_lines)
{
	int sel_line=0;
	char *file = NULL;
	bool found = 0;
	unsigned char ch;


	char filter[100] = {0};
	int filter_len = 0;
	while ((read(STDIN_FILENO, &ch, 1) == 1) && ch != 'q' && (found == false)) {
		switch (ch) {
		// let user scroll lines and select one:
			case '\n':
				// clear previous line color first.
				tui_write_line(t1, l->buf[sel_line], sel_line, -1, false);
				sel_line++;
				tui_write_line(t1, l->buf[sel_line], sel_line, -1, true);
				// tui_scroll_down(t1, 1);
				break;
			case '':
				tui_write_line(t1, l->buf[sel_line], sel_line, -1, false);
				sel_line--;
				tui_write_line(t1, l->buf[sel_line], sel_line, -1, true);
				break;
			case '\r':{
				// find the file and open it:
				char *tmp = strchr(l->buf[sel_line], ':');
				l->buf[sel_line][tmp-l->buf[sel_line]] = '\0';
				// printf("%d: %s\n", tmp-l->buf[sel_line], l->buf[sel_line]);
				found=true;
				file = l->buf[sel_line];
			} break;

		// let user enter fuzzy filter om lines
		// let user enter to takes current filtered results as the new search list. so to make further searches on this list.
			default:
				if (ch >= 32 && ch <= 127) {
					if (ch == 127 || ch == 8) { // backspace
						if (filter_len > 0) {
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
						if (fuzzy_match(l->buf[i], filter)) {
							// print matched line
							tui_write_line(t1, l->buf[i], line_no, -1, false);
							line_no++;
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

	tui_write_line(t1, l->buf[0], 0, -1, true);
	tui_write_lines(t1, (char*)(l->buf[1]), sizeof(l->buf[0]), total_lines-1, 1, -1);

	while (true) {
		char * file = interactive_filter(t1, l, total_lines);
		if (file) {
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
		} else {
			printf("No file selected, exiting.\n");
		}

	}

	/* while(true) {} */
	endwin();
	return 0;
}

