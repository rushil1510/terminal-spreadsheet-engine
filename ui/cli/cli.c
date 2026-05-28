/*
 * ui/cli/cli.c -- CLI REPL implementation.
 *
 * The loop reads one line per iteration and dispatches to one of:
 *   * Viewport movement: a single character from {w, a, s, d}.
 *   * `q`                                                    -- exit.
 *   * `disable_output` / `enable_output`                     -- output toggle.
 *   * `scroll_to <CELL>`                                     -- jump viewport.
 *   * Anything else                                          -- parser/engine.
 *
 * After each successful step the grid is reprinted, prefixed by a status line
 * showing how long the previous command took and whether it succeeded.
 */
#include "cli.h"

#include "../../core/config.h"
#include "../../core/helpers.h"
#include "../../engine/engine.h"
#include "../../parser/command.h"
#include "../../parser/label.h"
#include "../../workbook/workbook.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

ViewportState *g_view = NULL;
static CommandReport last_report;

/* --- Lifecycle ---------------------------------------------------------- */

static void view_init(void) {
    g_view = malloc(sizeof(ViewportState));
    if (!g_view) {
        fprintf(stderr, "cli: failed to allocate viewport state\n");
        return;
    }
    last_report = (CommandReport){ .command = "", .time_taken = 0.0,
                                   .status = 0, .error_msg = "" };
    g_view->start_row      = 0;
    g_view->start_col      = 0;
    g_view->visible_rows   = g_viewport_rows > g_total_rows ? g_total_rows : g_viewport_rows;
    g_view->visible_cols   = g_viewport_rows > g_total_cols ? g_total_cols : g_viewport_rows;
    g_view->output_enabled = 1;
}

static void view_destroy(void) {
    free(g_view);
    g_view = NULL;
}

/* --- Viewport math ----------------------------------------------------- */

static void view_jump(const short row, const short col) {
    g_view->start_row    = (short) int_max(int_min(row, g_total_rows - 1), 0);
    g_view->start_col    = (short) int_max(int_min(col, g_total_cols - 1), 0);
    g_view->visible_rows = (short) int_min(g_viewport_rows, g_total_rows - g_view->start_row);
    g_view->visible_cols = (short) int_min(g_viewport_rows, g_total_cols - g_view->start_col);
}

static void view_handle_wasd(const char ch) {
    switch (ch) {
        case 'w':
            if (g_view->start_row != 0)
                view_jump((short)(g_view->start_row - g_scroll_amount), g_view->start_col);
            break;
        case 's':
            if (g_view->start_row != g_total_rows - 1)
                view_jump((short)(g_view->start_row + g_scroll_amount), g_view->start_col);
            break;
        case 'a':
            if (g_view->start_col != 0)
                view_jump(g_view->start_row, (short)(g_view->start_col - g_scroll_amount));
            break;
        case 'd':
            if (g_view->start_col != g_total_cols - 1)
                view_jump(g_view->start_row, (short)(g_view->start_col + g_scroll_amount));
            break;
        default:
            break;
    }
}

/* --- Drawing ----------------------------------------------------------- */

static void render_grid(void) {
    if (!g_view->output_enabled) return;

    /* Column header row, indented past the row-label column. */
    printf("%-*s", DEFAULT_CELL_WIDTH, "");
    for (short j = 0; j < g_view->visible_cols; ++j) {
        const short actual_col = (short)(j + g_view->start_col);
        char label[MAX_COL_LABEL];
        column_index_to_label(actual_col, label);
        printf("%-*s", DEFAULT_CELL_WIDTH, label);
    }

    for (short i = 0; i < g_view->visible_rows; ++i) {
        const short actual_row = (short)(i + g_view->start_row);
        printf("\n%-*d", DEFAULT_CELL_WIDTH, actual_row + 1);
        for (short j = 0; j < g_view->visible_cols; ++j) {
            const short actual_col = (short)(j + g_view->start_col);
            const int   value      = engine_cell_value(rowcol_to_index(actual_row, actual_col));
            const Cell *cell       = workbook_cell(rowcol_to_index(actual_row, actual_col));
            if (cell->cell_state == CS_ZERO_ERROR)
                printf("%-*s", DEFAULT_CELL_WIDTH, "ERR");
            else
                printf("%-*d", DEFAULT_CELL_WIDTH, value);
        }
    }
    printf("\n");
}

/* --- REPL --------------------------------------------------------------- */

static int repl_once(void) {
    printf("[%.1f] (%s) > ", last_report.time_taken,
           last_report.status ? "ok" : last_report.error_msg);

    char line[CMD_BUFFER_SIZE];
    if (!fgets(line, CMD_BUFFER_SIZE, stdin)) return 0;
    /* Strip the trailing newline. */
    char *p = line;
    while (*p != '\n' && *p != '\0') ++p;
    *p = '\0';
    strcpy(last_report.command, line);

    struct timespec t0, t1, dt;
    clock_gettime(CLOCK_REALTIME, &t0);

    char  err_msg[64];
    if (strlen(line) == 1 && strchr("wasd", line[0])) {
        view_handle_wasd(line[0]);
        /* Touch every visible cell -- in the original this gave lazy
         * evaluation a chance to refresh values for the new viewport. */
        for (short i = 0; i < g_view->visible_rows; ++i) {
            for (short j = 0; j < g_view->visible_cols; ++j) {
                engine_cell_value(
                    rowcol_to_index((short)(i + g_view->start_row),
                                    (short)(j + g_view->start_col)));
            }
        }
        last_report.status = 1;
    } else if (strlen(line) == 1 && line[0] == 'q') {
        return 0;
    } else if (strlen(line) == 0) {
        strcpy(err_msg, "Empty Command");
        goto report_error;
    } else if (strcmp(line, "disable_output") == 0) {
        g_view->output_enabled = 0;
        last_report.status     = 1;
        last_report.time_taken = 0.0;
    } else if (strcmp(line, "enable_output") == 0) {
        g_view->output_enabled = 1;
        last_report.status     = 1;
        last_report.time_taken = 0.0;
    } else if (strncmp(line, "scroll_to", 9) == 0) {
        if (line[9] != ' ') {
            strcpy(err_msg, "Invalid scroll argument");
            goto report_error;
        }
        short row, col;
        if (!label_parse_reference(line + 10, &row, &col)) {
            strcpy(err_msg, "Invalid scroll argument");
            goto report_error;
        }
        if (row < 0 || row >= g_total_rows || col < 0 || col >= g_total_cols) {
            strcpy(err_msg, "Cell Reference Out of Bounds");
            goto report_error;
        }
        view_jump(row, col);
        last_report.status     = 1;
        last_report.time_taken = 0.0;
    } else {
        last_report = command_dispatch(line);
    }

    render_grid();
    clock_gettime(CLOCK_REALTIME, &t1);
    last_report.time_taken = timespec_diff_sec(t0, t1, &dt);
    return 1;

report_error:
    render_grid();
    last_report.status     = 0;
    last_report.time_taken = 0.0;
    strcpy(last_report.error_msg, err_msg);
    strcpy(last_report.command, line);
    return 1;
}

void cli_run(void) {
    view_init();
    if (!g_view) return;
    render_grid();
    last_report = (CommandReport){ .command = "", .time_taken = 0.0,
                                   .status = 1, .error_msg = "" };
    while (repl_once()) { /* loop */ }
    view_destroy();
}
