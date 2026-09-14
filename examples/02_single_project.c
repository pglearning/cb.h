// 示例 02：单项目构建
//
// 最常见的用法：项目根目录放一个构建脚本，负责把本项目编译出来。
// 为了让示例自包含（在哪儿跑都行），这里不读真实目录，而是在运行时生成一个
// 小项目再构建它；真实项目里把 generate_demo_project() 删掉即可。
//
// 编译运行（在仓库根目录）：
//     cc -o /tmp/ex02 examples/02_single_project.c && /tmp/ex02
//     /tmp/ex02 clean       # 清理

#define CB_ENABLE_ECHO // 打开"创建目录/执行命令"提示
#include "../cb.h"

#define DEMO_ROOT "./build/examples/single/"
#define SRC_DIR DEMO_ROOT "src/"
#define OUT_DIR DEMO_ROOT "build/"
#define APP OUT_DIR "app"

// ---------------------------------------------------------------- 演示用的小项目
static bool generate_demo_project(void)
{
    bool result = true;
    CB_String_Builder sb = CB_ZERO;

    if (!cb_mkdir_if_not_exists(SRC_DIR)) cb_return_defer(false);

    cb_sb_append_cstr(&sb,
        "#ifndef GREET_H\n#define GREET_H\n"
        "const char* greet(void);\n"
        "int add(int a, int b);\n"
        "#endif\n");
    if (!cb_write_entire_file(SRC_DIR "greet.h", sb.items, sb.count)) cb_return_defer(false);

    sb.count = 0;
    cb_sb_append_cstr(&sb,
        "#include \"greet.h\"\n"
        "const char* greet(void) { return \"hello from a generated project\"; }\n"
        "int add(int a, int b) { return a + b; }\n");
    if (!cb_write_entire_file(SRC_DIR "greet.c", sb.items, sb.count)) cb_return_defer(false);

    sb.count = 0;
    cb_sb_append_cstr(&sb,
        "#include <stdio.h>\n"
        "#include \"greet.h\"\n"
        "int main(void) { printf(\"%s\\n\", greet()); printf(\"2 + 3 = %d\\n\", add(2, 3)); return 0; }\n");
    if (!cb_write_entire_file(SRC_DIR "main.c", sb.items, sb.count)) cb_return_defer(false);

    cb_log(CB_INFO, "已生成演示项目到 %s", DEMO_ROOT);

defer:
    cb_sb_free(sb);
    return result;
}

// ---------------------------------------------------------------- 构建
static bool build_project(void)
{
    bool result = true;
    CB_Cmd cmd = CB_ZERO;
    CB_Procs procs = CB_ZERO;

    static const char* sources[] = {"main.c", "greet.c"};
    static const char* objects[] = {OUT_DIR "main.o", OUT_DIR "greet.o"};

    if (!cb_mkdir_if_not_exists(OUT_DIR)) cb_return_defer(false);

    // 1) 逐个编译。.async 让它们并行跑，.max_procs 限制并发数。
    for (size_t i = 0; i < CB_ARRAY_LEN(sources); ++i) {
        const char* src = cb_temp_sprintf("%s%s", SRC_DIR, sources[i]);
        const char* obj = objects[i];

        // 增量：目标比源新就跳过。cb_needs_rebuild 收一个依赖数组，单个源就是长度 1。
        const char* deps[] = {src};
        int needs = cb_needs_rebuild(obj, deps, CB_ARRAY_LEN(deps));
        if (needs < 0) cb_return_defer(false);
        if (needs == 0) {
            cb_log(CB_INFO, "跳过 %s（已是最新）", obj);
            continue;
        }

        cmd.count = 0; // cb_cmd_run 默认会清空，这里显式写出来
        cb_cmd_append(&cmd, "cc", "-Wall", "-Wextra", "-c", "-o", obj, src,
                      cb_temp_sprintf("-I%s", SRC_DIR));
        if (!cb_cmd_run(&cmd, .async = &procs, .max_procs = (size_t)cb_nprocs())) {
            cb_return_defer(false);
        }
    }

    // 2) 等所有编译结束
    if (!cb_procs_wait_and_reset(&procs)) cb_return_defer(false);

    // 3) 链接。有任何一个 .o 比可执行文件新就得重新链接。
    int needs_link = cb_needs_rebuild(APP, objects, CB_ARRAY_LEN(objects));
    if (needs_link < 0) cb_return_defer(false);
    if (needs_link == 0) {
        cb_log(CB_INFO, "%s 已是最新", APP);
    } else {
        cmd.count = 0;
        cb_cmd_append(&cmd, "cc", "-o", APP);
        cb_cmd_append(&cmd, objects[0], objects[1]);
        if (!cb_cmd_run(&cmd)) cb_return_defer(false);
    }

    cb_log(CB_INFO, "构建完成：%s", APP);

defer:
    cb_cmd_free(cmd);
    cb_da_free(procs);
    return result;
}

static bool clean(void)
{
    if (!cb_file_exists(DEMO_ROOT)) {
        cb_log(CB_INFO, "没有需要清理的东西");
        return true;
    }
    return cb_delete_directory_recursively(DEMO_ROOT);
}

int main(int argc, char** argv)
{
    // 自重建：本文件（或 cb.h）比可执行文件新时，自动重新编译自己再重跑。
    // 路径相对【当前工作目录】解析，所以本示例约定在仓库根目录运行。
    CB_SELF_REBUILD_PLUS(argc, argv, "cb.h");

    cb_set_log_handler(cb_default_log_handler);

    const char* program = cb_shift(argv, argc);
    const char* command = argc > 0 ? cb_shift(argv, argc) : "build";

    if (strcmp(command, "build") == 0) {
        if (!generate_demo_project()) return 1; // 真实项目里没有这一步
        if (!build_project()) return 1;
        cb_log(CB_INFO, "运行一下：%s", APP);
        CB_Cmd run = CB_ZERO;
        cb_cmd_append(&run, APP);
        bool ok = cb_cmd_run(&run);
        cb_cmd_free(run);
        return ok ? 0 : 1;
    }
    if (strcmp(command, "clean") == 0) return clean() ? 0 : 1;

    printf("用法: %s [build|clean]\n", program);
    return 0;
}
