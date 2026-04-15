/* mc_preproc_demo.c */
#include "mc_preproc.h"
#include <stdio.h>
static int print_hook(struct mc_preproc_hook *hook,
                      const mc_source_views *views) {
    (void)hook;
    printf("=== %s ===\n", views->meta.path);

    printf("RAW:\n%s\n", views->src_raw);
    printf("\nMIN:\n%s\n", views->src_min);
    printf("\nPP (FULL):\n%s\n", views->src_pp ? views->src_pp : "<pp failed>\n");
    printf("\nPP (USER ONLY):\n%s\n",
           views->src_pp_user ? views->src_pp_user : "<trim failed>\n");

    return 0;
}


int main(void) {
    mc_file_meta meta = {
        .path        = "example.c",
        .abs_path    = "example.c",
        .language    = "c",
        .compiler    = "clang",
        .compile_cmd = NULL,
        .git_commit  = NULL,
        .git_status  = NULL
    };

    mc_pp_config cfg = {
        .clang_path  = "clang",
        .extra_flags = "-std=c11"
    };

    mc_preproc_hook hook = {
        .user_data = NULL,
        .on_views  = print_hook,
        .destroy   = NULL
    };

    mc_run_preproc_pipeline(&meta, 1, &cfg, &hook);
    return 0;
}
