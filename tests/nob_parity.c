#include "test_diagnostics.h"
#include "cb.h"

static void test_svlit(void)
{
    printf("\n== CB_SVLIT：编译期 String_View 字面量 ==\n");

    CB_String_View sv = CB_SVLIT("hello");
    printf("CB_SVLIT(\"hello\")：count = %zu，内容 = |" CB_SV_FMT "|\n", sv.count, CB_SV_ARG(sv));

    CB_String_View empty = CB_SVLIT("");
    printf("CB_SVLIT(\"\")：count = %zu\n", empty.count);

    static const CB_String_View static_sv = CB_SVLIT_STATIC("static-literal");
    printf("CB_SVLIT_STATIC(\"static-literal\")：count = %zu，内容 = |" CB_SV_FMT "|\n", static_sv.count,
           CB_SV_ARG(static_sv));

    printf("与 cb_sv_from_cstr(\"hello\") 等价 -> %d\n", (int)cb_sv_eq(sv, cb_sv_from_cstr("hello")));

    CB_String_View zh = CB_SVLIT("中文");
    printf("CB_SVLIT(\"中文\")：count = %zu（多字节字面量按字节计长）\n", zh.count);

    printf("两个字面量可直接比较 -> %d\n", (int)cb_sv_eq(CB_SVLIT("abc"), CB_SVLIT("abc")));
}

static void test_cmd_extend_free(void)
{
    printf("\n== cb_cmd_extend / cb_cmd_free ==\n");

    CB_Cmd a = CB_ZERO;
    CB_Cmd b = CB_ZERO;
    cb_cmd_append(&a, "cc", "-c");
    cb_cmd_append(&b, "main.c", "-o", "main.o");

    cb_cmd_extend(&a, &b);
    printf("extend 后 a.count = %zu\n", a.count);

    for (size_t i = 0; i < a.count; ++i) printf("a.items[%zu] = %s\n", i, a.items[i]);

    cb_cmd_free(a);
    printf("cmd_free 清空句柄 items==NULL/count==0/capacity==0 = %d/%d/%d\n", (int)(a.items == NULL),
           (int)(a.count == 0), (int)(a.capacity == 0));
    cb_cmd_free(b);

    CB_Cmd x = CB_ZERO, y = CB_ZERO;
    cb_cmd_extend(&x, &y);
    printf("两个空 cmd extend 后 x.count = %zu\n", x.count);
    cb_cmd_free(x);
}

static void test_nanos(void)
{
    printf("\n== cb_nanos_since_unspecified_epoch ==\n");

    uint64_t t0 = cb_nanos_since_unspecified_epoch();

    volatile uint64_t spin = 0;
    for (uint64_t i = 0; i < 200000; ++i) spin += i;
    CB_UNUSED(spin);
    uint64_t t1 = cb_nanos_since_unspecified_epoch();

    printf("时间戳单调递增 = %d\n", (int)(t1 > t0));
    printf("起点非零 = %d\n", (int)(t0 > 0));

    double nanos_to_us = (double)(t1 - t0) / 1000.0;
    printf("纳秒差换算成微秒是合理量级（0 < x < 60s）= %d\n",
           (int)(nanos_to_us > 0 && nanos_to_us < 60.0 * 1000000.0));
}

static void test_needs_rebuild(void)
{
    printf("\n== cb_needs_rebuild ==\n");

    printf("写出 out.bin -> %d\n", (int)cb_write_entire_file("out.bin", "x", 1));
    printf("写出 in.txt -> %d\n", (int)cb_write_entire_file("in.txt", "y", 1));

    const char* missing_out[] = {"in.txt"};
    const char* missing_in[] = {"no-such-input.txt"};
    const char* normal[] = {"in.txt"};

    printf("输出不存在时返回 1 -> %d\n",
           cb_needs_rebuild("no-such-output.bin", missing_out, CB_ARRAY_LEN(missing_out)));

    {
        CB_Log_Level saved = cb_minimal_log_level;
        cb_minimal_log_level = CB_NO_LOGS;
        int bad = cb_needs_rebuild("out.bin", missing_in, CB_ARRAY_LEN(missing_in));
        cb_minimal_log_level = saved;
        printf("输入不存在时返回 -1 -> %d\n", bad);
    }

    int r = cb_needs_rebuild("out.bin", normal, CB_ARRAY_LEN(normal));
    printf("返回值为 0 或 1 -> %d\n", (int)(r == 0 || r == 1));
}

