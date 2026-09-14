// cb.h 性能基准测试
//
// 用法（在仓库根目录）：
//     ./cb bench              # 跑全部
//     ./cb bench containers   # 只跑某一组（memory/containers/strings/utf8/paths/fs/cmd/toolbox）
//
// 计数约定：每一项都是"跑 N 次操作的总耗时"，N 写在名字里（如 `da_append x1M`）。
// 想换算成单次耗时，用 Total(us) 除以 N 即可。
//
// 注意：这是"数量级参考"，不是严谨的 benchmark：
//   - 没有做预热，没有隔离 CPU，没有统计显著性
//   - 时间源是 clock_gettime(CLOCK_MONOTONIC)，单次调用本身就有几十纳秒开销，
//     所以极快的操作（几条指令）测出来的是"操作 + 计时开销"
//   - 目录遍历 / 进程启动这类会受文件系统与系统负载影响

#include "../cb.h"

#define BENCH_COUNT(arr) (sizeof(arr) / sizeof((arr)[0]))

static bool g_verbose = false;
static const char* g_only = NULL;

// ---------------------------------------------------------------- 防优化
// 基准测试最大的坑是"编译器把被测代码优化掉"：纯函数（如 sv_eq）的返回值没人用，
// 整个循环会被删掉，测出来就是空循环的时间。两条纪律：
//   1. 结果必须写进 volatile 全局 g_sink（BENCH_SINK），编译器不敢删
//   2. 输入从 volatile 变量读（BENCH_IN），每次迭代强制重载，循环不变量没法提到循环外
static volatile uint64_t g_sink = 0;
#define BENCH_SINK(value) (g_sink += (uint64_t)(value))
// 读一次 volatile 指针，强制每次迭代重新加载
#define BENCH_IN(volatile_ptr) ((const char*)(volatile_ptr))

// 各基准的输入，全部走 volatile
static const char* volatile g_in = "  key=value;another=thing  ";
static const char* volatile g_in_num = "123456";
static const char* volatile g_in_hex = "0xDEADBEEF";
static const char* volatile g_in_flt = "3.14159";
static const char* volatile g_in_zh = "\xE4\xB8\xAD";
static const char* volatile g_in_path = "src/a/b.c";
static const char* volatile g_in_16 = "0123456789abcdef";
static const char* volatile g_in_file = "build/bench_fs/data.bin";

static bool want(const char* section)
{
    return g_only == NULL || strcmp(g_only, section) == 0;
}

static void section(const char* name)
{
    fprintf(stdout, "\n---- %s ----\n", name);
}

static void note(const char* text)
{
    if (g_verbose) fprintf(stdout, "  %s\n", text);
}

// 遍历用：什么都不做的回调，纯测遍历本身的开销
static bool bench_walk_noop(CB_Walk_Entry entry)
{
    CB_UNUSED(entry);
    return true;
}

