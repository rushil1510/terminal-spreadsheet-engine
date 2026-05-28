/*
 * containers/memo_stack.h -- Resizable stack of StateMemo entries used by
 * the engine to remember the pre-walk state of each cell touched during a
 * cycle-detection DFS. If a cycle is detected, the engine pops the memo
 * stack to restore every cell's walk-state to what it was before the DFS.
 */
#ifndef TSE_CONTAINERS_MEMO_STACK_H
#define TSE_CONTAINERS_MEMO_STACK_H

#include "../workbook/cell_types.h"

void      memo_stack_init   (void);
void      memo_stack_destroy(void);

void      memo_stack_push (StateMemo element);
StateMemo memo_stack_pop  (void);
StateMemo memo_stack_top  (void);
int       memo_stack_size (void);
int       memo_stack_empty(void);
void      memo_stack_clear(void);

extern MemoStack g_memo_stack;

#endif /* TSE_CONTAINERS_MEMO_STACK_H */
