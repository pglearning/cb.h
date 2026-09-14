// HashMap（字符串键 + uint64 键）的实测
// 覆盖：插入/覆盖/查找/删除/墓碑复用/扩容/遍历/预分配/arena 后端/大数据量
#include "cb.h"

static void test_map_basic(void)
{
    printf("\n== CB_Map 基本操作 ==\n");

    CB_Map m = CB_ZERO;
    void* v = NULL;

    printf("空 map：count = %zu，查 nope -> %d\n", cb_map_count(&m), (int)cb_map_get_cstr(&m, "nope", &v));

    cb_map_put_cstr(&m, "answer", (void*)(intptr_t)42);
    cb_map_put_cstr(&m, "pi", (void*)(intptr_t)314);
    printf("插入两条后 count = %zu\n", m.count);

    // get 前把 v 置空，避免查不到时打出上一次的残留值
    v = NULL;
    bool got_answer = cb_map_get_cstr(&m, "answer", &v);
    printf("get answer -> %d，值 = %lld\n", (int)got_answer, (long long)(intptr_t)v);

    v = NULL;
    bool got_pi = cb_map_get_cstr(&m, "pi", &v);
    printf("get pi -> %d，值 = %lld\n", (int)got_pi, (long long)(intptr_t)v);

    v = NULL;
    printf("get 不存在的键 e -> %d\n", (int)cb_map_get_cstr(&m, "e", &v));
    printf("has(pi) -> %d\n", (int)cb_map_has(&m, cb_sv_from_cstr("pi")));

    // 覆盖：count 不应增长
    cb_map_put_cstr(&m, "answer", (void*)(intptr_t)7);
    v = NULL;
    bool got_overwritten = cb_map_get_cstr(&m, "answer", &v);
    printf("同键覆盖后 count = %zu，get answer -> %d，值 = %lld\n", m.count, (int)got_overwritten,
           (long long)(intptr_t)v);

    // 键被复制：改掉原字符串不影响 map
    char buf[16];
    memcpy(buf, "tempkey", 8);
    cb_map_put_cstr(&m, buf, (void*)(intptr_t)1);
    memset(buf, 'X', 7);
    v = NULL;
    bool got_copied = cb_map_get_cstr(&m, "tempkey", &v);
    printf("键被复制（原串已改）后 get tempkey -> %d，值 = %lld\n", (int)got_copied, (long long)(intptr_t)v);

    // 空串也是合法键
    cb_map_put_cstr(&m, "", (void*)(intptr_t)9);
    v = NULL;
    bool got_empty = cb_map_get_cstr(&m, "", &v);
    printf("空串作为键：get \"\" -> %d，值 = %lld\n", (int)got_empty, (long long)(intptr_t)v);

    // 带 NUL 的二进制键
    cb_map_put(&m, cb_sv_from_parts("a\0b", 3), (void*)(intptr_t)5);
    v = NULL;
    bool got_nul = cb_map_get(&m, cb_sv_from_parts("a\0b", 3), &v);
    printf("含 NUL 的键：get(\"a\\0b\", 3) -> %d，值 = %lld\n", (int)got_nul, (long long)(intptr_t)v);
    printf("前缀不同的键不相等（has(\"a\")）-> %d\n", (int)cb_map_has(&m, cb_sv_from_cstr("a")));

    // 值可以是 NULL
    cb_map_put_cstr(&m, "nullval", NULL);
    printf("值为 NULL 也能查到存在（has(nullval)）-> %d\n",
           (int)cb_map_has(&m, cb_sv_from_cstr("nullval")));

    cb_map_free(&m);
    printf("free 清空句柄 slots==NULL/capacity==0/count==0 = %d/%d/%d\n", (int)(m.slots == NULL),
           (int)(m.capacity == 0), (int)(m.count == 0));
}