// ================================================================ 内存
static void bench_memory(void)
{
    section("内存：Arena / Temp");

    {
        CB_Arena a = CB_ZERO;
        CB_TIMER_START("arena_alloc 32B x1M");
        for (size_t i = 0; i < 1000000; ++i) cb_arena_alloc(&a, 32);
        cb_timer_end_stat();
        cb_arena_free(&a);
    }
    {
        CB_Arena a = CB_ZERO;
        CB_TIMER_START("arena_alloc 4KB x100k");
        for (size_t i = 0; i < 100000; ++i) cb_arena_alloc(&a, 4096);
        cb_timer_end_stat();
        cb_arena_free(&a);
    }
    {
        CB_Arena a = CB_ZERO;
        CB_TIMER_START("arena save/rewind x1M");
        for (size_t i = 0; i < 1000000; ++i) {
            CB_Arena_Mark m = cb_arena_save(&a);
            cb_arena_alloc(&a, 64);
            cb_arena_rewind(&a, m);
        }
        cb_timer_end_stat();
        cb_arena_free(&a);
    }
    {
        CB_Arena a = CB_ZERO;
        CB_Arena_Mark m = cb_arena_save(&a);
        CB_TIMER_START("arena_strdup x1M");
        for (size_t i = 0; i < 1000000; ++i) {
            cb_arena_strdup(&a, "hello world");
            if ((i & 0xFFFF) == 0) cb_arena_rewind(&a, m);
        }
        cb_timer_end_stat();
        cb_arena_free(&a);
    }
    {
        CB_Arena a = CB_ZERO;
        CB_Arena_Mark m = cb_arena_save(&a);
        CB_TIMER_START("arena_sprintf x100k");
        for (size_t i = 0; i < 100000; ++i) {
            cb_arena_sprintf(&a, "value=%d/%s", (int)i, "tail");
            if ((i & 0x3FF) == 0) cb_arena_rewind(&a, m);
        }
        cb_timer_end_stat();
        cb_arena_free(&a);
    }
    {
        CB_Arena_Mark m = cb_temp_save();
        CB_TIMER_START("temp_alloc 32B x1M");
        for (size_t i = 0; i < 1000000; ++i) {
            cb_temp_alloc(32);
            if ((i & 0xFFFF) == 0) cb_temp_rewind(m);
        }
        cb_timer_end_stat();
    }
    {
        CB_Arena_Mark m = cb_temp_save();
        CB_TIMER_START("temp_sprintf x100k");
        for (size_t i = 0; i < 100000; ++i) {
            cb_temp_sprintf("value=%d/%s", (int)i, "tail");
            if ((i & 0x3FF) == 0) cb_temp_rewind(m);
        }
        cb_timer_end_stat();
    }
    {
        CB_TIMER_START("temp save/rewind x1M");
        for (size_t i = 0; i < 1000000; ++i) {
            CB_Arena_Mark m = cb_temp_save();
            cb_temp_alloc(64);
            cb_temp_rewind(m);
        }
        cb_timer_end_stat();
    }
    note("temp 的用法是 save/rewind，基准里每 64K 次回滚一次避免内存无限增长");
}

// ================================================================ 容器
typedef struct {
    int* items;
    size_t count;
    size_t capacity;
} IntArray;

static int cmp_int(const void* a, const void* b)
{
    int x = *(const int*)a, y = *(const int*)b;
    return (x > y) - (x < y);
}

