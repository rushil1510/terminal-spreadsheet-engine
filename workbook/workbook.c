/*
 * workbook/workbook.c -- Implementation of the dense Cell[] table and its
 * dependants graph. See workbook.h for the high-level contract.
 *
 * Storage strategy:
 *   * The entire workbook lives in one heap-allocated array of `Cell`
 *     structures sized rows * cols. Indexing is row-major via the helpers
 *     in core/helpers.c. This pays off when running with millions of cells
 *     because we avoid one pointer per cell.
 *   * Each cell starts with an inline 4-slot dependants array. The vast
 *     majority of cells in real workloads have <= 4 dependants and never
 *     pay for the AVL-set overhead. Once a 5th dependant appears the inline
 *     array is migrated into a freshly-allocated AVL set.
 */
#include "workbook.h"
#include "../core/config.h"
#include "../containers/int_stack.h"
#include "../containers/memo_stack.h"

#include <stdio.h>
#include <stdlib.h>

static Cell *table = NULL;

/* --- Helpers ------------------------------------------------------------ */

static DependantsArray *fresh_dep_array(void) {
    DependantsArray *arr = malloc(sizeof(DependantsArray));
    if (arr == NULL) {
        fprintf(stderr, "workbook: malloc for DependantsArray failed\n");
        exit(1);
    }
    for (int i = 0; i < 4; ++i) arr->dependants_cells[i] = -1;
    arr->size = 0;
    return arr;
}

static int dep_array_contains(const DependantsArray *arr, const int cell_index) {
    for (int i = 0; i < arr->size; ++i) {
        if (arr->dependants_cells[i] == cell_index) return 1;
    }
    return 0;
}

static void promote_to_set(Cell *cell, const int new_dependant) {
    /* Copy the inline array out, free it, then attach an AVL set. */
    AvlSet *set = avl_set_new();
    for (int i = 0; i < 4; ++i) {
        avl_set_insert(set, cell->dependants_array->dependants_cells[i]);
    }
    avl_set_insert(set, new_dependant);
    free(cell->dependants_array);
    cell->dependants_type = DK_SET;
    cell->dependants_set  = set;
}

/* --- Lifecycle ---------------------------------------------------------- */

void cell_init(Cell *cell) {
    cell->value           = 0;
    cell->expression_type = 0;
    cell->val1_type       = 0;
    cell->val2_type       = 0;
    cell->op              = 0;
    cell->cell_state      = 0;
    cell->val1            = 0;
    cell->val2            = 0;
    cell->dependants_type = DK_ARRAY;
    cell->dependants_array = fresh_dep_array();
}

void workbook_init(void) {
    const int total = (int) g_total_rows * (int) g_total_cols;
    table = malloc(sizeof(Cell) * (size_t) total);
    if (table == NULL) {
        fprintf(stderr, "workbook: failed to allocate %d cells\n", total);
        exit(1);
    }
    for (int i = 0; i < total; ++i) cell_init(&table[i]);
    int_stack_init();
    memo_stack_init();
}

void workbook_destroy(void) {
    const int total = (int) g_total_rows * (int) g_total_cols;
    for (int i = 0; i < total; ++i) {
        const Cell *c = &table[i];
        if (c->dependants_type == DK_ARRAY) free(c->dependants_array);
        else                                avl_set_free(c->dependants_set);
    }
    free(table);
    table = NULL;
    int_stack_destroy();
    memo_stack_destroy();
}

/* --- Lookups ------------------------------------------------------------ */

Cell *workbook_cell(const int cell_index) {
    return &table[cell_index];
}

int workbook_raw_value(const int cell_index) {
    const int total = (int) g_total_rows * (int) g_total_cols;
    if (cell_index < 0 || cell_index >= total) {
        fprintf(stderr, "workbook: raw_value out-of-bounds index %d\n", cell_index);
        return 0;
    }
    return table[cell_index].value;
}

/* --- Dependants graph --------------------------------------------------- */

