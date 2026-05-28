/*
 * ui/cli/cli.h -- Plain-text CLI front-end.
 *
 * The CLI keeps a tiny `ViewportState` that tracks the top-left visible row
 * and column. The viewport is shifted by WASD keystrokes (or hopped to with
 * `scroll_to <CELL>`); every other line is forwarded to the parser. This is
 * the front-end the test harness drives via redirected stdin.
 */
#ifndef TSE_UI_CLI_H
#define TSE_UI_CLI_H

typedef struct {
    short start_row;        /* topmost row currently visible             */
    short start_col;        /* leftmost column currently visible         */
    short visible_rows;     /* number of rows the viewport currently shows */
    short visible_cols;     /* number of columns the viewport shows        */
    int   output_enabled;   /* `disable_output` toggles this flag          */
} ViewportState;

extern ViewportState *g_view;

void cli_run(void);

#endif /* TSE_UI_CLI_H */
