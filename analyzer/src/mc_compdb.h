#ifndef MC_COMPDB_H
#define MC_COMPDB_H

#include <stddef.h>

struct mc_compile_db_entry {
    char *directory;
    char *file;
    char *resolved_file;
    char *command;
    char **arguments;
    size_t arg_count;
};

struct mc_compile_db {
    struct mc_compile_db_entry *entries;
    size_t count;
};

/* When a compile_commands.json entry provides only "arguments", the loader
 * synthesizes entry->command by joining argv with single-quoted shell tokens. */
int mc_load_compile_db(const char *path, struct mc_compile_db *db);
void mc_compile_db_free(struct mc_compile_db *db);

const struct mc_compile_db_entry *
mc_find_compile_db_entry(const struct mc_compile_db *db, const char *abs_path);

const char *
mc_compdb_lookup(const struct mc_compile_db *db, const char *abs_path);

#endif
