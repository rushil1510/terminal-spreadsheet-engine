/*
 * core/helpers.c -- Implementation of small cross-cutting utilities.
 */
#include "helpers.h"
#include "config.h"

#include <ctype.h>
#include <string.h>

int int_max(const int a, const int b) {
    return a >= b ? a : b;
}

int int_min(const int a, const int b) {
    return a <= b ? a : b;
}

void strip_spaces(char *s) {
    /* Two-cursor walk: src skips spaces, dst writes the kept characters. */
    char *src = s;
    char *dst = s;
    do {
        while (*src == ' ') {
            ++src;
        }
    } while ((*dst++ = *src++));
}

void ascii_upper(char *s) {
    for (size_t i = 0; s[i] != '\0'; ++i) {
        s[i] = (char) toupper((unsigned char) s[i]);
    }
}

int char_count(const char *s, const char c) {
    int n = 0;
    for (size_t i = 0; s[i] != '\0'; ++i) {
        if (s[i] == c) {
            ++n;
        }
    }
    return n;
}

double timespec_diff_sec(struct timespec t1, struct timespec t2,
                         struct timespec *out_delta) {
    out_delta->tv_nsec = t2.tv_nsec - t1.tv_nsec;
    out_delta->tv_sec  = t2.tv_sec  - t1.tv_sec;
    /* Carry/borrow normalization so the nsec field has the same sign as sec. */
    if (out_delta->tv_sec > 0 && out_delta->tv_nsec < 0) {
        out_delta->tv_nsec += 1000000000L;
        out_delta->tv_sec  -= 1;
    } else if (out_delta->tv_sec < 0 && out_delta->tv_nsec > 0) {
        out_delta->tv_nsec -= 1000000000L;
        out_delta->tv_sec  += 1;
    }
    return (double) out_delta->tv_sec
         + (double) out_delta->tv_nsec / 1000000000.0;
}

int rowcol_to_index(const short row, const short col) {
    return (int) row * (int) g_total_cols + (int) col;
}

short index_to_row(const int cell_index) {
    return (short) (cell_index / g_total_cols);
}

short index_to_col(const int cell_index) {
    return (short) (cell_index % g_total_cols);
}
