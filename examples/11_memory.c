// 示例 11：内存管理
//
// 覆盖：CB_Arena（显式作用域分配器）、cb_temp_*（不用管内存的临时分配）、
//       CB_OOM（分配失败的统一出口）、CB_ALLOC_TRACK（内存追踪与泄漏定位）、
//       CB_REALLOC / CB_FREE（替换分配出口）
//
// 编译运行（在仓库根目录）：
//     cc -o /tmp/ex11 examples/11_memory.c && /tmp/ex11
//     cc -DCB_ALLOC_TRACK -o /tmp/ex11 examples/11_memory.c && /tmp/ex11   # 打开追踪
//
// 注意 #include "cb.h" 必须是第一个 include。

#include "../cb.h"

static void title(const char* text) { printf("\n== %s ==\n", text); }

// ---------------------------------------------------------------- Arena
static void demo_arena(void)
{
    title("CB_Arena：分块增长的作用域分配器");

    CB_Arena arena = CB_ZERO;
    printf("  刚建好：begin=%s（还没分配任何块）\n", arena.begin == NULL ? "NULL" : "非空");

    // 分配：纯指针加法，没有逐次 malloc
    char* s = cb_arena_sprintf(&arena, "value=%d", 42);
    int* xs = (int*)cb_arena_alloc(&arena, 10 * sizeof(int));
    for (int i = 0; i < 10; ++i) xs[i] = i * i;
    printf("  分配后：begin=%s，字符串=\"%s\"，xs[9]=%d\n",
           arena.begin != NULL ? "非空" : "NULL", s, xs[9]);

    // 高对齐分配
    void* aligned = cb_arena_alloc_aligned(&arena, 64, 64);
    printf("  64 字节对齐分配：地址 %% 64 == %zu\n", (size_t)((uintptr_t)aligned % 64));

    // 字符串辅助
    char* dup = cb_arena_strdup(&arena, "copied");
    char* part = cb_arena_strndup(&arena, "abcdef", 3);
    printf("  strdup=\"%s\"，strndup=\"%s\"\n", dup, part);

    // 快照与回滚：O(1)，可跨越多个块
    CB_Arena_Mark mark = cb_arena_save(&arena);
    size_t before = arena.begin->count;
    for (int i = 0; i < 100; ++i) cb_arena_alloc(&arena, 1000);
    printf("  打了检查点后疯狂分配 100 次，第一块已用 %zu 字节（检查点处是 %zu）\n",
           arena.begin->count, before);
    cb_arena_rewind(&arena, mark);
    printf("  回滚后第一块已用 %zu 字节，恢复到检查点状态：%s\n",
           arena.begin->count, before == arena.begin->count ? "是" : "否");
    printf("  回滚前的字符串仍然有效：\"%s\"\n", s); // 回滚点之前的分配不受影响

    // realloc：arena 无法原地扩容，会新分配 + 拷贝并返回新指针
    void* grown = cb_arena_realloc(&arena, xs, 10 * sizeof(int), 20 * sizeof(int));
    printf("  arena_realloc 返回%s指针（arena 不支持原地扩容）\n", grown == xs ? "同一个" : "新的");

    // reset 与 free 的区别
    cb_arena_reset(&arena);
    printf("  reset 后：begin=%s（内存保留复用，不还给系统），第一块已用 %zu 字节\n",
           arena.begin != NULL ? "非空" : "NULL", arena.begin != NULL ? arena.begin->count : 0);
    cb_arena_free(&arena);
    printf("  free 后：begin=%s（真正还给系统）\n", arena.begin == NULL ? "NULL" : "非空");
}

// ---------------------------------------------------------------- Temp
static void demo_temp(void)
{
    title("cb_temp_*：arena 的线程局部实例，不用管内存");

    // 直接用，不需要初始化、不需要句柄
    char* path = cb_temp_sprintf("%s/%s.%s", "src", "main", "c");
    char* copy = cb_temp_strdup("duplicated");
    char* slice = cb_temp_strndup("abcdef", 3);
    printf("  temp_sprintf=\"%s\"，strdup=\"%s\"，strndup=\"%s\"\n", path, copy, slice);

    // 检查点：循环里每轮回收，内存不随循环增长
    CB_Arena_Mark mark = cb_temp_save();
    for (int i = 0; i < 3; ++i) {
        char* scratch = cb_temp_sprintf("迭代 #%d 的临时字符串", i);
        printf("  %s\n", scratch);
        cb_temp_rewind(mark);
    }
    printf("  循环结束，temp 用量回到检查点（不会累积）\n");

    // 检查点之前的分配不受回滚影响
    printf("  第一段分配的 path 仍然有效：%s\n", path);

    cb_temp_reset(); // 复位整个 temp 栈（保留已分配块）
    printf("  temp_reset 后仍可继续分配：%s\n", cb_temp_sprintf("reset 之后"));
}

