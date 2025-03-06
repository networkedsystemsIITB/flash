#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <ncurses.h>
#include <string.h>

#include <flash_monitor.h>
#include "flash_display.h"

WINDOW *output_win, *input_win;
int rows, cols;

static void reset(void)
{
	initscr();
	noecho();
	cbreak();
	keypad(stdscr, TRUE);
	curs_set(1);
	leaveok(output_win, TRUE);

	getmaxyx(stdscr, rows, cols);

	output_win = newwin(rows - 2, cols, 0, 0);
	input_win = newwin(1, cols, rows - 1, 0);
	scrollok(output_win, TRUE);
}

static void update_screen(const char *msg)
{
	// wclear(output_win);
	wprintw(output_win, "%s\n", msg);
	wrefresh(output_win);
}

static void run_prompt(void)
{
	char input[100];
	int pos = 0;

	while (1) {
		werase(input_win);
		mvwprintw(input_win, 0, 0, "Prompt:> ");
		wrefresh(input_win);

		pos = 0;
		while (1) {
			wmove(input_win, 0, pos + 9);
			wrefresh(input_win);
			int ch = wgetch(input_win);
			if (ch == '\n') {
				const char *msg = process_input(input);
				werase(input_win);
				mvwprintw(input_win, 0, 0, "Prompt:> ");
				wrefresh(input_win);
				update_screen(msg);
				wmove(input_win, 0, 9);
				wrefresh(input_win);
				break;
			} else if (ch == KEY_BACKSPACE || ch == 127) {
				if (pos > 0) {
					pos--;
					input[pos] = '\0';
				}
			} else if (pos < (int)sizeof(input) - 1) {
				input[pos++] = ch;
				input[pos] = '\0';
			}
			werase(input_win);
			mvwprintw(input_win, 0, 0, "Prompt:> %s", input);
			wmove(input_win, 0, pos + 9);
			wrefresh(input_win);
		}
	}
	return;
}

void *init_prompt(void *arg)
{
	(void)arg;
	reset();
	run_prompt();
	endwin();
	return NULL;
}
