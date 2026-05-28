/*
 * parser/label.h -- Cell-reference label codec.
 *
 *  A1, AA12, ABC456  <->  (row, col) pairs.
 *
 * Conversions are bounded by g_total_rows / g_total_cols so that out-of-range
 * references can be rejected by the parser at the source.
 */
#ifndef TSE_PARSER_LABEL_H
#define TSE_PARSER_LABEL_H

#include <stdbool.h>

short column_label_to_index (const char *col);
void  column_index_to_label (short col, char *buffer);

/* Parses a leading "<COL><ROW>" out of `ref`. Returns the number of bytes
 * consumed (so the caller can keep parsing past the reference), or 0 if no
 * valid reference is present at the prefix. On success row/col are written
 * with zero-based indices. */
int   label_parse_reference (const char *ref, short *row, short *col);

#endif /* TSE_PARSER_LABEL_H */
