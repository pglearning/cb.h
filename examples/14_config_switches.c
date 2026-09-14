// 示例 14：编译期开关总览
//
// 17 个编译期开关全部列在下面，本文件实测其中 5 个（标 ★ 的）。
// 所有开关都必须定义在 #include "cb.h" 之前：cb.h 在那一行就编译完了，写在后面一点作用都没有。
// 命令行 -D 与文件里 #define 等价。
//
// A. 行为开关（7 个，默认全关）：
//     CB_ENABLE_ECHO                  文件系统/命令等操作打印提示信息（示例 02/03/04/05/10 打开）
//     CB_DONT_DELETE_OLD_CB           保留上一次编译出的旧二进制（自重建时不删旧的）
//     CB_TRACE_CMD_RUN_FAIL_LOCATION  命令失败时打印它的调用点（文件:行号）
//     CB_ALLOC_TRACK                  每次分配带追踪头，可统计与报告泄漏（示例 11 用 -D 打开）
//     CB_STRIP_PREFIX                 去掉 cb_ / CB_ 前缀的别名（别名区由 tools/gen-docs.py 生成）★ 实测
//     CB_SHARED_STATE[_IMPL]          多 TU 共享全局状态；每个 TU 定义前者，恰好一个再定义 _IMPL
//                                     ——要跨 TU 才有效果，单文件示例演示不了
//     CB_OOM(size)                    顶掉默认的 OOM 处理器（默认打印 文件:行号 后 abort）★ 实测
//
// B. 容量 / 参数微调（9 个，括号里是默认值）：
//     CB_PATH_MAX                     (PATH_MAX)  路径缓冲上限，系统没有 PATH_MAX 时取 4096
//     CB_TIMER_MAX_DEPTH              (64)        计时器嵌套深度上限，统计表本身按需增长
//     CB_ARENA_REGION_INIT_CAPACITY   (64KB)      arena / temp 首块容量，写满自动追加新块 ★ 实测（改成 1KB）
//     CB_ARENA_ALIGN                  (alignof(max_align_t)) arena 分配对齐
//                                     ——cb.h 里是直接 #define 的，要改得改那处定义本身
//     CB_THREAD_LOCAL                 (_Thread_local，C++ 下 thread_local) 线程局部存储说明符，
//                                     单线程程序可以定义成空，省掉 TLS 访问开销
//     CB_DA_INIT_CAP                  (256)       动态数组首次扩容的容量 ★ 实测（改成 4）
//     CB_BITSET_WORD_BITS             (64)        位图一个字多少位，内部按 uint64_t 字存
//                                     ——同上，cb.h 里是直接 #define 的
//     CB_MAP_INIT_CAPACITY            (16)        HashMap 初始桶数，到负载上限后按 2 倍增长 ★ 实测（改成 4）
//     CB_WIN32_ERR_MSG_SIZE           (4KB)       Windows 错误信息缓冲大小，非 Windows 平台无效果
//
// C. 还有一个（1 个）：
//     CB_WARN_DEPRECATED              让 CB_DEPRECATED 真的展开成编译器的 deprecated 属性（默认关：
//                                     标了 deprecated 也不产生警告）
//
// 另有几个开关写在 cb.h 各自的小节里（不在上面的 17 个里）：CB_REALLOC / CB_FREE / CB_REALLOC_RAW /
// CB_FREE_RAW（内存分配收口）、CB_TEMP_CAPACITY（Arena / Temp Storage，默认跟随
// CB_ARENA_REGION_INIT_CAPACITY）、CB_ASSERT / CB_PANIC_BACKTRACE（Logger / Panic）。
//
// 编译运行（在仓库根目录）：
//     cc -o /tmp/ex14 examples/14_config_switches.c && /tmp/ex14

// handler 的签名里要用 size_t，所以先包含 <stddef.h>（cb.h 自己也会包含它）。
// cb.h 不必是第一个 include，先包含系统头是支持的用法。
#include <stddef.h>

