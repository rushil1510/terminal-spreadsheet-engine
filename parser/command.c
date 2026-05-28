/*
 * parser/command.c -- Command grammar parser and formula formatter.
 *
 * Strategy:
 *   * For dispatch, we run each candidate POSIX regex (one per grammar rule)
 *     in turn against the right-hand side of the `=`. The first match wins
 *     and uses scanf to extract the operands. Each rule routes to one of
 *     engine_set_value, engine_set_arithmetic, or engine_set_function.
 *   * The formatter is the inverse mapping; it reads the bit-packed Cell
 *     formula and emits the canonical text. It is used by the TUI to show
 *     the expression in the status pane and by the test driver to print
 *     the cell's formula.
 *
 * Regex compile cost is paid each call. For the workloads we target (one
 * formula per typed line) this is negligible. If profiling ever shows it as
 * a hotspot, precompiling at startup would be a tiny mechanical change.
 */
#include "command.h"

#include "label.h"
#include "../core/config.h"
#include "../core/helpers.h"
#include "../engine/engine.h"
#include "../workbook/workbook.h"

#include <ctype.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* --- Tiny regex wrappers ------------------------------------------------- */

static int regex_full_match(const char *pattern, const char *text) {
    regex_t r;
    if (regcomp(&r, pattern, REG_EXTENDED) != 0) return 0;
    const int rc = regexec(&r, text, 0, NULL, 0);
    regfree(&r);
    return rc == 0;
}

static int regex_prefix_match_len(const char *pattern, const char *text) {
    regex_t r;
    if (regcomp(&r, pattern, REG_EXTENDED) != 0) return 0;
    regmatch_t m;
    const int rc = regexec(&r, text, 1, &m, 0);
    regfree(&r);
    if (rc != 0) return 0;
    return (int) m.rm_eo;
}

/* --- Pretty-printer ----------------------------------------------------- */

static void render_value(char *out, size_t cap, const Value *v) {
    if (v->type == VK_INTEGER) {
        snprintf(out, cap, "%d", v->value);
    } else {
        char col_label[MAX_COL_LABEL];
        column_index_to_label(index_to_col(v->value), col_label);
        snprintf(out, cap, "%s%d", col_label, index_to_row(v->value) + 1);
    }
}

char *command_format_expression(const int cell_index) {
    static char out[CMD_BUFFER_SIZE];
    out[0] = '\0';
    Cell *cell = workbook_cell(cell_index);

    if (cell->expression_type == EK_VALUE) {
        if (cell->val1_type == VK_INTEGER) {
            snprintf(out, CMD_BUFFER_SIZE, "%d", cell->val1);
        } else {
            char col_label[MAX_COL_LABEL];
            column_index_to_label(index_to_col(cell->val1), col_label);
            snprintf(out, CMD_BUFFER_SIZE, "%s%d", col_label,
                     index_to_row(cell->val1) + 1);
        }
        return out;
    }

    if (cell->expression_type == EK_ARITHMETIC) {
        const char op =
            cell->op == AOP_ADD      ? '+' :
            cell->op == AOP_SUBTRACT ? '-' :
            cell->op == AOP_MULTIPLY ? '*' :
                                       '/';
        char left[64], right[64];
        const Value v1 = { cell->val1, cell->val1_type };
        const Value v2 = { cell->val2, cell->val2_type };
        render_value(left,  sizeof(left),  &v1);
        render_value(right, sizeof(right), &v2);
        snprintf(out, CMD_BUFFER_SIZE, "%s %c %s", left, op, right);
        return out;
    }

    /* EK_FUNCTION */
    const int fn = cell->val2_type * 4 + cell->op;
    const char *name =
        fn == FN_MIN   ? "MIN"   :
        fn == FN_MAX   ? "MAX"   :
        fn == FN_AVG   ? "AVG"   :
        fn == FN_SUM   ? "SUM"   :
        fn == FN_STDEV ? "STDEV" :
        fn == FN_SLEEP ? "SLEEP" : "?";

    if (fn == FN_SLEEP) {
        if (cell->val1_type == VK_INTEGER) {
            snprintf(out, CMD_BUFFER_SIZE, "%s(%d)", name, cell->val1);
        } else {
            char col_label[MAX_COL_LABEL];
            column_index_to_label(index_to_col(cell->val1), col_label);
            snprintf(out, CMD_BUFFER_SIZE, "%s(%s%d)", name, col_label,
                     index_to_row(cell->val1) + 1);
        }
        return out;
    }

    char start_label[MAX_COL_LABEL], end_label[MAX_COL_LABEL];
    column_index_to_label(index_to_col(cell->val1), start_label);
    column_index_to_label(index_to_col(cell->val2), end_label);
    snprintf(out, CMD_BUFFER_SIZE, "%s(%s%d:%s%d)", name,
             start_label, index_to_row(cell->val1) + 1,
             end_label,   index_to_row(cell->val2) + 1);
    return out;
}

