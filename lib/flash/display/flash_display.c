/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Debojeet Das
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <ncurses.h>
#include <string.h>
#include <dirent.h>
#include <sys/mman.h>
#include <fcntl.h>

#include <flash_monitor.h>
#include "flash_display.h"

#define HISTORY_SIZE 10
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

WINDOW *output_pad, *input_win;
int rows, cols;
int pad_top = 0;
int total_lines = 0;
bool auto_scroll = true;
int visible_rows;
static int log_fd = -1;
static int original_stdout_fd = -1;
static int original_stderr_fd = -1;
static char input_history[HISTORY_SIZE][100];
static int history_count = 0;
static int history_index = -1;

static const char *commands[] = { "logs", "clear", "load", "unload", "exit" };
static const int num_commands = sizeof(commands) / sizeof(commands[0]);

static const int ansi_to_ncurses[] = {
	COLOR_BLACK,   // 30
	COLOR_RED,     // 31
	COLOR_GREEN,   // 32
	COLOR_YELLOW,  // 33
	COLOR_BLUE,    // 34
	COLOR_MAGENTA, // 35
	COLOR_CYAN,    // 36
	COLOR_WHITE    // 37
};

static void init_display_colors(void)
{
	start_color();
	use_default_colors();

	// Initialize standard ANSI colors (30-37)
	for (int i = 0; i < 8; i++) {
		init_pair(i + 1, ansi_to_ncurses[i], -1);
	}

	for (int i = 0; i < 8; i++) {
		init_pair(i + 9, ansi_to_ncurses[i], -1);
	}
}

static void init(void)
{
	initscr();
	init_display_colors();

	if (!has_colors()) {
		endwin();
		fprintf(stderr, "Terminal doesn't support colors\n");
		exit(1);
	}

	mouseinterval(0);
	mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, NULL);

	noecho();
	cbreak();
	keypad(stdscr, TRUE);
	curs_set(1);

	getmaxyx(stdscr, rows, cols);
	visible_rows = rows - 2;
	output_pad = newpad(10000, cols);
	input_win = newwin(1, cols, rows - 1, 0);
	scrollok(output_pad, TRUE);
	keypad(input_win, TRUE);
}

static void init_logging(void)
{
	log_fd = memfd_create("app_logs", MFD_ALLOW_SEALING);

	original_stdout_fd = dup(STDOUT_FILENO);
	original_stderr_fd = dup(STDERR_FILENO);

	/* Redirect stdout/stderr to memfd */
	dup2(log_fd, STDOUT_FILENO);
	dup2(log_fd, STDERR_FILENO);

	/* Preserve original terminal output for debugging */
	int tty_fd = open("/dev/tty", O_WRONLY);
	dup2(tty_fd, STDOUT_FILENO);
	close(tty_fd);
}

static void show_logs(int log_fd)
{
	off_t log_size = lseek(log_fd, 0, SEEK_END);
	if (log_size == -1) {
		wprintw(output_pad, "Error determining log size\n");
		prefresh(output_pad, pad_top, 0, 0, 0, visible_rows - 1, cols - 1);
		return;
	}

	if (log_size == 0) {
		wprintw(output_pad, "No logs available\n");
		prefresh(output_pad, pad_top, 0, 0, 0, visible_rows - 1, cols - 1);
		return;
	}

	char *log_buf = (char *)malloc(log_size + 1);
	if (log_buf == NULL) {
		wprintw(output_pad, "Memory allocation failed\n");
		prefresh(output_pad, pad_top, 0, 0, 0, visible_rows - 1, cols - 1);
		return;
	}

	lseek(log_fd, 0, SEEK_SET);
	ssize_t n = read(log_fd, log_buf, log_size);
	if (n == -1) {
		wprintw(output_pad, "Error reading log file\n");
		free(log_buf);
		prefresh(output_pad, pad_top, 0, 0, 0, visible_rows - 1, cols - 1);
		return;
	}
	log_buf[n] = '\0';

	/* Clear and redraw pad contents */
	werase(output_pad);

	int attr = A_NORMAL;
	int color_pair = 0;
	char *ptr = log_buf;

	total_lines = 0;

	while (*ptr) {
		if (*ptr == '\x1B' && ptr[1] == '[') {
			ptr += 2; // Skip ESC[
			int code = 0;

			/* Parse ANSI code */
			while (*ptr >= '0' && *ptr <= '9') {
				code = code * 10 + (*ptr++ - '0');
			}

			/* Map ANSI to ncurses */
			if (code >= 30 && code <= 37) {
				color_pair = code - 30 + 1;
				attr = COLOR_PAIR(color_pair);
			} else if (code == 0) {
				attr = A_NORMAL;
				color_pair = 0;
			}

			if (*ptr == 'm')
				ptr++; // Skip 'm'
		} else {
			waddch(output_pad, *ptr | attr);
			if (*ptr == '\n')
				total_lines++;
			ptr++;
		}
	}
	total_lines++; // Account for final line

	/* Update pad display */
	if (auto_scroll) {
		pad_top = MAX(0, total_lines - visible_rows);
	}
	prefresh(output_pad, pad_top, 0, 0, 0, visible_rows - 1, cols - 1);

	free(log_buf);
}

