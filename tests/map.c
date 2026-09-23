#include "test_diagnostics.h"
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

    v = NULL;
    bool got_answer = cb_map_get_cstr(&m, "answer", &v);
    printf("get answer -> %d，值 = %lld\n", (int)got_answer, (long long)(intptr_t)v);

    v = NULL;
    bool got_pi = cb_map_get_cstr(&m, "pi", &v);
    printf("get pi -> %d，值 = %lld\n", (int)got_pi, (long long)(intptr_t)v);

    v = NULL;
    printf("get 不存在的键 e -> %d\n", (int)cb_map_get_cstr(&m, "e", &v));
    printf("has(pi) -> %d\n", (int)cb_map_has(&m, cb_sv_from_cstr("pi")));

    cb_map_put_cstr(&m, "answer", (void*)(intptr_t)7);
    v = NULL;
    bool got_overwritten = cb_map_get_cstr(&m, "answer", &v);
    printf("同键覆盖后 count = %zu，get answer -> %d，值 = %lld\n", m.count, (int)got_overwritten,
           (long long)(intptr_t)v);

    char buf[16];
    memcpy(buf, "tempkey", 8);
    cb_map_put_cstr(&m, buf, (void*)(intptr_t)1);
    memset(buf, 'X', 7);
    v = NULL;
    bool got_copied = cb_map_get_cstr(&m, "tempkey", &v);
    printf("键被复制（原串已改）后 get tempkey -> %d，值 = %lld\n", (int)got_copied, (long long)(intptr_t)v);

    cb_map_put_cstr(&m, "", (void*)(intptr_t)9);
    v = NULL;
    bool got_empty = cb_map_get_cstr(&m, "", &v);
    printf("空串作为键：get \"\" -> %d，值 = %lld\n", (int)got_empty, (long long)(intptr_t)v);

    cb_map_put(&m, cb_sv_from_parts("a\0b", 3), (void*)(intptr_t)5);
    v = NULL;
    bool got_nul = cb_map_get(&m, cb_sv_from_parts("a\0b", 3), &v);
    printf("含 NUL 的键：get(\"a\\0b\", 3) -> %d，值 = %lld\n", (int)got_nul, (long long)(intptr_t)v);
    printf("前缀不同的键不相等（has(\"a\")）-> %d\n", (int)cb_map_has(&m, cb_sv_from_cstr("a")));

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
            if (found) survivors_bad += 1;
        } else {
            if (!found || (intptr_t)v != i + 1) survivors_bad += 1;
        }
    }
    printf("删除后存活项中该删没删/值不对的条数 = %zu\n", survivors_bad);

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

    printf("init_capacity(1000) 后 capacity = %zu（>= 1000*4/3 = %d）\n", m.capacity,
           (int)(m.capacity >= 1000 * 4 / 3));
    size_t capacity_before = m.capacity;

    enum { N = 500 };
    for (int i = 0; i < N; ++i) {
        char key[16];
        snprintf(key, sizeof(key), "k%d", i);
        cb_map_put_cstr(&m, key, (void*)(intptr_t)i);
    }

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

