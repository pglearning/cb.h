// 示例 06：容器与算法
//
// 覆盖：动态数组、排序、位图、环形缓冲、哈希表（字符串键 / 整数键）
//
// 编译运行（在仓库根目录）：
//     cc -o /tmp/ex06 examples/06_containers.c && /tmp/ex06

#include "../cb.h"

static void title(const char* text)
{
    printf("\n== %s ==\n", text);
}

// ---------------------------------------------------------------- 动态数组
// 自定义数组类型：只要具备 items / count / capacity 三个字段，cb_da_* 就都能用。
typedef struct {
    int* items;
    size_t count;
    size_t capacity;
} IntArray;

typedef struct {
    const char** items;
    size_t count;
    size_t capacity;
} StrArray;

static void demo_dynamic_array(void)
{
    title("动态数组 cb_da_*");

    IntArray xs = CB_ZERO;

    // 追加
    cb_da_append(&xs, 10);
    cb_da_append(&xs, 20);
    int raw[] = {30, 40, 50};
    cb_da_append_many(&xs, raw, CB_ARRAY_LEN(raw));
    printf("追加 5 个: ");
    cb_da_foreach(int, it, &xs) printf("%d ", *it);
    printf("\n");

    // 插入 / 删除
    cb_da_insert(&xs, 0, 5);
    printf("头部插入 5: ");
    cb_da_foreach(int, it, &xs) printf("%d ", *it);
    printf("\n");

    cb_da_remove_ordered(&xs, 0);   // 保序删除
    printf("保序删除首项: ");
    cb_da_foreach(int, it, &xs) printf("%d ", *it);
    printf("\n");

    cb_da_remove_unordered(&xs, 0); // 用末尾元素顶替，O(1)
    printf("无序删除首项: ");
    cb_da_foreach(int, it, &xs) printf("%d ", *it);
    printf("\n");

    // 访问
    printf("first=%d last=%d count=%zu\n", cb_da_first(&xs), cb_da_last(&xs), xs.count);
    printf("pop=%d（弹出末项）\n", cb_da_pop(&xs));
    printf("反向遍历: ");
    cb_da_foreach_rev(int, it, &xs) printf("%d ", *it);
    printf("\n");

    // 知道大概要装多少时可以先留好容量，避免边插边扩容
    cb_da_reserve(&xs, 100);
    printf("reserve(100) 后 capacity = %zu\n", xs.capacity);

    // resize 与 clear
    cb_da_resize(&xs, 8);
    printf("resize 到 8 后 count=%zu（新增元素未初始化）\n", xs.count);
    cb_da_clear(&xs);
    printf("clear 后 count=%zu，但内存仍保留（capacity=%zu）\n", xs.count, xs.capacity);

    cb_da_free(xs);

    // 同一个宏用在完全不同的类型上
    StrArray names = CB_ZERO;
    cb_da_append(&names, "alpha");
    cb_da_append(&names, "beta");
    printf("字符串数组: ");
    cb_da_foreach(const char*, it, &names) printf("%s ", *it);
    printf("\n");
    cb_da_free(names);
}

// ---------------------------------------------------------------- 排序
static int cmp_asc(const void* a, const void* b)
{
    int x = *(const int*)a, y = *(const int*)b;
    return (x > y) - (x < y);
}
static int cmp_desc(const void* a, const void* b)
{
    return cmp_asc(b, a);
}

typedef struct {
    int key;
    int seq; // 用于观察稳定性
} Pair;
typedef struct {
    Pair* items;
    size_t count;
    size_t capacity;
} PairArray;

static int cmp_pair_key(const void* a, const void* b)
{
    int x = ((const Pair*)a)->key, y = ((const Pair*)b)->key;
    return (x > y) - (x < y);
}

static void demo_sort(void)
{
    title("排序 cb_da_sort / cb_da_sort_insertion");

    IntArray xs = CB_ZERO;
    int raw[] = {5, 3, 9, 1, 7};
    cb_da_append_many(&xs, raw, CB_ARRAY_LEN(raw));

    cb_da_sort(&xs, cmp_asc); // 走 qsort：快，但不稳定
    printf("cb_da_sort 升序:   ");
    cb_da_foreach(int, it, &xs) printf("%d ", *it);
    printf("\n");

    cb_da_sort(&xs, cmp_desc);
    printf("同一份数据降序:    ");
    cb_da_foreach(int, it, &xs) printf("%d ", *it);
    printf("\n");

    // 插入排序是稳定的：key 相同的元素保持原有相对顺序
    Pair pairs[] = {{2, 0}, {1, 1}, {2, 2}, {1, 3}};
    PairArray ps = CB_ZERO;
    cb_da_append_many(&ps, pairs, CB_ARRAY_LEN(pairs));
    cb_da_sort_insertion(&ps, cmp_pair_key);
    printf("插入排序（稳定）: ");
    cb_da_foreach(Pair, it, &ps) printf("%d/%d ", it->key, it->seq);
    printf("  ← key 相同时 seq 保持 0,2 与 1,3 的相对顺序\n");

    cb_da_free(xs);
    cb_da_free(ps);
}

