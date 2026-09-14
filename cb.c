// 必须放在最顶上：下面在 cb.h 之前还包含了 stdbool/stddef/stdio，而 glibc 的
// features.h 一旦先被处理过，后面再定义 _POSIX_C_SOURCE 就来不及了——-std=c11 下
// lstat/readlink/nanosleep/PATH_MAX 会变成未声明。（cb.h 里那份是 #ifndef 守卫的，不冲突）
#define _POSIX_C_SOURCE 200809L

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
// #define CB_ENABLE_ECHO
// #define CB_DONT_DELETE_OLD_CB
// #define CB_TRACE_CMD_RUN_FAIL_LOCATION
// C++ 下用 {0} 初始化多成员结构体会触发 -Wmissing-field-initializers /
// -Wmissing-designated-field-initializers（后者只有 clang 有），这两个告警没有信息量，就地关掉。
//
// 三层守卫，缺一不可（任何一层漏掉都会变成"本地干净、CI 报警"）：
//   1. #ifdef __cplusplus —— C 下 {0} 本来就不触发这个告警；
//   2. 按编译器分支 —— gcc 见到 `#pragma clang diagnostic` 会报 -Wunknown-pragmas
//      （-Wall 里带着它），clang 见到 `#pragma GCC diagnostic` 也一样；
//   3. __has_warning 探测 —— "-Wmissing-designated-field-initializers" 是较新 clang 才有的
//      warning group，老 clang 不认识它，直接写反而会报
//      "unknown warning group '-Wmissing-designated-field-initializers', ignored"。
//      gcc 那边 "-Wmissing-field-initializers" 从 4.x 就有，不需要探测。
#ifdef __cplusplus
#if defined(__clang__)
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#if defined(__has_warning)
#if __has_warning("-Wmissing-designated-field-initializers")
#pragma clang diagnostic ignored "-Wmissing-designated-field-initializers"
#endif // __has_warning("-Wmissing-designated-field-initializers")
#endif // defined(__has_warning)
#elif defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif // __clang__ / __GNUC__
#endif // __cplusplus

// cb.h 里的 cb_cc / cb_cc_flags 只是兜底（只有 -Wall -Wextra，没有 -I. 也没有 -std=），
// 满足不了本项目，所以在 include 之前把整对顶掉（那一整块是 #ifndef 守卫的默认值）。
//
// 配套用 cb_cc_output / cb_cc_inputs，不手写 "cc" 和 "-Wall ..." 字符串：编译器与 flag
// 只有这一处定义，换平台时改宏而不是改十几处调用。
// 需要指定 stdout 之类的选项时直接写 cb_cmd_run(&cmd, .stdout_path = p)，收尾一律 free(cmd.items)。
#ifdef __cplusplus
#define cb_cc(cmd) cb_cmd_append(cmd, "cc", "-x", "c++")
#define cb_cc_flags(cmd) cb_cmd_append(cmd, "-Wall", "-Wextra", "-Wswitch-enum", "-std=c++17", "-I.")
#else
#define cb_cc(cmd) cb_cmd_append(cmd, "cc")
#define cb_cc_flags(cmd) cb_cmd_append(cmd, "-Wall", "-Wextra", "-Wswitch-enum", "-std=gnu11", "-I.")
#endif // __cplusplus
#include "cb.h"

// Folder must end with forward slash /
#define BUILD_FOLDER "./build/"
#define TESTS_FOLDER "./tests/"

const char* test_names[] = {
    "bytes_for_utf8",
    "read_entire_dir",

    "arena",
    "chain",
    "error_paths",
    "fs",
    "map",
    "nob_parity",
    "stdlib",
    "toolbox",
    "dynamic_array",
    "string_builder",
    "string_view",
    "strip_prefix",
    "temp_storage",
};
#define test_names_count CB_ARRAY_LEN(test_names)

// 比对失败时最多打印多少行差异（金标准动辄几百行，全打出来会冲掉终端）
#define DIFF_MAX_LINES 10
// 每行差异最多打印多少字节
#define DIFF_MAX_BYTES 200

