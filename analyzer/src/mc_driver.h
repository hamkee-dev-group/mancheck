#ifndef MC_DRIVER_H
#define MC_DRIVER_H

#include "mc_preproc.h"

/* Top-level “do the analysis for this file” entrypoint.
   You plug your existing logic in here. */
int mc_analyze_file_from_views(const mc_source_views *views,
                               int run_id);  /* or whatever extra params */

#endif