static void update_screen(const char *msg)
{
	// Count newlines for scroll tracking
	char *tmp = strdup(msg);
	for (char *p = tmp; *p; p++)
		if (*p == '\n')
			total_lines++;
	total_lines++; // Count final line

	wprintw(output_pad, "%s\n", msg);

	if (auto_scroll) {
		pad_top = MAX(0, total_lines - visible_rows);
	}
	prefresh(output_pad, pad_top, 0, 0, 0, visible_rows - 1, cols - 1);
	free(tmp);
}

static void suggest_files(char *input, int *len, int *pos)
{
	char path[256] = { 0 };
	char *last_space = strrchr(input, ' ');
	char *current_word_start = last_space ? last_space + 1 : input;
	char *last_slash = strrchr(current_word_start, '/');

	/* Determine base directory */
	if (last_slash) {
		snprintf(path, sizeof(path), "%.*s", (int)(last_slash - current_word_start + 1), current_word_start);
	} else {
		strcpy(path, "./");
	}

	/* Open appropriate directory */
	DIR *d = opendir(strlen(path) ? path : ".");
	char *suggestions[100];
	int suggestion_count = 0;
	const char *search_prefix = last_slash ? last_slash + 1 : current_word_start;

	if (d) {
		struct dirent *dir;
		while ((dir = readdir(d)) != NULL) {
			if (strncmp(dir->d_name, search_prefix, strlen(search_prefix)) == 0) {
				/* Build suggestion with proper path handling */
				char *suggestion;
				if (last_slash) {
					suggestion = malloc(strlen(path) + strlen(dir->d_name) + 1);
					sprintf(suggestion, "%s%s", path, dir->d_name);
				} else {
					suggestion = strdup(dir->d_name);
				}

				/* Append / for directories */
				if (dir->d_type == DT_DIR) {
					suggestion = realloc(suggestion, strlen(suggestion) + 2);
					strcat(suggestion, "/");
				}

				suggestions[suggestion_count++] = suggestion;
			}
		}
		closedir(d);
	}

	/* Add command suggestions */
	for (int i = 0; i < num_commands; i++) {
		if (strncmp(commands[i], current_word_start, strlen(current_word_start)) == 0) {
			suggestions[suggestion_count++] = strdup(commands[i]);
		}
	}

	if (suggestion_count > 0) {
		int word_start = current_word_start - input;
		int word_len = *len - word_start;

		/* Clear current partial word */
		memset(input + word_start, 0, word_len);

		if (suggestion_count == 1) {
			/*  Insert full suggestion */
			strcpy(input + word_start, suggestions[0]);
			*len = word_start + strlen(suggestions[0]);
			*pos = *len;
		} else {
			/* Find common prefix */
			int common_len = strlen(suggestions[0]);
			for (int i = 1; i < suggestion_count; i++) {
				int j;
				for (j = 0; j < common_len; j++) {
					if (suggestions[i][j] != suggestions[0][j])
						break;
				}
				common_len = j;
			}
			strncpy(input + word_start, suggestions[0], common_len);
			*len = word_start + common_len;
			*pos = *len;
			input[*len] = '\0';
		}
	}

	/* Free suggestions */
	for (int i = 0; i < suggestion_count; i++) {
		free(suggestions[i]);
	}
}

static void clear_screen(void)
{
	getmaxyx(stdscr, rows, cols);

	if (input_win != NULL) {
		delwin(input_win);
	}

	if (output_pad != NULL) {
		delwin(output_pad);
	}

	input_win = newwin(1, cols, rows - 1, 0);
	keypad(input_win, TRUE); // Enable keypad for input_win
	werase(input_win);
	wrefresh(input_win);

	visible_rows = rows - 2;
	output_pad = newpad(10000, cols);
	scrollok(output_pad, TRUE);
	total_lines = 0;
	pad_top = 0;
	prefresh(output_pad, pad_top, 0, 0, 0, visible_rows - 1, cols - 1);
}

static void add_to_history(const char *input)
{
	if (history_count < HISTORY_SIZE) {
		strcpy(input_history[history_count], input);
		history_count++;
	} else {
		for (int i = 1; i < HISTORY_SIZE; i++) {
			strcpy(input_history[i - 1], input_history[i]);
		}
		strcpy(input_history[HISTORY_SIZE - 1], input);
	}
	history_index = history_count;
}

