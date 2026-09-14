// 示例 13：工具箱
//
// 覆盖：数学与位运算、时间与日期、随机数、运行环境探测、hex dump、CLI 参数解析
//
// 编译运行（在仓库根目录）：
//     cc -o /tmp/ex13 examples/13_toolbox.c && /tmp/ex13
//     /tmp/ex13 --verbose --out=build/app -j file1.txt file2.txt

#include "../cb.h"

static void title(const char* text) { printf("\n== %s ==\n", text); }

// ---------------------------------------------------------------- 数学与位运算
static void demo_math(void)
{
    title("数学与位运算");

    printf("  cb_min(3,7)              = %d\n", cb_min(3, 7));
    printf("  cb_max(3,7)              = %d\n", cb_max(3, 7));
    printf("  cb_clamp(15, 0, 10)      = %d\n", cb_clamp(15, 0, 10));
    int i = 1, j = 5;
    int m = cb_min(i++, j++);
    printf("  cb_min(i++, j++)         = %d（两个参数各只求值一次：i=%d, j=%d）\n", m, i, j);
    printf("  cb_align_up(5, 8)        = %d\n", cb_align_up(5, 8));
    printf("  cb_align_down(13, 8)     = %d\n", cb_align_down(13, 8));
    printf("  返回类型与第一个参数相同（C 用 _Generic 分派，C++ 用模板）\n\n");

    printf("  cb_is_pow2(1024)         = %s\n", cb_is_pow2(1024) ? "真" : "假");
    printf("  cb_is_pow2(1000)         = %s\n", cb_is_pow2(1000) ? "真" : "假");
    printf("  cb_next_pow2(1000)       = %llu\n", (unsigned long long)cb_next_pow2(1000));
    printf("  cb_next_pow2(1)          = %llu\n", (unsigned long long)cb_next_pow2(1));
    printf("  cb_popcount64(0xFF)      = %d\n", cb_popcount64(0xFF));
    printf("  cb_popcount64(UINT64_MAX)= %d\n", cb_popcount64(UINT64_MAX));
    printf("  cb_ctz64(8)              = %d（末尾 0 的个数）\n", cb_ctz64(8));
    printf("  cb_clz64(1)              = %d（开头 0 的个数）\n", cb_clz64(1));
    printf("  cb_ctz64(0) / cb_clz64(0) = %d / %d（定义为 64）\n", cb_ctz64(0), cb_clz64(0));
    printf("  cb_rotl64(0x8000000000000000, 1) = 0x%llX\n",
           (unsigned long long)cb_rotl64(0x8000000000000000ull, 1));
    printf("  cb_bswap32(0x12345678)   = 0x%X\n", cb_bswap32(0x12345678u));
    printf("  cb_is_little_endian()    = %s\n", cb_is_little_endian() ? "小端" : "大端");
}

// ---------------------------------------------------------------- 时间
static void demo_time(void)
{
    title("时间与日期");

    printf("  cb_time_now()            = %lld（当前 UTC 时间戳）\n", (long long)cb_time_now());
    printf("  cb_time_to_iso8601(0)    = %s\n", cb_time_to_iso8601(0));
    printf("  cb_time_to_iso8601(1e9)  = %s\n", cb_time_to_iso8601(1000000000));
    printf("  cb_time_to_iso8601(now)  = %s\n", cb_time_to_iso8601(cb_time_now()));
    printf("\n");

    double durations[] = {0.00042, 0.42, 1.0, 12.5, 59.9, 185.0, 7325.0};
    for (size_t i = 0; i < CB_ARRAY_LEN(durations); ++i) {
        printf("  cb_duration_to_str(%-8g) = %s\n", durations[i], cb_duration_to_str(durations[i]));
    }

    printf("\n  计时器（全部逻辑在 C11 内联函数里，宏只是薄包装）：\n");
    CB_TIMER_START("示例阶段");
    CB_TIMER_START("空转");
    volatile uint64_t acc = 0;
    for (uint64_t i = 0; i < 100000; ++i) acc += i;
    CB_UNUSED(acc);
    double us = cb_timer_end_stat();     // 返回耗时并累加统计
    printf("    空转耗时 %.1f 微秒\n", us);
    cb_timer_end_stat();

    uint64_t t0 = cb_nanos_since_unspecified_epoch();
    for (uint64_t i = 0; i < 100000; ++i) acc += i;
    uint64_t t1 = cb_nanos_since_unspecified_epoch();
    printf("    纳秒时间戳差 = %llu ns\n", (unsigned long long)(t1 - t0));

    printf("\n  直接用函数（宏 CB_TIMER_START/END 展开后就是它们）：\n");
    cb_timer_begin("直接调用");
    for (uint64_t i = 0; i < 100000; ++i) acc += i;
    printf("    cb_timer_end()      = %.1f 微秒（只取值，不记统计）\n", cb_timer_end());

    cb_timer_begin("带统计");
    for (uint64_t i = 0; i < 100000; ++i) acc += i;
    printf("    cb_timer_end_stat() = %.1f 微秒（取值并累加统计）\n", cb_timer_end_stat());

    cb_timer_begin("打印这一条");
    for (uint64_t i = 0; i < 100000; ++i) acc += i;
    printf("    cb_timer_end_print() 会把这一条打到 stderr：\n");
    cb_timer_end_print();

    // 直接查统计项
    cb_timer_begin("可查询的");
    for (uint64_t i = 0; i < 100000; ++i) acc += i;
    cb_timer_end_stat();
    CB_Timer_Stat* stat = cb_timer_get_stat("可查询的");
    printf("    cb_timer_get_stat(\"可查询的\") = count %zu, total %.1f us, min %.1f, max %.1f\n",
           stat->count, stat->total, stat->min, stat->max);

    printf("    cb_timer_print_stats() 打印整张统计表（走 stderr）\n");
    cb_timer_print_stats();

    // 宏形式（等价于上面的函数调用）
    CB_TIMER_START("宏形式");
    for (uint64_t i = 0; i < 100000; ++i) acc += i;
    printf("    CB_TIMER_END() = %.1f 微秒\n", CB_TIMER_END());

    CB_TIMER_START("宏形式-打印");
    for (uint64_t i = 0; i < 100000; ++i) acc += i;
    printf("    cb_timer_end_print() 打到 stderr：\n");
    cb_timer_end_print();

    cb_timer_reset();
    printf("    cb_timer_reset() 之后统计表已清空\n");

    printf("\n  底层时间源：cb_get_time_ms() = %.3f，cb_get_time_us() = %.1f\n",
           cb_get_time_ms(), cb_get_time_us());
}

