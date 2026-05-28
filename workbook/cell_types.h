/*
 * workbook/cell_types.h -- Value/Expression/Cell type vocabulary shared by
 * the workbook storage layer, the compute engine, the parser, and the UI.
 *
 * Layout notes:
 *  * Every Cell is bit-packed so we can keep tens of millions of them in RAM
 *    without crossing the 16-byte boundary per cell.
 *  * `Value`, `Arithmetic`, `Function`, `Expression` are *unpacked* helper
 *    structures returned by the parser; the workbook collapses them into the
 *    packed Cell layout on assignment.
 *  * The dependants list switches representation as it grows: an inline array
 *    of size 4 (cheap, no heap traffic) is upgraded to an AVL set on overflow.
 */
#ifndef TSE_WORKBOOK_CELL_TYPES_H
#define TSE_WORKBOOK_CELL_TYPES_H

/* --- Cell walk state (used by the DAG cycle-check and recompute pass) --- */
enum CellWalkState {
    CS_CLEAN              = 0,   /* value matches the formula            */
    CS_DFS_IN_PROGRESS    = 1,   /* on the cycle-detection DFS stack     */
    CS_CYCLE_CHECKED      = 2,   /* validated, awaiting recomputation    */
    CS_ZERO_ERROR         = 3    /* div-by-zero (propagates downstream)  */
};

/* --- Tagged union for a primitive Value (constant or cell reference) --- */
enum ValueKind {
    VK_INTEGER       = 0,
    VK_CELL_REF      = 1,
    VK_VALUE_ERROR   = 2
};

typedef struct {
    int             value;          /* integer literal *or* flat cell index */
    enum ValueKind  type: 2;
} __attribute__((packed)) Value;

/* --- Inline-then-AVL dependants list ------------------------------------ */
enum DependantsKind {
    DK_ARRAY = 0,
    DK_SET   = 1
};

typedef struct DependantsArray {
    int dependants_cells[4];
    int size;
} __attribute__((packed)) DependantsArray;

/* Forward declarations -- the AVL types live in containers/avl_set.h to keep
 * the dependency graph one-directional (workbook depends on containers, not
 * the reverse). They are pointed-to here only by pointer. */
struct AvlNode;
struct AvlSet;
typedef struct AvlSet AvlSet;

/* --- Range (used by aggregate functions) -------------------------------- */
typedef struct {
    int start_index;
    int end_index;
} __attribute__((packed)) Range;

/* --- Arithmetic & functions -------------------------------------------- */
enum ArithmeticOp {
    AOP_ADD       = 0,
    AOP_SUBTRACT  = 1,
    AOP_MULTIPLY  = 2,
    AOP_DIVIDE    = 3
};

typedef struct {
    enum ArithmeticOp type: 2;
    Value             value1;
    Value             value2;
} __attribute__((packed)) Arithmetic;

enum FunctionKind {
    FN_MIN   = 0,
    FN_MAX   = 1,
    FN_AVG   = 2,
    FN_SUM   = 3,
    FN_STDEV = 4,
    FN_SLEEP = 5
};

typedef struct {
    enum FunctionKind type: 3;
    union {
        Range range;       /* MIN/MAX/AVG/SUM/STDEV */
        Value value;       /* SLEEP                 */
    };
} __attribute__((packed)) Function;

/* --- Expression descriptor (returned by parser) ------------------------ */
enum ExpressionKind {
    EK_VALUE       = 0,
    EK_ARITHMETIC  = 1,
    EK_FUNCTION    = 2
};

typedef struct {
    enum ExpressionKind type: 2;
    union {
        Value      value;
        Arithmetic arithmetic;
        Function   function;
    };
} __attribute__((packed)) Expression;

/* --- Packed Cell layout -------------------------------------------------- *
 * The bit-fields encode the expression in-place to avoid storing a separate
 * tag byte per kind. See engine/engine.c for how the metadata mask is laid
 * out: bits [7:6]=expression kind, [5]=val1 kind, [4]=val2 kind, [3:2]=op. */
typedef struct Cell {
    int           value;               /* last evaluated integer value      */
    unsigned int  expression_type: 2;  /* enum ExpressionKind               */
    unsigned int  val1_type:       1;  /* 0 = int, 1 = cell-ref             */
    unsigned int  val2_type:       1;  /* 0 = int, 1 = cell-ref             *
                                        *  + carries function tag's HI bit  */
    unsigned int  op:              2;  /* AOP_* or FN_* low bits            */
    unsigned int  cell_state:      2;  /* enum CellWalkState                */
    int           val1;
    int           val2;
    enum DependantsKind dependants_type: 2;
    union {
        DependantsArray *dependants_array;
        AvlSet          *dependants_set;
    };
} __attribute__((packed)) Cell;

/* --- The stacks used by the engine's iterative DFS / Kahn walks --------- */
typedef struct {
    int  no_of_elements;
    int  dynamic_size;
    int *elements;
} IntStack;

typedef struct {
    int                  cell_index;
    enum CellWalkState   state: 2;
} StateMemo;

typedef struct {
    int        no_of_elements;
    int        dynamic_size;
    StateMemo *elements;
} MemoStack;

#endif /* TSE_WORKBOOK_CELL_TYPES_H */
