/*
 * core/config.c -- Storage for the global configuration variables declared in
 * core/config.h. Defaults are placeholders -- the real values are written by
 * apps/main_*.c when it parses argv. Keeping them defined in a single TU
 * avoids multiple-definition errors at link time.
 */
#include "config.h"

short g_scroll_amount     = 10;
short g_cmd_history_size  = 7;
short g_viewport_rows     = 10;
short g_debug_gui         = 0;
short g_gui_mode          = 0;
short g_total_rows        = 0;
short g_total_cols        = 0;
short g_lazy_evaluation   = 1;