// ---------------------------------------------------------------- 通用小工具
static void demo_generic(void)
{
    title("通用小工具：cb_swap / CB_ARRAY_LEN / CB_ARRAY_GET / cb_log_at");

    // cb_swap：交换两个同类型变量
    int x = 1, y = 2;
    cb_swap(int, x, y);
    printf("  cb_swap(int, x, y) 之后 x=%d y=%d\n", x, y);

    char a = 'a', b = 'b';
    cb_swap(char, a, b);
    printf("  cb_swap(char, a, b) 之后 a=%c b=%c\n", a, b);

    // CB_ARRAY_LEN：编译期求数组长度
    int nums[] = {10, 20, 30, 40};
    printf("  CB_ARRAY_LEN(nums) = %zu\n", CB_ARRAY_LEN(nums));

    // CB_ARRAY_GET：带越界断言的下标访问
    printf("  CB_ARRAY_GET(nums, 0) = %d，CB_ARRAY_GET(nums, 3) = %d\n",
           CB_ARRAY_GET(nums, 0), CB_ARRAY_GET(nums, 3));
    printf("  （越界时 CB_ARRAY_GET 会触发断言）\n");

    // cb_log_at：CB_LOG_AT 宏展开后就是它；需要手动传 file/line 时可以直接调
    cb_set_log_handler(cb_default_log_handler);
    printf("\n  cb_log_at 直接调用（带位置）→ stderr：\n");
    cb_log_at(CB_INFO, "hand-written-file.c", 99, "手动指定的位置：%s", "file/line 由你提供");
}

// ---------------------------------------------------------------- 随机数
static void demo_random(void)
{
    title("随机数（xoshiro256**，同种子跨平台可复现；不适合密码学）");

    CB_Rng a = CB_ZERO, b = CB_ZERO;
    cb_rng_seed(&a, 20250913);
    cb_rng_seed(&b, 20250913);

    printf("  种子 20250913 的前 5 个原始值：");
    for (int i = 0; i < 5; ++i) printf("%llu ", (unsigned long long)cb_rng_next(&a));
    printf("\n");

    printf("  cb_rng_range(100) 前 8 个：");
    for (int i = 0; i < 8; ++i) printf("%llu ", (unsigned long long)cb_rng_range(&b, 100));
    printf("\n");

    printf("  cb_rng_double() 前 3 个：");
    for (int i = 0; i < 3; ++i) printf("%.6f ", cb_rng_double(&b));
    printf("\n");

    printf("  cb_rng_range(0) = %llu（bound 为 0 时返回 0）\n",
           (unsigned long long)cb_rng_range(&b, 0));

    int deck[] = {1, 2, 3, 4, 5, 6, 7, 8};
    cb_rng_shuffle(&b, deck, CB_ARRAY_LEN(deck), sizeof(deck[0]));
    printf("  cb_rng_shuffle 之后：");
    for (size_t i = 0; i < CB_ARRAY_LEN(deck); ++i) printf("%d ", deck[i]);
    printf("\n");
}