static void bench_containers(void)
{
    section("容器：动态数组 / 位图 / 环形缓冲 / 哈希表");

    // 动态数组
    {
        IntArray xs = CB_ZERO;
        CB_TIMER_START("da_append x1M");
        for (size_t i = 0; i < 1000000; ++i) cb_da_append(&xs, (int)i);
        cb_timer_end_stat();
        cb_da_free(xs);
    }
    {
        IntArray xs = CB_ZERO;
        cb_da_reserve(&xs, 1000000);
        CB_TIMER_START("da_append 预分配 x1M");
        for (size_t i = 0; i < 1000000; ++i) cb_da_append(&xs, (int)i);
        cb_timer_end_stat();
        cb_da_free(xs);
    }
    {
        IntArray xs = CB_ZERO;
        CB_TIMER_START("da_insert 头部 x10k");
        for (size_t i = 0; i < 10000; ++i) cb_da_insert(&xs, 0, (int)i);
        cb_timer_end_stat();
        cb_da_free(xs);
    }
    {
        enum { RAW_COUNT = 10000 };
        IntArray xs = CB_ZERO;
        int* raw = (int*)cb_temp_alloc(RAW_COUNT * sizeof(int));
        for (int i = 0; i < RAW_COUNT; ++i) raw[i] = i;
        cb_da_append_many(&xs, raw, RAW_COUNT);
        CB_TIMER_START("da_remove_unordered x1M");
        for (size_t i = 0; i < 1000000; ++i) {
            if (xs.count == 0) cb_da_append_many(&xs, raw, RAW_COUNT);
            cb_da_remove_unordered(&xs, 0);
        }
        cb_timer_end_stat();
        cb_da_free(xs);
    }
    {
        IntArray xs = CB_ZERO;
        for (int i = 0; i < 100000; ++i) cb_da_append(&xs, i);
        volatile long long sum = 0;
        CB_TIMER_START("da_foreach x1M");
        for (size_t r = 0; r < 10; ++r) {
            cb_da_foreach(int, it, &xs) sum += *it;
        }
        cb_timer_end_stat();
        CB_UNUSED(sum);
        cb_da_free(xs);
    }
    {
        IntArray xs = CB_ZERO;
        for (int i = 0; i < 10000; ++i) cb_da_append(&xs, (i * 7919) % 100000);
        CB_TIMER_START("da_sort 10k元素 x100");
        for (size_t r = 0; r < 100; ++r) cb_da_sort(&xs, cmp_int);
        cb_timer_end_stat();
        cb_da_free(xs);
    }
    {
        IntArray xs = CB_ZERO;
        for (int i = 0; i < 1000; ++i) cb_da_append(&xs, (i * 7919) % 100000);
        CB_TIMER_START("da_sort_insertion 1k x100");
        for (size_t r = 0; r < 100; ++r) cb_da_sort_insertion(&xs, cmp_int);
        cb_timer_end_stat();
        cb_da_free(xs);
    }

    // 位图
    {
        CB_Bitset bs = CB_ZERO;
        cb_bitset_resize(&bs, 1000000);
        CB_TIMER_START("bitset_set x1M");
        for (size_t i = 0; i < 1000000; ++i) cb_bitset_set(&bs, i);
        cb_timer_end_stat();
        CB_TIMER_START("bitset_test x1M");
        for (size_t i = 0; i < 1000000; ++i) BENCH_SINK(cb_bitset_test(&bs, i));
        cb_timer_end_stat();
        cb_bitset_unset(&bs, 999999); // 在最后留一个 0，这样 find 要走满全程（1M 位）
        CB_TIMER_START("bitset_find 末位 x100");
        for (size_t i = 0; i < 100; ++i) BENCH_SINK(cb_bitset_find(&bs, false));
        cb_timer_end_stat();
        CB_TIMER_START("bitset_count x100");
        for (size_t i = 0; i < 100; ++i) BENCH_SINK(cb_bitset_count(&bs, true));
        cb_timer_end_stat();
        cb_bitset_free(&bs);
    }

    // 环形缓冲
    {
        CB_Ring ring = CB_ZERO;
        cb_ring_init(&ring, 4096);
        // 注意：chunk 必须明显小于容量，且写后立刻读，否则第一次写就写满，后面的写入全是空操作
        enum { RING_CHUNK = 256, RING_ROUNDS = 4096 };
        char in_buf[RING_CHUNK];
        char out_buf[RING_CHUNK];
        for (size_t i = 0; i < RING_CHUNK; ++i) in_buf[i] = (char)(i & 0x7F);
        memset(out_buf, 0, sizeof(out_buf));

        CB_TIMER_START("ring 写+读 1MB（各4096x256B）");
        for (size_t i = 0; i < RING_ROUNDS; ++i) {
            BENCH_SINK(cb_ring_write(&ring, in_buf, sizeof(in_buf)));
            BENCH_SINK(cb_ring_read(&ring, out_buf, sizeof(out_buf)));
        }
        cb_timer_end_stat();
        BENCH_SINK(out_buf[0]);
        BENCH_SINK(ring.count);
        cb_ring_free(&ring);
    }

    // 哈希表：字符串键
    {
        enum { KEY_COUNT = 100000, KEY_LEN = 16 };
        CB_Map m = CB_ZERO;
        // 放在 temp 上而不是栈上：100000*16 = 1.6MB，Windows 默认栈只有 1MB
        char* keys = (char*)cb_temp_alloc((size_t)KEY_COUNT * KEY_LEN);
        for (int i = 0; i < KEY_COUNT; ++i) snprintf(keys + (size_t)i * KEY_LEN, KEY_LEN, "key-%d", i);

        CB_TIMER_START("map_put x100k");
        for (int i = 0; i < KEY_COUNT; ++i) cb_map_put_cstr(&m, keys + (size_t)i * KEY_LEN, (void*)(intptr_t)i);
        cb_timer_end_stat();

        CB_TIMER_START("map_get 命中 x1M");
        for (int i = 0; i < 1000000; ++i) {
            void* v = NULL;
            if (cb_map_get_cstr(&m, keys + (size_t)(i % KEY_COUNT) * KEY_LEN, &v)) BENCH_SINK((intptr_t)v);
        }
        cb_timer_end_stat();

        CB_TIMER_START("map_iter 100k x10");
        for (int r = 0; r < 10; ++r) {
            cb_map_foreach(&m, it) { BENCH_SINK(it.key.count + (uint64_t)(uintptr_t)it.value); }
        }
        cb_timer_end_stat();

        CB_TIMER_START("map_del x100k");
        for (int i = 0; i < KEY_COUNT; ++i) cb_map_del(&m, cb_sv_from_cstr(keys + (size_t)i * KEY_LEN));
        cb_timer_end_stat();
        cb_map_free(&m);
    }

    // 哈希表：整数键
    {
        CB_Map_U64 m = CB_ZERO;
        CB_TIMER_START("map_u64_put x1M");
        for (uint64_t i = 0; i < 1000000; ++i) cb_map_u64_put(&m, i, (void*)(intptr_t)i);
        cb_timer_end_stat();

        CB_TIMER_START("map_u64_get x1M");
        for (uint64_t i = 0; i < 1000000; ++i) {
            void* v = NULL;
            if (cb_map_u64_get(&m, i, &v)) BENCH_SINK((intptr_t)v);
        }
        cb_timer_end_stat();
        cb_map_u64_free(&m);
    }
}