void workbook_add_dependant(const int source_cell_index,
                            const int dependent_cell_index) {
    const int total = (int) g_total_rows * (int) g_total_cols;
    if (source_cell_index    < 0 || source_cell_index    >= total) return;
    if (dependent_cell_index < 0 || dependent_cell_index >= total) return;

    Cell *src = &table[source_cell_index];
    if (src->dependants_type == DK_ARRAY) {
        if (dep_array_contains(src->dependants_array, dependent_cell_index)) return;
        if (src->dependants_array->size == 4) {
            promote_to_set(src, dependent_cell_index);
        } else {
            src->dependants_array->dependants_cells[src->dependants_array->size++] =
                dependent_cell_index;
        }
    } else {
        avl_set_insert(src->dependants_set, dependent_cell_index);
    }
}

void workbook_remove_dependant(const int source_cell_index,
                               const int dependent_cell_index) {
    const Cell *src = &table[source_cell_index];
    if (src->dependants_type == DK_ARRAY) {
        int hit = -1;
        for (int i = 0; i < src->dependants_array->size; ++i) {
            if (src->dependants_array->dependants_cells[i] == dependent_cell_index) {
                hit = i;
                break;
            }
        }
        if (hit < 0) return;
        for (int i = hit; i < src->dependants_array->size - 1; ++i) {
            src->dependants_array->dependants_cells[i] =
                src->dependants_array->dependants_cells[i + 1];
        }
        src->dependants_array->dependants_cells[src->dependants_array->size - 1] = -1;
        src->dependants_array->size -= 1;
    } else {
        avl_set_remove(src->dependants_set, dependent_cell_index);
    }
}

/* --- Unified iterator over dependants ---------------------------------- */

void dep_iter_begin(DepIter *iter, Cell *cell) {
    iter->cell      = cell;
    iter->array_idx = 0;
    iter->set_iter  = (cell->dependants_type == DK_SET)
                          ? avl_set_iter_new(cell->dependants_set)
                          : NULL;
}

int dep_iter_next(DepIter *iter) {
    if (iter->cell->dependants_type == DK_ARRAY) {
        if (iter->array_idx >= iter->cell->dependants_array->size) return -1;
        return iter->cell->dependants_array->dependants_cells[iter->array_idx++];
    }
    return avl_set_iter_next(iter->set_iter);
}

void dep_iter_end(DepIter *iter) {
    if (iter->set_iter) {
        avl_set_iter_free(iter->set_iter);
        iter->set_iter = NULL;
    }
}

/* --- Bit-packed -> Expression ------------------------------------------ */

Expression workbook_pack_expression(const int cell_index) {
    Cell *cell = workbook_cell(cell_index);
    Expression e;
    e.type = cell->expression_type;

    if (cell->expression_type == EK_VALUE) {
        Value v;
        v.type  = cell->val1_type == 0 ? VK_INTEGER : VK_CELL_REF;
        v.value = cell->val1;
        e.value = v;
        return e;
    }

    if (cell->expression_type == EK_ARITHMETIC) {
        Arithmetic a;
        a.type = cell->op;
        Value v1, v2;
        v1.type = cell->val1_type == 0 ? VK_INTEGER : VK_CELL_REF;
        v1.value = cell->val1;
        v2.type = cell->val2_type == 0 ? VK_INTEGER : VK_CELL_REF;
        v2.value = cell->val2;
        a.value1 = v1;
        a.value2 = v2;
        e.arithmetic = a;
        return e;
    }

    /* EK_FUNCTION: the function kind is encoded as val2_type * 4 + op. */
    Function f;
    f.type = cell->val2_type * 4 + cell->op;
    if (f.type == FN_SLEEP) {
        Value v;
        v.type  = cell->val1_type == 0 ? VK_INTEGER : VK_CELL_REF;
        v.value = cell->val1;
        f.value = v;
    } else {
        Range r;
        r.start_index = cell->val1;
        r.end_index   = cell->val2;
        f.range       = r;
    }
    e.function = f;
    return e;
}
