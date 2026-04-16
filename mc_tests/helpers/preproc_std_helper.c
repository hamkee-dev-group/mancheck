#include "mc_preproc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int
run_case(const char *name,
         const char *fixture,
         const char *compile_cmd,
         const char *expect_marker)
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
        .extra_flags = "-std=c11"
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

    if (!strstr(views.src_pp, expect_marker)) {
        fprintf(stderr, "%s: missing marker %s\n", name, expect_marker);
        mc_free_source_views(&views);
        return 1;
    }

    mc_free_source_views(&views);
    return 0;
}

int
main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s <fixture>\n", argv[0]);
        return 1;
    }

    const char *fixture = argv[1];

    if (run_case("split-token",
                 fixture,
                 "clang -c -std iso9899:2017 fixture.c",
                 "std-c17") != 0)
        return 1;

    if (run_case("last-std-wins",
                 fixture,
                 "clang -c -std=c11 -std=gnu2x fixture.c",
                 "std-c2x") != 0)
        return 1;

    if (run_case("compile-cmd-no-std",
                 fixture,
                 "clang -c -DMANCHECK_PP_STD=1 fixture.c",
                 "std-c11") != 0)
        return 1;

    if (run_case("null-compile-cmd",
                 fixture,
                 NULL,
                 "std-c11") != 0)
        return 1;

    return 0;
}
