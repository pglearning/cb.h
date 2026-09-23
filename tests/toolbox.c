#include "test_diagnostics.h"
#include "cb.h"

static void test_math(void)
{
    printf("\n== 数学与位运算 ==\n");

    printf("cb_min(3,7) = %d, cb_max(3,7) = %d\n", cb_min(3, 7), cb_max(3, 7));
    printf("cb_clamp(10,0,5) = %d, cb_clamp(-3,0,5) = %d, cb_clamp(2,0,5) = %d\n",
           cb_clamp(10, 0, 5), cb_clamp(-3, 0, 5), cb_clamp(2, 0, 5));

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

    printf("cb_time_to_iso8601(0) = %s\n", cb_time_to_iso8601(0));
    printf("cb_time_to_iso8601(1000000000) = %s\n", cb_time_to_iso8601(1000000000));
    printf("cb_time_to_iso8601(1757729200) = %s\n", cb_time_to_iso8601(1757729200));

    printf("cb_time_now() > 1600000000 = %d\n", (int)(cb_time_now() > 1600000000));

    printf("cb_duration_to_str(0.42) = %s\n", cb_duration_to_str(0.42));
    printf("cb_duration_to_str(12.5) = %s\n", cb_duration_to_str(12.5));
    printf("cb_duration_to_str(12.0) = %s\n", cb_duration_to_str(12.0));
    printf("cb_duration_to_str(185) = %s\n", cb_duration_to_str(185));
    printf("cb_duration_to_str(7325) = %s\n", cb_duration_to_str(7325));
    printf("cb_duration_to_str(-5) = %s\n", cb_duration_to_str(-5));
}

