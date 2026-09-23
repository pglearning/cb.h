#include "test_diagnostics.h"
#include "cb.h"

static void test_chain_pipeline(void)
{
    printf("\n== 三层管道：echo | tr | cat ==\n");

    CB_Chain chain = CB_ZERO;
    CB_Cmd cmd = CB_ZERO;

    printf("chain_begin -> %d\n", (int)cb_chain_begin(&chain));

    cb_cmd_append(&cmd, "echo", "hello-chain");
    printf("chain_cmd #1 (echo) -> %d\n", (int)cb_chain_cmd(&chain, &cmd));
    printf("chain_cmd 之后 cmd.count = %zu（默认会重置 cmd）\n", cmd.count);

    cb_cmd_append(&cmd, "tr", "a-z", "A-Z");
    printf("chain_cmd #2 (tr) -> %d\n", (int)cb_chain_cmd(&chain, &cmd));

    cb_cmd_append(&cmd, "cat");
    printf("chain_cmd #3 (cat) -> %d\n", (int)cb_chain_cmd(&chain, &cmd));

    printf("chain_end 重定向到文件 -> %d\n", (int)cb_chain_end(&chain, .stdout_path = "out.txt"));

    CB_String_Builder sb = CB_ZERO;
    printf("读回 out.txt -> %d\n", (int)cb_read_entire_file("out.txt", &sb));

    printf("out.txt 内容（%zu 字节）= |%.*s|\n", sb.count, (int)sb.count, sb.items != NULL ? sb.items : "");
    cb_sb_free(sb);

    cb_da_free(chain.cmd);
    cb_cmd_free(cmd);
}

static void test_chain_stdin_file(void)
{
    printf("\n== 从文件进、经两层管道、到文件出 ==\n");

    const char* content = "alpha\nbeta\ngamma\n";
    printf("写 in.txt -> %d\n", (int)cb_write_entire_file("in.txt", content, strlen(content)));

    CB_Chain chain = CB_ZERO;
    CB_Cmd cmd = CB_ZERO;

    printf("chain_begin 指定 stdin_path -> %d\n", (int)cb_chain_begin(&chain, .stdin_path = "in.txt"));

    cb_cmd_append(&cmd, "tr", "a-z", "A-Z");
    printf("chain_cmd #1 -> %d\n", (int)cb_chain_cmd(&chain, &cmd));

    cb_cmd_append(&cmd, "sort");
    printf("chain_cmd #2 -> %d\n", (int)cb_chain_cmd(&chain, &cmd));

    printf("chain_end -> %d\n", (int)cb_chain_end(&chain, .stdout_path = "out2.txt"));

    CB_String_Builder sb = CB_ZERO;
    printf("读回 out2.txt -> %d\n", (int)cb_read_entire_file("out2.txt", &sb));

    printf("out2.txt 内容（%zu 字节）= |%.*s|\n", sb.count, (int)sb.count, sb.items != NULL ? sb.items : "");
    cb_sb_free(sb);

    cb_da_free(chain.cmd);
    cb_cmd_free(cmd);
}

static void test_chain_err2out(void)
{
    printf("\n== err2out：stderr 并入 stdout ==\n");

    CB_Chain chain = CB_ZERO;
    CB_Cmd cmd = CB_ZERO;

    printf("chain_begin -> %d\n", (int)cb_chain_begin(&chain));

    cb_cmd_append(&cmd, "sh", "-c", "echo OUT; echo ERR 1>&2");
    printf("chain_cmd 带 err2out -> %d\n", (int)cb_chain_cmd(&chain, &cmd, .err2out = true));

    cb_cmd_append(&cmd, "cat");
    printf("chain_cmd #2 -> %d\n", (int)cb_chain_cmd(&chain, &cmd));

    printf("chain_end 同时给 stdout 与 stderr 路径 -> %d\n",
           (int)cb_chain_end(&chain, .stdout_path = "out3.txt", .stderr_path = "err3.txt"));

    CB_String_Builder sb = CB_ZERO;
    printf("读回 out3.txt -> %d\n", (int)cb_read_entire_file("out3.txt", &sb));
    printf("out3.txt 内容（%zu 字节）= |%.*s|\n", sb.count, (int)sb.count, sb.items != NULL ? sb.items : "");
    cb_sb_append_null(&sb);
    const char* text = sb.items != NULL ? sb.items : "";
    printf("含 OUT = %d, 含 ERR = %d（期望 OUT 与 ERR 一起出现在 stdout 文件里）\n",
           (int)(strstr(text, "OUT") != NULL), (int)(strstr(text, "ERR") != NULL));
    cb_sb_free(sb);

    printf("cb_file_exists(err3.txt) = %d\n", (int)cb_file_exists("err3.txt"));
    printf("cb_file_size(err3.txt) = %zu\n", cb_file_size("err3.txt"));

    cb_da_free(chain.cmd);
    cb_cmd_free(cmd);
}