// ① 去前缀：之后能写 temp_sprintf / String_View / SVLIT / da_append 这类短名字。
//    别名是"多加一套名字"，带前缀的名字照旧能用；不定义这个宏则一个别名都没有。
#define CB_STRIP_PREFIX

// ② 换掉默认的 OOM 处理器：先声明 handler + 定义宏，函数体写在 include 之后。
static void my_oom(size_t requested_size);
#define CB_OOM(size) my_oom(size)

// ③ 三个容量开关：故意定义成很小的值，运行时打印的容量变化就是它们在起作用。
#define CB_ARENA_REGION_INIT_CAPACITY (1024) // arena / temp 首块容量：默认 64KB
#define CB_DA_INIT_CAP 4                     // 动态数组首次扩容容量：默认 256
#define CB_MAP_INIT_CAPACITY 4               // HashMap 初始桶数：默认 16

#include "../cb.h"

// 处理器必须"不返回"：分配失败后调用方会继续用那个空指针。
// 长驻程序在这里写自己的降级 / 清理逻辑；本示例用一个好认的退出码结束进程。
static void my_oom(size_t requested_size)
{
    fprintf(stderr, "[my_oom] 分配 %zu 字节失败，在这里做自己的处理\n", requested_size);
    exit(42);
}

static void title(const char* text) { printf("\n== %s ==\n", text); }

// ---------------------------------------------------------------- 去前缀别名
static void demo_aliases(void)
{
    title("CB_STRIP_PREFIX：同一套接口的短名字");

    // 类型：CB_String_View -> String_View，CB_String_Builder -> String_Builder
    String_View path = SVLIT("examples/14_config_switches.c");
    printf("  String_View + SVLIT  = |" SV_FMT "|  count = %zu\n", SV_ARG(path), path.count);

    // 常量与宏：CB_ZERO -> ZERO，CB_ARRAY_LEN -> ARRAY_LEN
    String_Builder sb = ZERO;
    printf("  ARRAY_LEN(\"abc\")     = %zu\n", (size_t)ARRAY_LEN("abc"));

    // 函数：cb_temp_sprintf -> temp_sprintf，cb_sb_append* -> sb_append*
    char* out = temp_sprintf("%s/%s.%s", "build", "config", "c");
    sb_append_cstr(&sb, out);
    sb_appendf(&sb, " 共 %zu 个字符", sb.count);
    printf("  temp_sprintf         = %s\n", out);

    // StringBuilder 的 items 永远是合法的 C 字符串（count 不含结尾的 '\0'），可以直接 %s
    printf("  sb.items 直接打印     = |%s|\n", sb.items);
    sb_free(sb);

    // 容器：cb_File_Paths -> File_Paths，cb_da_* -> da_*
    File_Paths paths = ZERO;
    da_append(&paths, "a.c");
    da_append(&paths, "b.c");
    printf("  da_append            = count %zu，items[1] = %s\n", paths.count, paths.items[1]);
    da_free(paths);

    // 文件系统：cb_mkdir_if_not_exists -> mkdir_if_not_exists（中间层不存在会自己建）
    printf("  mkdir_if_not_exists  = %s\n",
           mkdir_if_not_exists("./build/examples/config_switches/dir/a/b") ? "成功" : "失败");

    // 别名就是 #define 到带前缀的名字：两套写法指向同一个函数
    printf("  两套名字是同一个函数  = %s\n",
           strcmp(temp_sprintf("x=%d", 1), cb_temp_sprintf("x=%d", 1)) == 0 ? "是" : "否");
}

