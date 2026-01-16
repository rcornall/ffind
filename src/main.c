#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <tui.h>
#include <list.h>

#define PREVIEW_HEIGHT 10
#define PREVIEW_WIDTH 90
//#define PREVIEW_WIDTH 30
//


/*
 * I want to store first rg contents.
 *
 * +-----------------------+
 * | rg_results[max_lines] |
 * +-----------------------+
 *
 * apply filter on this result list:
 *
 * +------------------------------------+
 * | tmp_filtered_rg_results[max_lines] |
 * +------------------------------------+
 *
 * hit enter to start new filtering on the filtered list:
 *
 * +------------------------------------+
 * | struct.filtered_rg_results[max_lines]     | <- tmp_filtered_results (copy, or move ).
 * +------------------------------------+
 *
 * filter again:
 * +------------------------------------+
 * | tmp_filtered_rg_results[max_lines] |
 * +------------------------------------+
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
 * select a line in the results, to open it or preview it.
 * hit g to open git blame next to it.
 *
 * what else.
 *
 * 	- preview selected entry.
 * 	- execute arbitrary cmd on all in list. (xargs).
 * 	- put the list into a file
 * 	- select lines: open them in vim?
	 * 	- open vim, but allow to close it back to the list afterwards. possibly in a pad floating window.
 * 	- rename all instances (sed)
 *
 * +
 */


int main(int argc, char *argv[]) {
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

	struct tui_window *t1 = tui_init(false, MAX_LINES, PREVIEW_WIDTH,
					 0,0,0,0);

	struct list *l = list_init();

	char cmd[100] = {0};
	snprintf(cmd, sizeof(cmd), "/bin/rg --vimgrep %s . 2>&1", search);
	int total_lines = list_popen(l, cmd);

	// printf("write: %d, %s\n", total_lines, &((char*)results)[256*2]);
	tui_write_line(t1, l->buf[0], 0, -1, true);
	tui_write_lines(t1, (char*)(l->buf[1]), sizeof(l->buf[0]), total_lines-1, 1, 0);


	int curr_line=0;
	int sel_line=0;
	int ch;
	char *file;
	int found =0;

	while ((ch = getch()) != 'q' && (found == 0)) {
		switch (ch) {
		// let user scroll lines and select one:
			case 'j':
				// clear previous line color first.
				tui_write_line(t1, l->buf[sel_line], sel_line, -1, false);
				sel_line++;
				tui_write_line(t1, l->buf[sel_line], sel_line, -1, true);
				// tui_scroll_down(t1, 1);
				break;
			case 'k':
				tui_write_line(t1, l->buf[sel_line], sel_line, -1, false);
				sel_line--;
				tui_write_line(t1, l->buf[sel_line], sel_line, -1, true);
				break;
			case '\r':
			case '\n': {
				// find the file and open it:
				char *tmp = strchr(l->buf[sel_line], ':');
				l->buf[sel_line][tmp-l->buf[sel_line]] = '\0';
				// printf("%d: %s\n", tmp-l->buf[sel_line], l->buf[sel_line]);
				found=1;
				file = l->buf[sel_line];
			} break;

		// let user enter fuzzy filter om lines
		// let user enter to takes current filtered results as the new search list. so to make further searches on this list.
			default:
				break;
		}
	}

	// add new window
	struct tui_window *t2 = tui_init(false, MAX_LINES, PREVIEW_WIDTH,
					 0,0,0,0);

	// 2. Load file into the Pad
	total_lines  = tui_write_file(t2, file);
	int current_line = 0;
	ch = 0;

	// 3. Event Loop for Scrolling
	while ((ch = getch()) != 'q') {
		switch (ch) {
			case 'j': // Vim down
			case KEY_DOWN:
				if (current_line < total_lines-2) current_line++;
				break;
			case 'k': // Vim up
			case KEY_UP:
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

	delwin(t2->w);

	prefresh(t1->w, current_line, 0, 2, 2, LINES - 3, COLS-3);

	while(true) {}
	endwin();
	return 0;
}

