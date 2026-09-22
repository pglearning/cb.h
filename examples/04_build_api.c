// The build API: CB_Cmd, process handling, redirection, async runs and pipes.
//
// cb_cmd_run(cmd, ...) is the single entry point; its options cover everything the older nob.h
// entry points did (sync/async/redirect):
//     .async = &procs, .max_procs = 4, .stdin_path/.stdout_path/.stderr_path, .dont_reset
#define CB_IMPLEMENTATION
#define CB_ENABLE_ECHO
#include "cb.h"

#define OUT_FOLDER "./build/examples/04_build_api/"

int main(void)
{
    cb_minimal_log_level = CB_INFO;
    if (!cb_mkdir_if_not_exists(OUT_FOLDER)) return 1;

    // ---- render a command without running it ------------------------------------------------
    CB_Cmd cmd = CB_ZERO;
    cb_cmd_append(&cmd, "echo", "hello", "build api");
    CB_String_Builder rendered = CB_ZERO;
    cb_cmd_to_sb(cmd, &rendered);
    cb_sb_append_null(&rendered);
    printf("rendered: %s\n", rendered.items);
    cb_sb_free(rendered);

    // ---- run it, and keep the command for a second run (.dont_reset) ------------------------
    if (!cb_cmd_run(&cmd, .dont_reset = true)) return 1;
    if (!cb_cmd_run(&cmd)) return 1;

    // ---- redirection ------------------------------------------------------------------------
    const char* out_path = OUT_FOLDER "redirect.txt";
    cb_cmd_append(&cmd, "echo", "redirected");
    if (!cb_cmd_run(&cmd, .stdout_path = out_path)) return 1;

    CB_String_Builder file = CB_ZERO;
    if (!cb_read_entire_file(out_path, &file)) return 1;
    printf("file says: %.*s", (int)file.count, file.items);
    cb_sb_free(file);

    // ---- file descriptors and pipes ----------------------------------------------------------
    CB_FD fd = cb_fd_open_write(OUT_FOLDER "fd.txt");
    if (fd == CB_INVALID_FD) return 1;
    cb_fd_close(fd);
    cb_log(CB_INFO, "wrote %s through a raw fd", OUT_FOLDER "fd.txt");

    // ---- async: run several commands, at most .max_procs at a time ---------------------------
    CB_Procs procs = CB_ZERO;
    for (int i = 0; i < 4; ++i) {
        cb_cmd_append(&cmd, "echo", cb_temp_sprintf("job %d", i));
        if (!cb_cmd_run(&cmd, .async = &procs, .max_procs = 2)) return 1;
    }
    if (!cb_procs_wait_and_reset(&procs)) return 1;
    free(procs.items);
    printf("async jobs done, nprocs = %d\n", cb_nprocs());

    // ---- chain: echo | tr | cat --------------------------------------------------------------
    CB_Chain chain = CB_ZERO;
    if (!cb_chain_begin(&chain)) return 1;
    cb_cmd_append(&cmd, "echo", "piped through three processes");
    if (!cb_chain_cmd(&chain, &cmd)) return 1;
    cb_cmd_append(&cmd, "tr", "a-z", "A-Z");
    if (!cb_chain_cmd(&chain, &cmd)) return 1;
    cb_cmd_append(&cmd, "cat");
    if (!cb_chain_cmd(&chain, &cmd)) return 1;
    if (!cb_chain_end(&chain)) return 1;
    free(chain.cmd.items);
    free(cmd.items);

    return 0;
}
