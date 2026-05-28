/*
 * core/helpers.h -- Small generic utilities used across modules.
 *
 *  * Integer min/max clamps (kept here so the call sites read as english).
 *  * In-place string transforms used by the command parser.
 *  * Time-delta arithmetic used by the command latency reporter.
 *  * Linear address arithmetic (row/col <-> flat cell index).
 *
 * No module-level state lives here -- every function is pure given its args.
 */
#ifndef TSE_CORE_HELPERS_H
#define TSE_CORE_HELPERS_H

#include <time.h>

int    int_max(int a, int b);
int    int_min(int a, int b);

/* String mutators. Operate in place on a NUL-terminated buffer. */
void   strip_spaces(char *s);
void   ascii_upper(char *s);
int    char_count(const char *s, char c);

/* Returns the signed difference (t2 - t1) in seconds (as double) and writes
 * the normalized timespec to *out_delta. Mirrors the timing helper used in
 * the original engine's command latency display. */
double timespec_diff_sec(struct timespec t1, struct timespec t2,
                         struct timespec *out_delta);

/* Address arithmetic. The workbook is a single contiguous Cell array indexed
 * by row * total_cols + col -- these helpers hide the math. */
int   rowcol_to_index(short row, short col);
short index_to_row(int cell_index);
short index_to_col(int cell_index);

#endif /* TSE_CORE_HELPERS_H */