// 按行比对，只打印前 max_lines 处不同（cb_sv_chop_by_delim 就地推进 sv，所以两个入参按值传）。
void print_output_diff(CB_String_View expected, CB_String_View actual, size_t max_lines)
{
    size_t line = 1;
    size_t shown = 0;
    size_t total = 0;

    while (expected.count > 0 || actual.count > 0) {
        CB_String_View e = cb_sv_chop_by_delim(&expected, '\n');
        CB_String_View a = cb_sv_chop_by_delim(&actual, '\n');
        if (!cb_sv_eq(e, a)) {
            total += 1;
            if (shown < max_lines) {
                // 两边都要按同一个上限截断，否则 "第 N 行" 那一行会长得没法看
                if (e.count > DIFF_MAX_BYTES) e.count = DIFF_MAX_BYTES;
                if (a.count > DIFF_MAX_BYTES) a.count = DIFF_MAX_BYTES;
                cb_log(CB_ERROR, "line %zu:", line);
                fprintf(stderr, "    EXPECTED: " CB_SV_FMT "\n", CB_SV_ARG(e));
                fprintf(stderr, "    ACTUAL:   " CB_SV_FMT "\n", CB_SV_ARG(a));
                shown += 1;
            }
        }
        line += 1;
    }

    if (total > shown) cb_log(CB_ERROR, "... and %zu more different lines", total - shown);
    if (total == 0) cb_log(CB_ERROR, "content looks equal, the difference must be invisible bytes");
}

bool build_and_run_test(const char* test_name, bool record)
{
    bool result = true;
    bool ran = false;
    CB_Cmd cmd = {0};
    CB_String_Builder src = {0};
    CB_String_Builder dst = {0};
    CB_String_View src_sv = {0};
    CB_String_View dst_sv = {0};
    const char* original_cwd = cb_get_current_dir_temp();

#ifdef _WIN32
    const char* src_stdout_path  = cb_temp_sprintf("%s%s.win32.stdout.txt", BUILD_FOLDER TESTS_FOLDER, test_name);
    const char* dst_stdout_path  = cb_temp_sprintf("%s%s.win32.stdout.txt", TESTS_FOLDER, test_name);
    const char* test_stdout_path = cb_temp_sprintf("../%s.win32.stdout.txt", test_name);
    const char* test_bin_path    = cb_temp_sprintf("../%s.exe", test_name);
#else
    const char* src_stdout_path  = cb_temp_sprintf("%s%s.stdout.txt", BUILD_FOLDER TESTS_FOLDER, test_name);
    const char* dst_stdout_path  = cb_temp_sprintf("%s%s.stdout.txt", TESTS_FOLDER, test_name);
    const char* test_stdout_path = cb_temp_sprintf("../%s.stdout.txt", test_name);
    const char* test_bin_path    = cb_temp_sprintf("../%s", test_name);
#endif // _WIN32
    const char* bin_path         = cb_temp_sprintf("%s%s", BUILD_FOLDER TESTS_FOLDER, test_name);
    const char* src_path         = cb_temp_sprintf("%s%s.c", TESTS_FOLDER, test_name);
    const char* test_cwd_path    = cb_temp_sprintf("%s%s%s.cwd", BUILD_FOLDER, TESTS_FOLDER, test_name);

    if (!cb_mkdir_if_not_exists(BUILD_FOLDER TESTS_FOLDER)) cb_return_defer(false);

    cb_cc(&cmd);
    cb_cc_flags(&cmd);
    cb_cc_output(&cmd, bin_path);
    cb_cc_inputs(&cmd, src_path);
    if (!cb_cmd_run(&cmd)) cb_return_defer(false);

    // 沙箱：测试进程的 cwd 就在里面，它创建的文件不会污染仓库根目录。
    // 每次运行前清空重建；成功后删除，失败时保留（方便进去看）。
    if (cb_file_exists(test_cwd_path)) {
        if (!cb_delete_directory_recursively(test_cwd_path)) cb_return_defer(false);
    }
    if (!cb_mkdir_if_not_exists(test_cwd_path)) cb_return_defer(false);
    if (!cb_set_current_dir(test_cwd_path)) cb_return_defer(false);

    // 只收走 stdout（金标准比对的对象），stderr 留在终端上（崩溃、断言失败、检测器报错都在那）。
    cb_cmd_append(&cmd, test_bin_path);
    ran = cb_cmd_run(&cmd, .stdout_path = test_stdout_path);

    if (!cb_set_current_dir(original_cwd)) cb_return_defer(false);
    if (!ran) cb_return_defer(false); // 测试程序自己的退出码非 0

    if (record) {
        if (!cb_copy_file(src_stdout_path, dst_stdout_path)) cb_return_defer(false);
    } else {
        // TODO: it would be cool to have a portable diff utility in here.
        if (!cb_read_entire_file(src_stdout_path, &src)) cb_return_defer(false);
        if (!cb_read_entire_file(dst_stdout_path, &dst)) cb_return_defer(false);

        src_sv = cb_sb_to_sv(src);
        dst_sv = cb_sb_to_sv(dst);

        if (!cb_sv_eq(src_sv, dst_sv)) {
            cb_log(CB_ERROR, "UNEXPECTED OUTPUT!");
            cb_log(CB_ERROR, "EXPECTED %s", dst_stdout_path);
            cb_log(CB_ERROR, "ACTUAL   %s", src_stdout_path);
            print_output_diff(dst_sv, src_sv, DIFF_MAX_LINES);
            cb_return_defer(false);
        }
    }

    cb_log(CB_INFO, "---- %s finished ----", bin_path);

defer:
    // 失败时保留沙箱，但当前目录必须换回来，不然接下来的清理全落在沙箱里
    if (result) {
        if (cb_file_exists(test_cwd_path)) {
            if (!cb_delete_directory_recursively(test_cwd_path)) result = false;
        }
    } else {
        if (cb_file_exists(test_cwd_path)) cb_log(CB_INFO, "sandbox kept at %s", test_cwd_path);
    }
    free(dst.items);
    free(src.items);
    free(cmd.items);
    return result;
}

