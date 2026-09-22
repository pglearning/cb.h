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

int main(void)
{
    test_svlit();
    test_cmd_extend_free();
    test_nanos();
    test_needs_rebuild();
    test_procs_wait_and_reset();

    return 0;
}