// ================================================================ 字符串
static void bench_strings(void)
{
    section("字符串：String_View / String_Builder / 数字解析");

    {
        CB_TIMER_START("sv_from_cstr x1M（含 strlen）");
        for (size_t i = 0; i < 1000000; ++i) {
            BENCH_SINK(cb_sv_from_cstr(BENCH_IN(g_in)).count);
        }
        cb_timer_end_stat();
    }
    {
        CB_TIMER_START("sv_trim x1M");
        for (size_t i = 0; i < 1000000; ++i) {
            BENCH_SINK(cb_sv_trim(cb_sv_from_cstr(BENCH_IN(g_in))).count);
        }
        cb_timer_end_stat();
    }
    {
        CB_TIMER_START("sv_chop_by_delim x1M");
        for (size_t i = 0; i < 1000000; ++i) {
            CB_String_View sv = cb_sv_from_cstr(BENCH_IN(g_in));
            BENCH_SINK(cb_sv_chop_by_delim(&sv, '=').count);
        }
        cb_timer_end_stat();
    }
    {
        CB_TIMER_START("sv_find x1M");
        for (size_t i = 0; i < 1000000; ++i) {
            CB_String_View sv = cb_sv_from_cstr(BENCH_IN(g_in));
            BENCH_SINK(cb_sv_find(&sv, 'a'));
        }
        cb_timer_end_stat();
    }
    {
        CB_TIMER_START("sv_eq x1M");
        for (size_t i = 0; i < 1000000; ++i) {
            CB_String_View a = cb_sv_from_cstr(BENCH_IN(g_in));
            BENCH_SINK(cb_sv_eq(a, a));
        }
        cb_timer_end_stat();
    }
    {
        CB_TIMER_START("sv_eq_ignore_case x1M");
        for (size_t i = 0; i < 1000000; ++i) {
            CB_String_View a = cb_sv_from_cstr(BENCH_IN(g_in));
            BENCH_SINK(cb_sv_eq_ignore_case(a, a));
        }
        cb_timer_end_stat();
    }
    {
        CB_TIMER_START("sv_to_temp_upper x100k");
        CB_Arena_Mark m = cb_temp_save();
        for (size_t i = 0; i < 100000; ++i) {
            cb_sv_to_temp_upper(CB_SVLIT("hello world"));
            if ((i & 0x3FF) == 0) cb_temp_rewind(m);
        }
        cb_timer_end_stat();
    }
    {
        CB_String_Builder sb = CB_ZERO;
        CB_TIMER_START("sb_append_cstr x1M");
        for (size_t i = 0; i < 1000000; ++i) {
            cb_sb_append_cstr(&sb, "x");
            if ((i & 0xFFFF) == 0) sb.count = 0;
        }
        cb_timer_end_stat();
        cb_sb_free(sb);
    }
    {
        CB_String_Builder sb = CB_ZERO;
        CB_TIMER_START("sb_appendf x100k");
        for (size_t i = 0; i < 100000; ++i) {
            cb_sb_appendf(&sb, "%d", (int)i);
            if ((i & 0x3FF) == 0) sb.count = 0;
        }
        cb_timer_end_stat();
        cb_sb_free(sb);
    }

    // 数字解析
    {
        CB_TIMER_START("sv_to_i64 x1M");
        for (size_t i = 0; i < 1000000; ++i) {
            int64_t v = 0;
            if (cb_sv_to_i64(cb_sv_from_cstr(BENCH_IN(g_in_num)), &v)) BENCH_SINK(v);
        }
        cb_timer_end_stat();
    }
    {
        CB_TIMER_START("sv_to_i64 0x前缀 x1M");
        for (size_t i = 0; i < 1000000; ++i) {
            int64_t v = 0;
            if (cb_sv_to_i64(cb_sv_from_cstr(BENCH_IN(g_in_hex)), &v)) BENCH_SINK(v);
        }
        cb_timer_end_stat();
    }
    {
        CB_Arena_Mark m = cb_temp_save();
        CB_TIMER_START("sv_to_f64 x100k（含 strtod）");
        for (size_t i = 0; i < 100000; ++i) {
            double v = 0;
            if (cb_sv_to_f64(cb_sv_from_cstr(BENCH_IN(g_in_flt)), &v)) BENCH_SINK((uint64_t)v);
            if ((i & 0x3FF) == 0) cb_temp_rewind(m);
        }
        cb_timer_end_stat();
    }
}

