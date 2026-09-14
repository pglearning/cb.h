// 工具箱实测：数学与位运算 / 时间日期 / 随机数 / 运行环境 / hex dump / CLI 解析
//
// 风格：纯 printf + golden 比对，只打实际观察到的值。
// stdout 必须确定性：任何耗时/计时数据都不能进 stdout（cb_dump_hex 自己走 stderr，正好符合这条）。
#include "test_diagnostics.h"
#include "cb.h"

static void test_math(void)
{
    printf("\n== 数学与位运算 ==\n");

    printf("cb_min(3,7) = %d, cb_max(3,7) = %d\n", cb_min(3, 7), cb_max(3, 7));
    printf("cb_clamp(10,0,5) = %d, cb_clamp(-3,0,5) = %d, cb_clamp(2,0,5) = %d\n",
           cb_clamp(10, 0, 5), cb_clamp(-3, 0, 5), cb_clamp(2, 0, 5));
    // cb_align_up / cb_align_down 是宏，结果类型跟着实参走，这里实参是 int
    printf("cb_align_up(5,8) = %d, cb_align_up(8,8) = %d\n",
           cb_align_up(5, 8), cb_align_up(8, 8));
    printf("cb_align_down(13,8) = %d, cb_align_down(8,8) = %d\n",
           cb_align_down(13, 8), cb_align_down(8, 8));

    printf("cb_is_pow2: 1 = %d, 1024 = %d, 0 = %d, 1000 = %d\n",
           (int)cb_is_pow2(1), (int)cb_is_pow2(1024), (int)cb_is_pow2(0), (int)cb_is_pow2(1000));
    printf("cb_next_pow2(0) = %llu, cb_next_pow2(1) = %llu\n",
           (unsigned long long)cb_next_pow2(0), (unsigned long long)cb_next_pow2(1));
    printf("cb_next_pow2(3) = %llu, cb_next_pow2(5) = %llu, cb_next_pow2(1024) = %llu\n",
           (unsigned long long)cb_next_pow2(3), (unsigned long long)cb_next_pow2(5),
           (unsigned long long)cb_next_pow2(1024));

    printf("cb_popcount64(0) = %d, cb_popcount64(1) = %d\n",
           cb_popcount64(0), cb_popcount64(1));
    printf("cb_popcount64(0xFF) = %d, cb_popcount64(UINT64_MAX) = %d\n",
           cb_popcount64(0xFF), cb_popcount64(UINT64_MAX));

    printf("cb_ctz64(1) = %d, cb_ctz64(8) = %d\n", cb_ctz64(1), cb_ctz64(8));
    printf("cb_ctz64(0) = %d\n", cb_ctz64(0));
    printf("cb_clz64(1) = %d, cb_clz64(0) = %d\n", cb_clz64(1), cb_clz64(0));

    printf("cb_rotl64(1,1) = 0x%llx, cb_rotl64(0x8000000000000000,1) = 0x%llx\n",
           (unsigned long long)cb_rotl64(1, 1),
           (unsigned long long)cb_rotl64(0x8000000000000000ull, 1));
    printf("cb_rotr64(2,1) = 0x%llx, cb_rotr64(1,1) = 0x%llx\n",
           (unsigned long long)cb_rotr64(2, 1),
           (unsigned long long)cb_rotr64(1, 1));
    printf("cb_rotl64(0x1234,0) = 0x%llx, cb_rotl64(0x1234,64) = 0x%llx\n",
           (unsigned long long)cb_rotl64(0x1234, 0),
           (unsigned long long)cb_rotl64(0x1234, 64));

    printf("cb_bswap32(0x12345678) = 0x%x\n", (unsigned)cb_bswap32(0x12345678u));
    printf("cb_bswap64(0x0123456789ABCDEF) = 0x%llx\n",
           (unsigned long long)cb_bswap64(0x0123456789ABCDEFull));
    printf("cb_is_little_endian() = %d\n", (int)cb_is_little_endian());
}

