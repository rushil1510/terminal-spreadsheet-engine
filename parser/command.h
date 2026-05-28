/*
 * parser/command.h -- Command parsing + pretty-printing of stored formulas.
 *
 * `command_dispatch` takes a raw line like `A1=B2+5` and routes it to the
 * appropriate engine.h setter. The grammar accepts:
 *   <CELL> = <int>
 *   <CELL> = <CELL>
 *   <CELL> = <int>   <op> <int>
 *   <CELL> = <int>   <op> <CELL>
 *   <CELL> = <CELL>  <op> <int>
 *   <CELL> = <CELL>  <op> <CELL>
 *   <CELL> = SUM|AVG|MIN|MAX|STDEV ( <CELL> : <CELL> )
 *   <CELL> = SLEEP ( <int> | <CELL> )
 * where <op> in { +, -, *, / }.
 *
 * `command_format_expression` is the inverse: it reads a cell's bit-packed
 * formula and produces a stable textual rendering used by the TUI's status
 * pane and by the test harness output.
 */
#ifndef TSE_PARSER_COMMAND_H
#define TSE_PARSER_COMMAND_H

#include "../core/config.h"

typedef struct {
    char   command[CMD_BUFFER_SIZE];
    double time_taken;
    int    status;                 /* 1 = ok, 0 = error */
    char   error_msg[64];
} CommandReport;

CommandReport command_dispatch          (const char *line);
char         *command_format_expression (int cell_index);

#endif /* TSE_PARSER_COMMAND_H */
