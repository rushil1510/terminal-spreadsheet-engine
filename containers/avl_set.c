/*
 * containers/avl_set.c -- AVL tree implementation.
 *
 * The tree maintains the invariant |bf(node)| <= 1, where bf is the
 * difference in subtree heights. After every insert or remove we walk up to
 * the root, recomputing heights and rotating when imbalance is detected.
 *
 * Iterative destruction/clear is used (not recursion) so very tall trees do
 * not blow the call stack.
 */
#include "avl_set.h"

#include <stdlib.h>

/* --- Node primitives ---------------------------------------------------- */

static int compare(const int a, const int b) {
    return a - b;
}

static int node_height(const AvlNode *n) {
    return n ? n->height : 0;
}

static int balance(const AvlNode *n) {
    return n ? node_height(n->left) - node_height(n->right) : 0;
}

static void recompute_height(AvlNode *n) {
    const int l = node_height(n->left);
    const int r = node_height(n->right);
    n->height = (l > r ? l : r) + 1;
}

static void reattach_parent(AvlNode *node, AvlNode *new_parent) {
    if (node) {
        node->parent = new_parent;
    }
}

/* --- Rotations ---------------------------------------------------------- *
 * `set` is passed so root re-pointing can happen here in a single place. */

static AvlNode *rotate_right(AvlSet *set, AvlNode *y) {
    AvlNode *x  = y->left;
    AvlNode *t2 = x->right;

    x->right  = y;
    y->left   = t2;
    x->parent = y->parent;
    y->parent = x;
    reattach_parent(t2, y);

    if (y == set->root) {
        set->root = x;
    } else if (x->parent->left == y) {
        x->parent->left = x;
    } else {
        x->parent->right = x;
    }
    recompute_height(y);
    recompute_height(x);
    return x;
}

static AvlNode *rotate_left(AvlSet *set, AvlNode *x) {
    AvlNode *y  = x->right;
    AvlNode *t2 = y->left;

    y->left   = x;
    x->right  = t2;
    y->parent = x->parent;
    x->parent = y;
    reattach_parent(t2, x);

    if (x == set->root) {
        set->root = y;
    } else if (y->parent->left == x) {
        y->parent->left = y;
    } else {
        y->parent->right = y;
    }
    recompute_height(x);
    recompute_height(y);
    return y;
}

static AvlNode *make_node(const int cell_index) {
    AvlNode *n = malloc(sizeof(AvlNode));
    if (!n) return NULL;
    n->cell_index = cell_index;
    n->left = n->right = n->parent = NULL;
    n->height = 1;
    return n;
}

/* Walk up from `start` toward the root, recomputing heights and rebalancing.
 * After at most O(log n) hops the tree invariant is restored. */
static void rebalance_to_root(AvlSet *set, AvlNode *start) {
    AvlNode *cur = start;
    while (cur) {
        recompute_height(cur);
        const int bf = balance(cur);
        if (bf > 1) {
            if (balance(cur->left) < 0) {
                cur->left = rotate_left(set, cur->left);
            }
            cur = rotate_right(set, cur);
        } else if (bf < -1) {
            if (balance(cur->right) > 0) {
                cur->right = rotate_right(set, cur->right);
            }
            cur = rotate_left(set, cur);
        }
        cur = cur->parent;
    }
}

/* --- Search helpers ----------------------------------------------------- */

static AvlNode *find_node(AvlNode *root, const int cell_index) {
    AvlNode *cur = root;
    while (cur) {
        const int cmp = compare(cur->cell_index, cell_index);
        if (cmp == 0) return cur;
        cur = cmp > 0 ? cur->left : cur->right;
    }
    return NULL;
}

static AvlNode *find_min(AvlNode *n) {
    while (n && n->left) n = n->left;
    return n;
}

static AvlNode *find_successor(const AvlNode *n) {
    if (n->right) return find_min(n->right);
    AvlNode *p = n->parent;
    while (p && n == p->right) {
        n = p;
        p = p->parent;
    }
    return p;
}

/* --- Public API --------------------------------------------------------- */