// ---------------------------------------------------------------- 没有别名的名字
static void demo_names_without_alias(void)
{
    title("11 个名字刻意没有别名");

    printf("  去掉前缀会撞标准库 / 系统库 / 第三方库的名字，一律保留 cb_ / CB_ 前缀：\n");
    printf("    log  rename  glob     撞 libm 的 log() / stdio.h 的 rename() / POSIX glob.h 的 glob()\n");
    printf("    rotl64  rotr64        撞 C23 <stdbit.h> 与 glibc 的同名函数\n");
    printf("    PATH_MAX  ERROR       撞 limits.h 的 PATH_MAX / mingw <wingdi.h> 的 #define ERROR 0\n");
    printf("    min  max  clamp  swap 撞 Windows 的 min/max 宏与 C++ 标准库\n");
    printf("  所以这些能力只能写带前缀的名字（名单由 tests/strip_prefix.c 的编译期断言守着）：\n\n");

    printf("  cb_min(3, 7)         = %d（不能写 min）\n", cb_min(3, 7));
    printf("  cb_max(3, 7)         = %d\n", cb_max(3, 7));
    printf("  cb_clamp(15, 0, 10)  = %d\n", cb_clamp(15, 0, 10));
    printf("  cb_rotl64(1, 1)      = %llu\n", (unsigned long long)cb_rotl64(1, 1));
    printf("  CB_PATH_MAX          = %d（CB_PATH_MAX 是可覆盖的容量开关，默认取 PATH_MAX）\n", (int)CB_PATH_MAX);
    printf("  CB_ERROR             = %d（日志级别常量也一样：有 INFO / WARN，没有 ERROR）\n", (int)CB_ERROR);
    printf("  cb_glob_match(\"*.c\", \"main.c\") = %s\n", cb_glob_match("*.c", "main.c") ? "匹配" : "不匹配");

    // 对比：函数 cb_log 保留前缀（撞 libm 的 log()），它的级别常量却有别名
    cb_log(INFO, "  cb_log 保留前缀，级别常量 INFO 去掉了前缀");
}

// ---------------------------------------------------------------- 容量开关实测
static void demo_capacity_switches(void)
{
    title("容量开关：定义成小值，看容量怎么长");

    // CB_ARENA_REGION_INIT_CAPACITY：arena 的第一块就是这个大小（默认 64KB，本示例 1KB）
    CB_Arena arena = CB_ZERO;
    cb_arena_alloc(&arena, 16);
    printf("  CB_ARENA_REGION_INIT_CAPACITY = %d 字节（默认 64 * 1024）\n",
           (int)CB_ARENA_REGION_INIT_CAPACITY);
    printf("  arena 首块容量 = %zu 字节；写满自动追加新块，所以这是首块容量而不是总量上限\n",
           arena.begin->capacity);
    cb_arena_free(&arena);

    // CB_DA_INIT_CAP：动态数组第一次扩容就是这个容量（默认 256，本示例 4）
    CB_File_Paths paths = CB_ZERO;
    printf("\n  CB_DA_INIT_CAP = %d（默认 256），每 append 一个看一次 capacity：\n", (int)CB_DA_INIT_CAP);
    for (int i = 0; i < 5; ++i) {
        cb_da_append(&paths, "x.c");
        printf("    第 %d 个后：count = %zu，capacity = %zu\n", i + 1, paths.count, paths.capacity);
    }
    cb_da_free(paths);

    // CB_MAP_INIT_CAPACITY：HashMap 的第一个桶数（默认 16，本示例 4），到负载上限按 2 倍增长
    CB_Map map = CB_ZERO;
    const char* keys[] = {"a", "b", "c", "d", "e", "f"};
    printf("\n  CB_MAP_INIT_CAPACITY = %d（默认 16），边插边看 capacity 什么时候翻倍：\n",
           (int)CB_MAP_INIT_CAPACITY);
    size_t last_capacity = 0;
    for (size_t i = 0; i < CB_ARRAY_LEN(keys); ++i) {
        cb_map_put_cstr(&map, keys[i], (void*)(intptr_t)(i + 1));
        if (map.capacity != last_capacity) {
            printf("    插入第 %zu 个后 capacity = %zu\n", i + 1, map.capacity);
            last_capacity = map.capacity;
        }
    }
    void* value = NULL;
    if (cb_map_get_cstr(&map, "e", &value)) printf("  查回 \"e\" = %d\n", (int)(intptr_t)value);
    printf("  最终：count = %zu，capacity = %zu\n", cb_map_count(&map), map.capacity);
    cb_map_free(&map);
}

