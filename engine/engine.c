/*
 * engine/engine.c -- Compute engine. See engine.h for the contract.
 *
 * Implementation notes:
 *   * The cycle check (detect_cycle) is an iterative DFS that uses the
 *     workbook's shared IntStack as its work-list and the MemoStack to
 *     remember every cell whose state it tweaks. On a cycle we pop the
 *     MemoStack to restore the pre-walk state.
 *   * The downstream recompute (propagate_recompute) is essentially Kahn's
 *     algorithm specialised to the spreadsheet shape: when we evaluate a
 *     cell C, every dependant D of C is enqueued only if *all* inputs of D
 *     are already in CS_CLEAN or CS_ZERO_ERROR. This means we never visit
 *     D until its topological predecessors are settled.
 *   * Iteration over a Cell's dependants is delegated to DepIter, which
 *     hides whether the dependants are stored as an inline array or an AVL
 *     set. This single iterator replaces the parallel array/set branches
 *     that were duplicated throughout the original engine.
 */
#include "engine.h"

#include "../core/config.h"
#include "../core/helpers.h"
#include "../workbook/workbook.h"
#include "../containers/int_stack.h"
#include "../containers/memo_stack.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/* ------------------------------------------------------------------------ *
 *                            Cycle detection                               *
 * ------------------------------------------------------------------------ */

static int detect_cycle(const int start_cell_index);
static void restore_walk_states(void);

/* ------------------------------------------------------------------------ *
 *                       Forward declarations for helpers                   *
 * ------------------------------------------------------------------------ */

static int  eval_cell(const Cell *cell, int *out_value);
static int  eval_function(const Cell *cell, int *out_value);
static int  all_inputs_resolved(const Cell *cell);
static void detach_inputs(Cell *cell, int cell_index);
static void attach_inputs(Cell *cell, int cell_index);
static void detach_explicit(int cell_index,
                            int expression_type, int val1_type, int val2_type,
                            int op, int val1, int val2);
static void attach_explicit(int cell_index,
                            int expression_type, int val1_type, int val2_type,
                            int op, int val1, int val2);
static int  apply_expression(int cell_index, int metadata, int val1, int val2);
static int  recover_after_cycle(int cell_index, int prev_meta,
                                int prev_val1, int prev_val2);
static void propagate_recompute(int start_cell_index);

/* ------------------------------------------------------------------------ *
 *                            Cycle detection                               *
 * ------------------------------------------------------------------------ */

static int detect_cycle(const int start_cell_index) {
    int_stack_clear();
    memo_stack_clear();
    int_stack_push(start_cell_index);

    while (!int_stack_empty()) {
        const int cur_idx = int_stack_top();
        Cell *cur = workbook_cell(cur_idx);

        /* Save the pre-walk state so we can restore it if we discover a
         * cycle further down. We only memo states 0 (CLEAN) and 3
         * (ZERO_ERROR) -- state 2 (CYCLE_CHECKED) is already a "transient"
         * marker the engine plants and clean_chain will eventually clear. */
        if (cur->cell_state == CS_CLEAN || cur->cell_state == CS_ZERO_ERROR) {
            const StateMemo memo = { cur_idx, cur->cell_state };
            memo_stack_push(memo);
        }
        cur->cell_state = CS_DFS_IN_PROGRESS;

        /* Leaf in the dependants graph -> done with this branch. */
        const int has_no_dependants =
            (cur->dependants_type == DK_ARRAY && cur->dependants_array->size == 0) ||
            (cur->dependants_type == DK_SET   && avl_set_size(cur->dependants_set) == 0);
        if (has_no_dependants) {
            int_stack_pop();
            cur->cell_state = CS_CYCLE_CHECKED;
            continue;
        }

        DepIter it;
        dep_iter_begin(&it, cur);
        int descended_into_child = 0;
        int cycle_found          = 0;
        int dep_idx;
        while ((dep_idx = dep_iter_next(&it)) != -1) {
            const Cell *dep = workbook_cell(dep_idx);
            if (dep->cell_state == CS_CLEAN || dep->cell_state == CS_ZERO_ERROR) {
                int_stack_push(dep_idx);
                descended_into_child = 1;
                break;
            }
            if (dep->cell_state == CS_DFS_IN_PROGRESS) {
                cycle_found = 1;
                break;
            }
        }
        dep_iter_end(&it);

        if (cycle_found) {
            int_stack_clear();
            return 0;
        }
        if (!descended_into_child) {
            int_stack_pop();
            cur->cell_state = CS_CYCLE_CHECKED;
        }
    }
    return 1;
}

static void restore_walk_states(void) {
    while (!memo_stack_empty()) {
        const StateMemo memo = memo_stack_top();
        workbook_cell(memo.cell_index)->cell_state = memo.state;
        memo_stack_pop();
    }
}

