// 示例 01：最小可用
//
// cb.h 最小的用法——只有 4 行有效代码。
//
// 编译运行（在仓库根目录）：
//     cc -o /tmp/ex01 examples/01_hello.c && /tmp/ex01

#include "../cb.h" // 推荐放最前面，但不是必须：cb.h 不是第一个 include 也能编译

int main(void)
{
    // 日志：三个级别，输出到 stderr
    cb_log(CB_INFO, "这是普通信息");
    cb_log(CB_WARN, "这是警告");
    cb_log(CB_ERROR, "这是错误");

    // CB_LOG_AT 自动补 __FILE__/__LINE__，宏在"你写的这一行"展开，位置精确指向你
    // 不需要位置时用 cb_log（库内部都这么用，避免报出 cb.h 自己的行号）
    CB_LOG_AT(CB_WARN, "带位置的日志，前缀会是本文件的这一行");

    printf("\n上面 4 行都输出在 stderr；这行在 stdout。\n");
    printf("cb.h 版本相关：C11 基线，header-only，无需 CB_IMPLEMENTATION。\n");
    return 0;
}