static void run_prompt(void)
{
	char input[100];
	int pos = 0;
	int len = 0;
	MEVENT event;

	while (1) {
		werase(input_win);
		mvwprintw(input_win, 0, 0, "flash:/> ");
		wrefresh(input_win);

		pos = 0;
		len = 0;
		memset(input, 0, sizeof(input));

		while (1) {
			wmove(input_win, 0, pos + 9);
			wrefresh(input_win);

			int ch = wgetch(input_win);

			/* Handle mouse events */
			if (ch == KEY_MOUSE) {
				if (getmouse(&event) == OK) {
					if (event.bstate & BUTTON4_PRESSED) {
						/* Wheel up */
						pad_top = MAX(0, pad_top - 3);
						auto_scroll = (pad_top >= total_lines - visible_rows);
					} else if (event.bstate & BUTTON5_PRESSED) { // Wheel down
						pad_top = MIN(total_lines - visible_rows, pad_top + 3);
						auto_scroll = (pad_top >= total_lines - visible_rows);
					}
					prefresh(output_pad, pad_top, 0, 0, 0, visible_rows - 1, cols - 1);
				}
				continue;
			}

			/* Handle keyboard scrolling */
			if (ch == KEY_PPAGE) {
				pad_top = MAX(0, pad_top - visible_rows);
				auto_scroll = false;
				prefresh(output_pad, pad_top, 0, 0, 0, visible_rows - 1, cols - 1);
				continue;
			} else if (ch == KEY_NPAGE) {
				pad_top = MIN(total_lines - visible_rows, pad_top + visible_rows);
				auto_scroll = (pad_top >= total_lines - visible_rows);
				prefresh(output_pad, pad_top, 0, 0, 0, visible_rows - 1, cols - 1);
				continue;
			}

			/* Handle input [TODO: Handle all commands together] */
			if (ch == '\n') {
				if (strcmp(input, "logs") == 0) {
					add_to_history(input);
					show_logs(log_fd);
					wrefresh(output_pad);
					werase(input_win);
					mvwprintw(input_win, 0, 0, "flash:/> ");
					wrefresh(input_win);
					wmove(input_win, 0, 9);
					wrefresh(input_win);
					break;
				} else if (strcmp(input, "clear") == 0) {
					clear_screen();
					break;
				} else {
					input[len] = '\0';
					add_to_history(input);
					const char *msg = process_input(input);
					werase(input_win);
					mvwprintw(input_win, 0, 0, "flash:/> ");
					wrefresh(input_win);
					update_screen(msg);
					wmove(input_win, 0, 9);
					wrefresh(input_win);
					break;
				}
			} else if (ch == KEY_BACKSPACE || ch == 127) {
				if (pos > 0) {
					memmove(&input[pos - 1], &input[pos], len - pos + 1);
					pos--;
					len--;
				}
			} else if (ch == KEY_LEFT) {
				if (pos > 0)
					pos--;
			} else if (ch == KEY_RIGHT) {
				if (pos < len)
					pos++;
			} else if (ch == KEY_DC) {
				if (pos < len) {
					memmove(&input[pos], &input[pos + 1], len - pos);
					len--;
				}
			} else if (ch == KEY_UP) {
				if (history_index > 0) {
					history_index--;
					strcpy(input, input_history[history_index]);
					len = strlen(input);
					pos = len;
				}
			} else if (ch == KEY_DOWN) {
				if (history_index < history_count - 1) {
					history_index++;
					strcpy(input, input_history[history_index]);
					len = strlen(input);
					pos = len;
				} else if (history_index == history_count - 1) {
					history_index++;
					memset(input, 0, sizeof(input));
					len = 0;
					pos = 0;
				}
			} else if (ch == '\t') {
				suggest_files(input, &len, &pos);
			} else if (ch >= 32 && ch <= 126 && len < (int)sizeof(input) - 1) {
				memmove(&input[pos + 1], &input[pos], len - pos);
				input[pos++] = ch;
				len++;
			}
			werase(input_win);
			mvwprintw(input_win, 0, 0, "flash:/> %s", input);
			wmove(input_win, 0, pos + 9);
			wrefresh(input_win);
		}
	}
	return;
}

void *init_prompt(void *arg)
{
	(void)arg;
	init();
	init_logging();
	run_prompt();
	endwin();
	return NULL;
}

void cleanup_exit(void)
{
	if (input_win != NULL) {
		delwin(input_win);
		input_win = NULL;
	}

	if (output_pad != NULL) {
		delwin(output_pad);
		output_pad = NULL;
	}

	if (log_fd != -1) {
		close(log_fd);
		log_fd = -1;
	}

	// Restore original stdout and stderr
	if (original_stdout_fd != -1) {
		dup2(original_stdout_fd, STDOUT_FILENO);
		close(original_stdout_fd);
		original_stdout_fd = -1;
	}

	if (original_stderr_fd != -1) {
		dup2(original_stderr_fd, STDERR_FILENO);
		close(original_stderr_fd);
		original_stderr_fd = -1;
	}

	endwin();
}