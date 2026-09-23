#define CB_IMPLEMENTATION
#define CB_ENABLE_ECHO

// The tests are built with the cb_cc_flags default from cb.h (see its Build Flags section): it is
// already language and platform aware, so a C++ build of this script does not pass -std=c99 to a
// C++ compiler. Override cb_cc_flags before including cb.h if a project needs different flags.

#include "cb.h"

#define BUILD_FOLDER "./build/"
#define TESTS_FOLDER "./tests/"

const char* test_names[] = {
    "alloc_track",
    "arena",
    "build_flags",
    "bytes_for_utf8",
    "chain",
    "dynamic_array",
    "error_paths",
    "fs",
    "logging",
    "map",
    "nob_parity",
    "procs",
    "read_entire_dir",
    "stdlib",
    "string_builder",
    "string_view",
    "strip_prefix",
    "temp_storage",
    "toolbox",
};
#define test_names_count CB_ARRAY_LEN(test_names)

// Build one test into build/tests/<name>, run it inside its own sandbox directory and compare the
// captured stdout with tests/<name>.stdout.txt. With record = true the captured output becomes the
// expected output instead.
bool build_and_run_test(const char* test_name, bool record)
{
    bool result = true;
    CB_Cmd cmd = CB_ZERO;
    CB_String_Builder src = CB_ZERO;
    CB_String_Builder dst = CB_ZERO;
    CB_String_View src_sv = CB_ZERO;
    CB_String_View dst_sv = CB_ZERO;

#ifdef _WIN32
    const char* src_stdout_path = cb_temp_sprintf("%s%s.win32.stdout.txt", BUILD_FOLDER TESTS_FOLDER, test_name);
    const char* dst_stdout_path = cb_temp_sprintf("%s%s.win32.stdout.txt", TESTS_FOLDER, test_name);
    const char* test_stdout_path = cb_temp_sprintf("../%s.win32.stdout.txt", test_name);
#else
    const char* src_stdout_path = cb_temp_sprintf("%s%s.stdout.txt", BUILD_FOLDER TESTS_FOLDER, test_name);
    const char* dst_stdout_path = cb_temp_sprintf("%s%s.stdout.txt", TESTS_FOLDER, test_name);
    const char* test_stdout_path = cb_temp_sprintf("../%s.stdout.txt", test_name);
#endif // _WIN32

    const char* bin_path = cb_temp_sprintf("%s%s", BUILD_FOLDER TESTS_FOLDER, test_name);
    const char* src_path = cb_temp_sprintf("%s%s.c", TESTS_FOLDER, test_name);
    const char* test_cwd_path = cb_temp_sprintf("%s%s%s.cwd", BUILD_FOLDER, TESTS_FOLDER, test_name);

    cb_cc(&cmd);
    cb_cc_flags(&cmd);
    cb_cc_output(&cmd, bin_path);
    cb_cc_inputs(&cmd, src_path);
    if (!cb_cmd_run(&cmd, .dont_reset = false)) cb_return_defer(false);

    if (cb_file_exists(test_cwd_path)) {
        if (!cb_delete_directory_recursively(test_cwd_path)) cb_return_defer(false);
    }
    if (!cb_mkdir_if_not_exists(test_cwd_path)) cb_return_defer(false);
    if (!cb_set_current_dir(test_cwd_path)) cb_return_defer(false);

#ifdef _WIN32
    cb_cmd_append(&cmd, cb_temp_sprintf("../%s.exe", test_name));
#else
    cb_cmd_append(&cmd, cb_temp_sprintf("../%s", test_name));
#endif // _WIN32
    if (!cb_cmd_run(&cmd, .stdout_path = test_stdout_path)) cb_return_defer(false);
    if (!cb_set_current_dir("../../../")) cb_return_defer(false);

    if (record) {
        if (!cb_copy_file(src_stdout_path, dst_stdout_path)) cb_return_defer(false);
    } else {
        // TODO: a portable diff utility would be nicer than dumping both buffers.
        if (!cb_read_entire_file(src_stdout_path, &src)) cb_return_defer(false);
        if (!cb_read_entire_file(dst_stdout_path, &dst)) cb_return_defer(false);

        src_sv = cb_sb_to_sv(src);
        dst_sv = cb_sb_to_sv(dst);

        if (!cb_sv_eq(src_sv, dst_sv)) {
            cb_log(CB_ERROR, "UNEXPECTED OUTPUT!");
            cb_log(CB_ERROR, "EXPECTED:");
            fprintf(stderr, CB_SV_FMT, CB_SV_ARG(dst_sv));
            cb_log(CB_ERROR, "ACTUAL:");
            fprintf(stderr, CB_SV_FMT, CB_SV_ARG(src_sv));
            cb_return_defer(false);
        }
    }

    cb_log(CB_INFO, "---- %s finished ----", bin_path);

defer:
    free(src.items);
    free(dst.items);
    free(cmd.items);
    return result;
}