static void test_map_delete(void)
{
    printf("\n== CB_Map 删除与墓碑 ==\n");

    CB_Map m = CB_ZERO;
    void* v = NULL;

    enum { N = 200 };
    char keys[N][16];
    for (int i = 0; i < N; ++i) {
        snprintf(keys[i], sizeof(keys[i]), "key%d", i);
        cb_map_put_cstr(&m, keys[i], (void*)(intptr_t)(i + 1));
    }
    printf("插入 200 条后 count = %zu\n", m.count);

    // 删掉一半
    bool del_all_ok = true;
    for (int i = 0; i < N; i += 2) {
        if (!cb_map_del(&m, cb_sv_from_cstr(keys[i]))) {
            del_all_ok = false;
            break;
        }
    }
    printf("删除一半全部成功 = %d，删除后 count = %zu\n", (int)del_all_ok, m.count);

    size_t survivors_bad = 0;
    for (int i = 0; i < N; ++i) {
        bool found = cb_map_get_cstr(&m, keys[i], &v);
        if (i % 2 == 0) {
            if (found) survivors_bad += 1; // 应已删除
        } else {
            if (!found || (intptr_t)v != i + 1) survivors_bad += 1; // 应还在且值正确
        }
    }
    printf("删除后存活项中该删没删/值不对的条数 = %zu\n", survivors_bad);

    // 删除后再插满：墓碑应被复用，count 正确
    for (int i = 0; i < N; i += 2) {
        cb_map_put_cstr(&m, keys[i], (void*)(intptr_t)(i + 1000));
    }
    printf("复用墓碑重新插满后 count = %zu\n", m.count);

    size_t refilled_bad = 0;
    for (int i = 0; i < N; ++i) {
        intptr_t expect = (i % 2 == 0) ? (intptr_t)(i + 1000) : (intptr_t)(i + 1);
        if (!cb_map_get_cstr(&m, keys[i], &v) || (intptr_t)v != expect) refilled_bad += 1;
    }
    printf("重新插入后值不对的条数 = %zu\n", refilled_bad);

    printf("删不存在的键 -> %d\n", (int)cb_map_del(&m, cb_sv_from_cstr("no-such-key")));

    cb_map_clear(&m);
    printf("clear 后 count = %zu，capacity = %zu（保留容量）\n", m.count, m.capacity);
    v = NULL;
    printf("clear 后查不到 keys[1] -> %d\n", (int)cb_map_get_cstr(&m, keys[1], &v));

    cb_map_free(&m);
}

static void test_map_iter(void)
{
    printf("\n== CB_Map 遍历与预分配 ==\n");

    CB_Map m = CB_ZERO;
    cb_map_init_capacity(&m, 1000);
    // 预分配会按 3/4 负载因子留余量，再向上取到 2 的幂
    printf("init_capacity(1000) 后 capacity = %zu（>= 1000*4/3 = %d）\n", m.capacity,
           (int)(m.capacity >= 1000 * 4 / 3));
    size_t capacity_before = m.capacity;

    enum { N = 500 };
    for (int i = 0; i < N; ++i) {
        char key[16];
        snprintf(key, sizeof(key), "k%d", i);
        cb_map_put_cstr(&m, key, (void*)(intptr_t)i);
    }
    // 预分配足够，插 500 条不应触发扩容
    printf("插入 500 条后 capacity = %zu（与预分配相同 = %d）\n", m.capacity,
           (int)(m.capacity == capacity_before));

    size_t visited = 0;
    int64_t sum = 0;
    cb_map_foreach(&m, it)
    {
        visited += 1;
        sum += (int64_t)(intptr_t)it.value;
        (void)it.key;
    }
    printf("遍历访问到 %zu 条，值总和 = %lld\n", visited, (long long)sum);

    // 遍历空 map
    CB_Map empty = CB_ZERO;
    visited = 0;
    cb_map_foreach(&empty, it2) { CB_UNUSED(it2); visited += 1; }
    printf("遍历空 map 访问到 %zu 条\n", visited);

    cb_map_free(&m);
}

static void test_map_grow(void)
{
    printf("\n== CB_Map 扩容 ==\n");

    CB_Map m = CB_ZERO;
    void* v = NULL;

    // 远超初始容量，触发多次 rehash
    enum { N = 5000 };
    char** keys = (char**)cb_temp_alloc(N * sizeof(char*));
    for (int i = 0; i < N; ++i) {
        char* key = cb_temp_sprintf("long-key-%d-%s", i, "padding-padding-padding");
        keys[i] = key;
        cb_map_put_cstr(&m, key, (void*)(intptr_t)(i * 3));
    }
    printf("插入 5000 条后 count = %zu，capacity = %zu（>= 5000*4/3 = %d）\n", m.count, m.capacity,
           (int)(m.capacity >= N * 4 / 3));

    size_t bad = 0;
    for (int i = 0; i < N; ++i) {
        v = NULL;
        if (!cb_map_get_cstr(&m, keys[i], &v) || (intptr_t)v != (intptr_t)(i * 3)) bad += 1;
    }
    printf("扩容后查不到/值不对的条数 = %zu\n", bad);

    cb_map_free(&m);
}

