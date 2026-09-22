// Single project build: the smallest useful build script.
//
// Every build needs the same four pieces: pick the compiler (cb_cc), add flags (cb_cc_flags), name
// the output (cb_cc_output), list the inputs (cb_cc_inputs), then run the command (cb_cmd_run).
// cb_needs_rebuild() skips the compiler when nothing changed.
#define CB_IMPLEMENTATION
#define CB_ENABLE_ECHO
#include "cb.h"

#define OUT_FOLDER "./build/examples/02_single_project/"
#define SRC_PATH OUT_FOLDER "main.c"
#define BIN_PATH OUT_FOLDER "hello"

static const char* source =
    "#include <stdio.h>\n"
    "int main(void)\n"
    "{\n"
    "    printf(\"hello from a single-project build\\n\");\n"
    "    return 0;\n"
    "}\n";

int main(void)
{
    cb_minimal_log_level = CB_INFO;

    if (!cb_mkdir_if_not_exists(OUT_FOLDER)) return 1;
    if (!cb_write_entire_file(SRC_PATH, source, strlen(source))) return 1;

    // 1 = rebuild needed (missing or older than the source), 0 = up to date, -1 = error.
    const char* inputs[] = {SRC_PATH};
    int needs_rebuild = cb_needs_rebuild(BIN_PATH, inputs, CB_ARRAY_LEN(inputs));
    if (needs_rebuild < 0) return 1;

    if (needs_rebuild) {
        CB_Cmd cmd = CB_ZERO;
        cb_cc(&cmd);
        cb_cc_flags(&cmd);
        cb_cc_output(&cmd, BIN_PATH);
        cb_cc_inputs(&cmd, SRC_PATH);
        if (!cb_cmd_run(&cmd)) return 1;
        free(cmd.items);
    } else {
        cb_log(CB_INFO, "%s is up to date", BIN_PATH);
    }

    CB_Cmd run = CB_ZERO;
    cb_cmd_append(&run, BIN_PATH);
    if (!cb_cmd_run(&run)) return 1;

    free(run.items);
    return 0;
}
