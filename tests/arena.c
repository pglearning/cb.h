// arena / temp 实测：对齐、跨 region 完整性、快照回滚、reset 复用、free；输出确定性，可作 golden 基线
#include "cb.h"

int main(void)
{
    // ---------------------------------------------------------------- 1. 对齐
    CB_Arena a = {0};
    void* p1 = cb_arena_alloc(&a, 1);
    void* p8 = cb_arena_alloc(&a, 8);
    void* p64 = cb_arena_alloc_aligned(&a, 8, 64);
    printf("alloc(1) 非空且按 max_align_t 对齐 = %d\n",
           (int)(p1 != NULL && ((uintptr_t)p1 % CB_ARENA_ALIGN) == 0));
    printf("alloc(8) 按 max_align_t 对齐 = %d\n", (int)(((uintptr_t)p8 % CB_ARENA_ALIGN) == 0));
    printf("alloc_aligned(8, 64) 按 64 对齐 = %d\n", (int)(((uintptr_t)p64 % 64) == 0));

    // ------------------------------------------------- 2. 超过首块容量的大分配
    size_t big_size = 1024 * 1024; // 超过首块 64KB，会新开 region
    char* big = (char*)cb_arena_alloc(&a, big_size);
    memset(big, 0xAB, big_size);
    // 打印首/中/尾三个字节（期望 0xAB = 171）
    printf("1MB 分配的首/中/尾字节 = %u %u %u\n",
           (unsigned)(unsigned char)big[0],
           (unsigned)(unsigned char)big[big_size / 2],
           (unsigned)(unsigned char)big[big_size - 1]);

    // ------------------------------------- 3. 大量小分配跨 region 后内容不被踩
    enum { N = 20000 }; // 4B * 20000 = 80KB > 64KB，会跨块
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

    // ------------------------------------------------ 4. 跨 region 的快照回滚
    CB_Arena b = {0};
    cb_arena_alloc(&b, 100);
    CB_Arena_Mark mark = cb_arena_save(&b);
    void* after_mark = cb_arena_alloc(&b, 200000); // 又会新开一块
    cb_arena_rewind(&b, mark);
    void* reused = cb_arena_alloc(&b, 200000);
    printf("rewind 可跨 region 回滚并复用同一地址 = %d\n", (int)(reused == after_mark));

    // ---------------------------------------------------- 5. reset 复用首块
    cb_arena_reset(&b);
    void* first = cb_arena_alloc(&b, 16);
    printf("reset 后从首块起点重新分配 = %d\n", (int)(first == b.begin->data));

    // ------------------------------------------------------- 6. 字符串辅助函数
    char* s = cb_arena_sprintf(&a, "value=%d/%s", 42, "end");
    printf("arena_sprintf = %s\n", s);
    char* d = cb_arena_strndup(&a, "abcdef", 3);
    printf("arena_strndup(\"abcdef\", 3) = %s\n", d);

    // ------------------------------------------- 7. temp 与 arena 同内核但独立
    CB_Arena_Mark tm = cb_temp_save();
    char* t1 = cb_temp_strdup("temp-string");
    cb_temp_rewind(tm);
    char* t2 = cb_temp_strdup("temp-string");
    printf("temp 回滚后复用同一块存储 = %d\n", (int)(t1 == t2));
    printf("temp 内容 = %s\n", t2);

    // ------------------------------------------------------------ 8. 释放
    cb_arena_free(&a);
    cb_arena_free(&b);
    printf("arena_free 后 begin/end 均为 NULL = %d\n", (int)(b.begin == NULL && b.end == NULL));

    return 0;
}
