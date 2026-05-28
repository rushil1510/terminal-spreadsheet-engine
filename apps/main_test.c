/*
 * apps/main_test.c -- Non-interactive test driver.
 *
 * Input format (stdin):
 *   T                        -- number of test cases
 *   for each case:
 *     N M                    -- N edits, M queries
 *     <N edit lines>         -- formulas or `scroll_to <CELL>` commands
 *     <M query lines>        -- bare cell references like "B42"
 *
 * Output format (stdout):
 *   * One line per edit: `[i] > <command> --> (<msg>) [<sec>s]`
 *   * One line per query:
 *     `<CELL> : <expression> --> Value: <int>, State: <state>`
 *
 * The output is consumed by the `tester` binary which compares it against
 * canonical model outputs.
 */
#include "../core/config.h"
#include "../core/helpers.h"
#include "../engine/engine.h"
#include "../parser/command.h"
#include "../parser/label.h"
#include "../workbook/workbook.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void parse_arguments(const int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: %s R C [options]\n", argv[0]);
        exit(1);
    }
    g_total_rows    = (short) strtol(argv[1], NULL, 10);
    g_total_cols    = (short) strtol(argv[2], NULL, 10);
    g_viewport_rows = 10;
}

static const char *state_name(const enum CellWalkState s) {
    switch (s) {
        case CS_CLEAN:           return "Clean";
        case CS_DFS_IN_PROGRESS: return "DFS In Progress";
        case CS_CYCLE_CHECKED:   return "Circular Checked";
        case CS_ZERO_ERROR:      return "Zero Error";
    }
    return "Unknown";
}

static void touch_viewport(const short vrow, const short vcol) {
    for (short r = vrow; r < vrow + g_viewport_rows && r < g_total_rows; ++r) {
        for (short c = vcol; c < vcol + g_viewport_rows && c < g_total_cols; ++c) {
            engine_cell_value(rowcol_to_index(r, c));
        }
    }
}

int main(const int argc, char *argv[]) {
    g_gui_mode = 1;        /* mirror original: enable latency timing */
    parse_arguments(argc, argv);

    int t;
    if (scanf("%d", &t) != 1) return 0;
    workbook_init();

    while (t-- > 0) {
        int n, m;
        if (scanf("%d %d\n", &n, &m) != 2) break;

        short v_row = 0;
        short v_col = 0;

        for (int i = 0; i < n; ++i) {
            char line[CMD_BUFFER_SIZE];
            if (!fgets(line, CMD_BUFFER_SIZE, stdin)) break;

            /* Strip the trailing newline. */
            char *nl = line;
            while (*nl != '\n' && *nl != '\0') ++nl;
            *nl = '\0';

            if (strncmp(line, "scroll_to", 9) == 0) {
                short row, col;
                if (!label_parse_reference(line + 10, &row, &col)) continue;
                v_row = row;
                v_col = col;
                struct timespec t0, t1, dt;
                clock_gettime(CLOCK_REALTIME, &t0);
                touch_viewport(v_row, v_col);
                clock_gettime(CLOCK_REALTIME, &t1);
                CommandReport report;
                strcpy(report.command, line);
                strcpy(report.error_msg, "ok");
                report.status     = 1;
                report.time_taken = timespec_diff_sec(t0, t1, &dt);
                printf("[%d] > %s --> (%s) [%.3fs]\n",
                       i + 1, report.command, report.error_msg, report.time_taken);
                continue;
            }

            CommandReport report = command_dispatch(line);
            if (report.status) strcpy(report.error_msg, "ok");
            printf("[%d] > %s --> (%s) [%.3fs]\n",
                   i + 1, report.command, report.error_msg, report.time_taken);
        }

        for (int i = 0; i < m; ++i) {
            char ref[CMD_BUFFER_SIZE];
            scanf("%[^\n]%*c", ref);
            short row, col;
            if (!label_parse_reference(ref, &row, &col)) continue;
            const int idx = rowcol_to_index(row, col);
            const Cell *cell = workbook_cell(idx);
            char expr[CMD_BUFFER_SIZE];
            strcpy(expr, command_format_expression(idx));
            printf("%s : %s --> Value: %d, State: %s\n",
                   ref, expr, cell->value, state_name(cell->cell_state));
        }
    }

    workbook_destroy();
    return 0;
}
