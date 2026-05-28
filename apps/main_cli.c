/*
 * apps/main_cli.c -- Entry point for the plain-text CLI binary (`./sheet`).
 *
 * Boots the workbook with the dimensions from argv, hands control to the
 * CLI REPL (ui/cli/cli.c), then tears the workbook down on exit.
 */
#include "../core/config.h"
#include "../engine/engine.h"
#include "../ui/cli/cli.h"
#include "../workbook/workbook.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(const char *program_name) {
    printf("Usage: %s R C [options]\n", program_name);
    printf("Options:\n");
    printf("  -s, --scroll <n>     Lines to scroll using WASD          (default 10)\n");
    printf("  -v, --viewport <n>   Maximum number of visible rows/cols (default 10)\n");
    printf("  -h, --help           Show this help message\n");
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
        } else if ((strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--viewport") == 0)
                   && i + 1 < argc) {
            const short v = (short) atoi(argv[++i]);
            g_viewport_rows = v < g_total_rows ? v : g_total_rows;
        } else {
            printf("Unknown argument: %s\n", argv[i]);
            print_usage(argv[0]);
            exit(1);
        }
    }
}

int main(const int argc, char *argv[]) {
    g_gui_mode = 0;
    parse_arguments(argc, argv);
    workbook_init();
    cli_run();
    workbook_destroy();
    return 0;
}
