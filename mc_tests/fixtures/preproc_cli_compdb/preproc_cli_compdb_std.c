#if __STDC_VERSION__ >= 202000L
const char *mc_preproc_cli_compdb_std_marker = "std-c2x";
#elif __STDC_VERSION__ >= 201710L
const char *mc_preproc_cli_compdb_std_marker = "std-c17";
#elif __STDC_VERSION__ >= 201112L
const char *mc_preproc_cli_compdb_std_marker = "std-c11";
#else
const char *mc_preproc_cli_compdb_std_marker = "std-legacy";
#endif