// ---------------------------------------------------------------- 位图
static void demo_bitset(void)
{
    title("位图 CB_Bitset");

    CB_Bitset bs = CB_ZERO;
    cb_bitset_resize(&bs, 200);
    printf("分配 %zu 位\n", bs.bits);

    for (int i = 0; i < 200; i += 40) cb_bitset_set(&bs, (size_t)i);
    printf("置位 0/40/80/120/160 之后：\n");
    printf("  置位总数 = %zu\n", cb_bitset_count(&bs, true));
    printf("  第一个 0 位 = %zu\n", cb_bitset_find(&bs, false));
    printf("  第一个 1 位 = %zu\n", cb_bitset_find(&bs, true));
    printf("  第 40 位 = %s\n", cb_bitset_test(&bs, 40) ? "1" : "0");

    cb_bitset_unset(&bs, 40);
    cb_bitset_toggle(&bs, 41);
    printf("unset(40) 与 toggle(41) 之后，置位总数 = %zu\n", cb_bitset_count(&bs, true));

    cb_bitset_resize(&bs, 400); // 扩容：旧位保留，新位为 0
    printf("扩容到 400 位后，第 80 位仍为 %s，第 399 位为 %s\n",
           cb_bitset_test(&bs, 80) ? "1" : "0", cb_bitset_test(&bs, 399) ? "1" : "0");

    cb_bitset_clear_all(&bs);
    printf("clear_all 后置位总数 = %zu\n", cb_bitset_count(&bs, true));
    cb_bitset_free(&bs);
}

// ---------------------------------------------------------------- 环形缓冲
static void demo_ring(void)
{
    title("环形缓冲 CB_Ring（字节流 FIFO，容量固定）");

    CB_Ring ring = CB_ZERO;
    cb_ring_init(&ring, 8);
    printf("容量 %zu\n", ring.capacity);

    cb_ring_write(&ring, "abcdef", 6);
    printf("写 6 字节后可读 %zu、剩余空间 %zu\n", ring.count, cb_ring_space(&ring));

    char buf[32] = CB_ZERO;
    size_t got = cb_ring_peek(&ring, buf, 3); // peek 不消费
    printf("peek 3 字节 = %.*s（可读数仍为 %zu）\n", (int)got, buf, ring.count);

    got = cb_ring_read(&ring, buf, 4);
    printf("read 4 字节 = %.*s（可读数 %zu）\n", (int)got, buf, ring.count);

    // 此时写指针在 6，剩余 6 字节空间，写入会绕回缓冲开头
    cb_ring_write(&ring, "123456", 6);
    got = cb_ring_read(&ring, buf, sizeof(buf) - 1);
    buf[got] = '\0';
    printf("绕回写入 6 字节后读出全部: %s\n", buf);

    // 上面已经读空：此时再读返回 0
    printf("空缓冲读出返回 %zu\n", cb_ring_read(&ring, buf, 4));

    // 写满 8 字节后再写：返回 0（拒绝，不覆盖旧数据）
    cb_ring_write(&ring, "12345678", 8);
    printf("写满后再写返回 %zu\n", cb_ring_write(&ring, "X", 1));

    cb_ring_clear(&ring);
    cb_ring_free(&ring);
}

