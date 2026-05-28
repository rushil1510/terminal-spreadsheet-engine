/*
 * apps/tester.c -- Output comparator used by the `make test` pipeline.
 *
 * Reads two files:
 *   argv[1]  -- "actual" output produced by the test binary; each row looks
 *               like `CELL : <expr> --> Value: <int>, State: <state>`.
 *   argv[2]  -- "expected" output from a model run; each row looks like
 *               `CELL: <int|#DIV/0!>`.
 *
 * Walks the actual file in order, looks each cell up in the expected file,
 * and prints a "Mismatch ..." line for any difference. Cells that were in a
 * Zero* state in the actual output are treated as the `#DIV/0!` token to
 * match the model file's convention.
 */
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINE_LENGTH 1024
#define MAX_CELLS       26000
#define CELL_ID_LENGTH  10
#define VALUE_LENGTH    20

typedef struct {
    char cell[CELL_ID_LENGTH];
    char value[VALUE_LENGTH];
} CellEntry;

static int load_actual(const char *path, CellEntry *out, const int cap) {
    FILE *f = fopen(path, "r");
    if (!f) {
        printf("Error opening file: %s\n", path);
        return 0;
    }
    regex_t  r;
    regmatch_t m[4];
    if (regcomp(&r,
        "([A-Z]{1,3}[0-9]+) : .*--> Value: (-?[0-9]+), State: ([A-Za-z]+)",
        REG_EXTENDED) != 0) {
        printf("Could not compile actual-output regex\n");
        fclose(f);
        return 0;
    }
    char line[MAX_LINE_LENGTH];
    int  n = 0;
    while (fgets(line, MAX_LINE_LENGTH, f) && n < cap) {
        if (regexec(&r, line, 4, m, 0) != 0) continue;

        int  len   = m[1].rm_eo - m[1].rm_so;
        if (len >= CELL_ID_LENGTH) len = CELL_ID_LENGTH - 1;
        strncpy(out[n].cell, line + m[1].rm_so, len);
        out[n].cell[len] = '\0';

        len = m[2].rm_eo - m[2].rm_so;
        if (len >= VALUE_LENGTH) len = VALUE_LENGTH - 1;
        strncpy(out[n].value, line + m[2].rm_so, len);
        out[n].value[len] = '\0';

        len = m[3].rm_eo - m[3].rm_so;
        char state[20] = {0};
        if (len < 20) {
            strncpy(state, line + m[3].rm_so, len);
            state[len] = '\0';
        }
        if (strcmp(state, "Zero") == 0) strcpy(out[n].value, "#DIV/0!");

        n += 1;
    }
    regfree(&r);
    fclose(f);
    return n;
}

static int load_expected(const char *path, CellEntry *out, const int cap) {
    FILE *f = fopen(path, "r");
    if (!f) {
        printf("Error opening file: %s\n", path);
        return 0;
    }
    regex_t  r;
    regmatch_t m[3];
    if (regcomp(&r, "([A-Z]{1,3}[0-9]+): (-?[0-9]+|#DIV/0!)", REG_EXTENDED) != 0) {
        printf("Could not compile expected-output regex\n");
        fclose(f);
        return 0;
    }
    char line[MAX_LINE_LENGTH];
    int  n = 0;
    while (fgets(line, MAX_LINE_LENGTH, f) && n < cap) {
        if (regexec(&r, line, 3, m, 0) != 0) continue;

        int len = m[1].rm_eo - m[1].rm_so;
        if (len >= CELL_ID_LENGTH) len = CELL_ID_LENGTH - 1;
        strncpy(out[n].cell, line + m[1].rm_so, len);
        out[n].cell[len] = '\0';

        len = m[2].rm_eo - m[2].rm_so;
        if (len >= VALUE_LENGTH) len = VALUE_LENGTH - 1;
        strncpy(out[n].value, line + m[2].rm_so, len);
        out[n].value[len] = '\0';

        n += 1;
    }
    regfree(&r);
    fclose(f);
    return n;
}

static int lookup(const CellEntry *expected, const int count,
                  const char *cell, char *out_value) {
    for (int i = 0; i < count; ++i) {
        if (strcmp(expected[i].cell, cell) == 0) {
            strcpy(out_value, expected[i].value);
            return 1;
        }
    }
    strcpy(out_value, "MISSING");
    return 0;
}

int main(const int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: %s <actual> <expected>\n", argv[0]);
        return 1;
    }
    CellEntry *actual_entries   = malloc(sizeof(CellEntry) * MAX_CELLS);
    CellEntry *expected_entries = malloc(sizeof(CellEntry) * MAX_CELLS);

    const int actual_n   = load_actual  (argv[1], actual_entries,   MAX_CELLS);
    const int expected_n = load_expected(argv[2], expected_entries, MAX_CELLS);

    int differences = 0;
    for (int i = 0; i < actual_n; ++i) {
        char expected_value[VALUE_LENGTH];
        lookup(expected_entries, expected_n, actual_entries[i].cell, expected_value);
        if (strcmp(actual_entries[i].value, expected_value) == 0) continue;

        /* Tolerance: some model outputs truncate `#DIV/0!` to `#DIV`. */
        if (strcmp(expected_value, "#DIV") == 0
            && strcmp(actual_entries[i].value, "#DIV/0!") == 0) continue;

        printf("Mismatch at %s: actual has %s, expected has %s\n",
               actual_entries[i].cell,
               actual_entries[i].value,
               expected_value);
        differences = 1;
    }
    if (!differences) printf("All values match\n");
    free(actual_entries);
    free(expected_entries);
    return 0;
}
