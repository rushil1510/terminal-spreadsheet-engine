/*
 * ui/tui/tui.c -- ncurses TUI driver.
 *
 * Responsibilities split across this file and `render.c`:
 *   * tui.c  -- input handling, mode switching, viewport math, lifecycle.
 *   * render.c -- pure drawing routines that read from g_tui and the
 *                 workbook.
 *
 * The viewport is panned lazily: render.c only ever touches cells in the
 * visible window, so very large workbooks still cost O(visible_rows *
 * visible_cols) per redraw.
 */
#include "tui.h"

#include "../../core/config.h"
#include "../../core/helpers.h"
#include "../../engine/engine.h"
#include "../../parser/command.h"
#include "../../parser/label.h"
#include "../../workbook/workbook.h"

#include <ncurses.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

TuiState *g_tui = NULL;

/* --- history ring ------------------------------------------------------- */

static void history_push(const CommandReport rep) {
    const int idx = (g_tui->cmd_history_start + g_tui->cmd_history_count)
                  % g_cmd_history_size;
    if (g_tui->cmd_history_count < g_cmd_history_size) {
        g_tui->cmd_history_count += 1;
    } else {
        g_tui->cmd_history_start =
            (g_tui->cmd_history_start + 1) % g_cmd_history_size;
    }
    strncpy(g_tui->cmd_history[idx].command, rep.command, CMD_BUFFER_SIZE - 1);
    g_tui->cmd_history[idx].command[CMD_BUFFER_SIZE - 1] = '\0';
    g_tui->cmd_history[idx].time_taken = rep.time_taken;
    g_tui->cmd_history[idx].status     = rep.status;
    if (rep.error_msg[0] != '\0') {
        strncpy(g_tui->cmd_history[idx].error_msg, rep.error_msg, 63);
        g_tui->cmd_history[idx].error_msg[63] = '\0';
    } else {
        g_tui->cmd_history[idx].error_msg[0] = '\0';
    }
}

/* --- lifecycle ---------------------------------------------------------- */

static void tui_state_init(void) {
    g_tui = malloc(sizeof(TuiState));
    if (!g_tui) return;

    short max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    g_tui->grid_win    = newwin(g_viewport_rows + 4, max_x, 0, 0);
    g_tui->status_win  = newwin(7, max_x, g_viewport_rows + 4, 0);
    g_tui->command_win = newwin(g_cmd_history_size + 1, max_x,
                                max_y - (g_cmd_history_size + 1), 0);
    g_tui->debug_win   = newwin(5, max_x, g_viewport_rows + 12, 0);

    keypad(g_tui->grid_win, TRUE);
    keypad(g_tui->command_win, TRUE);

    g_tui->cmd_history = malloc(sizeof(CommandReport) * (size_t) g_cmd_history_size);
    g_tui->curr_row    = 0;
    g_tui->curr_col    = 0;
    g_tui->mode        = TUI_INTERACTIVE;
    g_tui->cmd_pos     = 0;
    g_tui->command_input[0] = '\0';
    g_tui->viewport    = (TuiViewport){
        .start_row    = 0,
        .start_col    = 0,
        .visible_rows = g_viewport_rows,
        .visible_cols = (short)(g_viewport_rows > g_total_cols
                                ? g_total_cols : g_viewport_rows),
        .cell_width   = DEFAULT_CELL_WIDTH
    };
    g_tui->cmd_history_count = 0;
    g_tui->cmd_history_start = 0;
}

static void tui_state_destroy(void) {
    if (!g_tui) return;
    delwin(g_tui->grid_win);
    delwin(g_tui->status_win);
    delwin(g_tui->command_win);
    delwin(g_tui->debug_win);
    free(g_tui->cmd_history);
    free(g_tui);
    g_tui = NULL;
}

/* --- viewport math ------------------------------------------------------ */

