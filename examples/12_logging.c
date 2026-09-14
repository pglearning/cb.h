// 示例 12：日志、断言与 panic
//
// 覆盖：日志级别与抑制、CB_LOG_AT 的位置信息、内置处理器（默认/彩色/静音）
//       与自定义处理器、断言与 panic（CB_TODO / CB_UNREACHABLE / CB_ASSERT、
//       panic 调用栈与 -rdynamic）
//
// 编译运行（在仓库根目录）：
//     cc -o /tmp/ex12 examples/12_logging.c && /tmp/ex12
//     /tmp/ex12 panic      # 演示 panic 与调用栈（会 abort，属正常）

#include "../cb.h"

static void title(const char* text) { printf("\n== %s ==\n", text); }

// ---------------------------------------------------------------- 级别与抑制
static void demo_levels(void)
{
    title("cb_log 的三个级别与 cb_minimal_log_level");

    cb_set_log_handler(cb_default_log_handler);
    cb_minimal_log_level = CB_INFO;
    printf("  cb_minimal_log_level = CB_INFO 时：\n");
    cb_log(CB_INFO, "  INFO 会输出");
    cb_log(CB_WARN, "  WARN 会输出");
    cb_log(CB_ERROR, "  ERROR 会输出");

    cb_minimal_log_level = CB_ERROR;
    printf("  cb_minimal_log_level = CB_ERROR 时：\n");
    cb_log(CB_INFO, "  INFO 被抑制");
    cb_log(CB_WARN, "  WARN 被抑制");
    cb_log(CB_ERROR, "  ERROR 仍会输出");

    cb_minimal_log_level = CB_NO_LOGS;
    printf("  cb_minimal_log_level = CB_NO_LOGS 时：什么都不输出\n");
    cb_log(CB_ERROR, "  这条也不会出现");

    cb_minimal_log_level = CB_INFO;
    printf("  （已恢复 CB_INFO）\n");
}

// ---------------------------------------------------------------- 位置
static void demo_log_at(void)
{
    title("cb_log vs CB_LOG_AT：位置信息该由谁提供");

    // cb_log：不带位置。库内部都用它，避免报出 cb.h 自己的行号。
    cb_log(CB_INFO, "cb_log：不带位置");

    // CB_LOG_AT：在"你写的这一行"展开，位置精确指向你
    CB_LOG_AT(CB_INFO, "CB_LOG_AT：带位置，前缀是本文件的这一行");
    CB_LOG_AT(CB_WARN, "还支持格式化：%s = %d", "answer", 42);

    printf("\n  为什么要分两个：__FILE__/__LINE__ 展开在宏【被书写】的位置。\n");
    printf("  库函数体里写 cb_log，只会报出 cb.h 的内部行号（实测过），对你没帮助。\n");
}

// ---------------------------------------------------------------- 处理器
static void my_handler(CB_Log_Level level, const char* file, int line, const char* fmt, va_list args)
{
    // file 为 NULL 表示"无位置信息"
    fprintf(stdout, "    [自定义处理器] level=%d%s", (int)level,
            file != NULL ? " 有位置" : " 无位置");
    if (file != NULL) fprintf(stdout, " (%s:%d)", file, line);
    fprintf(stdout, ": ");
    vfprintf(stdout, fmt, args);
    fprintf(stdout, "\n");
}

static void demo_handlers(void)
{
    title("日志处理器：cb_set_log_handler / cb_get_log_handler");

    printf("  三个内置处理器：\n");
    printf("    cb_default_log_handler —— 带 [INFO]/[WARN]/[ERROR] 前缀（默认）\n");
    printf("    cb_cancer_log_handler  —— 带 emoji 与 ANSI 颜色\n");
    printf("    cb_null_log_handler    —— 丢弃一切（等价于静音）\n\n");

    printf("  当前处理器是 cb_default_log_handler: %s\n",
           cb_get_log_handler() == cb_default_log_handler ? "是" : "否");

    printf("  --- 用 cb_null_log_handler ---\n");
    cb_set_log_handler(cb_null_log_handler);
    cb_log(CB_ERROR, "这条被完全丢弃（连 stderr 都不写）");

    printf("  --- 用自定义处理器 ---\n");
    cb_set_log_handler(my_handler);
    cb_log(CB_INFO, "不带位置的日志");
    CB_LOG_AT(CB_WARN, "带位置的日志");

    printf("  --- 用 cb_cancer_log_handler（彩色+emoji）---\n");
    cb_set_log_handler(cb_cancer_log_handler);
    cb_log(CB_INFO, "这个是彩色的");

    cb_set_log_handler(cb_default_log_handler);
    printf("  （已恢复默认处理器）\n");
}

// ---------------------------------------------------------------- panic
static void deep3(void) { CB_TODO("演示：这条 TODO 会打印位置后 abort"); }
static void deep2(void) { deep3(); }
static void deep1(void) { deep2(); }

static void demo_panic(void)
{
    title("CB_TODO / CB_UNREACHABLE / CB_ASSERT / panic 调用栈");

    printf("  CB_TODO(\"消息\")         —— 打印 文件:行号 + TODO + 消息，然后 abort\n");
    printf("  CB_UNREACHABLE(\"消息\")  —— 同上，标签是 UNREACHABLE\n");
    printf("  CB_ASSERT(cond)         —— 标准 assert，会被 -DNDEBUG 关掉\n");
    printf("  CB_OOM(size)            —— 分配失败的出口，**不会**被 NDEBUG 关掉\n\n");
    printf("  Linux/glibc 下 panic 还会打印调用栈；链接时加 -rdynamic 才能看到函数名，\n");
    printf("  否则只有地址，可以用 addr2line -e <程序> <地址> 还原。\n");
    printf("  定义 CB_PANIC_BACKTRACE=0 可以关掉调用栈。\n\n");
    printf("  运行 `%s panic` 可以看到真实效果（会 abort，属正常）。\n", "ex12");
}

int main(int argc, char** argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    const char* arg = argc > 1 ? argv[1] : NULL;

    if (arg == NULL) {
        demo_levels();
        demo_log_at();
        demo_handlers();
        demo_panic();
        printf("\n全部日志示例执行完毕\n");
        return 0;
    }

    if (strcmp(arg, "panic") == 0) {
        cb_set_log_handler(cb_default_log_handler);
        printf("即将 panic（打印位置与调用栈后 abort）：\n");
        deep1();
        return 0;
    }
    if (strcmp(arg, "assert") == 0) {
        CB_ASSERT(1 == 2 && "演示断言失败");
        return 0;
    }

    printf("用法: %s [panic|assert]\n", argv[0]);
    return 0;
}
