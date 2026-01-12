#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define PREVIEW_HEIGHT 10
#define PREVIEW_WIDTH 50
#define MAX_LINES 1000  // Maximum lines the pad can hold
//#define PREVIEW_WIDTH 30

void display_file_preview(WINDOW *win, const char *filename) {
	FILE *fp = fopen(filename, "r");
	if (fp == NULL) {
		mvwprintw(win, 1, 2, "Error: Could not open %s", filename);
		return;
	}

	char buffer[256];
	int line_num = 1;
	int max_y, max_x;
	getmaxyx(win, max_y, max_x);

	// Read the file line by line and print to the window
	// Leave space for the border (line 1 to max_y-2)
	while (fgets(buffer, sizeof(buffer), fp) && line_num < max_y - 1) {
		mvwprintw(win, line_num, 2, "%.*s", max_x - 4, buffer);
		line_num++;
	}

	fclose(fp);
}


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
	//printf("%s\n", cmd);
	// get the grep output
	FILE *fp = popen(cmd, "r");
	if (fp == NULL) {
		printf("Failed to run rg\n");
		exit(1);
	}


	initscr();
	cbreak();
	noecho();
	keypad(stdscr, TRUE); // Enable arrow keys and others
	curs_set(0);

	// 1. Create a Pad instead of a Window
	// A pad is (Height, Width). We make it tall enough for the file.
	WINDOW *pad = newpad(MAX_LINES, PREVIEW_WIDTH);

	// getch does implicit refresh which messes with pad.
	// refresh here so getch doesnt clear pad window.
	refresh();

	char line[256];
	int total_lines = 0;
	while (fgets(line, sizeof(line), fp) && total_lines < MAX_LINES) {
		//printf("%s\n", line);
		mvwprintw(pad, total_lines, 0, "%s", line);
		total_lines++;
	}
	fclose(fp);
	fp = NULL;

	prefresh(pad, 0, 0, 2, 2, LINES-3, COLS-3);

	int curr_line=0;
	int sel_line=0;
	int ch;
	while ((ch = getch()) != 'q') {
		switch (ch) {
		// let user scroll lines and select one:
			case 'j':
				sel_line++;
				break;
			case 'k':
				sel_line--;
				break;
			case '\r':
			case '\n':
				printf("ENTER\n");
				break;

		// let user enter fuzzy filter om lines
		// let user enter to takes current filtered results as the new search list. so to make further searches on this list.
			default:
				break;
		}
	}

	// 2. Load file into the Pad
	fp = fopen("test.txt", "r");
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
}