static void test_timer(void)
{
    printf("\n== 计时器 ==\n");

    printf("CB_NANOS_PER_SEC = %llu, CB_TIMER_MAX_DEPTH = %d, cb_timer.stack 容量 = %zu\n",
           (unsigned long long)CB_NANOS_PER_SEC, (int)CB_TIMER_MAX_DEPTH,
           (size_t)CB_ARRAY_LEN(cb_timer.stack));

    // cb_get_time_ms / cb_get_time_us 读的是同一把单调时钟：只断言"有值"、"不减"和两者同源，
    // 绝不打印时间本身，否则 golden 无法跨平台、跨运行字节一致。
    double ms_first = cb_get_time_ms();
    double ms_second = cb_get_time_ms();
    double us_first = cb_get_time_us();
    double us_second = cb_get_time_us();
    printf("cb_get_time_ms() > 0 = %d, 连续两次不减 = %d\n",
           (int)(ms_first > 0), (int)(ms_second >= ms_first));
    printf("cb_get_time_us() > 0 = %d, 连续两次不减 = %d\n",
           (int)(us_first > 0), (int)(us_second >= us_first));

    // 同一时刻分别用两把读法取一次：us / 1000 落在 ms 的两次读数之间（容差 0.001ms 只为
    // 吸收 double 取整），说明两者刻度兼容。
    double ms_before = cb_get_time_ms();
    double us_middle = cb_get_time_us();
    double ms_after = cb_get_time_ms();
    printf("cb_get_time_us()/1000 落在两次 cb_get_time_ms() 之间 = %d\n",
           (int)(us_middle / 1000.0 >= ms_before - 0.001 &&
                 us_middle / 1000.0 <= ms_after + 0.001));

    cb_timer_reset();
    printf("cb_timer_reset 初始状态: stats_count = %zu, sp = %zu\n",
           cb_timer.stats_count, cb_timer.sp);

    // CB_TIMER_START / CB_TIMER_END 就是 cb_timer_begin / cb_timer_end 的短名字。
    CB_TIMER_START("phase");
    printf("CB_TIMER_START 之后 sp = %zu, 栈顶 name = %s\n", cb_timer.sp, cb_timer.stack[0].name);
    double phase_us = CB_TIMER_END();
    printf("CB_TIMER_END 返回的耗时 >= 0 = %d, 之后 sp = %zu\n",
           (int)(phase_us >= 0), cb_timer.sp);

    // 嵌套：后进先出，CB_Timer 的 stack 保存每一层的名字和起点。
    CB_Timer* timer = &cb_timer;
    cb_timer_begin("outer");
    cb_timer_begin("inner");
    printf("嵌套两层: sp = %zu, stack[0].name = %s, stack[1].name = %s\n",
           timer->sp, timer->stack[0].name, timer->stack[1].name);
    double inner_us = cb_timer_end();
    double outer_us = cb_timer_end();
    printf("cb_timer_end 返回内层/外层 >= 0 = %d %d, 嵌套结束后 sp = %zu\n",
           (int)(inner_us >= 0), (int)(outer_us >= 0), timer->sp);

    // CB_Timer_Frame 是栈上的一帧。
    cb_timer_begin("frame");
    CB_Timer_Frame frame = cb_timer.stack[timer->sp - 1];
    printf("CB_Timer_Frame: name 匹配 = %d, start 是有效时间戳 = %d\n",
           (int)(strcmp(frame.name, "frame") == 0), (int)(frame.start >= 0));
    (void)cb_timer_end();

    // cb_timer_end_stat：结束当前测量并累加到同名 CB_Timer_Stat。
    for (int i = 0; i < 3; ++i) {
        cb_timer_begin("loop");
        double iteration_us = cb_timer_end_stat();
        printf("cb_timer_end_stat 第 %d 次返回的耗时 >= 0 = %d\n", i + 1, (int)(iteration_us >= 0));
    }
    CB_Timer_Stat* loop_stat = cb_timer_get_stat("loop");
    printf("cb_timer_get_stat(\"loop\"): name = %s, count = %zu, min <= max = %d, min >= 0 = %d\n",
           loop_stat->name, loop_stat->count, (int)(loop_stat->min <= loop_stat->max),
           (int)(loop_stat->min >= 0));
    size_t stats_count = cb_timer.stats_count;
    CB_Timer_Stat* loop_stat_again = cb_timer_get_stat("loop");
    printf("同名再取一次是同一指针 = %d, stats_count 不变 = %d\n",
           (int)(loop_stat_again == loop_stat), (int)(cb_timer.stats_count == stats_count));

    CB_Timer_Stat* empty_stat = cb_timer_get_stat("created-empty");
    printf("cb_timer_get_stat 新名字会建条目: count = %zu, total = %.0f, stats_count = %zu\n",
           empty_stat->count, empty_stat->total, cb_timer.stats_count);

    // cb_timer_end_print 写 stderr，golden 看不到，只能断言"跑过且没有崩"。
    cb_timer_begin("end_print");
    cb_timer_end_print();
    printf("cb_timer_end_print 已执行（输出在 stderr），之后 sp = %zu\n", cb_timer.sp);

    // cb_timer_fprint_stats 可以写进文件：只断言文件非空、含条目名和表头，不打印内容。
    const char* stats_path = "timer_stats.txt";
    FILE* stats_file = fopen(stats_path, "wb");
    printf("fopen(\"%s\") 成功 = %d\n", stats_path, (int)(stats_file != NULL));
    if (stats_file != NULL) {
        cb_timer_fprint_stats(stats_file);
        fclose(stats_file);
    }
    cb_timer_fprint_stats(NULL); // 空指针直接返回
    printf("cb_timer_fprint_stats(NULL) 已执行 = %d\n", 1);

    CB_String_Builder stats_sb = CB_ZERO;
    bool stats_read = cb_read_entire_file(stats_path, &stats_sb);
    cb_sb_append_null(&stats_sb);
    printf("统计表文件: 非空 = %d, 含 \"loop\" = %d, 含 \"Count\" 表头 = %d\n",
           (int)(stats_sb.count > 0),
           (int)(stats_sb.items != NULL && strstr(stats_sb.items, "loop") != NULL),
           (int)(stats_sb.items != NULL && strstr(stats_sb.items, "Count") != NULL));
    printf("cb_read_entire_file 返回 = %d\n", (int)stats_read);
    cb_sb_free(stats_sb);
    cb_delete_file(stats_path);
    printf("cb_delete_file 之后文件仍存在 = %d\n", (int)cb_file_exists(stats_path));

    // cb_timer_print_stats 是 fprint_stats(stderr) 的快捷方式。
    cb_timer_print_stats();
    printf("cb_timer_print_stats 已执行（输出在 stderr） = %d\n", 1);

    // reset 清空统计和栈，但把已经分配的统计表留着复用。
    CB_Timer_Stat* table_before_reset = cb_timer.stats;
    cb_timer_reset();
    printf("cb_timer_reset 之后: stats_count = %zu, sp = %zu, 表指针不变 = %d, 表仍分配 = %d\n",
           cb_timer.stats_count, cb_timer.sp,
           (int)(cb_timer.stats == table_before_reset), (int)(cb_timer.stats != NULL));
}