static void test_procs_wait_and_reset(void)
{
    printf("\n== cb_procs_wait_and_reset ==\n");

    CB_Procs procs = CB_ZERO;
    CB_Cmd cmd = CB_ZERO;

#ifdef _WIN32
    const char* noop_cmd[] = {"cmd", "/c", "exit", "0"};
#else
    const char* noop_cmd[] = {"true"};
#endif

    for (size_t i = 0; i < CB_ARRAY_LEN(noop_cmd); ++i) cb_cmd_append(&cmd, noop_cmd[i]);
    printf("异步起第一条 -> %d\n", (int)cb_cmd_run(&cmd, .async = &procs));
    for (size_t i = 0; i < CB_ARRAY_LEN(noop_cmd); ++i) cb_cmd_append(&cmd, noop_cmd[i]);
    printf("异步起第二条 -> %d\n", (int)cb_cmd_run(&cmd, .async = &procs));
    printf("procs 里有 %zu 个进程\n", procs.count);

    printf("wait_and_reset -> %d\n", (int)cb_procs_wait_and_reset(&procs));
    printf("重置后 procs.count = %zu\n", procs.count);
    printf("数组内存保留可复用（items != NULL）= %d\n", (int)(procs.items != NULL));

    cb_cmd_free(cmd);
    cb_da_free(procs);
}

// cb_cmd_to_sb 只渲染 argv（不碰文件系统、不启动进程），所以输出是确定的：
// 含空格的参数会被单引号包起来，遇到 NULL 元素则提前停止渲染。
static void test_cmd_to_sb(void)
{
    printf("\n== cb_cmd_to_sb：只渲染 argv ==\n");

    CB_Cmd empty = CB_ZERO;
    CB_String_Builder sb = CB_ZERO;
    cb_cmd_to_sb(empty, &sb);
    cb_sb_append_null(&sb);
    printf("空命令 -> |%s|\n", sb.items != NULL ? sb.items : "");
    cb_sb_free(sb);

    // 宏 cb_cmd_append 在 C 下展开成 cb__cmd_append，这里直接用它 + 一个字面量数组
    CB_Cmd cmd = CB_ZERO;
    const char* args[] = {"cc", "-c", "my file.c", "-o", "out.o"};
    cb__cmd_append(&cmd, CB_ARRAY_LEN(args), args);
    printf("cb__cmd_append 传入 %zu 个字面量参数后 cmd.count = %zu\n", CB_ARRAY_LEN(args), cmd.count);

    CB_String_Builder rendered = CB_ZERO;
    cb_cmd_to_sb(cmd, &rendered);
    cb_sb_append_null(&rendered);
    printf("cb_cmd_to_sb -> |%s|\n", rendered.items != NULL ? rendered.items : "");
    cb_sb_free(rendered);

    cb_cmd_free(cmd);
}

