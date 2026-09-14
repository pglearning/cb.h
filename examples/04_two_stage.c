// 示例 04：两阶段构建
//
// 项目变大之后，你往往想"先根据环境生成一份配置，再按配置构建"。做法是把构建脚本
// 分成两个阶段：第一阶段生成 build/config.h，第二阶段按这份配置做真正的构建。
// 好处是引导过程始终只有一条命令：cc -o build examples/04_two_stage.c
//
// 示例在运行时于 build/examples/twostage/ 下生成：
//     build/config.h       第一阶段生成，你可以手工编辑它
//     src/main.c           #include "config.h"
//     build/app            最终产物
//
// 编译运行（在仓库根目录）：
//     cc -o /tmp/ex04 examples/04_two_stage.c && /tmp/ex04
//     /tmp/ex04 clean

#define CB_ENABLE_ECHO
#include "../cb.h"

#define ROOT "./build/examples/twostage/"
#define SRC_DIR ROOT "src/"
#define BUILD_DIR ROOT "build/"
#define SRC_BUILD_DIR ROOT "src_build/"

#define CONFIG_PATH BUILD_DIR "config.h"
#define APP BUILD_DIR "app"

// ---------------------------------------------------------------- 第一阶段：生成配置
static bool ensure_config(void)
{
    if (cb_file_exists(CONFIG_PATH) == 1) {
        cb_log(CB_INFO, "配置已存在：%s（想改就编辑它再重新运行）", CONFIG_PATH);
        return true;
    }

    cb_log(CB_INFO, "首次运行，生成默认配置 %s", CONFIG_PATH);

    // 配置里用宏开关功能
    CB_String_Builder sb = CB_ZERO;
    cb_sb_append_cstr(&sb,
        "// 由 04_two_stage 示例生成。改这里再重新运行构建脚本即可生效。\n"
        "#ifndef CONFIG_H\n"
        "#define CONFIG_H\n"
        "\n"
        "// 打开后程序会打印额外信息\n"
        "#define FEATURE_VERBOSE 1\n"
        "\n"
        "// 问候语（改掉它重新构建就能看到变化）\n"
        "#define GREETING \"hello from a configured build\"\n"
        "\n"
        "#endif // CONFIG_H\n");

    bool ok = cb_write_entire_file(CONFIG_PATH, sb.items, sb.count);
    cb_sb_free(sb);

    if (ok) {
        cb_log(CB_WARN, "==================================================");
        cb_log(CB_WARN, " 编辑 %s 可以改配置，然后重新运行本程序", CONFIG_PATH);
        cb_log(CB_WARN, "==================================================");
    }
    return ok;
}

// ---------------------------------------------------------------- 第一阶段的杂活
// 生成"被构建的程序"的源码。真实项目里这本来就是磁盘上的文件，这里生成只是为了自包含。
static bool generate_sources(void)
{
    bool result = true;
    CB_String_Builder sb = CB_ZERO;

    if (!cb_mkdir_if_not_exists(SRC_DIR) || !cb_mkdir_if_not_exists(SRC_BUILD_DIR)) cb_return_defer(false);

    // 被构建的程序：它 #include 了第一阶段生成的 config.h
    cb_sb_append_cstr(&sb,
        "#include <stdio.h>\n"
        "#include \"config.h\"\n"
        "\n"
        "int main(void)\n"
        "{\n"
        "    printf(\"%s\\n\", GREETING);\n"
        "#ifdef FEATURE_VERBOSE\n"
        "    printf(\"[verbose] 这条信息来自 FEATURE_VERBOSE 开关\\n\");\n"
        "#endif\n"
        "    return 0;\n"
        "}\n");
    if (!cb_write_entire_file(SRC_DIR "main.c", sb.items, sb.count)) cb_return_defer(false);

defer:
    cb_sb_free(sb);
    return result;
}

// ---------------------------------------------------------------- 第二阶段：真正构建
// 真实项目里这一段通常是单独的 src_build/configured.c；这里为了自包含直接在本进程里执行。
static bool second_stage_build(void)
{
    bool result = true;
    CB_Cmd cmd = CB_ZERO;

    const char* src = SRC_DIR "main.c";

    // config.h 变了也要重编，所以和源文件放在同一个依赖数组里
    const char* deps[] = {src, CONFIG_PATH};
    int needs = cb_needs_rebuild(APP, deps, CB_ARRAY_LEN(deps));
    if (needs < 0) cb_return_defer(false);

    if (needs == 0) {
        cb_log(CB_INFO, "%s 已是最新", APP);
        cb_return_defer(true);
    }

    cb_log(CB_INFO, "[第二阶段] 按配置编译 %s", src);
    cb_cmd_append(&cmd, "cc", "-Wall", "-Wextra", "-o", APP, src,
                  cb_temp_sprintf("-I%s", BUILD_DIR));
    if (!cb_cmd_run(&cmd)) cb_return_defer(false);

defer:
    cb_cmd_free(cmd);
    return result;
}

int main(int argc, char** argv)
{
    CB_SELF_REBUILD_PLUS(argc, argv, "cb.h");
    cb_set_log_handler(cb_default_log_handler);

    const char* program = cb_shift(argv, argc);
    const char* command = argc > 0 ? cb_shift(argv, argc) : "build";

    if (strcmp(command, "build") == 0) {
        if (!cb_mkdir_if_not_exists(BUILD_DIR)) return 1;

        // === 第一阶段 ===
        cb_log(CB_INFO, "[第一阶段] 准备配置");
        if (!ensure_config()) return 1;
        if (!generate_sources()) return 1;

        // === 第二阶段 ===
        cb_log(CB_INFO, "[第二阶段] 执行构建");
        if (!second_stage_build()) return 1;

        CB_Cmd run = CB_ZERO;
        cb_cmd_append(&run, APP);
        bool ok = cb_cmd_run(&run);
        cb_cmd_free(run);
        return ok ? 0 : 1;
    }

    if (strcmp(command, "clean") == 0) {
        if (!cb_file_exists(ROOT)) {
            cb_log(CB_INFO, "没有需要清理的东西");
            return 0;
        }
        return cb_delete_directory_recursively(ROOT) ? 0 : 1;
    }

    printf("用法: %s [build|clean]\n", program);
    printf("  第一次 build 会生成 %s，编辑它再 build 可以改配置\n", CONFIG_PATH);
    return 0;
}
