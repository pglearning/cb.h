#include <stddef.h>
#define CB_IMPLEMENTATION
#define CB_ENABLE_ECHO
#define CB_STRIP_PREFIX
#include "cb.h"

#define OUTPUT_FOLDER "./bin/"

typedef struct {
    const char* const * extra_flags;
    const char* include_dir;
    const char* source_dir;
    const char* output;
    bool should_run;
    const char* run_arg;
} Project;

static const Project projects[] = {
    {
        .extra_flags = (const char* const[]){"-fPIC", "-shared", NULL},
        .include_dir = "./examples/dynamic_library/include/",
        .source_dir = "./examples/dynamic_library/",
        .output = "core.so",
        .should_run = false,
        .run_arg = NULL,
    },
    {
        .extra_flags = (const char* const[]){"-Wl,-rpath,$ORIGIN", "./bin/core.so", NULL},
        .include_dir = "./examples/app/include/",
        .source_dir = "./examples/app/",
        .output = "app",
        .should_run = true,
        .run_arg = "7",
    },
};
#define projects_count ARRAY_LEN(projects)

// NOTE: This should collect all under project directory .c and .h file.
bool collect_sources(Walk_Entry entry)
{
    if (entry.type != FILE_REGULAR) return true;
    const char* ext = temp_file_ext(entry.path);
    if (ext == NULL) return true;
    if (strcmp(ext, ".c") != 0) return true;
    da_append((File_Paths*)entry.data, temp_strdup(entry.path));
    cb_log(CB_INFO, "%s", entry.path);
    return true;
}

// NOTE: This should collect all under project directory .h file.
bool collect_include(Walk_Entry entry)
{
    if (entry.type != FILE_REGULAR) return true;
    const char* ext = temp_file_ext(entry.path);
    if (ext == NULL) return true;
    if (strcmp(ext, ".h") != 0) return true;
    da_append((File_Paths*)entry.data, temp_strdup(entry.path));
    cb_log(CB_INFO, "%s", entry.path);
    return true;
}

bool project_run(const Project* project, const char* bin_path)
{
    Cmd cmd = ZERO;
    const char* stdout_path = temp_sprintf("%s%s.stdout.txt", OUTPUT_FOLDER, project->output);

    cmd_append(&cmd, bin_path);
    if (project->run_arg != NULL) cmd_append(&cmd, project->run_arg);

    bool ok = cmd_run(&cmd, .stdout_path = stdout_path);
    free(cmd.items);
    if (ok) cb_log(INFO, "run output -> %s", stdout_path);
    return ok;
}

bool project_build_and_run(const Project* project)
{
    bool result = true;
    Cmd cmd = ZERO;
    File_Paths files = ZERO;

    const char* src_dir = project->source_dir;
    const char* bin_path = temp_sprintf("%s%s", OUTPUT_FOLDER, project->output);

    if (!mkdir_if_not_exists(OUTPUT_FOLDER)) return_defer(false);
    if (!walk_dir(src_dir, collect_sources, .data = &files)) return_defer(false);
    if (files.count == 0) {
        cb_log(CB_ERROR, "no .c files under %s", src_dir);
        return_defer(false);
    }

    cc(&cmd);
    cc_flags(&cmd);
    if (project->include_dir) {
        cmd_append(&cmd, temp_sprintf("-I%s", project->include_dir));
    }
    cc_output(&cmd, bin_path);
    for (size_t i = 0; i < files.count; ++i) {
        cmd_append(&cmd, files.items[i]);
    }
    // 链接输入必须排在源码之后，否则 Ubuntu 默认的 --as-needed 会把库丢掉
    for (size_t i = 0; project->extra_flags != NULL && project->extra_flags[i] != NULL; ++i) {
        cmd_append(&cmd, project->extra_flags[i]);
    }
    if (!cmd_run(&cmd, .dont_reset = false)) return_defer(false);

    if (project->should_run && !project_run(project, bin_path)) return_defer(false);

defer:
    free(files.items);
    free(cmd.items);
    return result;
}

int main(int argc, char** argv)
{
    minimal_log_level = INFO;
    set_log_handler(default_log_handler);

    // Rebuild and re-run this build script whenever cb.h or this file changes. The dependency is
    // the cb.h this script actually includes (the copy next to it), not one found through the
    // current directory.
    SELF_REBUILD(argc, argv, "cb.h");

    for (size_t i = 0; i < projects_count; ++i) {
        Arena_Mark mark = temp_save();
        bool ok = project_build_and_run(&projects[i]);
        temp_rewind(mark);
        if (!ok) return 1;
    }
    return 0;
}