/* --- Right-hand side dispatch ------------------------------------------ */

static enum ArithmeticOp char_to_op(const char c) {
    return c == '+' ? AOP_ADD
         : c == '-' ? AOP_SUBTRACT
         : c == '*' ? AOP_MULTIPLY
                    : AOP_DIVIDE;
}

static int dispatch_rhs(const char *rhs, const short row, const short col) {
    const int target = rowcol_to_index(row, col);

    static const char re_int[]      = "^(-?[0-9]+)$";
    static const char re_ref[]      = "^([A-Z]{1,3}[1-9][0-9]{0,2})$";
    static const char re_int_int[]  = "^(-?[0-9]{1,})[\\+-\\*\\/](-?[0-9]+)$";
    static const char re_int_ref[]  = "^(-?[0-9]{1,})[\\+-\\*\\/]([A-Z]{1,3}[1-9][0-9]{0,2})$";
    static const char re_ref_int[]  = "^([A-Z]{1,3}[1-9][0-9]{0,2})[\\+-\\*\\/](-?[0-9]+)$";
    static const char re_ref_ref[]  = "^([A-Z]{1,3}[1-9][0-9]{0,2})[\\+-\\*\\/]([A-Z]{1,3}[1-9][0-9]{0,2})$";
    static const char re_func[]     = "^((SUM|MAX|MIN|AVG|STDEV)\\([A-Z]{1,3}[1-9][0-9]{0,2}\\:[A-Z]{1,3}[1-9][0-9]{0,2}\\))$";
    static const char re_sleep_int[]= "^(SLEEP\\((-?[0-9]+)\\))$";
    static const char re_sleep_ref[]= "^(SLEEP\\(([A-Z]{1,3}[1-9][0-9]{0,2})\\))$";

    if (regex_full_match(re_int, rhs)) {
        int v;
        sscanf(rhs, "%d", &v);
        const Value val = { v, VK_INTEGER };
        return engine_set_value(target, val);
    }
    if (regex_full_match(re_ref, rhs)) {
        short r2;
        char  c2[MAX_COL_LABEL];
        sscanf(rhs, "%[A-Z]%hd", c2, &r2);
        const Value val = { rowcol_to_index((short)(r2 - 1), column_label_to_index(c2)),
                            VK_CELL_REF };
        return engine_set_value(target, val);
    }
    if (regex_full_match(re_int_int, rhs)) {
        int v1, v2;
        char op;
        sscanf(rhs, "%d%c%d", &v1, &op, &v2);
        const Arithmetic a = {
            char_to_op(op),
            { v1, VK_INTEGER },
            { v2, VK_INTEGER }
        };
        return engine_set_arithmetic(target, a);
    }
    if (regex_full_match(re_int_ref, rhs)) {
        int v1;
        char op;
        short r2;
        char c2[MAX_COL_LABEL];
        sscanf(rhs, "%d%c%[A-Z]%hd", &v1, &op, c2, &r2);
        const Arithmetic a = {
            char_to_op(op),
            { v1, VK_INTEGER },
            { rowcol_to_index((short)(r2 - 1), column_label_to_index(c2)), VK_CELL_REF }
        };
        return engine_set_arithmetic(target, a);
    }
    if (regex_full_match(re_ref_int, rhs)) {
        int v2;
        char op;
        short r2;
        char c2[MAX_COL_LABEL];
        sscanf(rhs, "%[A-Z]%hd%c%d", c2, &r2, &op, &v2);
        const Arithmetic a = {
            char_to_op(op),
            { rowcol_to_index((short)(r2 - 1), column_label_to_index(c2)), VK_CELL_REF },
            { v2, VK_INTEGER }
        };
        return engine_set_arithmetic(target, a);
    }
    if (regex_full_match(re_ref_ref, rhs)) {
        char op;
        short r2, r3;
        char c2[MAX_COL_LABEL], c3[MAX_COL_LABEL];
        sscanf(rhs, "%[A-Z]%hd%c%[A-Z]%hd", c2, &r2, &op, c3, &r3);
        const Arithmetic a = {
            char_to_op(op),
            { rowcol_to_index((short)(r2 - 1), column_label_to_index(c2)), VK_CELL_REF },
            { rowcol_to_index((short)(r3 - 1), column_label_to_index(c3)), VK_CELL_REF }
        };
        return engine_set_arithmetic(target, a);
    }
    if (regex_full_match(re_func, rhs)) {
        char name[8];
        char sc[MAX_COL_LABEL], ec[MAX_COL_LABEL];
        short sr, er;
        sscanf(rhs, "%[A-Z](%[A-Z]%hd:%[A-Z]%hd)", name, sc, &sr, ec, &er);
        Function f;
        f.type = strcmp(name, "MIN")   == 0 ? FN_MIN
               : strcmp(name, "MAX")   == 0 ? FN_MAX
               : strcmp(name, "AVG")   == 0 ? FN_AVG
               : strcmp(name, "SUM")   == 0 ? FN_SUM
               :                             FN_STDEV;
        f.range = (Range){
            rowcol_to_index((short)(sr - 1), column_label_to_index(sc)),
            rowcol_to_index((short)(er - 1), column_label_to_index(ec))
        };
        return engine_set_function(target, f);
    }
    if (regex_full_match(re_sleep_int, rhs)) {
        char name[8];
        int v;
        sscanf(rhs, "%[A-Z](%d)", name, &v);
        Function f;
        f.type  = FN_SLEEP;
        f.value = (Value){ v, VK_INTEGER };
        return engine_set_function(target, f);
    }
    if (regex_full_match(re_sleep_ref, rhs)) {
        char name[8];
        short r2;
        char c2[MAX_COL_LABEL];
        sscanf(rhs, "%[A-Z](%[A-Z]%hd)", name, c2, &r2);
        Function f;
        f.type  = FN_SLEEP;
        f.value = (Value){
            rowcol_to_index((short)(r2 - 1), column_label_to_index(c2)),
            VK_CELL_REF
        };
        return engine_set_function(target, f);
    }
    return -1;
}

