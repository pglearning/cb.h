// 示例 04：两阶段构建
//
// 第一阶段生成配置，第二阶段按配置构建，引导过程始终只有一条命令：
//     cc -o /tmp/ex04 examples/04_two_stage.c && /tmp/ex04
//
// 第一阶段在 build/examples/twostage/ 下写两份东西（真实项目里它们本来就在磁盘上，这里
// 生成只为自包含）：config.h 是配置，只在缺失时生成、之后可以直接编辑；main.c 是被构建
// 的程序，它 #include "config.h"。产物是同目录下的 app，路径都相对当前工作目录。

// 构建相关的编译期开关：必须定义在 #include 之前才生效（cb.h 在这一行就编译完了）
#define CB_ENABLE_ECHO                     // 打印文件系统/命令操作的提示，能看到实际执行的 CMD 行
// #define CB_DONT_DELETE_OLD_CB           // 保留上一次编译出的旧二进制，自重建时不删旧的
// #define CB_TRACE_CMD_RUN_FAIL_LOCATION  // 命令失败时打印它的调用点（文件:行号）
// #define CB_ALLOC_TRACK                  // 每次分配带追踪头，可用 cb_alloc_report() 报告泄漏
#include "../cb.h"

#define ROOT "./build/examples/twostage/"
#define CONFIG_PATH ROOT "config.h"
#define SRC_PATH ROOT "main.c"

const char* project_names[] = {"app"};
#define project_names_count CB_ARRAY_LEN(project_names)

// 第一阶段：准备配置与源码。这里没有要回收的资源，所以直接 return
bool first_stage(void)
{
    const char* config =
        "// 第一阶段生成的配置，可以直接编辑\n"
        "#define GREETING \"hello from a configured build\"\n"
        "#define FEATURE_VERBOSE 1\n";
    const char* source =
        "#include <stdio.h>\n"
        "#include \"config.h\"\n"
        "int main(void)\n"
        "{\n"
        "    printf(\"%s\\n\", GREETING);\n"
        "#ifdef FEATURE_VERBOSE\n"
        "    printf(\"[verbose] 这段输出来自 FEATURE_VERBOSE\\n\");\n"
        "#endif\n"
        "    return 0;\n"
        "}\n";

    if (!cb_mkdir_if_not_exists(ROOT)) return false;

    // 配置只在缺失时生成：编辑它再跑一遍，就是"改配置"
    if (!cb_file_exists(CONFIG_PATH)) {
        if (!cb_write_entire_file(CONFIG_PATH, config, strlen(config))) return false;
    }
    cb_log(CB_INFO, "配置 %s（编辑它再跑一遍即可生效）", CONFIG_PATH);

    return cb_write_entire_file(SRC_PATH, source, strlen(source));
}

// 第二阶段 + 运行：按第一阶段生成的配置编译，然后跑起来
bool build_and_run(const char* name)
{
    bool result = true;
    CB_Cmd cmd = CB_ZERO;
    const char* bin_path = cb_temp_sprintf("%s%s", ROOT, name);

    if (!first_stage()) cb_return_defer(false);

    cb_cc(&cmd);
    cb_cc_flags(&cmd);
    cb_cc_output(&cmd, bin_path);
    cb_cc_inputs(&cmd, SRC_PATH);
    if (!cb_cmd_run(&cmd)) cb_return_defer(false);

    cb_cmd_append(&cmd, bin_path);
    if (!cb_cmd_run(&cmd)) cb_return_defer(false);

defer:
    free(cmd.items);
    return result;
}

int main(int argc, char** argv)
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif // _WIN32

    cb_minimal_log_level = CB_INFO;
    cb_set_log_handler(cb_default_log_handler);

    // 本文件（或 cb.h）比可执行文件新时，自动重新编译自己再重跑
    CB_SELF_REBUILD_PLUS(argc, argv, "cb.h");

    for (size_t i = 0; i < project_names_count; ++i) {
        if (!build_and_run(project_names[i])) return 1;
    }
    return 0;
}
