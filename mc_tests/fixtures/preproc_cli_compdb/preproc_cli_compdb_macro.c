#include "preproc_cli_compdb_header.h"

#ifdef MANCHECK_COMPILE_DB_FLAG
const char *mc_preproc_cli_compdb_macro_marker = "compile-cmd-macro";
#else
const char *mc_preproc_cli_compdb_macro_marker = "compile-cmd-macro-missing";
#endif

const char *mc_preproc_cli_compdb_include_marker =
    MANCHECK_COMPILE_DB_INCLUDE_MARKER;
