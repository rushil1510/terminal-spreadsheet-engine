/*
 * workbook/workbook.h -- Owner of the dense Cell[] table and the dependants
 * graph. The engine, the parser, and the UI all reach for cells through this
 * module rather than touching the global array directly.
 *
 * Responsibilities:
 *   * Allocate / free the workbook (size derived from g_total_rows/cols).
 *   * Initialize cells to the "empty" state (value 0, no formula, no deps).
 *   * Maintain each cell's outgoing dependants list, automatically promoting
 *     the inline 4-slot array to an AVL set on overflow.
 *   * Repackage a cell's bit-packed formula into a friendlier Expression
 *     descriptor (used by the parser when formatting expressions back to a
 *     human-readable string).
 *
 * A `DepIter` is provided so the engine can iterate a cell's dependants in
 * order without caring whether they are stored as an array or a set -- this
 * collapses what was a copy-pasted if/else branch in the original engine.
 */
#ifndef TSE_WORKBOOK_WORKBOOK_H
#define TSE_WORKBOOK_WORKBOOK_H

#include "cell_types.h"
#include "../containers/avl_set.h"

/* --- Lifecycle ---------------------------------------------------------- */
void workbook_init   (void);
void workbook_destroy(void);
void cell_init       (Cell *cell);

/* --- Lookups ------------------------------------------------------------ */
Cell *workbook_cell      (int cell_index);
int   workbook_raw_value (int cell_index);

/* --- Outgoing dependants graph ----------------------------------------- *
 * Edges are stored from the dependency to its dependants (so updates can be
 * pushed forward in topological order). */
void workbook_add_dependant   (int source_cell_index, int dependent_cell_index);
void workbook_remove_dependant(int source_cell_index, int dependent_cell_index);

/* --- Unified dependants iterator --------------------------------------- *
 * Hides whether the underlying storage is the inline array or the AVL set. */
typedef struct {
    Cell        *cell;
    int          array_idx;
    AvlSetIter  *set_iter;
} DepIter;

void dep_iter_begin(DepIter *iter, Cell *cell);
int  dep_iter_next (DepIter *iter);              /* -1 = exhausted */
void dep_iter_end  (DepIter *iter);

/* --- Bit-packed -> unpacked formula descriptor ------------------------- */
Expression workbook_pack_expression(int cell_index);

#endif /* TSE_WORKBOOK_WORKBOOK_H */
