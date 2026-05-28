/*
 * containers/avl_set.h -- Ordered set of int (cell index) keys backed by a
 * height-balanced AVL tree, with an in-order iterator for traversal.
 *
 * The workbook layer promotes a cell's dependants list to this representation
 * once the inline 4-slot array overflows. Operations are O(log n) and the
 * iterator yields the cell indices in ascending order, which matches the
 * deterministic traversal order expected by the engine's cycle-check and
 * recomputation passes.
 */
#ifndef TSE_CONTAINERS_AVL_SET_H
#define TSE_CONTAINERS_AVL_SET_H

#include <stddef.h>

typedef struct AvlNode {
    int             cell_index;
    struct AvlNode *left;
    struct AvlNode *right;
    struct AvlNode *parent;
    int             height: 8;
} __attribute__((packed)) AvlNode;

typedef struct AvlSet {
    AvlNode *root;
    int      size;
} __attribute__((packed)) AvlSet;

typedef struct {
    AvlNode *current;
} AvlSetIter;

AvlSet *avl_set_new   (void);
void    avl_set_free  (AvlSet *set);

int     avl_set_insert(AvlSet *set, int cell_index);
int     avl_set_remove(AvlSet *set, int cell_index);
int     avl_set_contains(AvlSet *set, int cell_index);
void    avl_set_clear (AvlSet *set);
size_t  avl_set_size  (const AvlSet *set);

AvlSetIter *avl_set_iter_new   (AvlSet *set);
int         avl_set_iter_next  (AvlSetIter *iter); /* returns -1 at end */
void        avl_set_iter_free  (AvlSetIter *iter);

#endif /* TSE_CONTAINERS_AVL_SET_H */