static void cursor_move(const short d_row, const short d_col) {
    const short new_row = (short) int_max(int_min(g_tui->curr_row + d_row, g_total_rows - 1), 0);
    const short new_col = (short) int_max(int_min(g_tui->curr_col + d_col, g_total_cols - 1), 0);

    /* Auto-scroll the viewport when the cursor crosses an edge. */
    if (new_row < g_tui->viewport.start_row) {
        g_tui->viewport.start_row = new_row;
    } else if (new_row >= g_tui->viewport.start_row + g_tui->viewport.visible_rows) {
        g_tui->viewport.start_row = (short)(new_row - g_tui->viewport.visible_rows + 1);
    }
    if (new_col < g_tui->viewport.start_col) {
        g_tui->viewport.start_col = new_col;
    } else if (new_col >= g_tui->viewport.start_col + g_tui->viewport.visible_cols) {
        g_tui->viewport.start_col = (short)(new_col - g_tui->viewport.visible_cols + 1);
    }
    g_tui->curr_row = new_row;
    g_tui->curr_col = new_col;
}

static void viewport_pan(const short d_row, const short d_col) {
    g_tui->viewport.start_row = (short) int_max(
        int_min(g_tui->viewport.start_row + d_row,
                g_total_rows - g_tui->viewport.visible_rows), 0);
    g_tui->viewport.start_col = (short) int_max(
        int_min(g_tui->viewport.start_col + d_col,
                g_total_cols - g_tui->viewport.visible_cols), 0);
}

static void cells_zoom(const short delta) {
    g_tui->viewport.cell_width = (short) int_max(
        int_min(g_tui->viewport.cell_width + delta, MAX_CELL_WIDTH),
        MIN_CELL_WIDTH);
}

/* --- command-mode processing ------------------------------------------- */

static int process_command_line(void) {
    char         cmd[CMD_BUFFER_SIZE];
    char         err_msg[64];
    CommandReport report;
    strcpy(cmd, g_tui->command_input);

    /* WASD: viewport pan. */
    if (strlen(cmd) == 1 && strchr("wasd", cmd[0])) {
        const char c = cmd[0];
        viewport_pan(c == 'w' ? -g_scroll_amount :
                     c == 's' ?  g_scroll_amount : 0,
                     c == 'a' ? -g_scroll_amount :
                     c == 'd' ?  g_scroll_amount : 0);
    } else if (strlen(cmd) == 1 && cmd[0] == 'q') {
        return 0;
    } else if (strcmp(cmd, "disable_output") == 0
            || strcmp(cmd, "enable_output")  == 0) {
        /* No-op in TUI: the grid is always rendered. */
    } else if (strncmp(cmd, "scroll_to", 9) == 0) {
        if (cmd[9] != ' ') {
            strcpy(err_msg, "Invalid scroll argument");
            goto report_error;
        }
        short row, col;
        if (!label_parse_reference(cmd + 10, &row, &col)) {
            strcpy(err_msg, "Invalid scroll argument");
            goto report_error;
        }
        if (row < 0 || row >= g_total_rows || col < 0 || col >= g_total_cols) {
            strcpy(err_msg, "Cell Reference Out of Bounds");
            goto report_error;
        }
        g_tui->curr_row = row;
        g_tui->curr_col = col;
        g_tui->viewport.start_row = (short) int_max(
            int_min(row - g_tui->viewport.visible_rows / 2,
                    g_total_rows - g_tui->viewport.visible_rows), 0);
        g_tui->viewport.start_col = (short) int_max(
            int_min(col - g_tui->viewport.visible_cols / 2,
                    g_total_cols - g_tui->viewport.visible_cols), 0);
    } else {
        char *eq = strchr(g_tui->command_input, '=');
        const int eq_count = char_count(g_tui->command_input, '=');
        if (eq && eq_count == 1) {
            const int eq_pos = (int)(eq - g_tui->command_input);
            *eq = '\0';
            short row, col;
            const int consumed = label_parse_reference(g_tui->command_input, &row, &col);
            *eq = '=';
            if (!consumed || eq_pos != consumed) {
                strcpy(err_msg, "Invalid Expression");
                goto report_error;
            }
            if (row < 0 || row >= g_total_rows || col < 0 || col >= g_total_cols) {
                strcpy(err_msg, "Cell Reference Out of Bounds");
                goto report_error;
            }
            report = command_dispatch(cmd);
            history_push(report);
        } else {
            strcpy(err_msg, "Unrecognized Command");
            goto report_error;
        }
    }
    g_tui->command_input[0] = '\0';
    g_tui->cmd_pos          = 0;
    return 1;

report_error:
    report.status     = 0;
    report.time_taken = 0.0;
    strcpy(report.error_msg, err_msg);
    strcpy(report.command,   cmd);
    history_push(report);
    return 1;
}

