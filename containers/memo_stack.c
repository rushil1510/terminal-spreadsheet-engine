/*
 * containers/memo_stack.c -- StateMemo stack with the same growth policy as
 * the IntStack: double-on-overflow, halve-on-underuse, initial capacity 4.
 */
#include "memo_stack.h"

#include <stdio.h>
#include <stdlib.h>

#define INITIAL_CAPACITY 4

MemoStack g_memo_stack;

static void grow(void) {
    g_memo_stack.dynamic_size *= 2;
    g_memo_stack.elements = realloc(
        g_memo_stack.elements,
        sizeof(StateMemo) * (size_t) g_memo_stack.dynamic_size);
    if (g_memo_stack.elements == NULL) {
        fprintf(stderr, "memo_stack: realloc failed during growth\n");
        exit(1);
    }
}

static void shrink_half(void) {
    g_memo_stack.dynamic_size /= 2;
    StateMemo *replacement = malloc(
        sizeof(StateMemo) * (size_t) g_memo_stack.dynamic_size);
    if (replacement == NULL) {
        fprintf(stderr, "memo_stack: malloc failed during shrink\n");
        exit(1);
    }
    for (int i = 0; i < g_memo_stack.no_of_elements; ++i) {
        replacement[i] = g_memo_stack.elements[i];
    }
    free(g_memo_stack.elements);
    g_memo_stack.elements = replacement;
}

void memo_stack_init(void) {
    g_memo_stack.elements = malloc(sizeof(StateMemo) * INITIAL_CAPACITY);
    if (g_memo_stack.elements == NULL) {
        fprintf(stderr, "memo_stack: initial malloc failed\n");
        exit(1);
    }
    g_memo_stack.no_of_elements = 0;
    g_memo_stack.dynamic_size   = INITIAL_CAPACITY;
}

void memo_stack_destroy(void) {
    free(g_memo_stack.elements);
    g_memo_stack.elements       = NULL;
    g_memo_stack.no_of_elements = 0;
    g_memo_stack.dynamic_size   = 0;
}

void memo_stack_push(const StateMemo element) {
    if (g_memo_stack.elements == NULL) {
        fprintf(stderr, "memo_stack: pushed to uninitialized stack\n");
        exit(1);
    }
    if (g_memo_stack.no_of_elements == g_memo_stack.dynamic_size) {
        grow();
    }
    g_memo_stack.elements[g_memo_stack.no_of_elements++] = element;
}

StateMemo memo_stack_pop(void) {
    if (g_memo_stack.no_of_elements < g_memo_stack.dynamic_size / 2
        && g_memo_stack.no_of_elements > INITIAL_CAPACITY) {
        shrink_half();
    }
    g_memo_stack.no_of_elements -= 1;
    return g_memo_stack.elements[g_memo_stack.no_of_elements];
}

StateMemo memo_stack_top(void) {
    return g_memo_stack.elements[g_memo_stack.no_of_elements - 1];
}

int memo_stack_size(void) {
    return g_memo_stack.no_of_elements;
}

int memo_stack_empty(void) {
    return g_memo_stack.no_of_elements == 0;
}

void memo_stack_clear(void) {
    g_memo_stack.no_of_elements = 0;
}