// ================================================================ utf8
static void bench_utf8(void)
{
    section("utf8");

    // 造一段 1MB 左右的混合文本
    CB_String_Builder text = CB_ZERO;
    while (text.count < 1000000) {
        cb_sb_append_cstr(&text, "ASCII part 中文部分 🙂 emoji ");
    }

    {
        CB_TIMER_START("utf8_validate 1MB x10");
        for (size_t i = 0; i < 10; ++i) {
            BENCH_SINK(cb_utf8_validate(cb_sb_to_sv(text), NULL));
            g_sink += text.count; // 让 text 每次都被真正读一遍
        }
        cb_timer_end_stat();
    }
    {
        CB_TIMER_START("sv_utf8_len 1MB x10");
        for (size_t i = 0; i < 10; ++i) BENCH_SINK(cb_sv_utf8_len(cb_sb_to_sv(text), NULL));
        cb_timer_end_stat();
    }
    {
        CB_TIMER_START("sv_utf8_next 1MB x5");
        for (size_t i = 0; i < 5; ++i) {
            CB_String_View walk = cb_sb_to_sv(text);
            uint32_t cp = 0;
            while (cb_sv_utf8_next(&walk, &cp)) BENCH_SINK(cp);
        }
        cb_timer_end_stat();
    }
    {
        CB_TIMER_START("utf8_decode x1M");
        for (size_t i = 0; i < 1000000; ++i) {
            uint32_t cp = 0;
            size_t len = 0;
            if (cb_utf8_decode(BENCH_IN(g_in_zh), 3, &cp, &len)) BENCH_SINK(cp + len);
        }
        cb_timer_end_stat();
    }
    {
        char buf[4] = CB_ZERO;
        CB_TIMER_START("utf8_encode x1M");
        for (size_t i = 0; i < 1000000; ++i) {
            BENCH_SINK(cb_utf8_encode(0x4E2D, buf));
            BENCH_SINK(buf[0]); // 让写入的缓冲区被真正读一次
        }
        cb_timer_end_stat();
    }
    cb_sb_free(text);
}