static void test_time(void)
{
    printf("\n== 时间与日期 ==\n");

    // 时间戳 -> ISO8601：全部是固定输入，输出可进 golden
    printf("cb_time_to_iso8601(0) = %s\n", cb_time_to_iso8601(0));
    printf("cb_time_to_iso8601(1000000000) = %s\n", cb_time_to_iso8601(1000000000));
    printf("cb_time_to_iso8601(1757729200) = %s\n", cb_time_to_iso8601(1757729200));

    // cb_time_now() 是当前时间，值本身不能进 golden；只打印"它是不是一个合理的 Unix 时间戳"（恒为 1）
    printf("cb_time_now() > 1600000000 = %d\n", (int)(cb_time_now() > 1600000000));

    printf("cb_duration_to_str(0.42) = %s\n", cb_duration_to_str(0.42));
    printf("cb_duration_to_str(12.5) = %s\n", cb_duration_to_str(12.5));
    printf("cb_duration_to_str(12.0) = %s\n", cb_duration_to_str(12.0));
    printf("cb_duration_to_str(185) = %s\n", cb_duration_to_str(185));
    printf("cb_duration_to_str(7325) = %s\n", cb_duration_to_str(7325));
    printf("cb_duration_to_str(-5) = %s\n", cb_duration_to_str(-5));
}

