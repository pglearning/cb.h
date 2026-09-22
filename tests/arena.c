#include "test_diagnostics.h"
#include "cb.h"

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

    return 0;
}
