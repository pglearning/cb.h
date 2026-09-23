#include "test_diagnostics.h"
#include "cb.h"

int main(void)
{

    CB_DArray array = { 0 };
    cb_da_append(&array, "a");

    const char* more[] = {"b", "c"};
    cb_da_append_many(&array, more, CB_ARRAY_LEN(more));

    cb_da_append_many(&array, array.items, array.count);

    (void)cb_da_pop(&array);
    printf("%s\n", cb_da_pop(&array));

    (void)cb_da_first(&array);
    printf("%s\n", cb_da_first(&array));

    (void)cb_da_last(&array);
    printf("%s\n", cb_da_last(&array));

    cb_da_remove_unordered(&array, 2);

    size_t index;
    cb_da_foreach(const char*, x, &array) {
        *x = "z";
        index = x - array.items;
    }
    printf("|last index = %zu|\n", index);

    cb_da_free(array);

    printf("\n== cb_da_reserve / cb_da_resize / cb_fa_append ==\n");

    // cb_da_reserve 直接调用：只保证容量，不动 count，也不会把已留出的容量缩回去。
    CB_DArray reserved = CB_ZERO;
    cb_da_reserve(&reserved, 3);
    printf("cb_da_reserve(3): count = %zu, capacity >= 3 = %d, items 已分配 = %d\n",
           reserved.count, (int)(reserved.capacity >= 3), (int)(reserved.items != NULL));
    size_t grown_capacity = reserved.capacity;
    cb_da_reserve(&reserved, 1);
    printf("cb_da_reserve(1) 保持已有容量 = %d\n", (int)(reserved.capacity == grown_capacity));
    cb_da_free(reserved);

    // cb_da_resize 放大把 count 提到目标值（新槽位是未初始化内存，这里只写不读），
    // 缩小只改 count，保留已分配的内存。
    CB_DArray words = CB_ZERO;
    cb_da_append(&words, "one");
    cb_da_resize(&words, 4);
    words.items[1] = "two";
    words.items[3] = "four";
    printf("cb_da_resize(1 -> 4): count = %zu, capacity >= 4 = %d\n",
           words.count, (int)(words.capacity >= 4));
    printf("cb_da_resize 放大的新槽位可按下标写入 = %s %s\n", words.items[1], words.items[3]);
    cb_da_resize(&words, 2);
    printf("cb_da_resize(4 -> 2): count = %zu, 保留的前两个元素 = %s %s\n",
           words.count, words.items[0], words.items[1]);
    size_t zero = 0;
    cb_da_resize(&words, zero);
    printf("cb_da_resize(2 -> 0): count = %zu, 内存仍保留 = %d\n",
           words.count, (int)(words.items != NULL));
    cb_da_free(words);

    // cb_fa_append 写入定长内联数组：{ T items[N]; size_t count; }，不分配、不增长。
    struct { const char* items[4]; size_t count; } fixed = CB_ZERO;
    cb_fa_append(&fixed, "fa-0");
    cb_fa_append(&fixed, "fa-1");
    cb_fa_append(&fixed, "fa-2");
    printf("cb_fa_append x3: count = %zu, items[1] = %s, 容量固定为 %zu\n",
           fixed.count, fixed.items[1], (size_t)CB_ARRAY_LEN(fixed.items));

    return 0;
}