/* ------------------------------------------------------------------------ *
 *                Re-evaluation in topological order                        *
 * ------------------------------------------------------------------------ */

static int all_inputs_resolved(const Cell *cell) {
    /* "Resolved" means CS_CLEAN or CS_ZERO_ERROR -- anything else (DFS or
     * CYCLE_CHECKED) means the cell still owes us work. */
    if (cell->expression_type == EK_VALUE) {
        if (cell->val1_type == VK_CELL_REF) {
            const Cell *d = workbook_cell(cell->val1);
            if (d->cell_state != CS_CLEAN && d->cell_state != CS_ZERO_ERROR) return 0;
        }
        return 1;
    }
    if (cell->expression_type == EK_ARITHMETIC) {
        if (cell->val1_type == VK_CELL_REF) {
            const Cell *d = workbook_cell(cell->val1);
            if (d->cell_state != CS_CLEAN && d->cell_state != CS_ZERO_ERROR) return 0;
        }
        if (cell->val2_type == VK_CELL_REF) {
            const Cell *d = workbook_cell(cell->val2);
            if (d->cell_state != CS_CLEAN && d->cell_state != CS_ZERO_ERROR) return 0;
        }
        return 1;
    }
    /* EK_FUNCTION */
    const int is_sleep_form = cell->val2_type == 1 && cell->op == 1;
    if (is_sleep_form) {
        if (cell->val1_type == VK_CELL_REF) {
            const Cell *d = workbook_cell(cell->val1);
            if (d->cell_state != CS_CLEAN && d->cell_state != CS_ZERO_ERROR) return 0;
        }
        return 1;
    }
    const short start_row = index_to_row(cell->val1);
    const short start_col = index_to_col(cell->val1);
    const short end_row   = index_to_row(cell->val2);
    const short end_col   = index_to_col(cell->val2);
    for (short r = start_row; r <= end_row; ++r) {
        for (short c = start_col; c <= end_col; ++c) {
            const Cell *d = workbook_cell(rowcol_to_index(r, c));
            if (d->cell_state != CS_CLEAN && d->cell_state != CS_ZERO_ERROR) {
                return 0;
            }
        }
    }
    return 1;
}

static void propagate_recompute(const int start_cell_index) {
    int_stack_clear();
    int_stack_push(start_cell_index);

    while (!int_stack_empty()) {
        const int cur_idx = int_stack_top();
        int_stack_pop();
        Cell *cur = workbook_cell(cur_idx);

        int new_value = 0;
        const int clean = eval_cell(cur, &new_value);
        cur->cell_state = clean ? CS_CLEAN : CS_ZERO_ERROR;
        cur->value      = new_value;

        DepIter it;
        dep_iter_begin(&it, cur);
        int dep_idx;
        while ((dep_idx = dep_iter_next(&it)) != -1) {
            const Cell *dep = workbook_cell(dep_idx);
            if (dep->cell_state != CS_CYCLE_CHECKED) continue;
            if (all_inputs_resolved(dep)) int_stack_push(dep_idx);
        }
        dep_iter_end(&it);
    }
}

/* ------------------------------------------------------------------------ *
 *                              Evaluation                                  *
 * ------------------------------------------------------------------------ */

static int read_operand(unsigned int kind, int slot, int *out_value) {
    if (kind == VK_INTEGER) {
        *out_value = slot;
        return 1;
    }
    if (kind == VK_CELL_REF) {
        const Cell *d = workbook_cell(slot);
        *out_value = d->value;
        return d->cell_state != CS_ZERO_ERROR;
    }
    fprintf(stderr, "engine: value held error tag\n");
    exit(1);
}

static int eval_arithmetic(const Cell *cell, int *out_value) {
    int a, b;
    if (!read_operand(cell->val1_type, cell->val1, &a)) return 0;
    if (!read_operand(cell->val2_type, cell->val2, &b)) return 0;
    switch (cell->op) {
        case AOP_ADD:      *out_value = a + b; break;
        case AOP_SUBTRACT: *out_value = a - b; break;
        case AOP_MULTIPLY: *out_value = a * b; break;
        case AOP_DIVIDE:
            if (b == 0) return 0;
            *out_value = a / b;
            break;
    }
    return 1;
}

