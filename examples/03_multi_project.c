// 示例 03：多项目构建
//
// 一个构建脚本管多个目标。目标之间的差异全部写在下面的表里，main 只负责按表的顺序
// 逐个调用 build_and_run —— 加一个目标就是往表里加一行，不用改构建逻辑。
// 表里都是真实存在的源文件，不是运行时造出来的演示项目。
//
// 编译运行（在仓库根目录，示例里的路径都相对当前工作目录）：
//     cc -o /tmp/ex03 examples/03_multi_project.c && /tmp/ex03

// 构建相关的编译期开关：必须定义在 #include 之前才生效（cb.h 在这一行就编译完了）
#define CB_ENABLE_ECHO                     // 打印文件系统/命令操作的提示，能看到实际执行的 CMD 行
// #define CB_DONT_DELETE_OLD_CB           // 保留上一次编译出的旧二进制，自重建时不删旧的
// #define CB_TRACE_CMD_RUN_FAIL_LOCATION  // 命令失败时打印它的调用点（文件:行号）
// #define CB_ALLOC_TRACK                  // 每次分配带追踪头，可用 cb_alloc_report() 报告泄漏
#include "../cb.h"

#define OUT_DIR "./build/examples/multi/"

typedef struct {
    const char* name;    // 产物名
    const char* source;  // 真实存在的源文件
    const char* run_arg; // 运行产物时额外传的参数，NULL 表示不传
} Project;

// 一个目标一行；表的顺序就是构建顺序（有依赖时被依赖的排前面）
const Project project_names[] = {
    {"cb", "cb.c", "list"},                 // cb.c 不带参数时跑测试，传 list 只列测试名
    {"hello", "examples/01_hello.c", NULL}, // 独立的小目标，直接跑
};
#define project_names_count CB_ARRAY_LEN(project_names)

// 构建一个目标并运行它
bool build_and_run(const Project* project)
{
    bool result = true;
    CB_Cmd cmd = CB_ZERO;
    const char* bin_path = cb_temp_sprintf("%s%s", OUT_DIR, project->name);

    if (!cb_mkdir_if_not_exists(OUT_DIR)) cb_return_defer(false);

    // 编译这个目标自己的源文件
    cb_cc(&cmd);
    cb_cc_flags(&cmd);
    cb_cc_output(&cmd, bin_path);
    cb_cc_inputs(&cmd, project->source);
    if (!cb_cmd_run(&cmd)) cb_return_defer(false);

    // 运行产物
    cb_cmd_append(&cmd, bin_path);
    if (project->run_arg != NULL) cb_cmd_append(&cmd, project->run_arg);
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
        if (!build_and_run(&project_names[i])) return 1;
    }
    return 0;
}
