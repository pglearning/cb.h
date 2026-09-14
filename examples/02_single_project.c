// 示例 02：单项目构建
//
// 最小可用的构建脚本：一个真实存在的源文件 -> 一个可执行文件 -> 运行它。
// 整个流程就是 cb.c 里 build_and_run_test 的骨架：拼命令 -> 跑命令 -> free(cmd.items)。
//
// 编译运行（在仓库根目录，示例里的路径都相对当前工作目录）：
//     cc -o /tmp/ex02 examples/02_single_project.c && /tmp/ex02

// 构建相关的编译期开关：必须定义在 #include 之前才生效（cb.h 在这一行就编译完了）
#define CB_ENABLE_ECHO                     // 打印文件系统/命令操作的提示，能看到实际执行的 CMD 行
// #define CB_DONT_DELETE_OLD_CB           // 保留上一次编译出的旧二进制，自重建时不删旧的
// #define CB_TRACE_CMD_RUN_FAIL_LOCATION  // 命令失败时打印它的调用点（文件:行号）
// #define CB_ALLOC_TRACK                  // 每次分配带追踪头，可用 cb_alloc_report() 报告泄漏
#include "../cb.h"

#define OUT_DIR "./build/examples/single/"
#define SRC_PATH "cb.c" // 真实文件：本项目的构建脚本自己

const char* project_names[] = {"app"};
#define project_names_count CB_ARRAY_LEN(project_names)

// 构建 project_names 里的一个目标并运行它
bool build_and_run(const char* name)
{
    bool result = true;
    CB_Cmd cmd = CB_ZERO;
    const char* bin_path = cb_temp_sprintf("%s%s", OUT_DIR, name);

    if (!cb_mkdir_if_not_exists(OUT_DIR)) cb_return_defer(false);

    // 编译：cb_cc 选编译器，cb_cc_flags 加参数，cb_cc_output / cb_cc_inputs 指定输出与输入。
    // 这三个宏按平台展开（MSVC 用 cl.exe，其余用 cc），所以构建脚本里不写死编译器。
    cb_cc(&cmd);
    cb_cc_flags(&cmd);
    cb_cc_output(&cmd, bin_path);
    cb_cc_inputs(&cmd, SRC_PATH);
    if (!cb_cmd_run(&cmd)) cb_return_defer(false);

    // 运行产物。cb.c 不带参数时默认跑测试，这里传 list 让它只列出测试名。
    cb_cmd_append(&cmd, bin_path, "list");
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