static int eval_function(const Cell *cell, int *out_value) {
    const int fn = cell->val2_type * 4 + cell->op;

    if (fn == FN_SLEEP) {
        if (cell->val1_type == VK_INTEGER) {
            *out_value = cell->val1;
            return 1;
        }
        /* CELL_REF */
        const Cell *d = workbook_cell(cell->val1);
        if (d->cell_state == CS_ZERO_ERROR) return 0;
        *out_value = workbook_raw_value(cell->val1);
        return 1;
    }

    const short start_row = index_to_row(cell->val1);
    const short start_col = index_to_col(cell->val1);
    const short end_row   = index_to_row(cell->val2);
    const short end_col   = index_to_col(cell->val2);
    const int   count     = (end_row - start_row + 1) * (end_col - start_col + 1);

    int acc = 0;
    if (fn == FN_MIN || fn == FN_MAX) {
        const Cell *first = workbook_cell(cell->val1);
        if (first->cell_state == CS_ZERO_ERROR) return 0;
        acc = first->value;
        for (short r = start_row; r <= end_row; ++r) {
            for (short c = start_col; c <= end_col; ++c) {
                const Cell *d = workbook_cell(rowcol_to_index(r, c));
                if (d->cell_state == CS_ZERO_ERROR) return 0;
                if (fn == FN_MIN ? d->value < acc : d->value > acc) acc = d->value;
            }
        }
        *out_value = acc;
        return 1;
    }
    if (fn == FN_AVG || fn == FN_SUM) {
        for (short r = start_row; r <= end_row; ++r) {
            for (short c = start_col; c <= end_col; ++c) {
                const Cell *d = workbook_cell(rowcol_to_index(r, c));
                if (d->cell_state == CS_ZERO_ERROR) return 0;
                acc += d->value;
            }
        }
        *out_value = (fn == FN_AVG) ? (acc / count) : acc;
        return 1;
    }
    if (fn == FN_STDEV) {
        for (short r = start_row; r <= end_row; ++r) {
            for (short c = start_col; c <= end_col; ++c) {
                const Cell *d = workbook_cell(rowcol_to_index(r, c));
                if (d->cell_state == CS_ZERO_ERROR) return 0;
                acc += d->value;
            }
        }
        const int mean = acc / count;
        double variance = 0.0;
        for (short r = start_row; r <= end_row; ++r) {
            for (short c = start_col; c <= end_col; ++c) {
                const Cell *d = workbook_cell(rowcol_to_index(r, c));
                if (d->cell_state == CS_ZERO_ERROR) return 0;
                const double delta = (double) d->value - (double) mean;
                variance += delta * delta;
            }
        }
        variance /= (double) count;
        *out_value = (int) round(sqrt(variance));
        return 1;
    }
    /* Should not happen if the metadata mask is sane. */
    *out_value = 0;
    return 1;
}

static int eval_cell(const Cell *cell, int *out_value) {
    if (cell->expression_type == EK_VALUE) {
        if (cell->val1_type == VK_INTEGER) {
            *out_value = cell->val1;
            return 1;
        }
        const Cell *d = workbook_cell(cell->val1);
        *out_value = d->value;
        return d->cell_state != CS_ZERO_ERROR;
    }
    if (cell->expression_type == EK_ARITHMETIC) return eval_arithmetic(cell, out_value);
    return eval_function(cell, out_value);
}

/* ------------------------------------------------------------------------ *
 *               Attaching / detaching the dependency graph                 *
 * ------------------------------------------------------------------------ */

static void detach_inputs(Cell *cell, const int cell_index) {
    detach_explicit(cell_index,
                    cell->expression_type, cell->val1_type, cell->val2_type,
                    cell->op, cell->val1, cell->val2);
}

static void attach_inputs(Cell *cell, const int cell_index) {
    attach_explicit(cell_index,
                    cell->expression_type, cell->val1_type, cell->val2_type,
                    cell->op, cell->val1, cell->val2);
}

static void detach_explicit(const int cell_index,
                            const int expression_type,
                            const int val1_type,
                            const int val2_type,
                            const int op,
                            const int val1,
                            const int val2) {
    if (expression_type == EK_VALUE) {
        if (val1_type == VK_CELL_REF) workbook_remove_dependant(val1, cell_index);
        return;
    }
    if (expression_type == EK_ARITHMETIC) {
        if (val1_type == VK_CELL_REF) workbook_remove_dependant(val1, cell_index);
        if (val2_type == VK_CELL_REF) workbook_remove_dependant(val2, cell_index);
        return;
    }
    /* EK_FUNCTION */
    const int is_sleep_form = (val2_type == 1 && op == 1);
    if (is_sleep_form) {
        if (val1_type == VK_CELL_REF) workbook_remove_dependant(val1, cell_index);
        return;
    }
    const short start_row = index_to_row(val1);
    const short start_col = index_to_col(val1);
    const short end_row   = index_to_row(val2);
    const short end_col   = index_to_col(val2);
    for (short r = start_row; r <= end_row; ++r) {
        for (short c = start_col; c <= end_col; ++c) {
            workbook_remove_dependant(rowcol_to_index(r, c), cell_index);
        }
    }
}