static void test_chain_dont_reset(void)
{
    printf("\n== dont_reset：保留 cmd 以便复用 ==\n");

    CB_Chain chain = CB_ZERO;
    CB_Cmd cmd = CB_ZERO;

    printf("chain_begin -> %d\n", (int)cb_chain_begin(&chain));

    cb_cmd_append(&cmd, "echo", "keep-me");
    printf("chain_cmd 带 dont_reset -> %d\n", (int)cb_chain_cmd(&chain, &cmd, .dont_reset = true));
    printf("dont_reset 时 cmd.count = %zu\n", cmd.count);

    printf("cb_chain_cmd 复用同一个 cmd -> %d\n", (int)cb_chain_cmd(&chain, &cmd));
    printf("chain_end -> %d\n", (int)cb_chain_end(&chain, .stdout_path = "out4.txt"));

    CB_String_Builder sb = CB_ZERO;
    printf("读回 out4.txt -> %d\n", (int)cb_read_entire_file("out4.txt", &sb));

    printf("out4.txt 内容（%zu 字节）= |%.*s|\n", sb.count, (int)sb.count, sb.items != NULL ? sb.items : "");
    cb_sb_free(sb);

    cb_da_free(chain.cmd);
    cb_cmd_free(cmd);
}

// 便捷宏 cb_chain_begin/cb_chain_cmd/cb_chain_end 只是把 CB_CLIT(CB_Chain_*_Opt){...} 传下去，
// 这一节直接构造这三个结构体并调用 _opt 版本，覆盖宏背后的真实入口。
static void test_chain_opt_functions(void)
{
    printf("\n== 直接调用 cb_chain_*_opt（不经便捷宏）==\n");

    const char* content = "delta\nbravo\ncharlie\n";
    printf("写 opt_in.txt -> %d\n", (int)cb_write_entire_file("opt_in.txt", content, strlen(content)));

    CB_Chain chain = CB_ZERO;
    CB_Cmd cmd = CB_ZERO;

    CB_Chain_Begin_Opt begin_opt = CB_ZERO;
    begin_opt.stdin_path = "opt_in.txt";
    printf("cb_chain_begin_opt(.stdin_path = \"opt_in.txt\") -> %d\n",
           (int)cb_chain_begin_opt(&chain, begin_opt));

    CB_Chain_Cmd_Opt cmd_opt = CB_ZERO;
    cmd_opt.dont_reset = true; // 保留 cmd.count，方便复用同一条命令
    cb_cmd_append(&cmd, "tr", "a-z", "A-Z");
    bool stage1 = cb_chain_cmd_opt(&chain, &cmd, cmd_opt);
    printf("cb_chain_cmd_opt #1（.dont_reset = true）-> %d\n", (int)stage1);
    printf("dont_reset 时 cmd.count 保留 = %zu\n", cmd.count);

    printf("cb_chain_cmd_opt #2（复用同一个 cmd）-> %d\n", (int)cb_chain_cmd_opt(&chain, &cmd, cmd_opt));

    CB_Cmd last = CB_ZERO;
    CB_Chain_Cmd_Opt last_opt = CB_ZERO;
    last_opt.err2out = true;
    cb_cmd_append(&last, "sh", "-c", "cat; echo OPT-ERR 1>&2");
    printf("cb_chain_cmd_opt #3（.err2out = true）-> %d\n", (int)cb_chain_cmd_opt(&chain, &last, last_opt));

    CB_Chain_End_Opt end_opt = CB_ZERO;
    end_opt.stdout_path = "opt_out.txt";
    end_opt.stderr_path = "opt_err.txt";
    printf("cb_chain_end_opt(.stdout_path/.stderr_path) -> %d\n", (int)cb_chain_end_opt(&chain, end_opt));

    CB_String_Builder sb = CB_ZERO;
    printf("读回 opt_out.txt -> %d\n", (int)cb_read_entire_file("opt_out.txt", &sb));
    printf("opt_out.txt 内容（%zu 字节）= |%.*s|\n", sb.count, (int)sb.count, sb.items != NULL ? sb.items : "");
    cb_sb_free(sb);

    printf("cb_file_exists(opt_err.txt) = %d，大小 = %zu（err2out 已把 stderr 并进 stdout，stderr 文件按语义留空）\n",
           (int)cb_file_exists("opt_err.txt"), cb_file_size("opt_err.txt"));

    cb_da_free(chain.cmd);
    cb_cmd_free(cmd);
    cb_cmd_free(last);
}

int main(void)
{
    // CB_Fd_List：chain 内部用来记住"命令跑完要关掉哪些 fd"的类型。
    CB_Fd_List fds = CB_ZERO;
    printf("CB_Fd_List（chain 的 fd 暂存表）: count = %zu\n", fds.count);

#ifdef _WIN32

    printf("chain 的行为用例需要 POSIX 工具（sh/tr/cat/sort），Windows 下跳过\n");
    return 0;
#endif
    test_chain_pipeline();
    test_chain_stdin_file();
    test_chain_err2out();
    test_chain_dont_reset();
    test_chain_opt_functions();
    return 0;
}