static void test_map_arena(void)
{
    printf("\n== CB_Map arena 后端 ==\n");

    CB_Arena arena = CB_ZERO;
    CB_Map m = CB_ZERO;
    cb_map_init_arena(&m, &arena, 0);

    void* v = NULL;
    for (int i = 0; i < 200; ++i) {
        char key[16];
        snprintf(key, sizeof(key), "a%d", i);
        cb_map_put_cstr(&m, key, (void*)(intptr_t)i);
    }
    printf("arena 后端插入 200 条后 count = %zu\n", m.count);
    printf("内存确实来自 arena（arena.begin != NULL）= %d\n", (int)(arena.begin != NULL));

    v = NULL;
    bool got = cb_map_get_cstr(&m, "a77", &v);
    printf("arena 后端 get a77 -> %d，值 = %lld\n", (int)got, (long long)(intptr_t)v);

    // arena 模式下 free 只是摘掉句柄，内存由 arena 统一回收
    cb_map_free(&m);
    printf("free 摘掉 arena 句柄 slots==NULL/arena==NULL = %d/%d\n", (int)(m.slots == NULL),
           (int)(m.arena == NULL));
    cb_arena_free(&arena);
}

static void test_map_u64(void)
{
    printf("\n== CB_Map_U64 ==\n");

    CB_Map_U64 m = CB_ZERO;
    void* v = NULL;

    cb_map_u64_put(&m, 0, (void*)(intptr_t)100);
    cb_map_u64_put(&m, UINT64_MAX, (void*)(intptr_t)200);
    cb_map_u64_put(&m, 12345678901234567890ull, (void*)(intptr_t)300);
    printf("插入 3 条（含 0 与 UINT64_MAX）后 count = %zu\n", m.count);

    v = NULL;
    bool got_zero = cb_map_u64_get(&m, 0, &v);
    printf("get(0) -> %d，值 = %lld\n", (int)got_zero, (long long)(intptr_t)v);

    v = NULL;
    bool got_max = cb_map_u64_get(&m, UINT64_MAX, &v);
    printf("get(UINT64_MAX) -> %d，值 = %lld\n", (int)got_max, (long long)(intptr_t)v);

    v = NULL;
    bool got_big = cb_map_u64_get(&m, 12345678901234567890ull, &v);
    printf("get(大整数) -> %d，值 = %lld\n", (int)got_big, (long long)(intptr_t)v);

    v = NULL;
    printf("get 不存在的键 1 -> %d\n", (int)cb_map_u64_get(&m, 1, &v));

    cb_map_u64_put(&m, 0, (void*)(intptr_t)999);
    v = NULL;
    bool got_overwritten = cb_map_u64_get(&m, 0, &v);
    printf("同键覆盖后 count = %zu，get(0) -> %d，值 = %lld\n", m.count, (int)got_overwritten,
           (long long)(intptr_t)v);

    bool del_zero = cb_map_u64_del(&m, 0);
    printf("删除 key 0 -> %d，删后 has(0) -> %d\n", (int)del_zero, (int)cb_map_u64_has(&m, 0));

    // 顺序整数键不应扎堆：容量应接近条目数
    CB_Map_U64 seq = CB_ZERO;
    for (uint64_t i = 0; i < 1000; ++i) cb_map_u64_put(&seq, i, (void*)(intptr_t)i);
    printf("顺序键插入 1000 条后 count = %zu\n", seq.count);

    size_t seq_bad = 0;
    for (uint64_t i = 0; i < 1000; ++i) {
        v = NULL;
        if (!cb_map_u64_get(&seq, i, &v) || (intptr_t)v != (intptr_t)i) seq_bad += 1;
    }
    printf("顺序键查不到/值不对的条数 = %zu\n", seq_bad);
    printf("splitmix64 打散后 capacity = %zu（<= 4096 = %d）\n", seq.capacity, (int)(seq.capacity <= 4096));

    size_t visited = 0;
    cb_map_u64_foreach(&seq, it)
    {
        visited += 1;
        (void)it.key;
        (void)it.value;
    }
    printf("u64 map 遍历到 %zu 条\n", visited);

    cb_map_u64_free(&seq);
    cb_map_u64_free(&m);
    printf("u64 map free 后 slots == NULL = %d\n", (int)(m.slots == NULL));
}

int main(void)
{
    test_map_basic();
    test_map_delete();
    test_map_iter();
    test_map_grow();
    test_map_arena();
    test_map_u64();

    return 0;
}