/* --- input dispatch ----------------------------------------------------- */

static int interactive_input(const int ch) {
    switch (ch) {
        case '\t': g_tui->mode = TUI_COMMAND; break;
        case 'w':  cursor_move(-1,  0); break;
        case 's':  cursor_move( 1,  0); break;
        case 'a':  cursor_move( 0, -1); break;
        case 'd':  cursor_move( 0,  1); break;
        case 'W':  viewport_pan(-1,  0); break;
        case 'S':  viewport_pan( 1,  0); break;
        case 'A':  viewport_pan( 0, -1); break;
        case 'D':  viewport_pan( 0,  1); break;
        case '+':  cells_zoom( 1); break;
        case '-':  cells_zoom(-1); break;
        case 'q':
        case 'Q':  return 0;
        case '\n': {
            char input[INPUT_BUFFER_SIZE] = {0};
            echo();
            mvwprintw(g_tui->grid_win, 12, 0, "[");
            wattron(g_tui->grid_win, COLOR_PAIR(4));
            wprintw(g_tui->grid_win, "Expression");
            wattroff(g_tui->grid_win, COLOR_PAIR(4));
            wprintw(g_tui->grid_win, "]> ");
            wrefresh(g_tui->grid_win);
            wgetnstr(g_tui->grid_win, input, INPUT_BUFFER_SIZE - 1);
            noecho();

            char col_label[MAX_COL_LABEL];
            column_index_to_label(g_tui->curr_col, col_label);
            char cmd[CMD_BUFFER_SIZE];
            snprintf(cmd, CMD_BUFFER_SIZE, "%s%d=%s",
                     col_label, g_tui->curr_row + 1, input);
            strcpy(g_tui->command_input, cmd);
            process_command_line();
            break;
        }
        case KEY_BACKSPACE:
        case 127: {
            /* Quick-clear the focused cell back to 0. */
            char col_label[MAX_COL_LABEL];
            column_index_to_label(g_tui->curr_col, col_label);
            char cmd[CMD_BUFFER_SIZE];
            snprintf(cmd, CMD_BUFFER_SIZE, "%s%d=0",
                     col_label, g_tui->curr_row + 1);
            strcpy(g_tui->command_input, cmd);
            process_command_line();
            g_tui->command_input[0] = '\0';
            g_tui->cmd_pos          = 0;
            break;
        }
        default: break;
    }
    return 1;
}

static int command_input(const int ch) {
    switch (ch) {
        case '\t':
            g_tui->mode = TUI_INTERACTIVE;
            break;
        case '\n':
            if (!process_command_line()) return 0;
            g_tui->command_input[0] = '\0';
            g_tui->cmd_pos          = 0;
            break;
        case KEY_BACKSPACE:
        case 127:
            if (g_tui->cmd_pos > 0) {
                g_tui->command_input[--g_tui->cmd_pos] = '\0';
            }
            break;
        default:
            if (g_tui->cmd_pos < CMD_BUFFER_SIZE - 1 && ch >= 32 && ch <= 126) {
                g_tui->command_input[g_tui->cmd_pos++] = (char) ch;
                g_tui->command_input[g_tui->cmd_pos]   = '\0';
            }
            break;
    }
    return 1;
}

/* --- public entry point ------------------------------------------------- */

static void ncurses_boot(void) {
    initscr();
    start_color();
    use_default_colors();
    init_pair(1, COLOR_BLUE,    -1);
    init_pair(2, COLOR_GREEN,   -1);
    init_pair(3, COLOR_RED,     -1);
    init_pair(4, COLOR_MAGENTA, -1);
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    tui_state_init();
    if (!g_tui) {
        endwin();
        fprintf(stderr, "tui: failed to initialise display state\n");
    }
}

void tui_run(void) {
    ncurses_boot();
    refresh();
    int alive = 1;
    while (alive) {
        tui_draw_all();
        const int ch = getch();
        alive = (g_tui->mode == TUI_INTERACTIVE)
              ? interactive_input(ch)
              : command_input(ch);
    }
    tui_state_destroy();
    endwin();
}
