// Multi-project build: one table of projects, each with its own source directory, extra compiler
// flags, output file and an optional run step. Sources are collected recursively with cb_walk_dir.
//
// This is the shape used for day-to-day builds; point source_dir at your own projects. The demo
// tree below is written by the script itself so the example runs anywhere.
#define CB_IMPLEMENTATION
#define CB_ENABLE_ECHO
#include "cb.h"

#define OUT_FOLDER "./build/examples/01_multi_project/"
#define BIN_FOLDER OUT_FOLDER "bin/"

typedef struct {
    bool should_run;
    const char* source_dir;
    const char* output;
    const char* const* include_dirs; // NULL-terminated paths relative to OUT_FOLDER, may be NULL
    const char* const* link_inputs;  // NULL-terminated files relative to OUT_FOLDER, may be NULL
    const char* const* extra_flags;  // NULL-terminated compiler flags, may be NULL
    const char* run_arg;
} Project;

static const char* const libmath_flags[] = {"-fPIC", "-shared", NULL};
static const char* const app_includes[] = {"libmath/", NULL};
static const char* const app_links[] = {"libmath.so", NULL};
// $ORIGIN makes the executable look for libmath.so next to itself (no LD_LIBRARY_PATH needed).
static const char* const app_flags[] = {"-Wl,-rpath,$ORIGIN", NULL};

static const Project projects[] = {
    {false, "libmath/", "libmath.so", NULL, NULL, libmath_flags, NULL},
    {true, "app/", "app", app_includes, app_links, app_flags, "42"},
};
#define projects_count CB_ARRAY_LEN(projects)

static const char* libmath_header =
    "#ifndef MATHLIB_H_\n"
    "#define MATHLIB_H_\n"
    "int mathlib_square(int x);\n"
    "#endif // MATHLIB_H_\n";

static const char* libmath_source =
    "#include \"mathlib.h\"\n"
    "int mathlib_square(int x) { return x * x; }\n";

static const char* app_source =
    "#include <stdio.h>\n"
    "#include <stdlib.h>\n"
    "#include \"mathlib.h\"\n"
    "int main(int argc, char** argv)\n"
    "{\n"
    "    int value = argc > 1 ? atoi(argv[1]) : 7;\n"
    "    printf(\"app: mathlib_square(%d) = %d\\n\", value, mathlib_square(value));\n"
    "    return 0;\n"
    "}\n";

// Collect every .c file below the project directory. The path handed to the callback lives in temp
// storage, so it is copied before it is stored.
bool collect_sources(CB_Walk_Entry entry)
{
    if (entry.type != CB_FILE_REGULAR) return true;
    const char* ext = cb_temp_file_ext(entry.path);
    if (ext == NULL || strcmp(ext, ".c") != 0) return true;
    cb_da_append((CB_File_Paths*)entry.data, cb_temp_strdup(entry.path));
    return true;
}

bool build_project(const Project* project)
{
    bool result = true;
    CB_Cmd cmd = CB_ZERO;
    CB_File_Paths sources = CB_ZERO;

    const char* src_dir = cb_temp_sprintf("%s%s", OUT_FOLDER, project->source_dir);
    const char* bin_path = cb_temp_sprintf("%s%s", BIN_FOLDER, project->output);

    if (!cb_mkdir_if_not_exists(OUT_FOLDER)) cb_return_defer(false);
    if (!cb_mkdir_if_not_exists(BIN_FOLDER)) cb_return_defer(false);
    if (!cb_walk_dir(src_dir, collect_sources, .data = &sources)) cb_return_defer(false);
    if (sources.count == 0) {
        cb_log(CB_ERROR, "No sources under %s", src_dir);
        cb_return_defer(false);
    }

    cb_cc(&cmd);
    cb_cc_flags(&cmd);
    for (size_t i = 0; project->extra_flags != NULL && project->extra_flags[i] != NULL; ++i) {
        cb_cmd_append(&cmd, project->extra_flags[i]);
    }
    // Headers are never passed as compiler inputs: add the include directories instead.
    cb_cmd_append(&cmd, cb_temp_sprintf("-I%s", src_dir));
    for (size_t i = 0; project->include_dirs != NULL && project->include_dirs[i] != NULL; ++i) {
        cb_cmd_append(&cmd, cb_temp_sprintf("-I%s%s", OUT_FOLDER, project->include_dirs[i]));
    }
    cb_cc_output(&cmd, bin_path);
    for (size_t i = 0; i < sources.count; ++i) {
        cb_cc_inputs(&cmd, sources.items[i]);
    }
    // Libraries come after the sources. Ubuntu's gcc/clang pass --as-needed by default, and a library
    // listed before the objects that need it is dropped at that point, so the link fails with
    // undefined references ("-lfoo after the .c files" is the portable order on every platform).
    for (size_t i = 0; project->link_inputs != NULL && project->link_inputs[i] != NULL; ++i) {
        cb_cmd_append(&cmd, cb_temp_sprintf("%s%s", BIN_FOLDER, project->link_inputs[i]));
    }
    if (!cb_cmd_run(&cmd)) cb_return_defer(false);

    if (project->should_run) {
        cb_cmd_append(&cmd, bin_path);
        if (project->run_arg != NULL) cb_cmd_append(&cmd, project->run_arg);
        if (!cb_cmd_run(&cmd)) cb_return_defer(false);
    }

defer:
    free(sources.items);
    free(cmd.items);
    return result;
}

bool write_demo_tree(void)
{
    if (!cb_mkdir_if_not_exists(OUT_FOLDER "libmath")) return false;
    if (!cb_mkdir_if_not_exists(OUT_FOLDER "app")) return false;
    if (!cb_write_entire_file(OUT_FOLDER "libmath/mathlib.h", libmath_header, strlen(libmath_header))) return false;
    if (!cb_write_entire_file(OUT_FOLDER "libmath/mathlib.c", libmath_source, strlen(libmath_source))) return false;
    if (!cb_write_entire_file(OUT_FOLDER "app/main.c", app_source, strlen(app_source))) return false;
    return true;
}

int main(int argc, char** argv)
{
    cb_minimal_log_level = CB_INFO;
    cb_set_log_handler(cb_default_log_handler);

    // Rebuild and re-run this build script whenever cb.h or this file changes.
    CB_SELF_REBUILD(argc, argv, "cb.h");

    if (!write_demo_tree()) return 1;

    for (size_t i = 0; i < projects_count; ++i) {
        CB_Arena_Mark mark = cb_temp_save();
        bool ok = build_project(&projects[i]);
        cb_temp_rewind(mark);
        if (!ok) return 1;
    }
    return 0;
}
