#ifndef MC_PREPROC_H
#define MC_PREPROC_H

#include <stddef.h>

typedef struct {
    const char *path;        /* repo-relative path */
    const char *abs_path;    /* absolute path for I/O / clang -E */
    const char *language;    /* "c", "h", ... */
    const char *compiler;    /* "clang", "gcc", ... */
    const char *compile_cmd; /* optional, full compile command */
    const char *git_commit;  /* optional, HEAD sha */
    const char *git_status;  /* "M", "A", "D", ... */
} mc_file_meta;

typedef struct {
    mc_file_meta meta;

    char *src_raw;      /* raw file as on disk */
    char *src_min;      /* minimal preprocessed */
    char *src_pp;       /* full clang -E (all headers) */
    char *src_pp_user;  /* preprocessed, only original file lines */
} mc_source_views;

typedef struct {
    const char *clang_path;   /* e.g. "clang" */
    const char *extra_flags;  /* e.g. "-std=c11 -Iinclude" */
} mc_pp_config;

struct mc_preproc_hook;

typedef struct mc_preproc_hook {
    void *user_data;

    int (*on_views)(struct mc_preproc_hook *hook,
                    const mc_source_views *views);

    void (*destroy)(struct mc_preproc_hook *hook);
} mc_preproc_hook;

/* Stage A: read file into src_raw */
int mc_load_file(const mc_file_meta *meta, mc_source_views *out);

/* Stage B: build src_min from src_raw (simple comment stripper + ws normalize) */
int mc_preprocess_minimal(mc_source_views *views);

/* Stage C: run clang -E to fill src_pp */
int mc_preprocess_clang(mc_source_views *views,
                        const mc_pp_config *cfg);

/* Full pipeline over an array of files, with optional hook per file */
int mc_run_preproc_pipeline(const mc_file_meta *files,
                            size_t file_count,
                            const mc_pp_config *pp_cfg,
                            mc_preproc_hook *hook);

/* Free all three source buffers */
void mc_free_source_views(mc_source_views *views);
/* Build src_pp_user from src_pp using #line markers */
int mc_preprocess_pp_trim_user(mc_source_views *views);


#endif
