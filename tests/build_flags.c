// 覆盖 cb.h 的默认“编译器抽象”。cb.h 对这四个宏各自有 #ifndef 守卫，所以在任何 #include
// 之前定义它们，就等于把默认的 编译器名 / 参数 / 输出 / 输入 拼装换成测试用的固定拼装：
// 覆盖后的命令行完全确定，可以直接进 golden（默认拼装里的编译器名是平台相关的）。
#define cb_cc(cmd) cb_cmd_append(cmd, "test-cc")
#define cb_cc_flags(cmd) cb_cmd_append(cmd, "-DFAKE=1", "-I.")
#define cb_cc_output(cmd, output_path) cb_cmd_append(cmd, "-o", (output_path))
#define cb_cc_inputs(cmd, ...) cb_cmd_append(cmd, __VA_ARGS__)

#include "test_diagnostics.h"
#include "cb.h"

static void test_cc_override(void)
{
    printf("\n== cb_cc / cb_cc_flags / cb_cc_output / cb_cc_inputs（覆盖默认实现）==\n");

    CB_Cmd cmd = CB_ZERO;
    printf("CB_Cmd cmd = CB_ZERO; cmd.count = %zu\n", cmd.count);

    cb_cc(&cmd);
    printf("cb_cc(&cmd) 之后 cmd.count = %zu\n", cmd.count);

    cb_cc_flags(&cmd);
    printf("cb_cc_flags(&cmd) 之后 cmd.count = %zu\n", cmd.count);

    cb_cc_output(&cmd, "test-out.bin");
    printf("cb_cc_output(&cmd, \"test-out.bin\") 之后 cmd.count = %zu\n", cmd.count);

    cb_cc_inputs(&cmd, "a.c", "b.c");
    printf("cb_cc_inputs(&cmd, \"a.c\", \"b.c\") 之后 cmd.count = %zu\n", cmd.count);

    for (size_t i = 0; i < cmd.count; ++i) printf("  cmd.items[%zu] = %s\n", i, cmd.items[i]);

    CB_String_Builder sb = CB_ZERO;
    cb_cmd_to_sb(cmd, &sb);
    cb_sb_append_null(&sb);
    printf("cb_cmd_to_sb(cmd) = |%s|\n", sb.items);
    cb_sb_free(sb);

    cb_cmd_free(cmd);
}

static void test_rebuild_urself_macro(void)
{
    printf("\n== CB_REBUILD_URSELF：默认的自重建命令行 ==\n");

    // 默认宏展开成一串逗号分隔的 argv 项：编译器名 + 参数 + binary_path + source_path。
    // 编译器名和参数随平台/编译器变化（cc / gcc / clang / cl.exe / tcc），
    // 所以只打印结构：项数、最后两项是不是传进去的 binary/source、以及首项非空。
    const char* args[] = {CB_REBUILD_URSELF("rebuilt.bin", "self.c")};
    size_t n = CB_ARRAY_LEN(args);

    printf("CB_ARRAY_LEN(CB_REBUILD_URSELF(\"rebuilt.bin\", \"self.c\")) = %zu\n", n);
    printf("至少含 binary_path 与 source_path 两项 = %d\n", (int)(n >= 2));
    printf("末项是 source_path(\"self.c\") = %d\n", (int)(n >= 1 && strcmp(args[n - 1], "self.c") == 0));
    printf("binary_path(\"rebuilt.bin\") 出现在倒数第二项 = %d\n",
           (int)(n >= 2 && strstr(args[n - 2], "rebuilt.bin") != NULL));
    printf("首项（编译器名）非空 = %d\n", (int)(n >= 1 && args[0] != NULL && args[0][0] != '\0'));
}

static void test_needs_rebuild(void)
{
    printf("\n== cb_needs_rebuild：输出与输入的时间戳 ==\n");

    printf("cb_write_entire_file(\"bf_in.txt\", ...) -> %d\n", (int)cb_write_entire_file("bf_in.txt", "in", 2));
    printf("cb_write_entire_file(\"bf_out.bin\", ...) -> %d\n", (int)cb_write_entire_file("bf_out.bin", "out", 3));

    const char* normal[] = {"bf_in.txt"};
    const char* missing_input[] = {"no-such-input.txt"};
    const char* missing_output[] = {"bf_in.txt"};

    printf("输出不存在 -> cb_needs_rebuild(\"no-such-out.bin\", {\"bf_in.txt\"}, 1) = %d（1 = 需要重建）\n",
           cb_needs_rebuild("no-such-out.bin", missing_output, CB_ARRAY_LEN(missing_output)));

    printf("先写输入再写输出，输出不旧于输入 -> cb_needs_rebuild(\"bf_out.bin\", {\"bf_in.txt\"}, 1) = %d（0 = 不需要重建）\n",
           cb_needs_rebuild("bf_out.bin", normal, CB_ARRAY_LEN(normal)));

    {
        // 输入不存在是错误：返回 -1，同时往 stderr 记一条日志（golden 看不见，这里静音）。
        CB_Log_Level saved = cb_minimal_log_level;
        cb_minimal_log_level = CB_NO_LOGS;
        int bad = cb_needs_rebuild("bf_out.bin", missing_input, CB_ARRAY_LEN(missing_input));
        cb_minimal_log_level = saved;
        printf("输入不存在 -> cb_needs_rebuild(\"bf_out.bin\", {\"no-such-input.txt\"}, 1) = %d（-1 = 出错）\n",
               bad);
    }
}

static void test_self_rebuild(int argc, char** argv)
{
    printf("\n== CB_SELF_REBUILD / cb__self_rebuild ==\n");

    // CB_SELF_REBUILD(argc, argv, "cb.h") 展开成：
    //     cb__self_rebuild(argc, argv, __FILE__, "cb.h", NULL)
    // 这里的 __FILE__ 是编译命令里的相对路径（运行器给的是 "./tests/build_flags.c"），
    // 而测试运行在 build/tests/build_flags.cwd 沙箱目录里 —— 那个相对路径在沙箱中不存在，
    // 所以不能直接调用宏本身（cb_needs_rebuild 会因输入缺失返回 -1 并 exit(1)）。
    // 下面把 argv[0]（这个测试二进制自己）当“源文件”传给 cb__self_rebuild：
    // 源与目标同一个文件、mtime 相同，cb_needs_rebuild 返回 0，走“无需重建”分支。
    // 运行器在运行之前刚刚编译过这个二进制，所以真实的
    //     CB_SELF_REBUILD(argc, argv, "cb.h")
    // 在同一场景下也会走这条分支。
    printf("argc > 0（cb__self_rebuild 内部用 cb_shift(argv, argc) 取 argv[0]）-> %d\n", (int)(argc > 0));
    if (argc > 0) {
        cb__self_rebuild(argc, argv, argv[0], NULL);
        printf("cb__self_rebuild(argc, argv, argv[0], NULL) 已返回：源不比二进制新 -> 没有重建\n");
    }
}

int main(int argc, char** argv)
{
    test_cc_override();
    test_rebuild_urself_macro();
    test_needs_rebuild();
    test_self_rebuild(argc, argv);
    return 0;
}