// ---------------------------------------------------------------- 内存追踪
static void demo_alloc_track(void)
{
    title("CB_ALLOC_TRACK：内存追踪与泄漏定位");

#ifdef CB_ALLOC_TRACK
    printf("  已开启追踪\n");

    // 故意泄漏一块：报告里会精确指出这一行
    char* leaked = (char*)CB_REALLOC(NULL, 1234);
    memset(leaked, 0, 1234);

    {   // 这块是正常的，会被释放
        CB_String_Builder sb = CB_ZERO;
        cb_sb_appendf(&sb, "temporary string builder");
        cb_sb_free(sb);
    }

    {   // arena 的分配也走同一收口，所以也会被统计
        CB_Arena a = CB_ZERO;
        cb_arena_sprintf(&a, "arena allocation");
        cb_arena_free(&a);
    }

    printf("  当前存活 %zu 块 / %zu 字节\n", cb_alloc_live_count(), cb_alloc_live_size());

    // temp 是 _Thread_local、生命周期到进程结束，属于常驻内存而不是泄漏
    printf("  ---- 报告如下（泄漏会精确到 文件:行号）----\n");
    cb_alloc_report();
    printf("  ---- 报告结束 ----\n");
#else
    printf("  未开启。用 -DCB_ALLOC_TRACK 重新编译本示例即可看到泄漏报告：\n");
    printf("      cc -DCB_ALLOC_TRACK -o ex11 examples/11_memory.c && ./ex11\n");
#endif
}

// ---------------------------------------------------------------- 替换分配出口
static void demo_realloc_hook(void)
{
    title("CB_REALLOC / CB_FREE：全库唯一的内存出口");

    printf("  全库只经这两个宏分配与释放，所以在 include 之前定义它们就能接管全部内存行为：\n\n");
    printf("      static size_t g_total = 0;\n");
    printf("      static void* my_realloc(void* p, size_t n) { g_total += n; return realloc(p, n); }\n");
    printf("      static void  my_free(void* p) { free(p); }\n");
    printf("      #define CB_REALLOC(ptr, size) my_realloc((ptr), (size))\n");
    printf("      #define CB_FREE(ptr)          my_free(ptr)\n");
    printf("      #include \"cb.h\"\n\n");
    printf("  可替换的出口还有：\n");
    printf("    CB_REALLOC_RAW / CB_FREE_RAW —— 追踪层自身用的底层出口\n");
    printf("    CB_OOM(size)                 —— 分配失败的处理（默认打印后 abort）\n");
    printf("    CB_TEMP_CAPACITY             —— temp 首块容量（默认 64KB，不够会自动追加）\n");
}

// ---------------------------------------------------------------- 分配失败
static void demo_oom(void)
{
    title("CB_OOM：分配失败的统一出口");

    printf("  容器 / arena / temp 的分配失败无法优雅上报（宏没有返回值可传），\n");
    printf("  所以统一走 CB_OOM：打印 文件:行号 与请求字节数，然后 abort。\n\n");
    printf("  它刻意不用 assert —— assert 会被 -DNDEBUG 关掉，而构建工具通常正是\n");
    printf("  release 编译的；关掉之后 OOM 会静默变成空指针崩溃，且崩在哪也说不清。\n\n");
    printf("  能优雅失败的 API（读文件 / 起进程 / cmd）仍然照常返回 false。\n");
    printf("  本示例不真的触发 OOM（那会 abort 掉进程）。\n\n");
    printf("  想自己接管（长驻程序里不要 abort）就在 include 之前顶掉它，不用改 cb.h：\n\n");
    printf("      static void my_oom(size_t size);   // 要先声明：cb.h 里的函数体会调用它\n");
    printf("      #define CB_OOM(size) my_oom(size)\n");
    printf("      #include \"cb.h\"\n");
}

// vsprintf 变体：当你自己有一个变参函数，需要把 va_list 转发给 cb.h 时用它
static char* my_format(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    char* result = cb_temp_vsprintf(fmt, args); // 走 temp
    va_end(args);
    return result;
}

static char* my_format_arena(CB_Arena* arena, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    char* result = cb_arena_vsprintf(arena, fmt, args); // 走指定 arena
    va_end(args);
    return result;
}

static void demo_vsprintf(void)
{
    title("cb_temp_vsprintf / cb_arena_vsprintf：转发已有的 va_list");

    printf("  自己包装的变参函数 -> temp: %s\n", my_format("%s=%d/%s", "key", 42, "end"));

    CB_Arena a = CB_ZERO;
    printf("  自己包装的变参函数 -> arena: %s\n", my_format_arena(&a, "%s-%d", "arena", 7));
    cb_arena_free(&a);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    demo_arena();
    demo_temp();
    demo_vsprintf();
    demo_alloc_track();
    demo_realloc_hook();
    demo_oom();

    printf("\n全部内存示例执行完毕\n");
    return 0;
}
