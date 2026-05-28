/*
 * engine/engine.h -- Compute engine: formula assignment, cycle detection,
 * topological re-evaluation, and arithmetic / function evaluation.
 *
 * Conceptual flow when assigning `A1 = <expression>`:
 *   1. Decode the previous formula and detach A1 from its old dependencies.
 *   2. Encode the new formula into A1's bit-packed Cell slots and register
 *      A1 as a dependant of each input.
 *   3. Run a cycle check (iterative DFS) starting from A1. If a cycle is
 *      detected, undo step 2 and reattach the old dependencies.
 *   4. Otherwise, walk A1's downstream dependants in topological order,
 *      re-evaluating each cell whose inputs have all been resolved.
 *
 * The metadata mask used internally is laid out as:
 *   bits [7:6] = expression kind (EK_VALUE / EK_ARITHMETIC / EK_FUNCTION)
 *   bit  [5]   = value1 kind     (0 = int literal, 1 = cell reference)
 *   bit  [4]   = value2 kind     (also carries the FN HI bit for functions)
 *   bits [3:2] = arithmetic op  / FN LO bits
 */
#ifndef TSE_ENGINE_ENGINE_H
#define TSE_ENGINE_ENGINE_H

#include "../workbook/cell_types.h"

/* Return values:  1 = applied,  0 = rejected (circular dependency). */
int engine_set_value      (int cell_index, Value value);
int engine_set_arithmetic (int cell_index, Arithmetic arithmetic);
int engine_set_function   (int cell_index, Function function);

/* Trivial accessor -- a cell's value is kept hot inside the Cell struct, so
 * reads are constant-time. Lazy refreshes are performed at write time by the
 * engine's downstream recompute pass. */
int engine_cell_value     (int cell_index);

#endif /* TSE_ENGINE_ENGINE_H */
