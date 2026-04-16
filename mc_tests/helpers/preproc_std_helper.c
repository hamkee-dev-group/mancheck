#include "mc_preproc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int
run_case(const char *name,
         const char *fixture,
         const char *compile_cmd,
         const char *extra_flags,
         const char *expect_marker1,
         const char *expect_marker2)
{
    mc_file_meta meta = {
        .path = fixture,
        .abs_path = fixture,
        .language = "c",
        .compiler = "clang",
        .compile_cmd = compile_cmd,
        .git_commit = NULL,
        .git_status = NULL
    };
    mc_source_views views;
    mc_pp_config cfg = {
        .clang_path = "clang",
        .extra_flags = extra_flags
    };

    if (mc_load_file(&meta, &views) != 0) {
        fprintf(stderr, "%s: mc_load_file failed\n", name);
        return 1;
    }

    if (mc_preprocess_clang(&views, &cfg) != 0 || !views.src_pp) {
        fprintf(stderr, "%s: mc_preprocess_clang failed\n", name);
        mc_free_source_views(&views);
        return 1;
    }

    if (!strstr(views.src_pp, expect_marker1)) {
        fprintf(stderr, "%s: missing marker %s\n", name, expect_marker1);
        mc_free_source_views(&views);
        return 1;
    }

    if (expect_marker2 && !strstr(views.src_pp, expect_marker2)) {
        fprintf(stderr, "%s: missing marker %s\n", name, expect_marker2);
        mc_free_source_views(&views);
        return 1;
    }

    mc_free_source_views(&views);
    return 0;
}

static int
run_error_case(const char *name,
               const char *fixture,
               const char *compile_cmd,
               const char *extra_flags)
{
    mc_file_meta meta = {
        .path = fixture,
        .abs_path = fixture,
        .language = "c",
        .compiler = "clang",
        .compile_cmd = compile_cmd,
        .git_commit = NULL,
        .git_status = NULL
    };
    mc_source_views views;
    mc_pp_config cfg = {
        .clang_path = "clang",
        .extra_flags = extra_flags
    };

    if (mc_load_file(&meta, &views) != 0) {
        fprintf(stderr, "%s: mc_load_file failed\n", name);
        return 1;
    }

    if (mc_preprocess_clang(&views, &cfg) == 0) {
        fprintf(stderr, "%s: expected mc_preprocess_clang error\n", name);
        mc_free_source_views(&views);
        return 1;
    }

    mc_free_source_views(&views);
    return 0;
}

int
main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr,
                "usage: %s <std-fixture> <compdb-fixture> <include-dir>\n",
                argv[0]);
        return 1;
    }

    const char *std_fixture = argv[1];
    const char *compdb_fixture = argv[2];
    const char *include_dir = argv[3];
    char compile_cmd[2048];

    if (run_case("split-token",
                 std_fixture,
                 "clang -c -std iso9899:2017 fixture.c",
                 "-std=c11",
                 "std-c17",
                 NULL) != 0)
        return 1;

    if (run_case("last-std-wins",
                 std_fixture,
                 "clang -c -std=c11 -std=gnu2x fixture.c",
                 "-std=c11",
                 "std-c2x",
                 NULL) != 0)
        return 1;

    if (run_case("compile-cmd-no-std",
                 std_fixture,
                 "clang -c -DMANCHECK_PP_STD=1 fixture.c",
                 "-std=c11",
                 "std-c11",
                 NULL) != 0)
        return 1;

    if (run_case("null-compile-cmd",
                 std_fixture,
                 NULL,
                 "-std=c11",
                 "std-c11",
                 NULL) != 0)
        return 1;

    if (run_error_case("compile-cmd-missing-include-arg",
                       std_fixture,
                       "clang -c -I",
                       "-std=c11") != 0)
        return 1;

    if (snprintf(compile_cmd,
                 sizeof(compile_cmd),
                 "clang -c -D MANCHECK_COMPILE_DB_FLAG=1 -I \"%s\" fixture.c",
                 include_dir) >= (int)sizeof(compile_cmd)) {
        fprintf(stderr, "compile command buffer too small\n");
        return 1;
    }

    if (run_case("compile-cmd-macro-and-include",
                 compdb_fixture,
                 compile_cmd,
                 "-std=c11",
                 "compile-cmd-macro",
                 "compile-cmd-include") != 0)
        return 1;

    return 0;
}
