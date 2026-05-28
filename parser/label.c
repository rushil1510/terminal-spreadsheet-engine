/*
 * parser/label.c -- Bijection between alphabetical column labels (A, AA, ZZ,
 * AAA, ...) and the engine's zero-based column indices.
 *
 * The encoding mirrors Excel: A=0, Z=25, AA=26, AZ=51, BA=52, ... A "base-26
 * with no zero digit" system. The two helpers are inverses of each other.
 */
#include "label.h"
#include "../core/config.h"

#include <ctype.h>
#include <string.h>

short column_label_to_index(const char *col) {
    short out = 0;
    const size_t len = strlen(col);
    for (size_t i = 0; i + 1 < len; ++i) {
        out = (short) ((out + (col[i] - 'A' + 1)) * 26);
    }
    out = (short) (out + (col[len - 1] - 'A'));
    return out;
}

void column_index_to_label(short col, char *buffer) {
    int len = 0;
    if (col >= 26) {
        const int high = col / 26 - 1;
        const int low  = col % 26;
        if (high >= 26) {
            buffer[len++] = (char) ('A' + (high / 26 - 1));
            buffer[len++] = (char) ('A' + (high % 26));
        } else {
            buffer[len++] = (char) ('A' + high);
        }
        buffer[len++] = (char) ('A' + low);
    } else {
        buffer[len++] = (char) ('A' + col);
    }
    buffer[len] = '\0';
}

int label_parse_reference(const char *ref, short *row, short *col) {
    char col_chars[MAX_COL_LABEL] = {0};
    int  i = 0;
    int  col_running = 0;

    /* Consume the column letters. Cap at g_total_cols to reject huge inputs. */
    while (isalpha((unsigned char) ref[i]) && isupper((unsigned char) ref[i])
           && col_running * 26 + (toupper((unsigned char) ref[i]) - 'A' + 1) <= g_total_cols) {
        col_chars[i] = (char) toupper((unsigned char) ref[i]);
        col_running  = col_running * 26 + (toupper((unsigned char) ref[i]) - 'A' + 1);
        i++;
    }
    col_chars[i] = '\0';

    /* A reference needs at least one digit after the letters. */
    if (!isdigit((unsigned char) ref[i])) {
        *col = -1;
        *row = -1;
        return 0;
    }

    int row_running     = 0;
    int saw_digit       = 0;
    while (isdigit((unsigned char) ref[i])
           && (saw_digit || ref[i] != '0')        /* reject leading zero */
           && row_running * 10 + (ref[i] - '0') <= g_total_rows) {
        row_running = row_running * 10 + (ref[i] - '0');
        saw_digit   = 1;
        i++;
    }
    if (!saw_digit || isdigit((unsigned char) ref[i])) {
        /* Either the row was empty, or there are still digits that
         * overflowed past TOT_ROWS -- both are invalid references. */
        *col = -1;
        *row = -1;
        return 0;
    }

    *col = column_label_to_index(col_chars);
    *row = (short) (row_running - 1);
    return i;
}
