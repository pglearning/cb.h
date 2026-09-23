#include "test_diagnostics.h"
#include "cb.h"

// cb_arena_vsprintf()/cb_temp_vsprintf() take a va_list, so a variadic wrapper is the only way to
// call them from a test. This is the same shape cb_arena_sprintf()/cb_temp_sprintf() use.
static char* vsprintf_into_arena(CB_Arena* a, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    char* result = cb_arena_vsprintf(a, fmt, args);
    va_end(args);
    return result;
}

static char* vsprintf_into_temp(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    char* result = cb_temp_vsprintf(fmt, args);
    va_end(args);
    return result;
}

int main(void)
{

    CB_Arena a = {0};
    void* p1 = cb_arena_alloc(&a, 1);
    void* p8 = cb_arena_alloc(&a, 8);
    void* p64 = cb_arena_alloc_aligned(&a, 8, 64);
    printf("alloc(1) 非空且按 max_align_t 对齐 = %d\n",
           (int)(p1 != NULL && ((uintptr_t)p1 % CB_ARENA_ALIGN) == 0));
    printf("alloc(8) 按 max_align_t 对齐 = %d\n", (int)(((uintptr_t)p8 % CB_ARENA_ALIGN) == 0));
    printf("alloc_aligned(8, 64) 按 64 对齐 = %d\n", (int)(((uintptr_t)p64 % 64) == 0));

    size_t big_size = 1024 * 1024;
    char* big = (char*)cb_arena_alloc(&a, big_size);
    memset(big, 0xAB, big_size);

    printf("1MB 分配的首/中/尾字节 = %u %u %u\n",
           (unsigned)(unsigned char)big[0],
           (unsigned)(unsigned char)big[big_size / 2],
           (unsigned)(unsigned char)big[big_size - 1]);

    enum { N = 20000 };
    int* ptrs[N];
    for (int i = 0; i < N; ++i) {
        ptrs[i] = (int*)cb_arena_alloc(&a, sizeof(int));
        *ptrs[i] = i;
    }
    size_t corrupted = 0;
    for (int i = 0; i < N; ++i) {
        if (*ptrs[i] != i) corrupted += 1;
    }
    printf("20000 次小分配跨 region 后内容不一致的条数 = %zu\n", corrupted);

    CB_Arena b = {0};
    cb_arena_alloc(&b, 100);
    CB_Arena_Mark mark = cb_arena_save(&b);
    void* after_mark = cb_arena_alloc(&b, 200000);
    cb_arena_rewind(&b, mark);
    void* reused = cb_arena_alloc(&b, 200000);
    printf("rewind 可跨 region 回滚并复用同一地址 = %d\n", (int)(reused == after_mark));

    cb_arena_reset(&b);
    void* first = cb_arena_alloc(&b, 16);
    cb_arena_reset(&b);
    void* again = cb_arena_alloc(&b, 16);
    printf("reset 后地址稳定可复用首块 = %d\n", (int)(first == again));
    printf("首块起点之后仍满足对齐 = %d\n", (int)(((uintptr_t)first % CB_ARENA_ALIGN) == 0));

    char* s = cb_arena_sprintf(&a, "value=%d/%s", 42, "end");
    printf("arena_sprintf = %s\n", s);
    char* d = cb_arena_strndup(&a, "abcdef", 3);
    printf("arena_strndup(\"abcdef\", 3) = %s\n", d);

    CB_Arena_Mark tm = cb_temp_save();
    char* t1 = cb_temp_strdup("temp-string");
    cb_temp_rewind(tm);
    char* t2 = cb_temp_strdup("temp-string");
    printf("temp 回滚后复用同一块存储 = %d\n", (int)(t1 == t2));
    printf("temp 内容 = %s\n", t2);

    cb_arena_free(&a);
    cb_arena_free(&b);
    printf("arena_free 后 begin/end 均为 NULL = %d\n", (int)(b.begin == NULL && b.end == NULL));

    printf("\n== arena_realloc / arena_strdup / vsprintf 形式 ==\n");

    CB_Arena r = CB_ZERO;
    char* small = (char*)cb_arena_alloc(&r, 8);
    memcpy(small, "abcdefg", 8); // 7 字节 + '\0'
    void* grown = cb_arena_realloc(&r, small, 8, 64);
    printf("arena_realloc(8 -> 64) 非空且复制了旧内容 = %d\n",
           (int)(grown != NULL && memcmp(grown, "abcdefg", 8) == 0));
    printf("arena_realloc(8 -> 64) 返回了新地址（arena 不能原地增长） = %d\n",
           (int)(grown != (void*)small));
    void* shrunk = cb_arena_realloc(&r, grown, 64, 16);
    printf("arena_realloc(64 -> 16) 缩小直接返回原指针 = %d\n", (int)(shrunk == grown));
    void* fresh = cb_arena_realloc(&r, NULL, 0, 32);
    printf("arena_realloc(NULL, 0, 32) 新分配且非空 = %d\n", (int)(fresh != NULL));

    const char* dup_src = "arena-strdup";
    char* dup = cb_arena_strdup(&r, dup_src);
    printf("arena_strdup(\"%s\") = %s\n", dup_src, dup);
    printf("arena_strdup 是独立副本（与原串地址不同） = %d\n",
           (int)((const void*)dup != (const void*)dup_src));

    char* vfmt = vsprintf_into_arena(&r, "vsprintf:%d/%s", 7, "seven");
    printf("arena_vsprintf(\"vsprintf:%%d/%%s\", 7, \"seven\") = %s\n", vfmt);

    char* tvfmt = vsprintf_into_temp("temp_vsprintf:%d/%s", 9, "nine");
    printf("temp_vsprintf(\"temp_vsprintf:%%d/%%s\", 9, \"nine\") = %s\n", tvfmt);

    CB_Arena_Region* head_region = r.begin;
    printf("CB_Arena_Region: begin 非空 = %d, capacity 非零 = %d, count <= capacity = %d\n",
           (int)(head_region != NULL), (int)(head_region->capacity > 0),
           (int)(head_region->count <= head_region->capacity));
    printf("首个 region 的 data 区间包含第一块分配 = %d\n",
           (int)(small >= (char*)head_region->data && small < (char*)head_region->data + head_region->capacity));
    printf("小分配仍在首个 region 内（begin == end） = %d\n", (int)(r.begin == r.end));

    void* huge = cb_arena_alloc(&r, 4 * 1024 * 1024);
    printf("4MB 分配非空 = %d, 之后 region 链 begin->next 非空 = %d, end == begin->next = %d\n",
           (int)(huge != NULL), (int)(r.begin->next != NULL), (int)(r.end == r.begin->next));

    cb_arena_free(&r);
    printf("region 链已释放（begin/end 均为 NULL） = %d\n", (int)(r.begin == NULL && r.end == NULL));

    return 0;
}
