#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <tui.h>

#define PREVIEW_HEIGHT 10
#define PREVIEW_WIDTH 70
#define MAX_LINES 1000  // Maximum lines the pad can hold
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

	char cmd[100] = {0};
	snprintf(cmd, sizeof(cmd), "/bin/rg --vimgrep %s . 2>&1", search);
	// get the grep output
	FILE *fp = popen(cmd, "r");
	if (fp == NULL) {
		printf("Failed to run rg\n");
		exit(1);
	}

	struct tui_window *t1 = tui_init(false, MAX_LINES, PREVIEW_WIDTH,
					 0,0,0,0);

	char line[256];
	char results[1000][256];
	int total_lines = 0;
	while (fgets(line, sizeof(line), fp) && total_lines < MAX_LINES) {
		strncpy(results[total_lines], line, sizeof(line));
		total_lines++;
	}
	fclose(fp);
	fp = NULL;

	// printf("write: %d, %s\n", total_lines, &((char*)results)[256*2]);
	tui_write_line(t1, results[0], 0, -1, true);
	tui_write_lines(t1, (char*)(results[1]), sizeof(results[0]), total_lines-1, 1, 0);


	int curr_line=0;
	int sel_line=0;
	int ch;
	char *file;
	int found =0;

	while ((ch = getch()) != 'q') {
		tui_write_line(t1, "test", 2, -1, false);
	}
#if 0
	while ((ch = getch()) != 'q' && (found == 0)) {
		switch (ch) {
		// let user scroll lines and select one:
			case 'j':
				// clear previous line color first.
				mvwprintw(pad, sel_line, 0, "TSET %s", results[sel_line]);
				sel_line++;
				wattron(pad, COLOR_PAIR(1));
				mvwprintw(pad, sel_line, 0, "TSET %s", results[sel_line]);
				wattroff(pad, COLOR_PAIR(1));
				prefresh(pad, 0, 0, 2, 2, LINES-3, COLS-3);
				break;
			case 'k':
				sel_line--;
				break;
			case '\r':
			case '\n':
				refresh();
				prefresh(pad, 0, 0, 2, 2, LINES-3, COLS-3);
				// printf("ENTER: %d, %s\n", sel_line, results[sel_line]);

				// find the file and open it:
				char *tmp = strchr(results[sel_line], ':');
				results[sel_line][tmp-results[sel_line]] = '\0';
				// printf("%d: %s\n", tmp-results[sel_line], results[sel_line]);
				found=1;
				file = results[sel_line];
				break;

		// let user enter fuzzy filter om lines
		// let user enter to takes current filtered results as the new search list. so to make further searches on this list.
			default:
				break;
		}
	}

	// 2. Load file into the Pad
	fp = fopen(file, "r");
	total_lines = 0;
	if (fp) {
		while (fgets(line, sizeof(line), fp) && total_lines < MAX_LINES) {
			mvwprintw(pad, total_lines, 0, "%s", line);
			total_lines++;
		}
		fclose(fp);
	} else {
		wprintw(pad, "File not found.");
	}

	int current_line = 0;
	ch = 0;

	prefresh(pad, current_line, 0, 2, 2, LINES - 3, COLS-3);

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
		prefresh(pad, current_line, 0, 2, 2, LINES - 3, COLS-3);
	}

	delwin(pad);
	endwin();
	return 0;
#endif
}

