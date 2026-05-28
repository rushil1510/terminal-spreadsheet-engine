/*
 * ui/tui/render.c -- All drawing routines for the TUI front-end.
 *
 * Split from tui.c so that the input/lifecycle code reads cleanly. Every
 * function here reads from g_tui and the workbook; none mutate state apart
 * from `werase`/`wrefresh` calls on the ncurses windows.
 */
#include "tui.h"

#include "../../core/config.h"
#include "../../core/helpers.h"
#include "../../engine/engine.h"
#include "../../parser/command.h"
#include "../../parser/label.h"
#include "../../workbook/workbook.h"
#include "../../containers/avl_set.h"

#include <math.h>
#include <ncurses.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

/* --- One cell --------------------------------------------------------- */

static void draw_cell_box(const Cell *cell,
                          const short y, const short x, const short width,
                          const int color_pair, const int reverse) {
    if (cell->cell_state == CS_ZERO_ERROR) {
        wattron(g_tui->grid_win, COLOR_PAIR(3));
        if (reverse) wattron(g_tui->grid_win, A_REVERSE);
        mvwprintw(g_tui->grid_win, y, x, "%-*s", width - 1, "ERR");
        wattroff(g_tui->grid_win, COLOR_PAIR(3));
        if (reverse) wattroff(g_tui->grid_win, A_REVERSE);
        return;
    }

    if (reverse)    wattron(g_tui->grid_win, A_REVERSE);
    if (color_pair) wattron(g_tui->grid_win, COLOR_PAIR(color_pair));

    /* Count decimal digits to decide whether the value fits in the column. */
    int   probe   = cell->value;
    short digits  = 0;
    while (probe) {
        digits  += 1;
        probe   /= 10;
    }
    if (cell->value == 0) digits = 1;
    digits += (cell->value < 0);

    if (!reverse && digits > width - 1) {
        const int scale = (int) pow(10, digits - width + 3);
        mvwprintw(g_tui->grid_win, y, x, "%d..", cell->value / scale);
    } else {
        mvwprintw(g_tui->grid_win, y, x, "%-*d", width - 1, cell->value);
    }

    if (color_pair) wattroff(g_tui->grid_win, COLOR_PAIR(color_pair));
    if (reverse)    wattroff(g_tui->grid_win, A_REVERSE);
}

/* --- Grid pane -------------------------------------------------------- */

static int color_for_state(const enum CellWalkState s) {
    if (s == CS_ZERO_ERROR) return 3;
    if (s != CS_CLEAN)      return 1;
    return 0;
}

static void render_grid(void) {
    werase(g_tui->grid_win);

    /* Column header row. */
    for (short j = 0; j < g_tui->viewport.visible_cols; ++j) {
        const short actual_col = (short)(j + g_tui->viewport.start_col);
        char label[MAX_COL_LABEL];
        column_index_to_label(actual_col, label);
        wattron(g_tui->grid_win, COLOR_PAIR(2));
        mvwprintw(g_tui->grid_win, 0,
                  (j + 1) * g_tui->viewport.cell_width, "%s", label);
        wattroff(g_tui->grid_win, COLOR_PAIR(2));
    }

    /* Eager touch (cheap because the engine caches values inside Cell). */
    for (short i = 0; i < g_tui->viewport.visible_rows; ++i) {
        for (short j = 0; j < g_tui->viewport.visible_cols; ++j) {
            engine_cell_value(rowcol_to_index(
                (short)(i + g_tui->viewport.start_row),
                (short)(j + g_tui->viewport.start_col)));
        }
    }

    for (short i = 0; i < g_tui->viewport.visible_rows; ++i) {
        const short actual_row = (short)(i + g_tui->viewport.start_row);
        wattron(g_tui->grid_win, COLOR_PAIR(2));
        mvwprintw(g_tui->grid_win, i + 1, 0, "%4d", actual_row + 1);
        wattroff(g_tui->grid_win, COLOR_PAIR(2));

        for (short j = 0; j < g_tui->viewport.visible_cols; ++j) {
            const short actual_col = (short)(j + g_tui->viewport.start_col);
            const short x = (short)((j + 1) * g_tui->viewport.cell_width);
            const short y = (short)(i + 1);

            int   color_pair = 0;
            int   reverse    = 0;
            Cell *cell       = workbook_cell(rowcol_to_index(actual_row, actual_col));

            if (g_tui->mode == TUI_INTERACTIVE) {
                if (actual_row == g_tui->curr_row && actual_col == g_tui->curr_col) {
                    reverse = 1;
                }
                color_pair = color_for_state(cell->cell_state);
            }
            draw_cell_box(cell, y, x, g_tui->viewport.cell_width, color_pair, reverse);
        }
    }

    /* If the cursor is in view in interactive mode, redraw it with reverse
     * highlight so it is visible regardless of which other styling applies. */
    if (g_tui->mode == TUI_INTERACTIVE
        && g_tui->curr_row >= g_tui->viewport.start_row
        && g_tui->curr_row <  g_tui->viewport.start_row + g_tui->viewport.visible_rows
        && g_tui->curr_col >= g_tui->viewport.start_col
        && g_tui->curr_col <  g_tui->viewport.start_col + g_tui->viewport.visible_cols) {
        Cell *focus = workbook_cell(rowcol_to_index(g_tui->curr_row, g_tui->curr_col));
        draw_cell_box(focus,
                      (short)(g_tui->curr_row - g_tui->viewport.start_row + 1),
                      (short)((g_tui->curr_col - g_tui->viewport.start_col + 1)
                              * g_tui->viewport.cell_width),
                      g_tui->viewport.cell_width,
                      color_for_state(focus->cell_state),
                      /*reverse=*/1);
    }

    wrefresh(g_tui->grid_win);
}

