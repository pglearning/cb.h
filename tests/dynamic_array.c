#include "cb.h"

int main(void)
{

    CB_DArray array = { 0 };
    cb_da_append(&array, "a");

    const char* more[] = {"b", "c"};
    cb_da_append_many(&array, more, CB_ARRAY_LEN(more));

    cb_da_append_many(&array, array.items, array.count);

    (void)cb_da_pop(&array); // 丢弃返回值时显式转 void
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
    return 0;
}