static void test_random(void)
{
    printf("\n== 随机数 ==\n");

    // 同种子必须产生同序列（跨平台可复现）；只打印"100 次是否全部相同"，不打印随机值本身
    CB_Rng a = CB_ZERO, b = CB_ZERO;
    cb_rng_seed(&a, 12345);
    cb_rng_seed(&b, 12345);
    bool same = true;
    for (int i = 0; i < 100; ++i) {
        if (cb_rng_next(&a) != cb_rng_next(&b)) same = false;
    }
    printf("同种子 100 次序列全同 = %d\n", (int)same);

    CB_Rng c = CB_ZERO;
    cb_rng_seed(&c, 54321);
    printf("不同种子首次输出不同 = %d\n", (int)(cb_rng_next(&c) != cb_rng_next(&b)));

    // 种子 0 也要能正常工作（状态不能全零）
    CB_Rng z = CB_ZERO;
    cb_rng_seed(&z, 0);
    uint64_t v1 = cb_rng_next(&z);
    uint64_t v2 = cb_rng_next(&z);
    printf("种子 0: v1 != 0 = %d, v1 != v2 = %d\n", (int)(v1 != 0), (int)(v1 != v2));

    // 范围
    CB_Rng r = CB_ZERO;
    cb_rng_seed(&r, 42);
    bool in_range = true;
    for (int i = 0; i < 1000; ++i) {
        if (cb_rng_range(&r, 10) >= 10) in_range = false;
    }
    printf("cb_rng_range(10) 1000 次全在 [0,10) = %d\n", (int)in_range);
    printf("cb_rng_range(0) = %llu\n", (unsigned long long)cb_rng_range(&r, 0));
    printf("cb_rng_range(1) = %llu\n", (unsigned long long)cb_rng_range(&r, 1));

    // [0,1)
    bool unit = true;
    for (int i = 0; i < 1000; ++i) {
        double d = cb_rng_double(&r);
        if (d < 0.0 || d >= 1.0) unit = false;
    }
    printf("cb_rng_double 1000 次全在 [0,1) = %d\n", (int)unit);

    // 洗牌：只验证元素集合不变，不打印打乱后的顺序（那不确定）
    int values[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    cb_rng_shuffle(&r, values, CB_ARRAY_LEN(values), sizeof(values[0]));
    int seen[10] = CB_ZERO;
    bool intact = true;
    for (size_t i = 0; i < CB_ARRAY_LEN(values); ++i) {
        if (values[i] < 0 || values[i] > 9 || seen[values[i]] != 0) intact = false;
        else seen[values[i]] = 1;
    }
    printf("洗牌后元素集合不变 = %d\n", (int)intact);
}

static void test_env(void)
{
    printf("\n== 运行环境 ==\n");

    char* path = cb_env_get("PATH");
    printf("cb_env_get(PATH) 非空 = %d\n", (int)(path != NULL && path[0] != '\0'));
    printf("cb_env_get(不存在的变量) == NULL = %d\n",
           (int)(cb_env_get("CB_DEFINITELY_NOT_SET_12345") == NULL));

    printf("cb_env_set(\"CB_TEST_ENV_VAR\", \"hello-env\") = %d\n",
           (int)cb_env_set("CB_TEST_ENV_VAR", "hello-env"));
    char* got = cb_env_get("CB_TEST_ENV_VAR");
    printf("set 之后读回 = %s\n", got != NULL ? got : "(NULL)");

    printf("cb_terminal_width() > 0 = %d\n", (int)(cb_terminal_width() > 0));
    // 测试是被重定向到文件的，所以这里应当是"非终端"
    printf("cb_stdout_is_tty() = %d\n", (int)cb_stdout_is_tty());
    printf("cb_color_enabled() = %d\n", (int)cb_color_enabled());
}

static void test_dump(void)
{
    printf("\n== hex dump ==\n");

    // 输出走 stderr，不进 golden；只验证调用不崩且不污染 stdout
    const unsigned char data[] = {0x00, 0x01, 0x41, 0x42, 0x7F, 0x80, 0xFF};
    cb_dump_hex(data, sizeof(data));
    cb_dump_hex(NULL, 0);
    printf("cb_dump_hex(data, 7) 与 cb_dump_hex(NULL, 0) 已执行 = %d\n", 1);
}

static void test_args(void)
{
    printf("\n== CLI 参数解析 ==\n");

    char* fake[] = {
        (char*)"prog",
        (char*)"--verbose",
        (char*)"--out=build/app",
        (char*)"-j",
        (char*)"-o=x",
        (char*)"--out=build/app2", // 同名重复，最后一次生效
        (char*)"--",
        (char*)"--not-an-option", // -- 之后全部是位置参数
        (char*)"file1",
        (char*)"file2",
    };
    int fake_argc = (int)CB_ARRAY_LEN(fake);

    CB_Args args = CB_ZERO;
    cb_args_parse(&args, fake_argc, fake);

    printf("cb_args_has(\"verbose\") = %d\n", (int)cb_args_has(&args, "verbose"));
    printf("cb_args_has(\"j\") = %d\n", (int)cb_args_has(&args, "j"));
    printf("cb_args_has(\"nope\") = %d\n", (int)cb_args_has(&args, "nope"));

    printf("cb_args_get(\"out\", \"?\") = %s\n", cb_args_get(&args, "out", "?"));
    printf("cb_args_get_first(\"out\") = %s\n", cb_args_get_first(&args, "out"));
    printf("cb_args_get(\"o\", \"?\") = %s\n", cb_args_get(&args, "o", "?"));
    printf("cb_args_get(\"missing\", \"default\") = %s\n", cb_args_get(&args, "missing", "default"));

    printf("cb_args_positional_count() = %zu\n", cb_args_positional_count(&args));
    for (size_t i = 0; i < cb_args_positional_count(&args); ++i) {
        printf("  positional[%zu] = %s\n", i, cb_args_positional(&args, i));
    }
    printf("cb_args_positional(99) == NULL = %d\n",
           (int)(cb_args_positional(&args, 99) == NULL));

    cb_args_free(&args);
    printf("cb_args_free 之后: options.items == NULL = %d, positionals.items == NULL = %d\n",
           (int)(args.options.items == NULL), (int)(args.positionals.items == NULL));

    // 空命令行
    char* empty[] = {(char*)"prog"};
    CB_Args e = CB_ZERO;
    cb_args_parse(&e, 1, empty);
    printf("空命令行: positional_count = %zu, has(\"x\") = %d\n",
           cb_args_positional_count(&e), (int)cb_args_has(&e, "x"));
    cb_args_free(&e);
}

int main(void)
{
    test_math();
    test_time();
    test_random();
    test_env();
    test_dump();
    test_args();

    return 0;
}
