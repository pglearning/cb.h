// Containers: dynamic arrays, sorting, bitset, ring buffer and both hash maps.
#define CB_IMPLEMENTATION
#include "cb.h"

static int cmp_int(const void* a, const void* b)
{
    int x = *(const int*)a;
    int y = *(const int*)b;
    return (x > y) - (x < y);
}

int main(void)
{
    // ---- dynamic array: any struct with items/count/capacity works with cb_da_* -------------
    typedef struct {
        int* items;
        size_t count;
        size_t capacity;
    } Ints;
    Ints xs = CB_ZERO;
    for (int i = 5; i > 0; --i) cb_da_append(&xs, i * i);
    printf("da: count = %zu, first = %d, last = %d\n", xs.count, cb_da_first(&xs), cb_da_last(&xs));

    cb_da_sort(&xs, cmp_int);
    printf("sorted:");
    cb_da_foreach(int, x, &xs) printf(" %d", *x);
    printf("\n");

    cb_da_remove_unordered(&xs, 0);
    int popped = cb_da_pop(&xs);
    printf("after remove_unordered(0): count = %zu, popped = %d\n", xs.count, popped);
    cb_da_free(xs);

    // ---- bitset ------------------------------------------------------------------------------
    CB_Bitset bits = CB_ZERO;
    cb_bitset_resize(&bits, 100);
    cb_bitset_set(&bits, 3);
    cb_bitset_set(&bits, 64);
    printf("bitset: test(3) = %d, test(4) = %d, count(true) = %zu, first set = %zu\n",
           cb_bitset_test(&bits, 3), cb_bitset_test(&bits, 4),
           cb_bitset_count(&bits, true), cb_bitset_find(&bits, true));
    cb_bitset_free(&bits);

    // ---- ring buffer: fixed capacity, never grows --------------------------------------------
    CB_Ring ring = CB_ZERO;
    if (!cb_ring_init(&ring, 4)) return 1;
    cb_ring_write(&ring, "abcd", 4);
    printf("ring: space = %zu, write with no space = %zu\n", cb_ring_space(&ring), cb_ring_write(&ring, "x", 1));
    char out[8] = CB_ZERO;
    size_t got = cb_ring_read(&ring, out, sizeof(out));
    printf("ring: read %zu bytes = %s\n", got, out);
    cb_ring_free(&ring);

    // ---- hash map with string keys (keys are copied into the map) ----------------------------
    CB_Map map = CB_ZERO;
    cb_map_put_cstr(&map, "one", (void*)(intptr_t)1);
    cb_map_put_cstr(&map, "two", (void*)(intptr_t)2);
    void* value = NULL;
    bool found = cb_map_get_cstr(&map, "two", &value);
    printf("map: get(\"two\") = %d, value = %d, count = %zu, has(\"three\") = %d\n",
           found, (int)(intptr_t)value, cb_map_count(&map), cb_map_has(&map, CB_SVLIT("three")));
    cb_map_del(&map, CB_SVLIT("one"));
    printf("map: after del count = %zu\n", cb_map_count(&map));
    cb_map_free(&map);

    // ---- hash map with uint64 keys -----------------------------------------------------------
    CB_Map_U64 by_id = CB_ZERO;
    cb_map_u64_put(&by_id, 7, (void*)(intptr_t)70);
    cb_map_u64_put(&by_id, 9, (void*)(intptr_t)90);
    void* hit = NULL;
    bool hit_ok = cb_map_u64_get(&by_id, 9, &hit);
    printf("map_u64: get(9) = %d, value = %d\n", hit_ok, (int)(intptr_t)hit);
    cb_map_u64_free(&by_id);
    return 0;
}
