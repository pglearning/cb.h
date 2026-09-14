// Cmd Chain 实测：真正跑管道并校验数据流
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
    // 期望 |HELLO-CHAIN\n|（经 tr 转大写）
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
    // 期望 |ALPHA\nBETA\nGAMMA\n|（先大写后排序）
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

    // sh -c 里同时往 stdout 与 stderr 写
    cb_cmd_append(&cmd, "sh", "-c", "echo OUT; echo ERR 1>&2");
    printf("chain_cmd 带 err2out -> %d\n", (int)cb_chain_cmd(&chain, &cmd, .err2out = true));

    cb_cmd_append(&cmd, "cat");
    printf("chain_cmd #2 -> %d\n", (int)cb_chain_cmd(&chain, &cmd));

    printf("chain_end 同时给 stdout 与 stderr 路径 -> %d\n",
           (int)cb_chain_end(&chain, .stdout_path = "out3.txt", .stderr_path = "err3.txt"));

    CB_String_Builder sb = CB_ZERO;
    printf("读回 out3.txt -> %d\n", (int)cb_read_entire_file("out3.txt", &sb));
    printf("out3.txt 内容（%zu 字节）= |%.*s|\n", sb.count, (int)sb.count, sb.items != NULL ? sb.items : "");
    const char* text = sb.items != NULL ? sb.items : "";
    printf("含 OUT = %d, 含 ERR = %d（期望 OUT 与 ERR 一起出现在 stdout 文件里）\n",
           (int)(strstr(text, "OUT") != NULL), (int)(strstr(text, "ERR") != NULL));
    cb_sb_free(sb);

    // err2out 时 stderr 文件被显式建成空文件（语义一致）
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

    // 复用同一个 cmd 作链尾：第二次调用会先把上一条跑起来并接上管道，而 echo 不消费 stdin，最终只有链尾这一行输出。
    printf("cb_chain_cmd 复用同一个 cmd -> %d\n", (int)cb_chain_cmd(&chain, &cmd));
    printf("chain_end -> %d\n", (int)cb_chain_end(&chain, .stdout_path = "out4.txt"));

    CB_String_Builder sb = CB_ZERO;
    printf("读回 out4.txt -> %d\n", (int)cb_read_entire_file("out4.txt", &sb));
    // 期望 |keep-me\n|
    printf("out4.txt 内容（%zu 字节）= |%.*s|\n", sb.count, (int)sb.count, sb.items != NULL ? sb.items : "");
    cb_sb_free(sb);

    cb_da_free(chain.cmd);
    cb_cmd_free(cmd);
}

int main(void)
{
#ifdef _WIN32
    // 这些用例依赖 sh / tr / cat / sort，Windows 原生环境没有，故跳过（mingw 下可编译通过）。
    printf("chain 的行为用例需要 POSIX 工具（sh/tr/cat/sort），Windows 下跳过\n");
    return 0;
#endif
    test_chain_pipeline();
    test_chain_stdin_file();
    test_chain_err2out();
    test_chain_dont_reset();
    return 0;
}
