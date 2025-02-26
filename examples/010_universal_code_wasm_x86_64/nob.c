#define NOB_IMPLEMENTATION
#define NOB_STRIP_PREFIX
#include "nob.h"

#define BUILD_FOLDER "build/"

int main(int argc, char **argv)
{
    NOB_GO_REBUILD_URSELF(argc, argv);

    Cmd cmd = {0};
    Procs procs = {0};

    if (!mkdir_if_not_exists(BUILD_FOLDER)) return 1;

    cmd_append(&cmd, "clang");
    cmd_append(&cmd, "-Wall", "-Wextra", "-ggdb", "-I.");
    cmd_append(&cmd, "-o", BUILD_FOLDER"expr.native", "expr.c");
    da_append(&procs, nob_cmd_run_async_and_reset(&cmd));

    cmd_append(&cmd, "clang");
    cmd_append(&cmd, "-Wall", "-Wextra", "-I.");
    cmd_append(&cmd, "-DPLATFORM_WASM");
    cmd_append(&cmd, "--target=wasm32", "-Os", "-fno-builtin", "--no-standard-libraries", "-Wl,--no-entry", "-Wl,--export=main", "-Wl,--export=__heap_base", "-Wl,--allow-undefined");
    cmd_append(&cmd, "-o", BUILD_FOLDER"expr.wasm", "expr.c");
    da_append(&procs, nob_cmd_run_async_and_reset(&cmd));

    if (!nob_procs_wait_and_reset(&procs)) return 1;

    return 0;
}
