/*
 * ui/tui/tui.h -- ncurses-based TUI front-end.
 *
 * Public surface is intentionally tiny: a single tui_run() entry point.
 * The internal DisplayState lives next to the renderer so the two
 * translation units (`tui.c` and `render.c`) can share it cheaply.
 *
 * Modes:
 *   INTERACTIVE -- WASD moves the cursor cell; SHIFT + WASD pans the viewport;
 *                  ENTER opens an inline editor on the focused cell; +/-
 *                  resizes columns; BACKSPACE clears a cell to 0; TAB enters
 *                  COMMAND mode.
 *   COMMAND     -- characters accumulate into a command line; ENTER submits
 *                  it to the parser. TAB returns to INTERACTIVE.
 */
#ifndef TSE_UI_TUI_H
#define TSE_UI_TUI_H

#include <ncurses.h>
#include "../../core/config.h"
#include "../../parser/command.h"

typedef struct {
    short start_row;
    short start_col;
    short visible_rows;
    short visible_cols;
    short cell_width;
} TuiViewport;

typedef enum {
    TUI_INTERACTIVE,
    TUI_COMMAND
} TuiMode;

typedef struct {
    WINDOW *grid_win;
    WINDOW *status_win;
    WINDOW *command_win;
    WINDOW *debug_win;
    short   curr_row;
    short   curr_col;
    TuiMode mode;
    char    command_input[CMD_BUFFER_SIZE];
    short   cmd_pos;
    TuiViewport viewport;
    CommandReport *cmd_history;
    int            cmd_history_count;
    int            cmd_history_start;
} TuiState;

extern TuiState *g_tui;

void tui_run        (void);
void tui_draw_all   (void);
void tui_debug      (const char *fmt, ...);

#endif /* TSE_UI_TUI_H */