// cb_map_foreach 展开成同一个 for 循环；这里手工调用 cb_map_iter/cb_map_next，
// 并顺带按状态数一遍 CB_Map_Slot 数组（CB_MAP_EMPTY/CB_MAP_USED/CB_MAP_TOMBSTONE）。
static void test_map_manual_iter(void)
{
    printf("\n== CB_Map 手工迭代与槽位状态 ==\n");

    CB_Map m = CB_ZERO;
    for (int i = 0; i < 8; ++i) {
        char key[8];
        snprintf(key, sizeof(key), "mk%d", i);
        cb_map_put_cstr(&m, key, (void*)(intptr_t)(i * 10));
    }

    printf("CB_MAP_EMPTY = %d, CB_MAP_USED = %d, CB_MAP_TOMBSTONE = %d\n", (int)CB_MAP_EMPTY,
           (int)CB_MAP_USED, (int)CB_MAP_TOMBSTONE);

    CB_Map_Iter it = cb_map_iter(&m);
    printf("cb_map_iter 之后 it.index = %zu，it.map 指向原 map = %d\n", it.index, (int)(it.map == &m));

    size_t visited = 0;
    int64_t sum = 0;
    bool keys_ok = true;
    while (cb_map_next(&it)) {
        visited += 1;
        sum += (int64_t)(intptr_t)it.value;
        // it.key 指向 map 自己持有的键拷贝，不是插入时用的那份
        if (it.key.data == NULL || it.key.count == 0) keys_ok = false;
    }
    printf("cb_map_next 循环访问到 %zu 条，值总和 = %lld，每个键都非空 = %d\n", visited, (long long)sum,
           (int)keys_ok);
    printf("与 cb_map_count 一致 = %d，遍历结束后 it.index = %zu\n", (int)(visited == cb_map_count(&m)),
           it.index);

    size_t used = 0;
    size_t tombstones = 0;
    size_t empty = 0;
    for (size_t i = 0; i < m.capacity; ++i) {
        CB_Map_Slot* slot = &m.slots[i];
        switch (slot->state) {
        case CB_MAP_EMPTY:
            empty += 1;
            break;
        case CB_MAP_USED:
            used += 1;
            break;
        case CB_MAP_TOMBSTONE:
            tombstones += 1;
            break;
        default:
            break;
        }
    }
    printf("槽位状态统计（capacity = %zu）：USED = %zu，TOMBSTONE = %zu，EMPTY = %zu\n", m.capacity, used,
           tombstones, empty);

    cb_map_del(&m, cb_sv_from_cstr("mk3"));
    used = 0;
    tombstones = 0;
    empty = 0;
    for (size_t i = 0; i < m.capacity; ++i) {
        CB_Map_Slot* slot = &m.slots[i];
        if (slot->state == CB_MAP_USED) used += 1;
        if (slot->state == CB_MAP_TOMBSTONE) tombstones += 1;
        if (slot->state == CB_MAP_EMPTY) empty += 1;
    }
    printf("删除一条后：count = %zu，USED = %zu，TOMBSTONE = %zu，EMPTY = %zu\n", cb_map_count(&m), used,
           tombstones, empty);

    cb_map_free(&m);
}

// 两个哈希都是纯函数（FNV-1a 64 与 splitmix64 终混），打印固定值即可当回归基线。
static void test_map_hashes(void)
{
    printf("\n== cb_hash_bytes / cb_hash_u64（固定值）==\n");

    printf("cb_hash_u64(0) = %llu\n", (unsigned long long)cb_hash_u64(0));
    printf("cb_hash_u64(1) = %llu\n", (unsigned long long)cb_hash_u64(1));
    printf("cb_hash_u64(UINT64_MAX) = %llu\n", (unsigned long long)cb_hash_u64(UINT64_MAX));
    printf("相邻键被打散（hash(1) != hash(2)）= %d\n", (int)(cb_hash_u64(1) != cb_hash_u64(2)));

    printf("cb_hash_bytes(\"\", 0) = %llu\n", (unsigned long long)cb_hash_bytes("", 0));
    printf("cb_hash_bytes(\"hello\", 5) = %llu\n", (unsigned long long)cb_hash_bytes("hello", 5));
    printf("长度参与哈希（4 字节与 5 字节不同）= %d\n",
           (int)(cb_hash_bytes("hello", 4) != cb_hash_bytes("hello", 5)));
    printf("NUL 也是数据（\"a\\0b\" 与 \"a\" 不同）= %d\n",
           (int)(cb_hash_bytes("a\0b", 3) != cb_hash_bytes("a", 1)));
}