typedef struct {
    const char* name;
    const char* signature;
    const char* description;
} Command;

typedef struct {
    Command* items;
    size_t count;
    size_t capacity;

    bool picked;
    const char* picked_name;
    const char* picked_at_file;
    int picked_at_line;
} Commands;

void commands_reset(Commands* commands)
{
    commands->count = 0;
    commands->picked = false;
}

#define command(arg, commands, name, signature, description) \
    command_loc(__FILE__, __LINE__, (arg), (commands), (name), (signature), (description))
bool command_loc(const char* file, int line, const char* arg, Commands* commands, const char* name, const char* signature, const char* description)
{
    if (commands->picked) {
        fprintf(stderr, "%s:%d: ASSERTION FAILED: the branch for command `%s` fell through.\n", commands->picked_at_file, commands->picked_at_line, commands->picked_name);
        fprintf(stderr, "%s:%d: NOTE: the execution proceeded to here, but the command was already picked.\n", file, line);
        abort();
    }
    Command cmd = {
        .name = name,
        .signature = signature,
        .description = description,
    };
    cb_da_append(commands, cmd);
    commands->picked_name = name;
    commands->picked_at_line = line;
    commands->picked_at_file = file;
    commands->picked = (strcmp(arg, name) == 0);
    return commands->picked;
}

void print_available_commands(Commands commands)
{
    size_t max_name_width = 0;
    size_t max_sign_width = 0;
    cb_da_foreach(Command, command, &commands) {
        size_t name_width = strlen(command->name);
        size_t sign_width = strlen(command->signature);
        if (name_width > max_name_width) max_name_width = name_width;
        if (sign_width > max_sign_width) max_sign_width = sign_width;
    }
    cb_log(CB_INFO, "Available commands:");
    cb_da_foreach(Command, command, &commands) {
        cb_log(CB_INFO, "    %-*s %-*s - %s", (int)max_name_width, command->name, (int)max_sign_width, command->signature, command->description);
    }
}

bool run_tests(int argc, char** argv, bool record)
{
    if (!cb_mkdir_if_not_exists(BUILD_FOLDER)) return false;
    if (!cb_mkdir_if_not_exists(BUILD_FOLDER TESTS_FOLDER)) return false;

    size_t failed_count = 0;
    size_t total_count = argc > 0 ? (size_t)argc : test_names_count;
    if (argc <= 0) {
        for (size_t i = 0; i < test_names_count; ++i) {
            CB_Arena_Mark mark = cb_temp_save();
            if (!build_and_run_test(test_names[i], record)) failed_count += 1;
            cb_temp_rewind(mark);
        }
    } else {
        while (argc > 0) {
            CB_Arena_Mark mark = cb_temp_save();
            const char* test_name = cb_shift(argv, argc);
            if (!build_and_run_test(test_name, record)) failed_count += 1;
            cb_temp_rewind(mark);
        }
    }

    if (failed_count > 0) {
        cb_log(CB_ERROR, "%zu/%zu test(s) passed", total_count - failed_count, total_count);
        return false;
    }
    cb_log(CB_INFO, "%zu/%zu test(s) passed", total_count, total_count);
    return true;
}

int main(int argc, char** argv)
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif // _WIN32

    cb_minimal_log_level = CB_INFO;
    cb_set_log_handler(cb_cancer_log_handler);

    // Rebuild and re-run this build script whenever cb.h or cb.c changes (cb.c is __FILE__ here).
    CB_SELF_REBUILD(argc, argv, "cb.h");

    const char* program_name = cb_shift(argv, argc);
    const char* command_name = "test";
    if (argc > 0) command_name = cb_shift(argv, argc);

    Commands commands = CB_ZERO;
    commands_reset(&commands);

    if (command(command_name, &commands, "test", "[test_names...]", "Run the tests and compare their output")) {
        return run_tests(argc, argv, false) ? 0 : 1;
    }

    if (command(command_name, &commands, "record", "[test_names...]", "Run the tests and record their output as expected")) {
        return run_tests(argc, argv, true) ? 0 : 1;
    }

    if (command(command_name, &commands, "list", "", "List the available tests")) {
        cb_log(CB_INFO, "Tests:");
        for (size_t i = 0; i < test_names_count; ++i) {
            cb_log(CB_INFO, "    %s", test_names[i]);
        }
        cb_log(CB_INFO, "Use %s test <names...> to run individual tests", program_name);
        return 0;
    }

    if (command(command_name, &commands, "help", "", "Print this help message")) {
        print_available_commands(commands);
        return 0;
    }

    print_available_commands(commands);
    cb_log(CB_ERROR, "Unknown command %s", command_name);
    return 1;
}
