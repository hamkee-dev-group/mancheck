#include <stdio.h>

void mc_preproc_cli_compdb_macro_e2e(void)
{
    char buf[8];

#ifdef MANCHECK_COMPILE_DB_FLAG
    gets(buf);
#else
    (void)buf;
#endif
}