// ---------------------------------------------------------------- 运行环境
static void demo_env(void)
{
    title("运行环境探测");

    char* path = cb_env_get("PATH");
    printf("  cb_env_get(\"PATH\")        = %s（长度 %zu，结果在 temp 上）\n",
           path != NULL ? "非空" : "NULL", path != NULL ? strlen(path) : 0);
    printf("  cb_env_get(\"绝不存在的变量\") = %s\n",
           cb_env_get("CB_DEFINITELY_NOT_SET_12345") == NULL ? "NULL" : "非 NULL");

    cb_env_set("CB_EXAMPLE_VAR", "hello-env");
    printf("  cb_env_set 之后读回         = %s\n", cb_env_get("CB_EXAMPLE_VAR"));

    printf("  cb_stdout_is_tty()          = %s（重定向到文件时为假）\n",
           cb_stdout_is_tty() ? "真" : "假");
    printf("  cb_terminal_width()         = %d\n", cb_terminal_width());
    printf("  cb_color_enabled()          = %s（尊重 NO_COLOR 环境变量）\n",
           cb_color_enabled() ? "真" : "假");
}

// ---------------------------------------------------------------- hex dump
static void demo_hexdump(void)
{
    title("cb_dump_hex：调试二进制数据（输出到 stderr）");

    unsigned char data[] = {0x00, 0x01, 0x41, 0x42, 0x7F, 0x80, 0xFF,
                            'h', 'e', 'l', 'l', 'o', 0x0A, 0x0D, 0x09, 0x20,
                            0xDE, 0xAD, 0xBE, 0xEF};
    printf("  下面这些是 stderr 输出（16 字节一行：偏移 + 十六进制 + ASCII）：\n");
    cb_dump_hex(data, sizeof(data));
    printf("  空输入也安全：\n");
    cb_dump_hex(NULL, 0);
}

// ---------------------------------------------------------------- CLI
static void demo_cli(int argc, char** argv)
{
    title("CLI 参数解析：cb_args_parse");

    printf("  规则：--key=value 是选项、--key 是开关、-k=value 也认、\n");
    printf("        -- 之后的全部是位置参数、其余是位置参数\n\n");

    if (argc > 1) {
        printf("  解析真实命令行（argc=%d）：\n", argc);
    } else {
        printf("  没有传参数，用一组模拟参数演示：\n");
        static char* fake[] = {
            (char*)"prog", (char*)"--verbose", (char*)"--out=build/app",
            (char*)"-j", (char*)"-D=X", (char*)"--", (char*)"--not-an-option",
            (char*)"file1.txt", (char*)"file2.txt",
        };
        argc = (int)CB_ARRAY_LEN(fake);
        argv = fake;
    }

    CB_Args args = CB_ZERO;
    cb_args_parse(&args, argc, argv);

    printf("    开关 --verbose 存在? %s\n", cb_args_has(&args, "verbose") ? "是" : "否");
    printf("    开关 -j 存在?        %s\n", cb_args_has(&args, "j") ? "是" : "否");
    printf("    选项 --out 的值      = %s\n", cb_args_get(&args, "out", "(未提供)"));
    printf("    选项 -D 的值         = %s\n", cb_args_get(&args, "D", "(未提供)"));
    printf("    不存在的选项         = %s\n", cb_args_get(&args, "missing", "(默认值)"));
    printf("    位置参数 %zu 个：", cb_args_positional_count(&args));
    for (size_t i = 0; i < cb_args_positional_count(&args); ++i) {
        printf(" %s", cb_args_positional(&args, i));
    }
    printf("\n");
    printf("    越界访问位置参数返回 %s\n", cb_args_positional(&args, 99) == NULL ? "NULL" : "非 NULL");

    cb_args_free(&args);
    printf("    cb_args_free 之后 options.items = %s\n", args.options.items == NULL ? "NULL" : "非空");

    printf("\n  完整用法示例（一个构建脚本的子命令 + 选项）：\n");
    printf("      const char* cmd = argc > 0 ? cb_shift(argv, argc) : \"build\";\n");
    printf("      CB_Args args = CB_ZERO; cb_args_parse(&args, argc, argv);\n");
    printf("      bool verbose = cb_args_has(&args, \"verbose\");\n");
    printf("      const char* out = cb_args_get(&args, \"out\", \"build/app\");\n");
}

int main(int argc, char** argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    cb_set_log_handler(cb_default_log_handler);

    demo_math();
    demo_time();
    demo_random();
    demo_env();
    demo_generic();
    demo_hexdump();
    demo_cli(argc, argv);

    printf("\n全部工具箱示例执行完毕\n");
    return 0;
}