// u64 map 的两条预分配路径：init_capacity（堆）与 init_arena（arena 后端），
// 外加 cb_map_u64_count / cb_map_u64_clear / 手工 cb_map_u64_iter 循环。
static void test_map_u64_backends(void)
{
    printf("\n== CB_Map_U64 预分配 / arena / count / clear ==\n");

    CB_Map_U64 m = CB_ZERO;
    void* v = NULL;

    cb_map_u64_init_capacity(&m, 1000);
    size_t capacity_before = m.capacity;
    printf("cb_map_u64_init_capacity(1000) 后 capacity = %zu（>= 1000*4/3 = %d）\n", m.capacity,
           (int)(m.capacity >= 1000 * 4 / 3));

    for (uint64_t i = 0; i < 500; ++i) cb_map_u64_put(&m, i, (void*)(intptr_t)i);
    printf("插入 500 条后 cb_map_u64_count = %zu，capacity 未增长 = %d\n", cb_map_u64_count(&m),
           (int)(m.capacity == capacity_before));

    size_t bad = 0;
    for (uint64_t i = 0; i < 500; ++i) {
        v = NULL;
        if (!cb_map_u64_get(&m, i, &v) || (intptr_t)v != (intptr_t)i) bad += 1;
    }
    printf("预分配后查不到/值不对的条数 = %zu\n", bad);

    cb_map_u64_clear(&m);
    printf("cb_map_u64_clear 后 count = %zu，capacity = %zu（保留容量）\n", cb_map_u64_count(&m), m.capacity);
    v = NULL;
    printf("clear 后查不到 key 1 -> %d\n", (int)cb_map_u64_get(&m, 1, &v));

    cb_map_u64_put(&m, 7, (void*)(intptr_t)77);
    v = NULL;
    bool reused = cb_map_u64_get(&m, 7, &v);
    printf("clear 后复用同一张表：get(7) -> %d，值 = %lld\n", (int)reused, (long long)(intptr_t)v);

    cb_map_u64_free(&m);
    printf("free 之后再查 -> %d，slots == NULL = %d\n", (int)cb_map_u64_get(&m, 7, &v),
           (int)(m.slots == NULL));

    CB_Map_U64 empty = CB_ZERO;
    printf("全零（CB_ZERO）map：cb_map_u64_count = %zu，capacity = %zu\n", cb_map_u64_count(&empty),
           empty.capacity);

    CB_Arena arena = CB_ZERO;
    CB_Map_U64 am = CB_ZERO;
    cb_map_u64_init_arena(&am, &arena, 0);
    for (uint64_t i = 0; i < 200; ++i) cb_map_u64_put(&am, i * 2, (void*)(intptr_t)i);
    printf("arena 后端插入 200 条后 count = %zu，capacity 是 2 的幂 = %d\n", cb_map_u64_count(&am),
           (int)((am.capacity & (am.capacity - 1)) == 0));
    printf("内存确实来自 arena（arena.begin != NULL）= %d\n", (int)(arena.begin != NULL));

    v = NULL;
    bool got = cb_map_u64_get(&am, 200, &v);
    printf("arena 后端 get(200) -> %d，值 = %lld\n", (int)got, (long long)(intptr_t)v);

    cb_map_u64_free(&am);
    printf("free 摘掉 arena 句柄 slots==NULL/arena==NULL = %d/%d\n", (int)(am.slots == NULL),
           (int)(am.arena == NULL));
    cb_arena_free(&arena);

    CB_Map_U64 seq = CB_ZERO;
    for (uint64_t i = 1; i <= 6; ++i) cb_map_u64_put(&seq, i, (void*)(intptr_t)(i * 3));

    CB_Map_U64_Iter it = cb_map_u64_iter(&seq);
    printf("cb_map_u64_iter 之后 it.index = %zu，it.map 指向原 map = %d\n", it.index, (int)(it.map == &seq));

    size_t visited = 0;
    unsigned long long key_sum = 0;
    unsigned long long value_sum = 0;
    while (cb_map_u64_next(&it)) {
        visited += 1;
        key_sum += (unsigned long long)it.key;
        value_sum += (unsigned long long)(intptr_t)it.value;
    }
    printf("cb_map_u64_next 循环访问到 %zu 条，key 和 = %llu，value 和 = %llu\n", visited, key_sum, value_sum);
    printf("与 cb_map_u64_count 一致 = %d\n", (int)(visited == cb_map_u64_count(&seq)));

    size_t used = 0;
    size_t empty_slots = 0;
    for (size_t i = 0; i < seq.capacity; ++i) {
        CB_Map_U64_Slot* slot = &seq.slots[i];
        if (slot->state == CB_MAP_USED) used += 1;
        if (slot->state == CB_MAP_EMPTY) empty_slots += 1;
    }
    printf("u64 槽位：USED = %zu，EMPTY = %zu，capacity = %zu\n", used, empty_slots, seq.capacity);

    cb_map_u64_free(&seq);
}

int main(void)
{
    test_map_basic();
    test_map_delete();
    test_map_iter();
    test_map_manual_iter();
    test_map_grow();
    test_map_arena();
    test_map_u64();
    test_map_u64_backends();
    test_map_hashes();

    return 0;
}
