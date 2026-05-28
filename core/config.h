/*
 * core/config.h -- Global runtime configuration knobs and compile-time limits.
 *
 * The spreadsheet engine relies on a handful of process-wide variables that
 * describe the workbook dimensions, the scroll behavior, and which UI surface
 * was selected. They are populated by the entry-point in apps/main_*.c after
 * parsing argv and then read throughout the engine, parser, and UI layers.
 *
 * Compile-time limits (cell widths, buffer sizes) live as #defines so they can
 * size stack buffers without dynamic allocation overhead.
 */
#ifndef TSE_CORE_CONFIG_H
#define TSE_CORE_CONFIG_H

#include <time.h>

/* --- Display layout limits ---------------------------------------------- */
#define MIN_CELL_WIDTH        5
#define MAX_CELL_WIDTH        12
#define DEFAULT_CELL_WIDTH    8

/* --- Input/Buffer sizes ------------------------------------------------- */
#define CMD_BUFFER_SIZE       256
#define INPUT_BUFFER_SIZE     64
#define MAX_COL_LABEL         4

/* --- Process-wide runtime knobs ----------------------------------------- *
 * Defined in core/config.c and populated by the application entry-points.  */
extern short g_scroll_amount;        /* Rows/cols moved per WASD keystroke   */
extern short g_cmd_history_size;     /* Lines of TUI command history shown   */
extern short g_viewport_rows;        /* Rows visible in the viewport         */
extern short g_debug_gui;            /* 1 = enable the TUI debug pane        */
extern short g_gui_mode;             /* 1 = TUI binary, 0 = CLI/test binary  */
extern short g_total_rows;           /* Workbook row count   (from argv[1])  */
extern short g_total_cols;           /* Workbook column count(from argv[2])  */
extern short g_lazy_evaluation;      /* Reserved; matches original feature   */

#endif /* TSE_CORE_CONFIG_H */