// ================================================================ 路径
static void bench_paths(void)
{
    section("路径与 glob");

    {
        CB_Arena_Mark m = cb_temp_save();
        CB_TIMER_START("path_join x1M");
        for (size_t i = 0; i < 1000000; ++i) {
            cb_path_join("src", "main.c");
            if ((i & 0x3FF) == 0) cb_temp_rewind(m);
        }
        cb_timer_end_stat();
    }
    {
        CB_Arena_Mark m = cb_temp_save();
        CB_TIMER_START("path_normalize x100k");
        for (size_t i = 0; i < 100000; ++i) {
            cb_path_normalize("a/./b/../c//d");
            if ((i & 0xFF) == 0) cb_temp_rewind(m);
        }
        cb_timer_end_stat();
    }
    {
        CB_Arena_Mark m = cb_temp_save();
        CB_TIMER_START("path_replace_ext x1M");
        for (size_t i = 0; i < 1000000; ++i) {
            cb_path_replace_ext("src/main.c", "o");
            if ((i & 0x3FF) == 0) cb_temp_rewind(m);
        }
        cb_timer_end_stat();
    }
    {
        CB_TIMER_START("path_is_absolute x1M");
        for (size_t i = 0; i < 1000000; ++i) BENCH_SINK(cb_path_is_absolute(BENCH_IN(g_in_path)));
        cb_timer_end_stat();
    }
    {
        CB_TIMER_START("glob_match x1M");
        for (size_t i = 0; i < 1000000; ++i) {
            BENCH_SINK(cb_glob_match("src/*/*.c", BENCH_IN(g_in_path)));
        }
        cb_timer_end_stat();
    }
    {
        CB_Arena_Mark m = cb_temp_save();
        CB_TIMER_START("path_absolute x100k");
        for (size_t i = 0; i < 100000; ++i) {
            cb_path_absolute("src");
            if ((i & 0xFF) == 0) cb_temp_rewind(m);
        }
        cb_timer_end_stat();
    }
}

// ================================================================ 文件系统
static void bench_fs(void)
{
    section("文件系统（受文件系统与页缓存影响，仅供参考）");

    cb_mkdir_if_not_exists("build/bench_fs");
    const char* path = "build/bench_fs/data.bin";
    char payload[4096];
    memset(payload, 'x', sizeof(payload));
    cb_write_entire_file(path, payload, sizeof(payload));

    {
        CB_TIMER_START("file_exists x100k");
        for (size_t i = 0; i < 100000; ++i) BENCH_SINK(cb_file_exists(BENCH_IN(g_in_file)) == 1);
        cb_timer_end_stat();
    }
    {
        CB_TIMER_START("file_size x100k");
        for (size_t i = 0; i < 100000; ++i) BENCH_SINK(cb_file_size(BENCH_IN(g_in_file)));
        cb_timer_end_stat();
    }
    {
        CB_TIMER_START("write_file 4KB x20k");
        for (size_t i = 0; i < 20000; ++i) cb_write_entire_file(path, payload, sizeof(payload));
        cb_timer_end_stat();
    }
    {
        CB_TIMER_START("read_file 4KB x20k");
        for (size_t i = 0; i < 20000; ++i) {
            CB_String_Builder sb = CB_ZERO;
            if (cb_read_entire_file(path, &sb)) BENCH_SINK(sb.count);
            cb_sb_free(sb);
        }
        cb_timer_end_stat();
    }
    {
        CB_TIMER_START("write_atomic 4KB x5k");
        for (size_t i = 0; i < 5000; ++i) cb_write_entire_file_atomic(path, payload, sizeof(payload));
        cb_timer_end_stat();
    }
    {
        CB_TIMER_START("mmap open+close x20k");
        for (size_t i = 0; i < 20000; ++i) {
            CB_Mmap m = CB_ZERO;
            if (cb_mmap_open(path, &m)) cb_mmap_close(&m);
        }
        cb_timer_end_stat();
    }
    {
        CB_File_Paths hits = CB_ZERO;
        CB_TIMER_START("glob 列目录 x1k");
        for (size_t i = 0; i < 1000; ++i) {
            hits.count = 0;
            cb_glob("build/bench_fs", "*", &hits);
        }
        cb_timer_end_stat();
        cb_da_free(hits);
    }
    {
        CB_TIMER_START("path_walk 遍历 x1k");
        for (size_t i = 0; i < 1000; ++i) {
            cb_walk_dir("build/bench_fs", bench_walk_noop);
        }
        cb_timer_end_stat();
    }
    cb_delete_directory_recursively("build/bench_fs");
    note("文件相关的项受页缓存影响很大，重复运行会明显变快");
}

