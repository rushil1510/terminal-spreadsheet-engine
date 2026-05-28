/*
 * apps/main_gui.c -- Entry point for the ncurses TUI binary (`./sheet`).
 *
 * Sets g_gui_mode so the parser records command latencies, parses argv,
 * then hands control to the TUI driver.
 */
#include "../core/config.h"
#include "../ui/tui/tui.h"
#include "../workbook/workbook.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(const char *program_name) {
    printf("Usage: %s R C [options]\n", program_name);
    printf("Options:\n");
    printf("  -s, --scroll <n>            Rows/cols moved per WASD              (default 10)\n");
    printf("  -c, --command-history <n>   Lines of command history shown        (default  7)\n");
    printf("  -v, --viewport <n>          Maximum visible rows/cols             (default 10)\n");
    printf("  -d, --debug <0/1>           Show the debug pane                   (default  0)\n");
    printf("  -l, --lazy <0/1>            Lazy evaluation toggle (reserved)     (default  1)\n");
    printf("  -h, --help                  Show this help message\n");
}

static void parse_arguments(const int argc, char *argv[]) {
    if (argc < 3) {
        print_usage(argv[0]);
        exit(1);
    }
    g_total_rows = (short) strtol(argv[1], NULL, 10);
    g_total_cols = (short) strtol(argv[2], NULL, 10);
    for (int i = 3; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            exit(0);
        }
        if ((strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--scroll") == 0)
            && i + 1 < argc) {
            g_scroll_amount = (short) atoi(argv[++i]);
        } else if ((strcmp(argv[i], "-c") == 0
                 || strcmp(argv[i], "--command-history") == 0)
                && i + 1 < argc) {
            g_cmd_history_size = (short) atoi(argv[++i]);
        } else if ((strcmp(argv[i], "-v") == 0
                 || strcmp(argv[i], "--viewport") == 0)
                && i + 1 < argc) {
            const short v = (short) atoi(argv[++i]);
            g_viewport_rows = v < g_total_rows ? v : g_total_rows;
        } else if ((strcmp(argv[i], "-d") == 0
                 || strcmp(argv[i], "--debug") == 0)
                && i + 1 < argc) {
            g_debug_gui = (short) atoi(argv[++i]);
        } else if ((strcmp(argv[i], "-l") == 0
                 || strcmp(argv[i], "--lazy") == 0)
                && i + 1 < argc) {
            g_lazy_evaluation = (short) atoi(argv[++i]);
        } else {
            printf("Unknown argument: %s\n", argv[i]);
            print_usage(argv[0]);
            exit(1);
        }
    }
}

int main(const int argc, char *argv[]) {
    g_gui_mode = 1;
    parse_arguments(argc, argv);
    workbook_init();
    tui_run();
    workbook_destroy();
    return 0;
}