AvlSet *avl_set_new(void) {
    AvlSet *s = malloc(sizeof(AvlSet));
    if (s) {
        s->root = NULL;
        s->size = 0;
    }
    return s;
}

/* Iterative post-order deletion: take left first, then right, then free.
 * Detaches children as we descend so the back-link still points back up. */
static void drain_subtree(AvlNode *start) {
    AvlNode *cur = start;
    while (cur) {
        if (cur->left) {
            AvlNode *l = cur->left;
            cur->left  = NULL;
            cur        = l;
            continue;
        }
        if (cur->right) {
            AvlNode *r = cur->right;
            cur->right = NULL;
            cur        = r;
            continue;
        }
        AvlNode *p = cur->parent;
        free(cur);
        cur = p;
    }
}

void avl_set_free(AvlSet *set) {
    if (!set) return;
    drain_subtree(set->root);
    free(set);
}

void avl_set_clear(AvlSet *set) {
    if (!set) return;
    drain_subtree(set->root);
    set->root = NULL;
    set->size = 0;
}

int avl_set_insert(AvlSet *set, const int cell_index) {
    if (!set) return 0;

    if (!set->root) {
        set->root = make_node(cell_index);
        if (!set->root) return 0;
        set->size = 1;
        return 1;
    }

    AvlNode *cur = set->root;
    AvlNode *parent = NULL;
    while (cur) {
        parent = cur;
        const int cmp = compare(cell_index, cur->cell_index);
        if (cmp == 0) return 0;       /* already present */
        cur = cmp < 0 ? cur->left : cur->right;
    }
    AvlNode *fresh = make_node(cell_index);
    if (!fresh) return 0;
    fresh->parent = parent;
    if (compare(cell_index, parent->cell_index) < 0) parent->left  = fresh;
    else                                             parent->right = fresh;
    set->size += 1;
    rebalance_to_root(set, fresh);
    return 1;
}

int avl_set_remove(AvlSet *set, const int cell_index) {
    if (!set) return 0;
    AvlNode *target = find_node(set->root, cell_index);
    if (!target) return 0;

    AvlNode *fix_from = NULL;

    if (!target->left || !target->right) {
        /* At most one child: splice it in. */
        AvlNode *child  = target->left ? target->left : target->right;
        AvlNode *parent = target->parent;
        if (child)  child->parent = parent;
        if (!parent) {
            set->root = child;
        } else {
            if (parent->left == target) parent->left  = child;
            else                        parent->right = child;
            fix_from = parent;
        }
        free(target);
    } else {
        /* Two children: copy successor's key in, then remove the successor
         * (which has at most a right child). */
        AvlNode *succ   = find_successor(target);
        target->cell_index = succ->cell_index;
        AvlNode *parent = succ->parent;
        AvlNode *child  = succ->right;
        if (parent->left == succ) parent->left  = child;
        else                      parent->right = child;
        if (child) child->parent = parent;
        fix_from = parent;
        free(succ);
    }

    set->size -= 1;
    if (fix_from) rebalance_to_root(set, fix_from);
    return 1;
}

int avl_set_contains(AvlSet *set, const int cell_index) {
    if (!set) return 0;
    return find_node(set->root, cell_index) ? 1 : 0;
}

size_t avl_set_size(const AvlSet *set) {
    return set ? (size_t) set->size : 0;
}

/* --- Iterator ----------------------------------------------------------- */

AvlSetIter *avl_set_iter_new(AvlSet *set) {
    if (!set) return NULL;
    AvlSetIter *iter = malloc(sizeof(AvlSetIter));
    if (!iter) return NULL;
    AvlNode *cur = set->root;
    while (cur && cur->left) cur = cur->left;
    iter->current = cur;
    return iter;
}

int avl_set_iter_next(AvlSetIter *iter) {
    if (!iter || !iter->current) return -1;
    const int v = iter->current->cell_index;
    iter->current = find_successor(iter->current);
    return v;
}

void avl_set_iter_free(AvlSetIter *iter) {
    free(iter);
}
