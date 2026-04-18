#include "mc_compdb.h"

#include <stdio.h>
#include <stdlib.h>

int
main(int argc, char **argv)
{
    struct mc_compile_db db;
    const struct mc_compile_db_entry *entry;
    const char *command;

    if (argc != 3) {
        fprintf(stderr, "usage: %s <compile_commands.json> <abs-source-path>\n",
                argv[0]);
        return 1;
    }

    if (mc_load_compile_db(argv[1], &db) != 0) {
        fprintf(stderr, "load failed\n");
        return 1;
    }

    entry = mc_find_compile_db_entry(&db, argv[2]);
    command = mc_compdb_lookup(&db, argv[2]);

    if (!entry) {
        fprintf(stderr, "lookup failed\n");
        mc_compile_db_free(&db);
        return 1;
    }

    if (!command) {
        fprintf(stderr, "missing command\n");
        mc_compile_db_free(&db);
        return 1;
    }

    printf("command=%s\n", command);
    printf("argc=%zu\n", entry->arg_count);
    for (size_t i = 0; i < entry->arg_count; i++)
        printf("argv[%zu]=%s\n", i, entry->arguments[i]);

    mc_compile_db_free(&db);
    return 0;
}