static void test_general_macros(void)
{
    printf("\n== 通用宏 ==\n");

    // cb_swap(T, a, b)：任意类型借一个临时变量交换。
    int left = 1, right = 2;
    cb_swap(int, left, right);
    printf("cb_swap(int, 1, 2) 之后 left = %d, right = %d\n", left, right);

    const char* word_a = "alpha";
    const char* word_b = "beta";
    cb_swap(const char*, word_a, word_b);
    printf("cb_swap(const char*, \"alpha\", \"beta\") 之后 = %s %s\n", word_a, word_b);

    // cb_shift(argv, argc)：取走第一个参数并把指针前移、计数减一（bash 的 shift）。
    const char* argv_like[] = {"prog", "first", "second"};
    const char** arg_ptr = argv_like;
    int arg_count = (int)CB_ARRAY_LEN(argv_like);
    const char* shifted = cb_shift(arg_ptr, arg_count);
    printf("cb_shift 取到 \"%s\", 之后 count = %d, *ptr = %s\n", shifted, arg_count, *arg_ptr);

    // CB_ARRAY_GET(array, index)：带断言的按下标读取。
    int values[] = {10, 20, 30};
    printf("CB_ARRAY_GET(values, 1) = %d, CB_ARRAY_LEN(values) = %zu\n",
           CB_ARRAY_GET(values, 1), (size_t)CB_ARRAY_LEN(values));

    // CB_LINE_END 是宿主的换行（Linux "\\n"、Windows "\\r\\n"）。toolbox 的 golden 不分平台，
    // 所以只打印与平台无关的事实：结尾一定是 '\\n'，长度是 1 或 2。
    size_t line_end_len = strlen(CB_LINE_END);
    printf("CB_LINE_END 以 '\\n' 结尾 = %d, 长度是 1 或 2 = %d\n",
           (int)(line_end_len > 0 && CB_LINE_END[line_end_len - 1] == '\n'),
           (int)(line_end_len == 1 || line_end_len == 2));

    // CB_PROCESS_ID() 每次运行都不同，只断言它有效、同一次运行内稳定。
    unsigned long pid_first = CB_PROCESS_ID();
    unsigned long pid_second = CB_PROCESS_ID();
    printf("CB_PROCESS_ID() > 0 = %d, 两次调用相同 = %d\n",
           (int)(pid_first > 0), (int)(pid_first == pid_second));
}

static void test_random(void)
{
    printf("\n== 随机数 ==\n");

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

    CB_Rng z = CB_ZERO;
    cb_rng_seed(&z, 0);
    uint64_t v1 = cb_rng_next(&z);
    uint64_t v2 = cb_rng_next(&z);
    printf("种子 0: v1 != 0 = %d, v1 != v2 = %d\n", (int)(v1 != 0), (int)(v1 != v2));

    CB_Rng r = CB_ZERO;
    cb_rng_seed(&r, 42);
    bool in_range = true;
    for (int i = 0; i < 1000; ++i) {
        if (cb_rng_range(&r, 10) >= 10) in_range = false;
    }
    printf("cb_rng_range(10) 1000 次全在 [0,10) = %d\n", (int)in_range);
    printf("cb_rng_range(0) = %llu\n", (unsigned long long)cb_rng_range(&r, 0));
    printf("cb_rng_range(1) = %llu\n", (unsigned long long)cb_rng_range(&r, 1));

    bool unit = true;
    for (int i = 0; i < 1000; ++i) {
        double d = cb_rng_double(&r);
        if (d < 0.0 || d >= 1.0) unit = false;
    }
    printf("cb_rng_double 1000 次全在 [0,1) = %d\n", (int)unit);

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

    printf("cb_stdout_is_tty() = %d\n", (int)cb_stdout_is_tty());
    printf("cb_color_enabled() = %d\n", (int)cb_color_enabled());
}

static void test_dump(void)
{
    printf("\n== hex dump ==\n");

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
        (char*)"--out=build/app2",
        (char*)"--",
        (char*)"--not-an-option",
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
    test_timer();
    test_random();
    test_env();
    test_dump();
    test_args();
    test_general_macros();

    return 0;
}