// ================================================================ 进程与命令
static void bench_cmd(void)
{
    section("进程与命令（进程启动开销占主导）");

    {
        CB_Cmd cmd = CB_ZERO;
        CB_TIMER_START("cmd_append x1M");
        for (size_t i = 0; i < 1000000; ++i) {
            cmd.count = 0;
            cb_cmd_append(&cmd, "cc", "-Wall", "-o", "out", "in.c");
        }
        cb_timer_end_stat();
        cb_cmd_free(cmd);
    }
    {
        CB_Cmd cmd = CB_ZERO;
        cb_cmd_append(&cmd, "cc", "-Wall", "-Wextra", "-o", "my app", "src/main.c");
        CB_String_Builder sb = CB_ZERO;
        CB_TIMER_START("cmd_to_sb x100k");
        for (size_t i = 0; i < 100000; ++i) {
            sb.count = 0;
            cb_cmd_to_sb(cmd, &sb);
        }
        cb_timer_end_stat();
        cb_sb_free(sb);
        cb_cmd_free(cmd);
    }
#ifdef _WIN32
    const char* noop[] = {"cmd", "/c", "exit", "0"};
#else
    const char* noop[] = {"true"};
#endif
    {
        CB_Cmd cmd = CB_ZERO;
        for (size_t i = 0; i < BENCH_COUNT(noop); ++i) cb_cmd_append(&cmd, noop[i]);
        CB_TIMER_START("cmd_run 启动进程 x200");
        for (size_t i = 0; i < 200; ++i) {
            cmd.count = 0;
            for (size_t k = 0; k < BENCH_COUNT(noop); ++k) cb_cmd_append(&cmd, noop[k]);
            cb_cmd_run(&cmd);
        }
        cb_timer_end_stat();
        cb_cmd_free(cmd);
    }
}