// 直接调用 cb_cmd_run_opt / cb__cmd_run_opt_with_location（cb_cmd_run 宏展开后的两层），
// 并逐个覆盖 CB_Cmd_Opt 的字段：.dont_reset、.stdout_path、.stderr_path、.max_procs。
static void test_cmd_run_opt_direct(void)
{
    printf("\n== cb_cmd_run_opt / cb__cmd_run_opt_with_location 直接调用 ==\n");

#ifdef _WIN32
    const char* noop[] = {"cmd", "/c", "exit", "0"};
#else
    const char* noop[] = {"true"};
#endif

    CB_Cmd cmd = CB_ZERO;
    for (size_t i = 0; i < CB_ARRAY_LEN(noop); ++i) cb_cmd_append(&cmd, noop[i]);

    CB_Cmd_Opt keep_opt = CB_ZERO;
    keep_opt.dont_reset = true;
    printf("cb_cmd_run_opt(.dont_reset = true) -> %d\n", (int)cb_cmd_run_opt(&cmd, keep_opt));
    printf("dont_reset 时 cmd.count 保留 = %zu\n", cmd.count);

    CB_Cmd_Opt reset_opt = CB_ZERO;
    printf("同一个 cmd 再跑一次（默认重置）-> %d\n", (int)cb_cmd_run_opt(&cmd, reset_opt));
    printf("默认重置后 cmd.count = %zu\n", cmd.count);
    cb_cmd_free(cmd);

    CB_Cmd redirect = CB_ZERO;
    for (size_t i = 0; i < CB_ARRAY_LEN(noop); ++i) cb_cmd_append(&redirect, noop[i]);

    CB_Cmd_Opt redirect_opt = CB_ZERO;
    redirect_opt.stdout_path = "run_opt_out.txt";
    redirect_opt.stderr_path = "run_opt_err.txt";
    printf("cb__cmd_run_opt_with_location(.stdout_path/.stderr_path, __FILE__, __LINE__) -> %d\n",
           (int)cb__cmd_run_opt_with_location(&redirect, __FILE__, __LINE__, redirect_opt));
    printf("stdout 文件被创建（noop 命令不写东西）= %d，大小 = %zu\n",
           (int)(cb_file_exists("run_opt_out.txt") != 0), cb_file_size("run_opt_out.txt"));
    printf("stderr 文件被创建，大小 = %zu\n", cb_file_size("run_opt_err.txt"));
    cb_cmd_free(redirect);

    printf("\n== .stderr_path：stderr 单独落文件，stdout 保持空 ==\n");
    CB_Cmd erring = CB_ZERO;
#ifdef _WIN32
    cb_cmd_append(&erring, "cmd", "/c", "echo ERR-MARK 1>&2");
#else
    cb_cmd_append(&erring, "sh", "-c", "echo ERR-MARK 1>&2");
#endif

    CB_Cmd_Opt err_opt = CB_ZERO;
    err_opt.stdout_path = "err_stdout.txt";
    err_opt.stderr_path = "err_stderr.txt";
    printf("cb_cmd_run_opt(.stdout_path + .stderr_path) -> %d\n", (int)cb_cmd_run_opt(&erring, err_opt));

    CB_String_Builder out_sb = CB_ZERO;
    bool out_read = cb_read_entire_file("err_stdout.txt", &out_sb);
    printf("读回 stdout 文件 -> %d，字节数 = %zu（应为 0）\n", (int)out_read, out_sb.count);
    cb_sb_free(out_sb);

    CB_String_Builder err_sb = CB_ZERO;
    bool err_read = cb_read_entire_file("err_stderr.txt", &err_sb);
    cb_sb_append_null(&err_sb);
    printf("读回 stderr 文件 -> %d，含 ERR-MARK = %d\n", (int)err_read,
           (int)(err_sb.items != NULL && strstr(err_sb.items, "ERR-MARK") != NULL));
    cb_sb_free(err_sb);
    cb_cmd_free(erring);

    printf("\n== .stdin_path：stdin 从文件来，stdout 到文件去 ==\n");
    const char* input = "delta\nbravo\n";
    printf("写 opt_in.txt -> %d\n", (int)cb_write_entire_file("opt_in.txt", input, strlen(input)));

    CB_Cmd passthrough = CB_ZERO;
#ifdef _WIN32
    cb_cmd_append(&passthrough, "cmd", "/c", "more");
#else
    // cat 原样搬运，所以两个平台都只断言“内容还在 + 行数没变”，不断言换行风格
    cb_cmd_append(&passthrough, "cat");
#endif

    CB_Cmd_Opt pipe_opt = CB_ZERO;
    pipe_opt.stdin_path = "opt_in.txt";
    pipe_opt.stdout_path = "opt_out.txt";
    printf("cb_cmd_run_opt(.stdin_path + .stdout_path) -> %d\n", (int)cb_cmd_run_opt(&passthrough, pipe_opt));

    CB_String_Builder pipe_sb = CB_ZERO;
    bool pipe_read = cb_read_entire_file("opt_out.txt", &pipe_sb);
    cb_sb_append_null(&pipe_sb);
    size_t newlines = 0;
    for (size_t i = 0; i < pipe_sb.count; ++i) {
        if (pipe_sb.items[i] == '\n') newlines += 1;
    }
    printf("读回 opt_out.txt -> %d，含 delta = %d，含 bravo = %d，行数 = %zu\n", (int)pipe_read,
           (int)(pipe_sb.items != NULL && strstr(pipe_sb.items, "delta") != NULL),
           (int)(pipe_sb.items != NULL && strstr(pipe_sb.items, "bravo") != NULL), newlines);
    cb_sb_free(pipe_sb);
    cb_cmd_free(passthrough);

    printf("\n== .max_procs：异步节流 ==\n");
    CB_Procs procs = CB_ZERO;
    CB_Cmd_Opt async_opt = CB_ZERO;
    async_opt.async = &procs;
    async_opt.max_procs = 1;

    for (int i = 0; i < 3; ++i) {
        CB_Cmd async_cmd = CB_ZERO;
        for (size_t k = 0; k < CB_ARRAY_LEN(noop); ++k) cb_cmd_append(&async_cmd, noop[k]);

        bool started = cb_cmd_run_opt(&async_cmd, async_opt);
        printf("第 %d 次异步启动 -> %d，池中进程数 = %zu（max_procs = 1，所以每次都先等上一个退出）\n", i + 1,
               (int)started, procs.count);
        cb_cmd_free(async_cmd);
    }

    bool waited = cb_procs_wait_and_reset(&procs);
    printf("cb_procs_wait_and_reset -> %d，重置后 procs.count = %zu\n", (int)waited, procs.count);
    cb_da_free(procs);
}

int main(void)
{
    test_svlit();
    test_cmd_extend_free();
    test_nanos();
    test_needs_rebuild();
    test_procs_wait_and_reset();
    test_cmd_to_sb();
    test_cmd_run_opt_direct();

    return 0;
}