static void attach_explicit(const int cell_index,
                            const int expression_type,
                            const int val1_type,
                            const int val2_type,
                            const int op,
                            const int val1,
                            const int val2) {
    if (expression_type == EK_VALUE) {
        if (val1_type == VK_CELL_REF) workbook_add_dependant(val1, cell_index);
        return;
    }
    if (expression_type == EK_ARITHMETIC) {
        if (val1_type == VK_CELL_REF) workbook_add_dependant(val1, cell_index);
        if (val2_type == VK_CELL_REF) workbook_add_dependant(val2, cell_index);
        return;
    }
    /* EK_FUNCTION */
    const int is_sleep_form = (val2_type == 1 && op == 1);
    if (is_sleep_form) {
        if (val1_type == VK_CELL_REF) workbook_add_dependant(val1, cell_index);
        return;
    }
    const short start_row = index_to_row(val1);
    const short start_col = index_to_col(val1);
    const short end_row   = index_to_row(val2);
    const short end_col   = index_to_col(val2);
    for (short r = start_row; r <= end_row; ++r) {
        for (short c = start_col; c <= end_col; ++c) {
            workbook_add_dependant(rowcol_to_index(r, c), cell_index);
        }
    }
}

/* ------------------------------------------------------------------------ *
 *                  Metadata mask helpers + the recovery path               *
 * ------------------------------------------------------------------------ */

static int pack_metadata(const int expression_type,
                         const int val1_type,
                         const int val2_type,
                         const int op) {
    return expression_type * 64
         + val1_type       * 32
         + val2_type       * 16
         + op              * 4;
}

static int recover_after_cycle(const int cell_index,
                               const int prev_meta,
                               const int prev_val1,
                               const int prev_val2) {
    restore_walk_states();
    Cell *cell = workbook_cell(cell_index);

    /* Detach the *current* (rejected) inputs. */
    detach_inputs(cell, cell_index);

    /* Decode the saved metadata and reinstall the previous formula. */
    const int prev_expression_type = (255 & prev_meta) >> 6;
    const int prev_val1_type       = (63  & prev_meta) >> 5;
    const int prev_val2_type       = (31  & prev_meta) >> 4;
    const int prev_op              = (15  & prev_meta) >> 2;

    attach_explicit(cell_index,
                    prev_expression_type, prev_val1_type, prev_val2_type,
                    prev_op, prev_val1, prev_val2);

    cell->expression_type = prev_expression_type;
    cell->val1_type       = prev_val1_type;
    cell->val2_type       = prev_val2_type;
    cell->op              = prev_op;
    cell->val1            = prev_val1;
    cell->val2            = prev_val2;
    return 0;
}

/* ------------------------------------------------------------------------ *
 *                       The single point of mutation                       *
 * ------------------------------------------------------------------------ */

static int apply_expression(const int cell_index,
                            const int metadata,
                            const int val1,
                            const int val2) {
    Cell *cell = workbook_cell(cell_index);

    /* Snapshot the existing formula so we can roll back on cycle. */
    const int prev_meta = pack_metadata(cell->expression_type,
                                        cell->val1_type,
                                        cell->val2_type,
                                        cell->op);
    const int prev_val1 = cell->val1;
    const int prev_val2 = cell->val2;

    detach_inputs(cell, cell_index);

    cell->expression_type = (255 & metadata) >> 6;
    cell->val1_type       = (63  & metadata) >> 5;
    cell->val2_type       = (31  & metadata) >> 4;
    cell->op              = (15  & metadata) >> 2;
    cell->val1            = val1;
    cell->val2            = val2;

    attach_inputs(cell, cell_index);

    if (!detect_cycle(cell_index)) {
        return recover_after_cycle(cell_index, prev_meta, prev_val1, prev_val2);
    }
    propagate_recompute(cell_index);
    return 1;
}

/* ------------------------------------------------------------------------ *
 *                                Public API                                *
 * ------------------------------------------------------------------------ */

int engine_set_value(const int cell_index, const Value value) {
    const int metadata = (value.type == VK_INTEGER) ? 0 : 32;
    return apply_expression(cell_index, metadata, value.value, 0);
}

int engine_set_arithmetic(const int cell_index, const Arithmetic arithmetic) {
    const int metadata = 64
                       + arithmetic.value1.type * 32
                       + arithmetic.value2.type * 16
                       + arithmetic.type        * 4;
    return apply_expression(cell_index, metadata,
                            arithmetic.value1.value, arithmetic.value2.value);
}

int engine_set_function(const int cell_index, const Function function) {
    if (function.type == FN_SLEEP) {
        const int metadata = 128 + function.value.type * 32 + function.type * 4;
        return apply_expression(cell_index, metadata, function.value.value, 0);
    }
    const int metadata = 128 + function.type * 4;
    return apply_expression(cell_index, metadata,
                            function.range.start_index,
                            function.range.end_index);
}

int engine_cell_value(const int cell_index) {
    return workbook_cell(cell_index)->value;
}