// ================================================================ 工具箱
static void bench_toolbox(void)
{
    section("工具箱：哈希 / 随机数 / 时间 / 数学 / CLI");

    {
        CB_TIMER_START("hash_bytes 16B x1M");
        for (size_t i = 0; i < 1000000; ++i) BENCH_SINK(cb_hash_bytes(BENCH_IN(g_in_16), 16));
        cb_timer_end_stat();
    }
    {
        CB_TIMER_START("hash_u64 x1M");
        for (size_t i = 0; i < 1000000; ++i) BENCH_SINK(cb_hash_u64((uint64_t)(uintptr_t)g_in + i));
        cb_timer_end_stat();
    }
    {
        CB_Rng rng = CB_ZERO;
        cb_rng_seed(&rng, 1);
        CB_TIMER_START("rng_next x1M");
        for (size_t i = 0; i < 1000000; ++i) BENCH_SINK(cb_rng_next(&rng));
        cb_timer_end_stat();
        CB_TIMER_START("rng_range(1000) x1M");
        for (size_t i = 0; i < 1000000; ++i) BENCH_SINK(cb_rng_range(&rng, 1000));
        cb_timer_end_stat();
    }
    {
        CB_TIMER_START("get_time_us x1M");
        for (size_t i = 0; i < 1000000; ++i) BENCH_SINK((uint64_t)cb_get_time_us());
        cb_timer_end_stat();
        CB_TIMER_START("nanos_since_epoch x1M");
        for (size_t i = 0; i < 1000000; ++i) BENCH_SINK(cb_nanos_since_unspecified_epoch());
        cb_timer_end_stat();
    }
    {
        CB_Arena_Mark m = cb_temp_save();
        CB_TIMER_START("duration_to_str x100k");
        for (size_t i = 0; i < 100000; ++i) {
            cb_duration_to_str(12.5);
            if ((i & 0x3FF) == 0) cb_temp_rewind(m);
        }
        cb_timer_end_stat();
        CB_TIMER_START("time_to_iso8601 x100k");
        for (size_t i = 0; i < 100000; ++i) {
            cb_time_to_iso8601(1000000000);
            if ((i & 0x3FF) == 0) cb_temp_rewind(m);
        }
        cb_timer_end_stat();
    }
    {
        CB_TIMER_START("popcount64 x1M");
        for (size_t i = 0; i < 1000000; ++i) BENCH_SINK(cb_popcount64((uint64_t)i));
        cb_timer_end_stat();
        CB_TIMER_START("ctz64/clz64 x1M");
        for (size_t i = 0; i < 1000000; ++i) {
            BENCH_SINK(cb_ctz64((uint64_t)i | 1) + cb_clz64((uint64_t)i | 1));
        }
        cb_timer_end_stat();
        CB_TIMER_START("rotl64 x1M");
        for (size_t i = 0; i < 1000000; ++i) BENCH_SINK(cb_rotl64((uint64_t)i, 7));
        cb_timer_end_stat();
    }
    {
        char* fake[] = {(char*)"prog", (char*)"--verbose", (char*)"--out=x", (char*)"-j",
                        (char*)"file1", (char*)"file2"};
        int fake_argc = (int)BENCH_COUNT(fake);
        CB_TIMER_START("args_parse x100k");
        for (size_t i = 0; i < 100000; ++i) {
            CB_Args args = CB_ZERO;
            cb_args_parse(&args, fake_argc, fake);
            cb_args_free(&args);
        }
        cb_timer_end_stat();
    }
}

// ================================================================ main
typedef struct {
    const char* name;
    void (*fn)(void);
} Bench;

static const Bench benches[] = {
    {"memory", bench_memory},
    {"containers", bench_containers},
    {"strings", bench_strings},
    {"utf8", bench_utf8},
    {"paths", bench_paths},
    {"fs", bench_fs},
    {"cmd", bench_cmd},
    {"toolbox", bench_toolbox},
};

int main(int argc, char** argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    cb_set_log_handler(cb_default_log_handler);
    cb_minimal_log_level = CB_ERROR; // 基准测试不需要 INFO 噪音

    // 用法: bench [组名] [-v]
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-v") == 0) {
            g_verbose = true;
        } else if (g_only == NULL) {
            g_only = argv[i];
        }
    }

    if (g_only != NULL) {
        bool known = false;
        for (size_t i = 0; i < BENCH_COUNT(benches); ++i) {
            if (strcmp(benches[i].name, g_only) == 0) known = true;
        }
        if (!known) {
            fprintf(stdout, "未知的组：%s\n可用的组：", g_only);
            for (size_t i = 0; i < BENCH_COUNT(benches); ++i) fprintf(stdout, "%s ", benches[i].name);
            fprintf(stdout, "\n");
            return 1;
        }
    }

    fprintf(stdout, "cb.h 性能基准\n");
    fprintf(stdout, "  每项是「N 次操作的总耗时」，N 写在名字里；换算单次耗时用 Total(us) / N\n");
    fprintf(stdout, "  时间源：clock_gettime(CLOCK_MONOTONIC)（Windows 为 QueryPerformanceCounter）\n");

    for (size_t i = 0; i < BENCH_COUNT(benches); ++i) {
        if (!want(benches[i].name)) continue;
        benches[i].fn();
    }

    fprintf(stdout, "\n");
    // 基准结果是主要输出，所以走 stdout（便于 ./cb bench > bench.txt 保存）
    cb_timer_fprint_stats(stdout);

    if (g_verbose) {
        // cb_timer_print_stats() 是等价的宏形式，它固定打到 stderr
        fprintf(stdout, "\n（-v：下面这张表由 cb_timer_print_stats() 打到 stderr）\n");
        cb_timer_print_stats();
    }
    return 0;
}
