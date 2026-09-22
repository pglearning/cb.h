#include "test_diagnostics.h"
#include "cb.h"

int main(void)
{
    cb_temp_alloc(10);

    cb_temp_strdup("hello ");
    printf("|%s|\n", cb_temp_strdup("hello "));

    cb_temp_strndup("hello ", 2);
    printf("|%s|\n", cb_temp_strndup("hello ", 2));

    int num = 34;
    cb_temp_sprintf("h%d", num);
    printf("|%s|\n", cb_temp_sprintf("h%d", num));

    cb_temp_reset();

    CB_Arena_Mark save_mark = cb_temp_save();
    printf("|save mark count = %zu|\n", save_mark.count);

    char* before_rewind = cb_temp_strdup("hello ");
    cb_temp_rewind(save_mark);

    char* after_rewind = cb_temp_strdup("hello ");
    printf("|rewind rollback = %d|\n", (int)(before_rewind == after_rewind));

    return 0;
}
