// cb_temp_* 实测：临时分配 / 检查点回滚
//
// 风格：纯 printf + golden 比对。
// temp 存储是 _Thread_local，活到进程结束、故意不释放（设计如此，不是泄漏），所以这里不写 free。
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

    // cb_temp_reset 之后计数归零，cb_temp_save 记录的就是当前检查点
    CB_Arena_Mark save_mark = cb_temp_save();
    printf("|save mark count = %zu|\n", save_mark.count);

    char* before_rewind = cb_temp_strdup("hello ");
    cb_temp_rewind(save_mark);

    // 回滚之后重新分配应当拿回同一块地址；地址本身不打印（会破坏 golden 的确定性），只打印比较结果。
    char* after_rewind = cb_temp_strdup("hello ");
    printf("|rewind rollback = %d|\n", (int)(before_rewind == after_rewind));

    return 0;
}