CommandReport command_dispatch(const char *line) {
    struct timespec t0, t1, dt;
    CommandReport report;
    strcpy(report.command, line);
    report.time_taken = 0.0;
    report.status     = 0;
    report.error_msg[0] = '\0';

    if (g_gui_mode) clock_gettime(CLOCK_REALTIME, &t0);

    /* Left-hand side: capture "<CELL>=" prefix using a quick regex. */
    static const char re_lhs[] = "^[A-Z]{1,3}[1-9][0-9]{0,2}\\=";
    const int prefix = regex_prefix_match_len(re_lhs, line);
    if (prefix == 0) {
        strcpy(report.error_msg, "Invalid Cell Reference");
        goto done_err;
    }

    char  col_label[MAX_COL_LABEL];
    short row;
    sscanf(line, "%[A-Z]%hd=", col_label, &row);
    row -= 1;
    const short col = column_label_to_index(col_label);

    if (row < 0 || row >= g_total_rows || col < 0 || col >= g_total_cols) {
        strcpy(report.error_msg, "Cell Reference out of bounds");
        goto done_err;
    }

    const int outcome = dispatch_rhs(line + prefix, row, col);
    if (outcome == -1) {
        strcpy(report.error_msg, "Invalid Expression");
        goto done_err;
    }
    if (outcome == 0) {
        strcpy(report.error_msg, "Circular Dependency");
        goto done_err;
    }

    if (g_gui_mode) {
        clock_gettime(CLOCK_REALTIME, &t1);
        report.time_taken = timespec_diff_sec(t0, t1, &dt);
    }
    report.status = 1;
    return report;

done_err:
    if (g_gui_mode) {
        clock_gettime(CLOCK_REALTIME, &t1);
        report.time_taken = timespec_diff_sec(t0, t1, &dt);
    }
    report.status = 0;
    return report;
}