// ---------------------------------------------------------------- CB_OOM
static void demo_oom_override(void)
{
    title("CB_OOM：分配失败不再 abort，改走自己的处理器");

    printf("  本文件在 include 之前写了：\n");
    printf("      static void my_oom(size_t requested_size);\n");
    printf("      #define CB_OOM(size) my_oom(size)\n");
    printf("      #include \"cb.h\"\n");
    printf("  于是容器 / arena / temp 的分配失败都会调用 my_oom，而不是默认的 cb__oom。\n\n");

#if defined(_WIN32)
    printf("  Windows 下没有 fork，跳过真触发（处理器本身就是上面那个函数）\n");
#else
    // 真触发一次，但放到子进程里：处理器以 42 结束进程，父进程检查退出码，
    // 所以 ./cb examples 整体仍然正常结束（退出码 0）。
    pid_t pid = fork();
    if (pid < 0) {
        printf("  fork 失败，跳过真触发\n");
        return;
    }
    if (pid == 0) {
        // 子进程：cb_alloc_check(ptr, n) 是库在每次分配之后紧跟的那一步校验，
        // 传 NULL 就等价于分配失败——不必真的把内存耗光。
        void* p = NULL;
        cb_alloc_check(p, 4096); // 处理器不返回：my_oom 里 exit(42)
        exit(0);                 // 正常情况到不了这里
    }

    int status = 0;
    if (waitpid(pid, &status, 0) == pid && WIFEXITED(status)) {
        printf("  子进程退出码 = %d（来自 my_oom 的 exit(42)，说明覆盖生效了）\n", WEXITSTATUS(status));
    }
#endif

    printf("  真实运行中只要有一次分配失败就会走这个处理器；本示例为了能跑完，只在子进程里触发。\n");
}

// ---------------------------------------------------------------- 没有实测的开关
static void demo_other_switches(void)
{
    title("其余开关：为什么本文件没有实测");

    printf("  CB_ENABLE_ECHO —— 文件系统 / 命令操作打印提示信息，示例 02/03/04/05/10 都打开着。\n");
    printf("  CB_ALLOC_TRACK —— 每次分配带追踪头，示例 11 演示；./cb examples 正是用 -DCB_ALLOC_TRACK 编它。\n\n");

    printf("  CB_SHARED_STATE[_IMPL] —— 多 TU 共享全局状态（日志级别 / temp 栈 / timer 统计 / 追踪计数）。\n");
    printf("    恰好一个 TU 定义 CB_SHARED_STATE_IMPL（自动蕴含 CB_SHARED_STATE），其余 TU 定义\n");
    printf("    CB_SHARED_STATE；单文件里两者写在一起没有意义。用法见 docs/guide.md 第 0 节 ③。\n\n");

    printf("  CB_WIN32_ERR_MSG_SIZE —— 只在 Windows 下生效，其它平台看不出差别。\n");
    printf("  CB_ARENA_ALIGN / CB_BITSET_WORD_BITS —— 同样在 #include 之前定义即可覆盖\n");
    printf("    本身，所以这里只列出来、不 #define（定义了反而是重定义）。\n");
    printf("  CB_TIMER_MAX_DEPTH / CB_THREAD_LOCAL / CB_DONT_DELETE_OLD_CB /\n");
    printf("  CB_TRACE_CMD_RUN_FAIL_LOCATION / CB_WARN_DEPRECATED —— 要跑到对应场景才看得出差别\n");
    printf("    （计时器嵌套到上限、多线程、自重建、命令失败、用了标 deprecated 的接口），\n");
    printf("    按需在文件顶部或命令行定义即可。\n");
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    demo_aliases();
    demo_names_without_alias();
    demo_capacity_switches();
    demo_oom_override();
    demo_other_switches();

    printf("\n全部编译期开关示例执行完毕\n");
    return 0;
}