/* --- Status pane ------------------------------------------------------ */

static void render_status(void) {
    werase(g_tui->status_win);
    char col_label[MAX_COL_LABEL];
    column_index_to_label(g_tui->curr_col, col_label);
    const int focus_idx = rowcol_to_index(g_tui->curr_row, g_tui->curr_col);
    Cell *focus = workbook_cell(focus_idx);

    wattron(g_tui->status_win, COLOR_PAIR(2));
    mvwprintw(g_tui->status_win, 0, 0, "Current Cell:     ");
    mvwprintw(g_tui->status_win, 1, 0, "Cell Value:       ");
    mvwprintw(g_tui->status_win, 2, 0, "Cell Expression:  ");
    wattroff(g_tui->status_win, COLOR_PAIR(2));

    mvwprintw(g_tui->status_win, 0, 18, "%s%d", col_label, g_tui->curr_row + 1);
    if (focus->cell_state == CS_ZERO_ERROR)
        mvwprintw(g_tui->status_win, 1, 18, "ERR");
    else
        mvwprintw(g_tui->status_win, 1, 18, "%d", focus->value);
    mvwprintw(g_tui->status_win, 2, 18, "%s", command_format_expression(focus_idx));

    wattron(g_tui->status_win, COLOR_PAIR(2));
    wprintw(g_tui->status_win, "\nDependants:       ");
    wattroff(g_tui->status_win, COLOR_PAIR(2));

    /* Walk the dependants graph in deterministic order. */
    DepIter it;
    dep_iter_begin(&it, focus);
    int printed = 0;
    int idx;
    while ((idx = dep_iter_next(&it)) != -1 && printed < 20) {
        char label[MAX_COL_LABEL];
        column_index_to_label(index_to_col(idx), label);
        const enum CellWalkState s = workbook_cell(idx)->cell_state;
        int color = 0;
        if (s == CS_ZERO_ERROR)   { color = 3; wattron(g_tui->status_win, COLOR_PAIR(3)); }
        else if (s != CS_CLEAN)   { color = 1; wattron(g_tui->status_win, COLOR_PAIR(1)); }
        wprintw(g_tui->status_win, "%s%d ", label, index_to_row(idx) + 1);
        if (color) wattroff(g_tui->status_win, COLOR_PAIR(color));
        printed += 1;
    }
    /* If the iterator still has data we didn't print, signal truncation. */
    if (dep_iter_next(&it) != -1) wprintw(g_tui->status_win, "...");
    dep_iter_end(&it);

    /* Dependant count line. */
    size_t total_deps =
        focus->dependants_type == DK_ARRAY ? (size_t) focus->dependants_array->size
                                           : avl_set_size(focus->dependants_set);
    wattron(g_tui->status_win, COLOR_PAIR(2));
    wprintw(g_tui->status_win, "\nDependant Count:  ");
    wattroff(g_tui->status_win, COLOR_PAIR(2));
    wprintw(g_tui->status_win, "%lu", (unsigned long) total_deps);

    wrefresh(g_tui->status_win);
}

/* --- Command-history pane --------------------------------------------- */

static void render_history(void) {
    wclear(g_tui->command_win);
    const int top_offset = g_cmd_history_size - g_tui->cmd_history_count;
    for (int i = 0; i < g_tui->cmd_history_count; ++i) {
        const int idx = (g_tui->cmd_history_start + i) % g_cmd_history_size;
        CommandReport *r = &g_tui->cmd_history[idx];

        mvwprintw(g_tui->command_win, top_offset + i, 0, "~$ %s ", r->command);
        wattron(g_tui->command_win, COLOR_PAIR(1));
        wprintw(g_tui->command_win, "[%.1fs]", r->time_taken);
        wattroff(g_tui->command_win, COLOR_PAIR(1));

        wprintw(g_tui->command_win, " [");
        if (r->status) {
            wattron(g_tui->command_win, COLOR_PAIR(2));
            wprintw(g_tui->command_win, "ok");
            wattroff(g_tui->command_win, COLOR_PAIR(2));
        } else {
            wattron(g_tui->command_win, COLOR_PAIR(3));
            wprintw(g_tui->command_win, "error: %s", r->error_msg);
            wattroff(g_tui->command_win, COLOR_PAIR(3));
        }
        wprintw(g_tui->command_win, "]");
    }
    if (g_tui->mode == TUI_COMMAND) {
        wattron(g_tui->command_win, COLOR_PAIR(4));
        mvwprintw(g_tui->command_win, g_cmd_history_size, 0, "~$ ");
        wattroff(g_tui->command_win, COLOR_PAIR(4));
        wprintw(g_tui->command_win, "%s", g_tui->command_input);
    }
    wrefresh(g_tui->command_win);
}

/* --- Public --------------------------------------------------------- */

void tui_debug(const char *fmt, ...) {
    if (!g_debug_gui) return;
    va_list args;
    va_start(args, fmt);
    wclear(g_tui->debug_win);
    vw_printw(g_tui->debug_win, fmt, args);
    va_end(args);
    wrefresh(g_tui->debug_win);
}

void tui_draw_all(void) {
    render_grid();
    render_status();
    render_history();
}
