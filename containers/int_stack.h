/*
 * containers/int_stack.h -- Dynamically-sized stack of ints, used by the
 * iterative cycle-detection / recomputation walks in engine/engine.c.
 *
 * One global instance (`g_index_stack`) is provided so the engine can reuse
 * a single scratch buffer across calls without re-allocating on every push.
 * The geometry mirrors the original engine: start at capacity 4, double on
 * overflow, halve when usage falls below 50%.
 */
#ifndef TSE_CONTAINERS_INT_STACK_H
#define TSE_CONTAINERS_INT_STACK_H

#include "../workbook/cell_types.h"

void int_stack_init   (void);
void int_stack_destroy(void);

void int_stack_push (int element);
int  int_stack_pop  (void);
int  int_stack_top  (void);
int  int_stack_size (void);
int  int_stack_empty(void);
void int_stack_clear(void);

extern IntStack g_index_stack;

#endif /* TSE_CONTAINERS_INT_STACK_H */