// ---------------------------------------------------------------- 哈希表
static void demo_map(void)
{
    title("哈希表 CB_Map（字符串键）/ CB_Map_U64（整数键）");

    CB_Map m = CB_ZERO;
    void* v = NULL;

    cb_map_put_cstr(&m, "answer", (void*)(intptr_t)42);
    cb_map_put_cstr(&m, "pi", (void*)(intptr_t)314);
    cb_map_put_cstr(&m, "e", (void*)(intptr_t)271);
    printf("插入 3 条，count = %zu\n", cb_map_count(&m));

    if (cb_map_get_cstr(&m, "answer", &v)) printf("get(answer) = %d\n", (int)(intptr_t)v);
    printf("has(pi) = %s，has(nope) = %s\n",
           cb_map_has(&m, CB_SVLIT("pi")) ? "真" : "假",
           cb_map_has(&m, CB_SVLIT("nope")) ? "真" : "假");

    // 键是被复制的：改掉原字符串不影响 map
    char key_buf[16];
    memcpy(key_buf, "copied", 7);
    cb_map_put_cstr(&m, key_buf, (void*)(intptr_t)7);
    memset(key_buf, 'X', 6);
    cb_map_get_cstr(&m, "copied", &v);
    printf("键被复制（改掉原字符串后仍能查到 copied = %d）\n", (int)(intptr_t)v);

    // 遍历（顺序不保证：底层是开放寻址）
    printf("遍历: ");
    cb_map_foreach(&m, it) printf("%.*s=%d ", (int)it.key.count, it.key.data, (int)(intptr_t)it.value);
    printf("\n");

    // 覆盖与删除
    cb_map_put_cstr(&m, "answer", (void*)(intptr_t)43);
    cb_map_get_cstr(&m, "answer", &v);
    printf("覆盖 answer 后 = %d，count 仍为 %zu\n", (int)(intptr_t)v, cb_map_count(&m));

    cb_map_del(&m, CB_SVLIT("e"));
    printf("删除 e 后 count = %zu\n", cb_map_count(&m));

    cb_map_clear(&m);
    printf("clear 后 count = %zu（容量保留）\n", cb_map_count(&m));
    cb_map_free(&m);

    // 预分配容量：避免插入过程中反复 rehash
    CB_Map big = CB_ZERO;
    cb_map_init_capacity(&big, 1000);
    printf("\n预分配 1000 容量后 capacity = %zu（按 3/4 负载因子取 2 的幂）\n", big.capacity);
    cb_map_free(&big);

    // 接到 arena 上：所有内存从 arena 取
    CB_Arena arena = CB_ZERO;
    CB_Map arena_map = CB_ZERO;
    cb_map_init_arena(&arena_map, &arena, 0);
    cb_map_put_cstr(&arena_map, "in-arena", (void*)(intptr_t)1);
    printf("arena 后端的 map：count = %zu，arena.begin %s\n",
           cb_map_count(&arena_map), arena.begin != NULL ? "非空" : "为空");
    cb_map_free(&arena_map);
    cb_arena_free(&arena);

    // 整数键版本
    CB_Map_U64 counts = CB_ZERO;
    for (uint64_t i = 0; i < 5; ++i) cb_map_u64_put(&counts, i * 1000, (void*)(intptr_t)(i + 1));
    cb_map_u64_put(&counts, UINT64_MAX, (void*)(intptr_t)999);
    void* got = NULL;
    cb_map_u64_get(&counts, 3000, &got);
    printf("\n整数键: get(3000) = %d，count = %zu\n", (int)(intptr_t)got, cb_map_u64_count(&counts));
    printf("整数键遍历: ");
    cb_map_u64_foreach(&counts, it) printf("%llu ", (unsigned long long)it.key);
    printf("\n");
    printf("u64 map 手动迭代（cb_map_u64_iter / cb_map_u64_next）: ");
    for (CB_Map_U64_Iter uit = cb_map_u64_iter(&counts); cb_map_u64_next(&uit); ) {
        printf("%llu ", (unsigned long long)uit.key);
    }
    printf("\n");
    cb_map_u64_free(&counts);

    // 手动迭代（cb_map_foreach 宏内部就是用它）
    CB_Map manual = CB_ZERO;
    cb_map_put_cstr(&manual, "a", (void*)(intptr_t)1);
    cb_map_put_cstr(&manual, "b", (void*)(intptr_t)2);
    printf("\n手动迭代（cb_map_iter / cb_map_next）: ");
    for (CB_Map_Iter it = cb_map_iter(&manual); cb_map_next(&it); ) {
        printf("%.*s ", (int)it.key.count, it.key.data);
    }
    printf("\n");
    cb_map_free(&manual);

    // 哈希函数是公开的：需要自己算哈希做分桶/缓存时可以直接用
    printf("cb_hash_bytes(\"hello\") = 0x%llX\n",
           (unsigned long long)cb_hash_bytes("hello", 5));
    printf("cb_hash_u64(42)          = 0x%llX\n", (unsigned long long)cb_hash_u64(42));

    // u64 map 的预分配 / arena 后端 / clear
    CB_Map_U64 prepared = CB_ZERO;
    cb_map_u64_init_capacity(&prepared, 500);
    printf("u64 map 预分配 500 后 capacity = %zu\n", prepared.capacity);
    cb_map_u64_put(&prepared, 1, (void*)(intptr_t)1);
    cb_map_u64_clear(&prepared);
    printf("u64 map clear 后 count = %zu（容量保留）\n", cb_map_u64_count(&prepared));
    cb_map_u64_free(&prepared);

    CB_Arena map_arena = CB_ZERO;
    CB_Map_U64 arena_u64 = CB_ZERO;
    cb_map_u64_init_arena(&arena_u64, &map_arena, 0);
    cb_map_u64_put(&arena_u64, 7, (void*)(intptr_t)7);
    printf("u64 map 用 arena 后端：count = %zu\n", cb_map_u64_count(&arena_u64));
    cb_map_u64_free(&arena_u64);
    cb_arena_free(&map_arena);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    demo_dynamic_array();
    demo_sort();
    demo_bitset();
    demo_ring();
    demo_map();

    printf("\n全部容器示例执行完毕\n");
    return 0;
}
