/*
 * containers/int_stack.c -- Resizable LIFO stack of ints.
 *
 * Growth policy: capacity starts at INITIAL_CAPACITY (4) and doubles whenever
 * a push would overflow. Shrink policy: when a pop leaves the live count
 * below half the capacity *and* the capacity is still larger than the
 * initial floor, shrink to half. This matches the amortized-O(1) amortized
 * push/pop behavior expected by the engine.
 */
#include "int_stack.h"

#include <stdio.h>
#include <stdlib.h>

#define INITIAL_CAPACITY 4

IntStack g_index_stack;

static void grow(void) {
    g_index_stack.dynamic_size *= 2;
    g_index_stack.elements = realloc(
        g_index_stack.elements,
        sizeof(int) * (size_t) g_index_stack.dynamic_size);
    if (g_index_stack.elements == NULL) {
        fprintf(stderr, "int_stack: realloc failed during growth\n");
        exit(1);
    }
}

static void shrink_half(void) {
    g_index_stack.dynamic_size /= 2;
    int *replacement = malloc(sizeof(int) * (size_t) g_index_stack.dynamic_size);
    if (replacement == NULL) {
        fprintf(stderr, "int_stack: malloc failed during shrink\n");
        exit(1);
    }
    for (int i = 0; i < g_index_stack.no_of_elements; ++i) {
        replacement[i] = g_index_stack.elements[i];
    }
    free(g_index_stack.elements);
    g_index_stack.elements = replacement;
}

void int_stack_init(void) {
    g_index_stack.elements = malloc(sizeof(int) * INITIAL_CAPACITY);
    if (g_index_stack.elements == NULL) {
        fprintf(stderr, "int_stack: initial malloc failed\n");
        exit(1);
    }
    g_index_stack.no_of_elements = 0;
    g_index_stack.dynamic_size   = INITIAL_CAPACITY;
}

void int_stack_destroy(void) {
    free(g_index_stack.elements);
    g_index_stack.elements      = NULL;
    g_index_stack.no_of_elements = 0;
    g_index_stack.dynamic_size   = 0;
}

void int_stack_push(const int element) {
    if (g_index_stack.elements == NULL) {
        fprintf(stderr, "int_stack: pushed to uninitialized stack\n");
        exit(1);
    }
    if (g_index_stack.no_of_elements == g_index_stack.dynamic_size) {
        grow();
    }
    g_index_stack.elements[g_index_stack.no_of_elements++] = element;
}

int int_stack_pop(void) {
    if (g_index_stack.no_of_elements < g_index_stack.dynamic_size / 2
        && g_index_stack.no_of_elements > INITIAL_CAPACITY) {
        shrink_half();
    }
    g_index_stack.no_of_elements -= 1;
    return g_index_stack.elements[g_index_stack.no_of_elements];
}

int int_stack_top(void) {
    return g_index_stack.elements[g_index_stack.no_of_elements - 1];
}

int int_stack_size(void) {
    return g_index_stack.no_of_elements;
}

int int_stack_empty(void) {
    return g_index_stack.no_of_elements == 0;
}

void int_stack_clear(void) {
    g_index_stack.no_of_elements = 0;
}