typedef struct {
    const char* name;
    const char* arg; // 传给示例的额外参数，可为 NULL
} Example;

const Example examples[] = {
    {"01_hello", NULL},
    {"02_single_project", NULL},
    {"03_multi_project", NULL},
    {"04_two_stage", NULL},
    {"05_build_api", NULL},
    {"06_containers", NULL},
    {"07_strings", NULL},
    {"08_utf8", NULL},
    {"09_paths", NULL},
    {"10_filesystem", NULL},
    {"11_memory", NULL},
    {"12_logging", NULL},
    {"13_toolbox", NULL},
    {"14_config_switches", NULL},
};
#define examples_count CB_ARRAY_LEN(examples)

// 编译并运行一个示例。示例的输出不走重定向：它们是给人看的说明，默认就该完整显示。
// 示例必须在仓库根目录跑，它们内部的路径都相对于当前工作目录。
bool build_and_run_example(const char* name, const char* arg)
{
    bool result = true;
    CB_Cmd cmd = {0};
    const char* src_path = cb_temp_sprintf("examples/%s.c", name);
    const char* bin_path = cb_temp_sprintf("%sexamples/%s", BUILD_FOLDER, name);

    if (!cb_mkdir_if_not_exists(BUILD_FOLDER "examples/")) cb_return_defer(false);

    cb_cc(&cmd);
    cb_cc_flags(&cmd);
    // 11_memory 打开内存追踪后才会打印泄漏报告，12_logging 的调用栈要 -rdynamic 才有符号
    if (strcmp(name, "11_memory") == 0) cb_cmd_append(&cmd, "-DCB_ALLOC_TRACK");
    if (strcmp(name, "12_logging") == 0) cb_cmd_append(&cmd, "-rdynamic");
    cb_cc_output(&cmd, bin_path);
    cb_cc_inputs(&cmd, src_path);
    if (!cb_cmd_run(&cmd)) cb_return_defer(false);

    cb_cmd_append(&cmd, bin_path);
    if (arg != NULL) cb_cmd_append(&cmd, arg);
    if (!cb_cmd_run(&cmd)) cb_return_defer(false);

defer:
    free(cmd.items);
    return result;
}

// 编译 bench/bench.c 并运行。基准必须用 -O2：debug 构建测出来的数字毫无意义。
// 表就是要给人看的，所以它的输出不重定向；argv 原样透传，用来选组。
bool build_and_run_bench(int argc, char** argv)
{
    bool result = true;
    int i = 0;
    CB_Cmd cmd = {0};
    const char* bin_path = BUILD_FOLDER "bench/bench";

    if (!cb_mkdir_if_not_exists(BUILD_FOLDER "bench/")) cb_return_defer(false);

    cb_cc(&cmd);
    cb_cc_flags(&cmd);
    cb_cmd_append(&cmd, "-O2");
    cb_cc_output(&cmd, bin_path);
    cb_cc_inputs(&cmd, "bench/bench.c");
    if (!cb_cmd_run(&cmd)) cb_return_defer(false);

    cb_cmd_append(&cmd, bin_path);
    for (i = 0; i < argc; ++i) cb_cmd_append(&cmd, argv[i]);
    if (!cb_cmd_run(&cmd)) cb_return_defer(false);

defer:
    free(cmd.items);
    return result;
}

// 用 C++ 编译器编译构建脚本本身并跑一下 list。clang++ 与 g++ 都要过：g++ 更严格。
bool check_cxx_compat(void)
{
    bool result = true;
    size_t i = 0;
    CB_Cmd cmd = {0};
    CB_Cmd cxx = {0};
    const char* bin_path = BUILD_FOLDER "cb_cxx_check";
    const char* log_path = BUILD_FOLDER "cb_cxx_check.list.txt";
    const char* cxx_compilers[] = {"clang++", "g++"};

    if (!cb_mkdir_if_not_exists(BUILD_FOLDER)) cb_return_defer(false);

    for (i = 0; i < CB_ARRAY_LEN(cxx_compilers); ++i) {
        // "-x c++" 不能省：clang++ 见到 .c 会警告 "treating 'c' input as 'c++' is deprecated"
        cb_cmd_append(&cxx, cxx_compilers[i], "-Wall", "-Wextra", "-Wswitch-enum", "-std=c++17", "-I.", "-x", "c++");
        cb_cc_output(&cxx, bin_path);
        cb_cc_inputs(&cxx, "cb.c");
        if (!cb_cmd_run(&cxx)) cb_return_defer(false);
    }

    // 跑一个不需要重新构建自身的子命令。输出收走：这只是冒烟检查，不该污染调用者的终端。
    // 注意 stdout 与 stderr 都要收：cb_log 写的是 stderr，只重定向 stdout 会漏一整页测试列表。
    cb_cmd_append(&cmd, bin_path, "list");
    if (!cb_cmd_run(&cmd, .stdout_path = log_path, .stderr_path = log_path)) cb_return_defer(false);
    cb_delete_file(log_path);

defer:
    free(cxx.items);
    free(cmd.items);
    return result;
}

// python3 在不在。gen-docs.py 以 1 退出表示"文档过期或有无使用者的接口"，这是正常的
// 检查结果，不能和"脚本根本没跑起来"混为一谈。
bool python3_available(void)
{
    bool result = false;
    CB_Cmd cmd = {0};
#ifdef _WIN32
    const char* null_device = "NUL";
#else
    const char* null_device = "/dev/null";
#endif // _WIN32

    cb_cmd_append(&cmd, "python3", "--version");
    result = cb_cmd_run(&cmd, .stdout_path = null_device, .stderr_path = null_device);

    free(cmd.items);
    return result;
}

// docs/api.md 与 docs/api-coverage.md 是从 cb.h 自动生成的，不能手改。
// check_only 时不写文件，只校验"文档是不是最新的"和"有没有没人用的公共接口"。
bool run_docs(bool check_only)
{
    bool result = true;
    CB_Cmd cmd = {0};
    const char* script = "tools/gen-docs.py";

    if (!cb_file_exists(script)) {
        cb_log(CB_ERROR, "%s not found, run this from the project root", script);
        cb_return_defer(false);
    }

    if (!python3_available()) {
        cb_log(CB_ERROR, "python3 is required to generate the docs");
        cb_return_defer(false);
    }

    cb_cmd_append(&cmd, "python3", script);
    if (check_only) cb_cmd_append(&cmd, "--check");
    // 脚本自己会解释失败原因（哪些文档过期、哪些接口没人用），这里不再重复一遍
    if (!cb_cmd_run(&cmd)) cb_return_defer(false);

defer:
    free(cmd.items);
    return result;
}

bool clean_build_folder(void)
{
    if (!cb_file_exists(BUILD_FOLDER)) {
        cb_log(CB_INFO, "nothing to clean, %s does not exist", BUILD_FOLDER);
        return true;
    }
    if (!cb_delete_directory_recursively(BUILD_FOLDER)) return false;
    cb_log(CB_INFO, "removed %s", BUILD_FOLDER);
    return true;
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

#define command(arg, commands, name, signature, description) command_loc(__FILE__, __LINE__, (arg), (commands), (name), (signature), (description))
bool command_loc(const char* file, int line, const char* arg, Commands* commands, const char* name, const char* signature, const char* description)
{
    if (commands->picked) {
        fprintf(stderr, "%s:%d: ASSERTION FAILED: the branch for command `%s` fell through.\n", commands->picked_at_file, commands->picked_at_line, commands->picked_name);
        fprintf(stderr, "%s:%d: NOTE: the execution proceeded to here, but the command was already picked.\n", file, line);
        abort();
    }
    Command command = {
        .name = name,
        .signature = signature,
        .description = description,
    };
    cb_da_append(commands, command);
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
    cb_da_foreach(Command, command, &commands)
    {
        size_t name_width = strlen(command->name);
        size_t sign_width = strlen(command->signature);
        if (name_width > max_name_width) max_name_width = name_width;
        if (sign_width > max_sign_width) max_sign_width = sign_width;
    }
    cb_log(CB_INFO, "Available commands:");
    cb_da_foreach(Command, command, &commands)
    {
        cb_log(CB_INFO, "    %-*s %-*s - %s", (int)max_name_width, command->name, (int)max_sign_width, command->signature, command->description);
    }
}

int main(int argc, char* argv[])
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif // _WIN32

    cb_minimal_log_level = CB_INFO;
    cb_set_log_handler(cb_default_log_handler);

    CB_SELF_REBUILD_PLUS(argc, argv, "cb.h");

    const char* program_name = cb_shift(argv, argc);
    const char* command_name = "test";
    if (argc > 0) command_name = cb_shift(argv, argc);
    // -h / --help 等价于 help 命令
    if (strcmp(command_name, "-h") == 0 || strcmp(command_name, "--help") == 0) command_name = "help";

    Commands commands = {0};
    commands_reset(&commands);

    if (command(command_name, &commands, "test", "[test_names...]", "Run the tests checking their expected output")) {
        if (!check_cxx_compat()) return 1;

        size_t failed_count = 0;
        size_t count = 0;
        if (argc <= 0) {
            for (size_t i = 0; i < test_names_count; ++i) {
                CB_Arena_Mark mark = cb_temp_save();
                if (!build_and_run_test(test_names[i], false)) failed_count += 1;
                cb_temp_rewind(mark);
                count += 1;
            }
        } else {
            while (argc > 0) {
                CB_Arena_Mark mark = cb_temp_save();
                const char* test_name = cb_shift(argv, argc);
                if (!build_and_run_test(test_name, false)) failed_count += 1;
                cb_temp_rewind(mark);
                count += 1;
            }
        }
        if (failed_count > 0) {
            cb_log(CB_ERROR, "%zu/%zu test(s) failed", failed_count, count);
            return 1;
        }
        cb_log(CB_INFO, "%zu/%zu test(s) passed", count, count);

        return 0;
    }

    if (command(command_name, &commands, "record", "[test_names...]", "Record expected output of the tests")) {
        size_t failed_count = 0;
        if (argc <= 0) {
            for (size_t i = 0; i < test_names_count; ++i) {
                CB_Arena_Mark mark = cb_temp_save();
                if (!build_and_run_test(test_names[i], true)) failed_count += 1;
                cb_temp_rewind(mark);
            }
        } else {
            while (argc > 0) {
                CB_Arena_Mark mark = cb_temp_save();
                const char* test_name = cb_shift(argv, argc);
                if (!build_and_run_test(test_name, true)) failed_count += 1;
                cb_temp_rewind(mark);
            }
        }
        if (failed_count > 0) {
            cb_log(CB_ERROR, "%zu test(s) failed to record", failed_count);
            return 1;
        }

        return 0;
    }

    if (command(command_name, &commands, "bench", "[group]", "Build and run bench/bench.c with -O2, argv is passed through")) {
        return build_and_run_bench(argc, argv) ? 0 : 1;
    }

    if (command(command_name, &commands, "examples", "", "Build and run every example from examples/, showing its whole output")) {
        size_t failed_count = 0;
        for (size_t i = 0; i < examples_count; ++i) {
            CB_Arena_Mark mark = cb_temp_save();
            cb_log(CB_INFO, "---- %s ----", examples[i].name);
            if (!build_and_run_example(examples[i].name, examples[i].arg)) {
                cb_log(CB_ERROR, "example %s failed", examples[i].name);
                failed_count += 1;
            }
            cb_temp_rewind(mark);
        }
        if (failed_count > 0) {
            cb_log(CB_ERROR, "%zu example(s) failed", failed_count);
            return 1;
        }

        return 0;
    }

    if (command(command_name, &commands, "clean", "", "Remove the build folder")) {
        return clean_build_folder() ? 0 : 1;
    }

    if (command(command_name, &commands, "docs", "[--check]", "Regenerate docs/ from cb.h, --check only verifies it is up to date")) {
        return run_docs(argc > 0 && strcmp(argv[0], "--check") == 0) ? 0 : 1;
    }

    if (command(command_name, &commands, "list", "", "List available tests")) {
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
