// cc：C 编译器驱动（gcc / clang），cb.h 的构建 API 拼的就是它的命令行。
//   -c 只编译 / -o 输出文件 / -g 调试 / -O2 优化 / -I 头文件目录 / -L 库目录 /
//   -l 库名 / -D 定义 / -Wall 告警 / -std 语言标准
// 源文件、头文件、.o、.so 都能直接作为输入传给 cc：
//   cc main.c other.c -I other.h cc.h -L math.so func.so -l func math -o a && a
//

////////////////////////////////////////////////////////////////////////////////////////////////////
//  NOTE:
//      - header-only：任何 .c 只要 #include "cb.h" 就能用全部功能，不再需要 #define CB_IMPLEMENTATION
//      - 文件结构：配置宏 -> 平台头 -> 通用宏 -> 类型 -> 函数声明 -> 函数定义（两区按同一顺序分节）
//      - 语言基线 C11；裸 clang/gcc 直接编译即可，无需任何额外 -D 参数
//      - 版本：CB_VERSION_MAJOR / CB_VERSION_MINOR / CB_VERSION_PATCH / CB_VERSION_STRING
//
//  可选的编译期开关（共 17 个；在 #include "cb.h" 之前定义才生效，不定义就用下面的默认值）：
//
//    行为开关（7 个，默认全关）：
//      CB_ENABLE_ECHO                  文件系统/命令等操作打印提示信息（默认关）
//      CB_DONT_DELETE_OLD_CB           保留上一次编译出的旧二进制（默认关：旧的会被删掉）
//      CB_TRACE_CMD_RUN_FAIL_LOCATION  命令失败时打印其调用点 文件:行号（默认关）
//      CB_ALLOC_TRACK                  每次分配带内部追踪头，可统计与报告泄漏（默认关）
//      CB_STRIP_PREFIX                 生成去 cb_ 前缀的别名（默认关；别名区在文件末尾，由 tools/gen-docs.py 生成）
//      CB_SHARED_STATE[_IMPL]          多 TU 共享全局状态（默认关：每个 TU 各一份，见 General 一节）
//      CB_OOM(size)                    顶掉默认的 OOM 处理器（默认打印后 abort，见"内存分配收口"一节）
//
//    容量 / 参数微调（9 个，默认值如下）：
//      CB_PATH_MAX                     路径缓冲上限，默认 PATH_MAX（系统没定义它时 4096）
//      CB_TIMER_MAX_DEPTH              计时器嵌套深度上限，默认 64（统计表本身按需增长）
//      CB_ARENA_REGION_INIT_CAPACITY   arena / temp 首块容量，默认 64KB（写满自动追加新块，不是总量上限）
//      CB_ARENA_ALIGN                  arena 分配对齐，默认 alignof(max_align_t)
//      CB_THREAD_LOCAL                 线程局部存储说明符，默认 C 下 _Thread_local、C++ 下 thread_local
//                                      （单线程程序定义成空可以省掉 TLS 访问开销）
//      CB_DA_INIT_CAP                  动态数组首次扩容的容量，默认 256
//      CB_BITSET_WORD_BITS             位图一个字多少位，默认 64（内部按 uint64_t 字存储）
//      CB_MAP_INIT_CAPACITY            HashMap 初始桶数，默认 16（到负载上限后按 2 倍增长）
//      CB_WIN32_ERR_MSG_SIZE           Windows 错误信息缓冲大小，默认 4KB（非 Windows 平台无效果）
//
//    还有一个（1 个）：
//      CB_WARN_DEPRECATED              让 CB_DEPRECATED 真的展开成编译器的 deprecated 属性（默认关：标了也不报警告）
//
//    以上每一个都在 cb.h 里有 #ifndef 守卫，在 #include 之前定义即可覆盖。
//    另有几个开关写在各自的小节里：CB_REALLOC / CB_FREE / CB_REALLOC_RAW / CB_FREE_RAW（内存分配收口）、
//    CB_TEMP_CAPACITY（Arena / Temp Storage）、CB_ASSERT / CB_PANIC_BACKTRACE（Logger / Panic）。
//    逐个开关的示例见 examples/14_config_switches.c，总览表见 examples/README.md。
//
//  设计取舍（不是 bug；遇到了按这里说的做）：
//      1. arena 不支持单独释放，只支持整体复位/整体回收：cb_arena_reset 把游标归零、内存留给下次
//         复用，cb_arena_free 才还给系统。用 arena 后端建的 map 重哈希时产生的旧槽位同样要等复位/
//         释放。要精确回收就用默认的 realloc 后端，或者 cb_map_init_capacity 时一次给足容量。
//      2. C++ 下 cb_return_defer 要求所有变量声明写在第一个 goto 之前（C++ 不允许 goto 跳过带
//         初始化的声明）；C 下没有这个限制。
//      3. cb_da_* 宏对"任何具备 items / count / capacity 三个字段的结构体"通用，刻意不做类型检查：
//         CB_DArray、CB_String_Builder、CB_Cmd、CB_Procs 等十几个容器共用同一组宏，代价是字段名
//         写错时报错信息不好看。
//      4. C 里 cb_min / cb_max / cb_clamp 只覆盖 15 个标准算术类型（_Bool / char / signed char /
//         unsigned char / short / unsigned short / int / unsigned int / long / unsigned long /
//         long long / unsigned long long / float / double / long double），枚举与指针要先显式转换；
//         C++ 的模板版没有这个限制。
//      5. 分配失败默认 abort。容器 / arena / temp 的分配失败没有返回值可传，统一走死亡开关 CB_OOM；
//         能优雅失败的 API（读文件、起进程、cmd）仍然返回 false。
//         想自己接管就在 include 之前 #define CB_OOM(size) 你的处理器(size)。
////////////////////////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////////////////////////
// 分节目录（按文件顺序）
//
//   版本
//   Platform Header
//   General
//   内存分配收口
//   Logger / Panic
//   Timer
//   Arena / Temp Storage
//   Dynamic Array
//   Bitset
//   Ring Buffer
//   Sort
//   StringBuilder
//   StringView
//   UTF-8 Support
//   StringView Tools（大小写 / 数字解析 / split-join）
//   HashMap
//   HashMap：uint64 键版本
//   File System
//   路径处理
//   File System 扩展：元信息 / 递归建目录 / 原子写 / 符号链接 / glob / mmap
//   Math & Bits
//   Time & Date
//   Random
//   Runtime Environment
//   Hex Dump
//   CLI Args
//   Process & FD
//   Cmd
//   Cmd Chain：把多条命令用管道串起来
//   C Builder
//
// 跳转：在本文件内搜索节名即可，每一节都以一排 //// 横幅开头。
////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef STD_CB_H
#define STD_CB_H

////////////////////////////////////////////////////////////////////////////////////////////////////
// 版本
////////////////////////////////////////////////////////////////////////////////////////////////////
// 语义化版本。数字是唯一真源，版本字符串由数字拼出来（改一处即可）。
#define CB_VERSION_MAJOR 1
#define CB_VERSION_MINOR 1
#define CB_VERSION_PATCH 0

#define CB__STRINGIFY_(x) #x
#define CB__STRINGIFY(x) CB__STRINGIFY_(x)
#define CB_VERSION_STRING \
    CB__STRINGIFY(CB_VERSION_MAJOR) "." CB__STRINGIFY(CB_VERSION_MINOR) "." CB__STRINGIFY(CB_VERSION_PATCH)

// 必须在任何系统头之前定义，否则 -std=c11 下 lstat/readlink/nanosleep/PATH_MAX 不可见。
// 万一系统头已经先被包含过，底下那段 glibc 兜底声明会补上缺的原型。
#if !defined(_WIN32)
#  ifndef _POSIX_C_SOURCE
#    define _POSIX_C_SOURCE 200809L
#  endif
#endif

#ifdef _WIN32
// 必须在任何头文件之前：否则 MSVC/mingw 会对 fopen/strcpy 等标准函数报警告
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS 1
#endif // !_CRT_SECURE_NO_WARNINGS
#endif // _WIN32

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdalign.h> // alignas/alignof：C11 下是宏，C++ 下是关键字
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <limits.h>
#include <time.h>

////////////////////////////////////////////////////////////////////////////////////////////////////
// Platform Header
////////////////////////////////////////////////////////////////////////////////////////////////////
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
// 裁掉 windows.h 里用不到的部分（旧 MinGW 的惯用技巧），只保留这两个开关。
// 不能加 _WINCON_：它会裁掉控制台 API（cb_terminal_width 要 GetConsoleScreenBufferInfo、
// cb.c 要 SetConsoleOutputCP）；也不能加 _WINGDI_：它会裁掉 wingdi.h，而控制台头文件
// 又依赖里面的 LF_FACESIZE。
#define _WINUSER_
#define _IMM_
// 顺序很重要：windows.h 必须最先包含，direct.h / io.h / shellapi.h 都依赖它里面的宏。
#include <windows.h>
#include <direct.h>
#include <io.h>
#include <shellapi.h>
#include <winioctl.h> // FSCTL_GET_REPARSE_POINT：读取符号链接目标
#else
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#ifdef __FreeBSD__
#include <sys/sysctl.h>
#endif
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mman.h>  // mmap：大文件零拷贝读取
#include <sys/ioctl.h> // ioctl(TIOCGWINSZ)：终端宽度
#endif

#ifdef __HAIKU__
#include <image.h>
#endif

////////////////////////////////////////////////////////////////////////////////////////////////////
// POSIX 兜底声明：只在 "glibc + POSIX 被关掉" 这一种情况下生效
//
// cb.h 在最顶部定义 _POSIX_C_SOURCE，但 glibc 的 <features.h> 一辈子只处理一次：
// 用户先包含了 <stdio.h> 之类的系统头时，features.h 早就按"没有 POSIX"处理完了，
// 之后再定义 _POSIX_C_SOURCE 也来不及——clock_gettime / CLOCK_MONOTONIC / fileno /
// readlink / symlink / lstat / gmtime_r / setenv / nanosleep 全部变成未声明。
//
// 这里在"系统头已经包含完"之后按 <time.h>/<stdio.h> 自己的守门宏逐个补齐缺的原型
// （重复声明一个已存在的函数在 C++ 下是错误，所以必须逐个判断），
// 于是 cb.h 不必是第一个被 include。其它平台、其它 libc、以及正常用法下这里一个字都不加。
#if !defined(_WIN32) && defined(__GLIBC__) && !defined(__USE_XOPEN2K)

#ifdef __cplusplus
extern "C" {
#endif

// <stdio.h> 里 fileno 的守门宏是 __USE_POSIX，<time.h> 里 gmtime_r 是 __USE_POSIX
#  ifndef __USE_POSIX
extern int fileno(FILE* stream);
extern struct tm* gmtime_r(const time_t* __restrict timer, struct tm* __restrict result);
#  endif

// <time.h> 里这两个的守门宏是 __USE_POSIX199309
#  ifndef __USE_POSIX199309
#    ifndef CLOCK_MONOTONIC
#      define CLOCK_MONOTONIC 1 // glibc <time.h> 里的枚举值就是 1
#    endif
extern int clock_gettime(clockid_t clock_id, struct timespec* tp);
extern int nanosleep(const struct timespec* requested_time, struct timespec* remaining);
#  endif

// 下面四个的守门宏都是 __USE_XOPEN2K，也就是本块的条件本身
extern int setenv(const char* name, const char* value, int replace);
extern int lstat(const char* __restrict path, struct stat* __restrict buf);
extern int symlink(const char* from, const char* to);
extern ssize_t readlink(const char* __restrict path, char* __restrict buf, size_t len);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // !_WIN32 && __GLIBC__ && !__USE_XOPEN2K

// PATH_MAX 在 POSIX 里是可选的（可能压根没定义），用它之前先兜底。
#ifndef CB_PATH_MAX
#ifdef PATH_MAX
#define CB_PATH_MAX PATH_MAX
#else
#define CB_PATH_MAX 4096
#endif // PATH_MAX
#endif // !CB_PATH_MAX

////////////////////////////////////////////////////////////////////////////////////////////////////
// General
////////////////////////////////////////////////////////////////////////////////////////////////////
#define CBDEF static inline

#ifndef CB_ASSERT
#include <assert.h>
#define CB_ASSERT assert
#endif // !CB_ASSERT

// printf 风格格式串检查（编译期）
#if defined(__GNUC__) || defined(__clang__)
#ifdef __MINGW_PRINTF_FORMAT
#define CB_PRINTF_FORMAT(STRING_INDEX, FIRST_TO_CHECK) __attribute__((format(__MINGW_PRINTF_FORMAT, STRING_INDEX, FIRST_TO_CHECK)))
#else
#define CB_PRINTF_FORMAT(STRING_INDEX, FIRST_TO_CHECK) __attribute__((format(printf, STRING_INDEX, FIRST_TO_CHECK)))
#endif // __MINGW_PRINTF_FORMAT
#else
// MSVC 的 C 模式没有等价的属性，降级为无操作
#define CB_PRINTF_FORMAT(STRING_INDEX, FIRST_TO_CHECK)
#endif // __GNUC__ || __clang__

// 给"允许只写一部分字段"的选项结构体补 C++ 默认成员初始化器：cb_walk_dir(root, fn,
// .post_order = true) 这类写法（还有 cb_cmd_run / cb_chain_*）只写关心的字段，没写的取默认值，
// 两个语言下都不会触发 -Wmissing-field-initializers / -Wmissing-designated-field-initializers。
// C 没有这个语法，宏展开成空。
#ifdef __cplusplus
#define CB__DEFAULT(value) = value
#else
#define CB__DEFAULT(value)
#endif // __cplusplus

// ---- 全局状态：默认每 TU 一份，可选共享 ----
//
// 日志级别、日志处理器、timer、temp 栈、分配统计都是全局状态。
// 默认（什么都不用定义）：每个 TU 一份 static，零依赖、可单独编译，但多 TU 程序里
// A.c 设的 cb_minimal_log_level，B.c 看不见。
//
// 共享（可选）：
//   1. 每个 TU 都定义 CB_SHARED_STATE        → 这些变量变成 extern 声明
//   2. 恰好一个 TU 再定义 CB_SHARED_STATE_IMPL → 那个 TU 给出唯一一份定义
//      （CB_SHARED_STATE_IMPL 自动蕴含 CB_SHARED_STATE）
//   例：cc -c a.c -DCB_SHARED_STATE -DCB_SHARED_STATE_IMPL
//       cc -c b.c -DCB_SHARED_STATE
//       cc a.o b.o -o app
//
// 共享模式的两条限制：
//   - temp 栈是 _Thread_local 的：共享后是"每线程一份、所有 TU 共用"，跨 TU 的
//     cb_temp_save/cb_temp_rewind 会互相影响（这正是共享的目的）。
//   - 静态 inline 函数在 C 里是每个 TU 一份，拿 cb_get_log_handler() 和
//     &cb_default_log_handler 比指针时跨 TU 可能不相等——共享模式不改变这点。
#ifdef CB_SHARED_STATE_IMPL
#ifndef CB_SHARED_STATE
#define CB_SHARED_STATE
#endif // !CB_SHARED_STATE
#endif // CB_SHARED_STATE_IMPL

// 初始化式用 ... 接收：初值里的逗号会被预处理器当成参数分隔符，普通参数接不住。
#if !defined(CB_SHARED_STATE)
#define CB__STATE(type, name, ...) static type name = __VA_ARGS__
#elif defined(CB_SHARED_STATE_IMPL)
#define CB__STATE(type, name, ...) type name = __VA_ARGS__
#else
#define CB__STATE(type, name, ...) extern type name
#endif

////////////////////////////////////////////////////////////////////////////////////////////////////
// 内存分配收口
////////////////////////////////////////////////////////////////////////////////////////////////////
// 全库只有 CB_REALLOC / CB_FREE 两个出口，因此替换它们即可接管全部内存行为。
// CB_REALLOC_RAW / CB_FREE_RAW 是真正的底层，追踪层自身用它，避免递归。
#include <stdlib.h>
#ifndef CB_REALLOC_RAW
#define CB_REALLOC_RAW(ptr, size) realloc((ptr), (size))
#endif // !CB_REALLOC_RAW
#ifndef CB_FREE_RAW
#define CB_FREE_RAW(ptr) free(ptr)
#endif // !CB_FREE_RAW

#ifdef CB_ALLOC_TRACK
// 追踪模式：每次分配带一个内部头，记录大小与调用点，可统计与报告泄漏。
// 注意：开启后分配的指针不能再传给未开启该模式的模块。
CBDEF void* cb__tracked_realloc(void* ptr, size_t size, const char* file, int line);
CBDEF void cb__tracked_free(void* ptr, const char* file, int line);
// 打印当前存活块、峰值与泄漏清单（含分配点的 文件:行号）
CBDEF void cb_alloc_report(void);
CBDEF size_t cb_alloc_live_count(void);
CBDEF size_t cb_alloc_live_size(void);
#define CB_REALLOC(ptr, size) cb__tracked_realloc((ptr), (size), __FILE__, __LINE__)
#define CB_FREE(ptr) cb__tracked_free((ptr), __FILE__, __LINE__)
#else
#ifndef CB_REALLOC
#define CB_REALLOC(ptr, size) CB_REALLOC_RAW((ptr), (size))
#endif // !CB_REALLOC
#ifndef CB_FREE
#define CB_FREE(ptr) CB_FREE_RAW(ptr)
#endif // !CB_FREE
#endif // CB_ALLOC_TRACK

// OOM：所有"不可恢复"的分配失败都走这里。
// 刻意不用 assert：assert 会被 -DNDEBUG 关掉，之后 OOM 就成了没有原因的静默崩溃。
// __FILE__/__LINE__ 展开后是调用点（哪怕隔了多层宏），所以能精确定位。
//
// 想自己接管（长驻程序里统一走自己的日志/降级逻辑）就在 include 之前顶掉它
// （它是 #ifndef 守卫的），handler 要先声明：
//
//     static void my_oom(size_t requested_size);
//     #define CB_OOM(size) my_oom(size)
//     #include "cb.h"
//
// 之后容器 / arena / temp 的分配失败都会调用 my_oom(size)。注意 CB_OOM 必须
// "不返回"：分配失败后调用方会继续用那个空指针。
#ifndef CB_OOM
#define CB_OOM(requested_size) cb__oom((requested_size), __FILE__, __LINE__)
#endif // !CB_OOM
CBDEF void cb__oom(size_t requested_size, const char* file, int line);
// 分配后立刻校验：失败即终止。语句宏，紧跟赋值语句使用。
#define cb_alloc_check(ptr, requested_size) \
    do {                                    \
        if ((ptr) == NULL) CB_OOM(requested_size); \
    } while (0)

#ifdef __cplusplus
#define CB_DECLTYPE_CAST(T) (decltype(T))
#else
#define CB_DECLTYPE_CAST(T)
#endif // __cplusplus

#define cb_swap(T, a, b) \
    do {                 \
        T t = a;         \
        a = b;           \
        b = t;           \
    } while (0)

// 取走当前第一个参数并把指针前移（c_sb）
#define cb_shift(ptr_data, count) (CB_ASSERT((count) > 0), (count)--, *(ptr_data)++)

#if defined(__cplusplus)
#define CB_CLIT(type) type
#else
#define CB_CLIT(type) (type)
#endif

////////////////////////////////////////////////////////////////////////////////////////////////////
// Logger / Panic
////////////////////////////////////////////////////////////////////////////////////////////////////
#ifdef CB_WARN_DEPRECATED
#ifndef CB_DEPRECATED
#if defined(__GNUC__) || defined(__clang__)
#define CB_DEPRECATED(message) __attribute__((deprecated(message)))
#elif defined(_MSC_VER)
#define CB_DEPRECATED(message) __declspec(deprecated(message))
#else
#define CB_DEPRECATED(...)
#endif
#endif /* CB_DEPRECATED */
#else
#define CB_DEPRECATED(...)
#endif /* CB_WARN_DEPRECATED */

// 零初始化：C 用 {0}，C++ 用 {}，两边都不触发 -Wmissing-field-initializers。
#ifdef __cplusplus
#define CB_ZERO { }
#else
#define CB_ZERO { 0 }
#endif // __cplusplus


// 例：char str1[] = "hello"; sizeof(str1) = 6，CB_ARRAY_LEN(str1) = 6
#define CB_ARRAY_LEN(array) (sizeof(array) / sizeof(array[0]))
#define CB_ARRAY_GET(array, index) \
    (CB_ASSERT((size_t)index < CB_ARRAY_LEN(array)), array[(size_t)index])

// 消除“未使用参数”告警
#define CB_UNUSED(value) (void)(value)

// 跳到函数末尾的 defer: 标签并把返回值写进 result。
//
// 使用约定（两条都必须满足，否则编译不过）：
//   1. 当前作用域里要有一个名为 result 的变量
//   2. 函数里要有一个 defer: 标签
// 典型写法：
//     bool foo(void)
//     {
//         bool result = true;
//         ...
//         if (失败) cb_return_defer(false);
//     defer:
//         ...清理...
//         return result;
//     }
//
// C++ 下 goto 不能跳过带初始化的变量声明，所以变量声明都要写在第一个 cb_return_defer 之前。
#define cb_return_defer(value) \
    do {                       \
        result = (value);      \
        goto defer;            \
    } while (0)


// 日志分两级用法：
//   cb_log(level, ...)      库内部与一般用途：不带位置，输出干净
//   CB_LOG_AT(level, ...)   你自己的代码里用：自动带上 文件:行号
// 例：CB_LOG_AT(CB_ERROR, "配置解析失败") → [ERROR] mycode.c:42: 配置解析失败

typedef enum {
    CB_INFO,
    CB_WARN,
    CB_ERROR,
    CB_NO_LOGS,
} CB_Log_Level;

// 日志处理器。file 为 NULL（line 为 0）表示"无位置信息"，处理器自行决定是否打印。
typedef void(CB_Log_Handler)(CB_Log_Level level, const char* file, int line, const char* fmt, va_list args);

CB__STATE(CB_Log_Level, cb_minimal_log_level, CB_INFO);

CBDEF CB_Log_Handler cb_default_log_handler;
CBDEF CB_Log_Handler cb_cancer_log_handler;
CBDEF CB_Log_Handler cb_null_log_handler;
CB__STATE(CB_Log_Handler*, cb_log_handler, &cb_default_log_handler);

CBDEF void cb_log(CB_Log_Level level, const char* fmt, ...);
// 带位置的日志。一般用下面的 CB_LOG_AT 宏，不要手写 file/line。
CBDEF void cb_log_at(CB_Log_Level level, const char* file, int line, const char* fmt, ...);
#define CB_LOG_AT(level, ...) cb_log_at((level), __FILE__, __LINE__, __VA_ARGS__)

CBDEF void cb_set_log_handler(CB_Log_Handler* handler);
CBDEF CB_Log_Handler* cb_get_log_handler(void);
CBDEF void cb_default_log_handler(CB_Log_Level level, const char* file, int line, const char* fmt, va_list args);
CBDEF void cb_cancer_log_handler(CB_Log_Level level, const char* file, int line, const char* fmt, va_list args);
CBDEF void cb_null_log_handler(CB_Log_Level level, const char* file, int line, const char* fmt, va_list args);

// panic：打印 文件:行号 + 标签 + 消息，然后 abort。
// Linux(glibc) 下额外打印调用栈：加 -rdynamic 才有函数名，否则只有地址（可用 addr2line 还原）；
// 定义 CB_PANIC_BACKTRACE=0 可关闭。
#ifndef CB_PANIC_BACKTRACE
#if defined(__linux__) && defined(__GLIBC__) && !defined(_WIN32)
#define CB_PANIC_BACKTRACE 1
#else
#define CB_PANIC_BACKTRACE 0
#endif
#endif // !CB_PANIC_BACKTRACE

#if CB_PANIC_BACKTRACE
#include <execinfo.h>
#endif

CBDEF void cb__panicf(const char* file, int line, const char* label, const char* format, ...);
#define CB_TODO(...) cb__panicf(__FILE__, __LINE__, "TODO", __VA_ARGS__)
#define CB_UNREACHABLE(...) cb__panicf(__FILE__, __LINE__, "UNREACHABLE", __VA_ARGS__)

// ---- 定义 ----
CBDEF void cb__panicf(const char* file, int line, const char* label, const char* format, ...)
{
    fprintf(stderr, "%s:%d: %s: ", file, line, label);
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fprintf(stderr, "\n");

#if CB_PANIC_BACKTRACE
    {
        void* frames[32];
        int count = backtrace(frames, (int)CB_ARRAY_LEN(frames));
        fprintf(stderr, "--- backtrace (%d frames) ---\n", count);
        backtrace_symbols_fd(frames, count, 2); // 2 = STDERR_FILENO，避免为它引入 unistd.h
    }
#endif // CB_PANIC_BACKTRACE

    abort();
}

CBDEF void cb_log(CB_Log_Level level, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    cb_log_handler(level, NULL, 0, fmt, args);
    va_end(args);
}

CBDEF void cb_log_at(CB_Log_Level level, const char* file, int line, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    cb_log_handler(level, file, line, fmt, args);
    va_end(args);
}

CBDEF void cb_set_log_handler(CB_Log_Handler* handler)
{
    cb_log_handler = handler;
}

CBDEF CB_Log_Handler* cb_get_log_handler(void)
{
    return cb_log_handler;
}

CBDEF void cb_default_log_handler(CB_Log_Level level, const char* file, int line, const char* fmt, va_list args)
{
    if (level < cb_minimal_log_level) return;

    switch (level) {
    case CB_INFO:
        fprintf(stderr, "[INFO] ");
        break;
    case CB_WARN:
        fprintf(stderr, "[WARN] ");
        break;
    case CB_ERROR:
        fprintf(stderr, "[ERROR] ");
        break;
    case CB_NO_LOGS:
        return;
    default:
        CB_UNREACHABLE("CB_Log_Level");
    }

    if (file != NULL) fprintf(stderr, "%s:%d: ", file, line);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
}

CBDEF void cb_cancer_log_handler(CB_Log_Level level, const char* file, int line, const char* fmt, va_list args)
{
    switch (level) {
    case CB_INFO:
        fprintf(stderr, "ℹ️ \x1b[36m[INFO]\x1b[0m ");
        break;
    case CB_WARN:
        fprintf(stderr, "⚠️ \x1b[33m[WARN]\x1b[0m ");
        break;
    case CB_ERROR:
        fprintf(stderr, "🚨 \x1b[31m[ERROR]\x1b[0m ");
        break;
    case CB_NO_LOGS:
        return;
    default:
        CB_UNREACHABLE("CB_Log_Level");
    }

    if (file != NULL) fprintf(stderr, "%s:%d: ", file, line);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
}

CBDEF void cb_null_log_handler(CB_Log_Level level, const char* file, int line, const char* fmt, va_list args)
{
    CB_UNUSED(level);
    CB_UNUSED(file);
    CB_UNUSED(line);
    CB_UNUSED(fmt);
    CB_UNUSED(args);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Timer
////////////////////////////////////////////////////////////////////////////////////////////////////
// 用法：
//     CB_TIMER_START("phase");  ...被测代码...  double us = CB_TIMER_END();
//     cb_timer_end_print();   打印这一条
//     cb_timer_end_stat();    取耗时并累加到同名统计项
//     cb_timer_print_stats(); 打印统计表
//
// 时间源：Windows 用 QueryPerformanceCounter，其余平台用 clock_gettime(CLOCK_MONOTONIC)。
// 统计表按需增长，不会因为计时名变多而丢数据。
// 输出一律走 stderr——计时是诊断信息，混进 stdout 会污染程序正常输出与回归测试基线。

// 嵌套深度上限。这不是统计条目上限：统计表按需增长，不会丢数据。
#ifndef CB_TIMER_MAX_DEPTH
#define CB_TIMER_MAX_DEPTH 64
#endif // !CB_TIMER_MAX_DEPTH

typedef struct {
    const char* name;
    double total;
    size_t count;
    double min;
    double max;
} CB_Timer_Stat;

typedef struct {
    const char* name;
    double start;
} CB_Timer_Frame;

typedef struct {
    CB_Timer_Stat* stats; // 动态数组（items/count/capacity 三件套）
    size_t stats_count;
    size_t stats_capacity;
    CB_Timer_Frame stack[CB_TIMER_MAX_DEPTH];
    size_t sp;
} CB_Timer;

CB__STATE(CB_Timer, cb_timer, CB_ZERO);

// ---- 声明 ----
CBDEF double cb_get_time_ms(void);
CBDEF double cb_get_time_us(void);

CBDEF void cb_timer_begin(const char* name);
// 结束当前计时并返回耗时（微秒）
CBDEF double cb_timer_end(void);
// 结束当前计时、返回耗时，并累加到同名统计项
CBDEF double cb_timer_end_stat(void);
// 结束当前计时并把这一条打印到 stderr
CBDEF void cb_timer_end_print(void);
// 找到（或新建）同名统计项；不会返回 NULL（统计表满了会自动扩容）
CBDEF CB_Timer_Stat* cb_timer_get_stat(const char* name);
// 打印统计表到指定流（基准测试用它打到 stdout，便于重定向保存）
CBDEF void cb_timer_fprint_stats(FILE* out);
// 打印统计表到 stderr（默认；计时是诊断信息）
CBDEF void cb_timer_print_stats(void);
// 清空统计表与计时栈（已分配的统计表内存保留复用）
CBDEF void cb_timer_reset(void);

// 只有最常用的这两个保留短名字（CB_TIMER_START / CB_TIMER_END），其余直接用函数名
#define CB_TIMER_START(timer_name) cb_timer_begin(timer_name)
#define CB_TIMER_END() cb_timer_end()

// ---- 定义 ----
#ifdef _WIN32

// 注意：Windows 下 cb_get_time_us 约 53ns/次，比 Linux 的 22ns 慢一倍多，
// 那是 QueryPerformanceCounter 本身的代价。
CBDEF double cb_get_time_ms(void)
{
    LARGE_INTEGER freq, count;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);
    return (double)count.QuadPart * 1000 / freq.QuadPart;
}
CBDEF double cb_get_time_us(void)
{
    LARGE_INTEGER freq, count;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);
    return (double)count.QuadPart * 1000000 / freq.QuadPart;
}
#else

// 用单调时钟：gettimeofday 是墙钟，会被 NTP 校正或手动改时间影响（间隔可能变负或跳变）
CBDEF double cb_get_time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}
CBDEF double cb_get_time_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000000.0 + (double)ts.tv_nsec / 1000.0;
}
#endif // _WIN32

CBDEF void cb_timer_begin(const char* name)
{
    if (cb_timer.sp >= CB_TIMER_MAX_DEPTH) {
        cb__panicf(__FILE__, __LINE__, "CB_TIMER",
                   "定时器嵌套超过 %d 层：某个 CB_TIMER_START 没有配对的 CB_TIMER_END",
                   (int)CB_TIMER_MAX_DEPTH);
    }
    cb_timer.stack[cb_timer.sp].name = name;
    cb_timer.stack[cb_timer.sp].start = cb_get_time_us();
    cb_timer.sp += 1;
}

CBDEF double cb_timer_end(void)
{
    if (cb_timer.sp == 0) {
        cb__panicf(__FILE__, __LINE__, "CB_TIMER", "CB_TIMER_END 比 CB_TIMER_START 多，计时栈已空");
    }
    cb_timer.sp -= 1;
    return cb_get_time_us() - cb_timer.stack[cb_timer.sp].start;
}

// 纳秒级单调时间戳。语义与 cb_get_time_us 一致，只是精度更高（用于细分性能测量）。
// "unspecified epoch" 表示起点未定义，只应拿两次调用的差值。
CBDEF uint64_t cb_nanos_since_unspecified_epoch(void)
{
#ifdef _WIN32
    LARGE_INTEGER now, freq;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    return (uint64_t)(now.QuadPart * 1000000000 / freq.QuadPart);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
#endif // _WIN32
}

CBDEF CB_Timer_Stat* cb_timer_get_stat(const char* name)
{
    for (size_t i = 0; i < cb_timer.stats_count; ++i) {
        if (strcmp(cb_timer.stats[i].name, name) == 0) return &cb_timer.stats[i];
    }

    if (cb_timer.stats_count == cb_timer.stats_capacity) {
        size_t new_capacity = cb_timer.stats_capacity == 0 ? 16 : cb_timer.stats_capacity * 2;
        size_t bytes = new_capacity * sizeof(*cb_timer.stats);
        cb_timer.stats = (CB_Timer_Stat*)CB_REALLOC(cb_timer.stats, bytes);
        cb_alloc_check(cb_timer.stats, bytes);
        cb_timer.stats_capacity = new_capacity;
    }

    CB_Timer_Stat* stat = &cb_timer.stats[cb_timer.stats_count++];
    stat->name = name;
    stat->total = 0;
    stat->count = 0;
    stat->min = 0;
    stat->max = 0;
    return stat;
}

CBDEF double cb_timer_end_stat(void)
{
    double elapsed = cb_timer_end();
    // cb_timer_end() 已经弹过栈，stack[sp] 就是刚结束的那一层
    CB_Timer_Stat* stat = cb_timer_get_stat(cb_timer.stack[cb_timer.sp].name);
    stat->total += elapsed;
    stat->count += 1;
    if (stat->count == 1) {
        stat->min = elapsed;
        stat->max = elapsed;
    } else {
        if (elapsed < stat->min) stat->min = elapsed;
        if (elapsed > stat->max) stat->max = elapsed;
    }
    return elapsed;
}

CBDEF void cb_timer_end_print(void)
{
    double elapsed = cb_timer_end();
    fprintf(stderr, "[%s] took %.3f us\n", cb_timer.stack[cb_timer.sp].name, elapsed);
}

CBDEF void cb_timer_fprint_stats(FILE* out)
{
    if (out == NULL) return;

    // 名字列宽按实际内容算，免得长名字把后面的列挤歪
    size_t name_width = strlen("Name");
    for (size_t i = 0; i < cb_timer.stats_count; ++i) {
        size_t len = strlen(cb_timer.stats[i].name);
        if (len > name_width) name_width = len;
    }

    fprintf(out, "\n========== Timer Statistics ==========\n");
    fprintf(out, "%-*s %10s %12s %12s %12s %12s\n",
            (int)name_width, "Name", "Count", "Avg(us)", "Min(us)", "Max(us)", "Total(us)");
    for (size_t i = 0; i < cb_timer.stats_count; ++i) {
        CB_Timer_Stat* s = &cb_timer.stats[i];
        double avg = s->count > 0 ? s->total / (double)s->count : 0.0;
        fprintf(out, "%-*s %10zu %12.3f %12.3f %12.3f %12.3f\n",
                (int)name_width, s->name, s->count, avg, s->min, s->max, s->total);
    }
    fprintf(out, "========================================\n");
}

CBDEF void cb_timer_print_stats(void)
{
    cb_timer_fprint_stats(stderr);
}

CBDEF void cb_timer_reset(void)
{
    cb_timer.stats_count = 0;
    cb_timer.sp = 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Arena / Temp Storage
////////////////////////////////////////////////////////////////////////////////////////////////////
// arena：显式作用域分配器。可创建任意多个实例，按 region 链分块增长，永不"满"。
// temp ：arena 的一个 _Thread_local 全局实例的薄封装，提供"不用管内存"的临时分配。
// 两者共用同一份内核，区别只在使用模型：显式句柄 vs 隐式单例。
// CB_TEMP_CAPACITY 是第一块的大小而不是总量上限，写满后自动追加新块；
// cb_temp_alloc 不会返回 NULL（分配不出来直接 OOM 终止）。

#ifndef CB_ARENA_REGION_INIT_CAPACITY
#define CB_ARENA_REGION_INIT_CAPACITY (64 * 1024)
#endif // !CB_ARENA_REGION_INIT_CAPACITY

#ifndef CB_TEMP_CAPACITY
#define CB_TEMP_CAPACITY CB_ARENA_REGION_INIT_CAPACITY
#endif // !CB_TEMP_CAPACITY

// 对齐粒度用 max_align_t，足以承载任何标准类型
#ifndef CB_ARENA_ALIGN
#define CB_ARENA_ALIGN (alignof(max_align_t))
#endif // !CB_ARENA_ALIGN

// 线程局部：每个线程一套独立的 temp 栈，多线程下不会互相踩。
// 单线程程序可 #define CB_THREAD_LOCAL 为空以省掉 TLS 访问开销。
#ifndef CB_THREAD_LOCAL
#ifdef __cplusplus
#define CB_THREAD_LOCAL thread_local
#else
#define CB_THREAD_LOCAL _Thread_local
#endif // __cplusplus
#endif // !CB_THREAD_LOCAL

typedef struct CB_Arena_Region CB_Arena_Region;
struct CB_Arena_Region {
    CB_Arena_Region* next;
    size_t count;                               // 已用字节数，始终是 CB_ARENA_ALIGN 的整数倍
    size_t capacity;                            // 本块可用字节数
#ifdef __cplusplus
    // C++ 没有柔性数组成员（-Wpedantic 会报 C99 扩展）。改用 1 元素数组：
    // 数据区偏移不变，见下面的 CB__ARENA_REGION_DATA_OFFSET。
    alignas(max_align_t) unsigned char data[1];
#else
    alignas(max_align_t) unsigned char data[]; // 强制 data 按 max_align_t 对齐
#endif
};

// 数据区相对 region 起始的偏移。必须用 offsetof 而不是 sizeof：C++ 版的 sizeof
// 含尾部填充，而 offsetof 在两种语言下都等于 C 版的 sizeof(CB_Arena_Region)。
#define CB__ARENA_REGION_DATA_OFFSET offsetof(CB_Arena_Region, data)

typedef struct {
    CB_Arena_Region* begin; // 链头
    CB_Arena_Region* end;   // 当前分配块
    size_t init_capacity;   // 首个块的容量，0 表示使用 CB_ARENA_REGION_INIT_CAPACITY
} CB_Arena;

// 快照：记录 (块, 块内偏移)，回滚 O(1)，可跨越多个块
typedef struct {
    CB_Arena_Region* region;
    size_t count;
} CB_Arena_Mark;

// ---- 声明 ----
CBDEF void cb__oom(size_t requested_size, const char* file, int line);

CBDEF void* cb_arena_alloc(CB_Arena* a, size_t size);
CBDEF void* cb_arena_alloc_aligned(CB_Arena* a, size_t size, size_t alignment);
// 注意：arena 无法原地扩容。new_size > old_size 时会新分配并拷贝，返回的可能是新指针。
CBDEF void* cb_arena_realloc(CB_Arena* a, void* old_ptr, size_t old_size, size_t new_size);
CBDEF char* cb_arena_strdup(CB_Arena* a, const char* cstr);
CBDEF char* cb_arena_strndup(CB_Arena* a, const char* cstr, size_t n);
CBDEF char* cb_arena_sprintf(CB_Arena* a, const char* fmt, ...) CB_PRINTF_FORMAT(2, 3);
CBDEF char* cb_arena_vsprintf(CB_Arena* a, const char* fmt, va_list ap);
CBDEF CB_Arena_Mark cb_arena_save(CB_Arena* a);
CBDEF void cb_arena_rewind(CB_Arena* a, CB_Arena_Mark mark);
// 清空内容但保留已分配的块供复用（arena 的常规用法，不把内存还给系统）
CBDEF void cb_arena_reset(CB_Arena* a);
// 真正把所有块还给系统
CBDEF void cb_arena_free(CB_Arena* a);

CBDEF void* cb_temp_alloc(size_t size);
CBDEF char* cb_temp_strdup(const char* cstr);
CBDEF char* cb_temp_strndup(const char* cstr, size_t n);
CBDEF char* cb_temp_sprintf(const char* fmt, ...) CB_PRINTF_FORMAT(1, 2);
CBDEF char* cb_temp_vsprintf(const char* fmt, va_list ap);
// 复位整个 temp 栈（保留已分配的块，不清空整个存储）
CBDEF void cb_temp_reset(void);
// 保存检查点。用法：CB_Arena_Mark mark = cb_temp_save(); ... cb_temp_rewind(mark);
CBDEF CB_Arena_Mark cb_temp_save(void);
// 回到检查点，此后的分配全部失效
CBDEF void cb_temp_rewind(CB_Arena_Mark mark);

// ---- 定义 ----
CBDEF void cb__oom(size_t requested_size, const char* file, int line)
{
    fprintf(stderr, "%s:%d: OOM: 分配 %zu 字节失败\n", file, line, requested_size);
    abort();
}

#ifdef CB_ALLOC_TRACK
// 追踪层：每次分配前面挂一个记录头。头里含一个 max_align_t 成员，
// 保证 sizeof 是对齐粒度的整数倍，返回给用户的指针仍然满足最大对齐要求。
typedef struct CB__Alloc_Record {
    struct CB__Alloc_Record* prev;
    struct CB__Alloc_Record* next;
    max_align_t align_guard;
    size_t size;
    const char* file;
    int line;
} CB__Alloc_Record;

CB__STATE(CB__Alloc_Record*, cb__alloc_head, NULL);
CB__STATE(size_t, cb__alloc_live_count, 0);
CB__STATE(size_t, cb__alloc_live_size, 0);
CB__STATE(size_t, cb__alloc_peak_size, 0);
CB__STATE(size_t, cb__alloc_total_count, 0);

CBDEF void* cb__tracked_realloc(void* ptr, size_t size, const char* file, int line)
{
    if (ptr == NULL) {
        if (size == 0) size = 1;
        size_t total = sizeof(CB__Alloc_Record) + size;
        CB__Alloc_Record* rec = (CB__Alloc_Record*)CB_REALLOC_RAW(NULL, total);
        cb_alloc_check(rec, total);
        rec->prev = NULL;
        rec->next = cb__alloc_head;
        if (cb__alloc_head != NULL) cb__alloc_head->prev = rec;
        cb__alloc_head = rec;
        rec->size = size;
        rec->file = file;
        rec->line = line;
        cb__alloc_live_count += 1;
        cb__alloc_live_size += size;
        cb__alloc_total_count += 1;
        if (cb__alloc_live_size > cb__alloc_peak_size) cb__alloc_peak_size = cb__alloc_live_size;
        return (char*)rec + sizeof(CB__Alloc_Record);
    }

    if (size == 0) { // realloc(p, 0) 语义等同 free
        cb__tracked_free(ptr, file, line);
        return NULL;
    }

    CB__Alloc_Record* rec = (CB__Alloc_Record*)(void*)((char*)ptr - sizeof(CB__Alloc_Record));
    size_t total = sizeof(CB__Alloc_Record) + size;
    CB__Alloc_Record* moved = (CB__Alloc_Record*)CB_REALLOC_RAW(rec, total);
    cb_alloc_check(moved, total);
    if (moved != rec) { // 链表节点被搬走了，把邻居的指针接回来
        if (moved->prev != NULL) moved->prev->next = moved;
        else cb__alloc_head = moved;
        if (moved->next != NULL) moved->next->prev = moved;
    }
    cb__alloc_live_size = cb__alloc_live_size - moved->size + size;
    if (cb__alloc_live_size > cb__alloc_peak_size) cb__alloc_peak_size = cb__alloc_live_size;
    moved->size = size;
    moved->file = file;
    moved->line = line;
    cb__alloc_total_count += 1;
    return (char*)moved + sizeof(CB__Alloc_Record);
}

CBDEF void cb__tracked_free(void* ptr, const char* file, int line)
{
    CB_UNUSED(file);
    CB_UNUSED(line);
    if (ptr == NULL) return;
    CB__Alloc_Record* rec = (CB__Alloc_Record*)(void*)((char*)ptr - sizeof(CB__Alloc_Record));
    if (rec->prev != NULL) rec->prev->next = rec->next;
    else cb__alloc_head = rec->next;
    if (rec->next != NULL) rec->next->prev = rec->prev;
    cb__alloc_live_count -= 1;
    cb__alloc_live_size -= rec->size;
    CB_FREE_RAW(rec);
}

CBDEF size_t cb_alloc_live_count(void) { return cb__alloc_live_count; }
CBDEF size_t cb_alloc_live_size(void) { return cb__alloc_live_size; }

CBDEF void cb_alloc_report(void)
{
    fprintf(stderr, "=== cb alloc report ===\n");
    fprintf(stderr, "live : %zu blocks, %zu bytes\n", cb__alloc_live_count, cb__alloc_live_size);
    fprintf(stderr, "peak : %zu bytes, %zu allocations total\n", cb__alloc_peak_size, cb__alloc_total_count);
    for (CB__Alloc_Record* r = cb__alloc_head; r != NULL; r = r->next) {
        fprintf(stderr, "  LEAK %zu bytes from %s:%d\n", r->size, r->file != NULL ? r->file : "?", r->line);
    }
}
#endif // CB_ALLOC_TRACK

CBDEF CB_Arena_Region* cb__arena_new_region(size_t capacity)
{
    size_t size = CB__ARENA_REGION_DATA_OFFSET + capacity;
    CB_Arena_Region* r = (CB_Arena_Region*)CB_REALLOC(NULL, size);
    cb_alloc_check(r, size);
    r->next = NULL;
    r->count = 0;
    r->capacity = capacity;
    return r;
}

CBDEF void cb__arena_free_region(CB_Arena_Region* r)
{
    CB_FREE(r);
}

CBDEF void* cb_arena_alloc_aligned(CB_Arena* a, size_t size, size_t alignment)
{
    size_t align = alignment > CB_ARENA_ALIGN ? alignment : CB_ARENA_ALIGN;
    size_t need = size == 0 ? 1 : size;

    for (;;) {
        if (a->end == NULL) {
            size_t capacity = a->init_capacity ? a->init_capacity : CB_ARENA_REGION_INIT_CAPACITY;
            if (capacity < need + align) capacity = need + align;
            a->end = cb__arena_new_region(capacity);
            if (a->begin == NULL) a->begin = a->end;
            continue;
        }

        // 按"绝对地址"把当前位置抬高到 alignment 边界：data 本身只保证 max_align_t
        // 对齐，alignment 更大时只把偏移量凑成倍数并不能让最终地址满足要求。
        {
            uintptr_t base = (uintptr_t)a->end->data;
            uintptr_t addr = base + (uintptr_t)a->end->count;
            uintptr_t aligned = (addr + (uintptr_t)align - 1) / (uintptr_t)align * (uintptr_t)align;
            size_t offset = (size_t)(aligned - base);
            if (offset + need <= a->end->capacity) {
                void* result = a->end->data + offset;
                a->end->count = offset + need;
                return result;
            }
        }

        if (a->end->next != NULL) {
            a->end = a->end->next; // 回滚过的旧块可能还能用，先往前走
            continue;
        }

        {
            size_t capacity = a->init_capacity ? a->init_capacity : CB_ARENA_REGION_INIT_CAPACITY;
            if (capacity < need + align) capacity = need + align;
            a->end->next = cb__arena_new_region(capacity);
            a->end = a->end->next;
        }
    }
}

CBDEF void* cb_arena_alloc(CB_Arena* a, size_t size)
{
    return cb_arena_alloc_aligned(a, size, CB_ARENA_ALIGN);
}

CBDEF void* cb_arena_realloc(CB_Arena* a, void* old_ptr, size_t old_size, size_t new_size)
{
    if (new_size <= old_size) return old_ptr;
    void* new_ptr = cb_arena_alloc(a, new_size);
    if (old_ptr != NULL && old_size > 0) memcpy(new_ptr, old_ptr, old_size);
    return new_ptr;
}

CBDEF char* cb_arena_strdup(CB_Arena* a, const char* cstr)
{
    return cb_arena_strndup(a, cstr, strlen(cstr));
}

CBDEF char* cb_arena_strndup(CB_Arena* a, const char* cstr, size_t n)
{
    char* result = (char*)cb_arena_alloc(a, n + 1);
    memcpy(result, cstr, n);
    result[n] = '\0';
    return result;
}

CBDEF char* cb_arena_vsprintf(CB_Arena* a, const char* fmt, va_list ap)
{
    va_list args;
    va_copy(args, ap);
    int n = vsnprintf(NULL, 0, fmt, args);
    va_end(args);
    if (n < 0) n = 0;

    char* result = (char*)cb_arena_alloc(a, (size_t)n + 1);
    va_copy(args, ap);
    vsnprintf(result, (size_t)n + 1, fmt, args);
    va_end(args);
    return result;
}

CBDEF char* cb_arena_sprintf(CB_Arena* a, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    char* result = cb_arena_vsprintf(a, fmt, args);
    va_end(args);
    return result;
}

CBDEF CB_Arena_Mark cb_arena_save(CB_Arena* a)
{
    CB_Arena_Mark mark;
    mark.region = a->end;
    mark.count = a->end != NULL ? a->end->count : 0;
    return mark;
}

CBDEF void cb_arena_rewind(CB_Arena* a, CB_Arena_Mark mark)
{
    if (mark.region == NULL) { // 快照来自尚未分配过任何块的 arena
        cb_arena_reset(a);
        return;
    }
    mark.region->count = mark.count;
    for (CB_Arena_Region* r = mark.region->next; r != NULL; r = r->next) {
        r->count = 0;
    }
    a->end = mark.region;
}

CBDEF void cb_arena_reset(CB_Arena* a)
{
    for (CB_Arena_Region* r = a->begin; r != NULL; r = r->next) {
        r->count = 0;
    }
    a->end = a->begin;
}

CBDEF void cb_arena_free(CB_Arena* a)
{
    CB_Arena_Region* r = a->begin;
    while (r != NULL) {
        CB_Arena_Region* next = r->next;
        cb__arena_free_region(r);
        r = next;
    }
    a->begin = NULL;
    a->end = NULL;
}

// temp 就是 arena 的一个线程局部实例（CB__STATE 决定每 TU 一份还是全局共享）
CB__STATE(CB_THREAD_LOCAL CB_Arena, cb__temp_arena, {0, 0, CB_TEMP_CAPACITY});

CBDEF void* cb_temp_alloc(size_t size) { return cb_arena_alloc(&cb__temp_arena, size); }
CBDEF char* cb_temp_strdup(const char* cstr) { return cb_arena_strdup(&cb__temp_arena, cstr); }
CBDEF char* cb_temp_strndup(const char* cstr, size_t n) { return cb_arena_strndup(&cb__temp_arena, cstr, n); }
CBDEF char* cb_temp_vsprintf(const char* fmt, va_list ap) { return cb_arena_vsprintf(&cb__temp_arena, fmt, ap); }
CBDEF void cb_temp_reset(void) { cb_arena_reset(&cb__temp_arena); }
CBDEF CB_Arena_Mark cb_temp_save(void) { return cb_arena_save(&cb__temp_arena); }
CBDEF void cb_temp_rewind(CB_Arena_Mark mark) { cb_arena_rewind(&cb__temp_arena, mark); }
CBDEF char* cb_temp_sprintf(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    char* result = cb_arena_vsprintf(&cb__temp_arena, fmt, args);
    va_end(args);
    return result;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Dynamic Array
////////////////////////////////////////////////////////////////////////////////////////////////////
typedef struct {
    const char** items;
    size_t count;
    size_t capacity;
} CB_DArray;

#ifndef CB_DA_INIT_CAP
#define CB_DA_INIT_CAP 256
#endif // !CB_DA_INIT_CAP

#define cb_da_free(da) CB_FREE((da).items)

#define cb_da_reserve(da, new_capacity)                                                                                 \
    do {                                                                                                                \
        if ((new_capacity) > (da)->capacity) {                                                                          \
            if ((da)->capacity == 0)                                                                                    \
                (da)->capacity = CB_DA_INIT_CAP;                                                                        \
            while ((new_capacity) > (da)->capacity) {                                                                   \
                (da)->capacity *= 2;                                                                                    \
            }                                                                                                           \
            (da)->items = CB_DECLTYPE_CAST((da)->items) CB_REALLOC((da)->items, (da)->capacity * sizeof(*(da)->items)); \
            cb_alloc_check((da)->items, (da)->capacity * sizeof(*(da)->items));                                        \
        }                                                                                                               \
    } while (0)

#define cb_da_append(da, data)                \
    do {                                      \
        cb_da_reserve((da), (da)->count + 1); \
        (da)->items[(da)->count++] = (data);  \
    } while (0)


#define cb_da_append_many(da, new_items, new_items_count)                                              \
    do {                                                                                               \
        size_t cb__n = (size_t)(new_items_count);                                                      \
        /* n == 0 时 new_items 允许是 NULL，必须整个跳过（否则是 UB）                             */    \
        if (cb__n > 0) {                                                                               \
            const char* cb__src = (const char*)(new_items);                                            \
            /* 自我追加：源就在目标缓冲区里，realloc 搬走后原指针会失效，且区间重叠 */                 \
            bool cb__self = (da)->items != NULL &&                                                     \
                            cb__src >= (const char*)(da)->items &&                                      \
                            cb__src < (const char*)(da)->items + (da)->count * sizeof(*(da)->items);   \
            size_t cb__off = cb__self ? (size_t)(cb__src - (const char*)(da)->items) : 0;              \
            cb_da_reserve((da), (da)->count + cb__n);                                                  \
            if (cb__self) cb__src = (const char*)(da)->items + cb__off;                                \
            memmove((da)->items + (da)->count, cb__src, cb__n * sizeof(*(da)->items));                 \
            (da)->count += cb__n;                                                                      \
        }                                                                                              \
    } while (0)

#define cb_da_resize(da, new_size)     \
    do {                               \
        cb_da_reserve((da), new_size); \
        (da)->count = (new_size);      \
    } while (0)

#define cb_da_pop(da) (da)->items[(CB_ASSERT((da)->count > 0), --(da)->count)]
#define cb_da_first(da) (da)->items[(CB_ASSERT((da)->count > 0), 0)]
#define cb_da_last(da) (da)->items[(CB_ASSERT((da)->count > 0), (da)->count - 1)]
// 用最后一个元素覆盖被删元素（不保序）
#define cb_da_remove_unordered(da, i)                \
    do {                                             \
        size_t j = (i);                              \
        CB_ASSERT(j < (da)->count);                  \
        (da)->items[j] = (da)->items[--(da)->count]; \
    } while (0);

#define cb_da_foreach(Type, it, da) \
    for (Type* it = (da)->items; it < (da)->items + (da)->count; ++it)

// 清空内容但保留已分配的内存（供下次复用）
#define cb_da_clear(da) ((da)->count = 0)

// 在 index 处插入一个元素，后面的元素整体后移
#define cb_da_insert(da, index, data)                                      \
    do {                                                                   \
        size_t cb__i = (index);                                            \
        CB_ASSERT(cb__i <= (da)->count);                                    \
        cb_da_reserve((da), (da)->count + 1);                               \
        memmove((da)->items + cb__i + 1, (da)->items + cb__i,               \
                ((da)->count - cb__i) * sizeof(*(da)->items));               \
        (da)->items[cb__i] = (data);                                        \
        (da)->count += 1;                                                   \
    } while (0)

// 按序删除（保留其余元素的相对顺序）。元素很大且不关心顺序时用 cb_da_remove_unordered。
#define cb_da_remove_ordered(da, index)                                    \
    do {                                                                   \
        size_t cb__i = (index);                                            \
        CB_ASSERT(cb__i < (da)->count);                                     \
        memmove((da)->items + cb__i, (da)->items + cb__i + 1,               \
                ((da)->count - cb__i - 1) * sizeof(*(da)->items));           \
        (da)->count -= 1;                                                   \
    } while (0)

// 反向遍历（count 为 0 时用 items 本身，不做 items-1 的指针运算）
#define cb_da_foreach_rev(Type, it, da)                                    \
    for (Type* it = ((da)->count > 0 ? (da)->items + (da)->count - 1 : (da)->items); \
         (da)->count > 0 && it >= (da)->items; --it)

////////////////////////////////////////////////////////////////////////////////////////////////////
// Bitset
////////////////////////////////////////////////////////////////////////////////////////////////////
// 定长位图。bits 是位数；内部按 uint64_t 字存储，容量按需增长。

typedef struct {
    uint64_t* words;
    size_t word_count;
    size_t word_capacity;
    size_t bits;
} CB_Bitset;

#ifndef CB_BITSET_WORD_BITS
#define CB_BITSET_WORD_BITS 64
#endif // !CB_BITSET_WORD_BITS

// ---- 声明 ----
// 把位数调整为 bits（变大时新增位为 0，变小时直接丢弃高位）
CBDEF void cb_bitset_resize(CB_Bitset* bs, size_t bits);
CBDEF void cb_bitset_set(CB_Bitset* bs, size_t index);
CBDEF void cb_bitset_unset(CB_Bitset* bs, size_t index);
CBDEF void cb_bitset_toggle(CB_Bitset* bs, size_t index);
CBDEF bool cb_bitset_test(const CB_Bitset* bs, size_t index);
// 全部置 0（保留已分配内存）
CBDEF void cb_bitset_clear_all(CB_Bitset* bs);
// 值等于 target 的位数统计
CBDEF size_t cb_bitset_count(const CB_Bitset* bs, bool target);
// 第一个值为 target 的位的下标；没有则返回 (size_t)-1
CBDEF size_t cb_bitset_find(const CB_Bitset* bs, bool target);
CBDEF void cb_bitset_free(CB_Bitset* bs);

// ---- 定义 ----
CBDEF void cb_bitset_resize(CB_Bitset* bs, size_t bits)
{
    size_t need_words = (bits + CB_BITSET_WORD_BITS - 1) / CB_BITSET_WORD_BITS;
    if (need_words > bs->word_capacity) {
        size_t new_capacity = bs->word_capacity == 0 ? 4 : bs->word_capacity;
        while (new_capacity < need_words) new_capacity *= 2;
        size_t bytes = new_capacity * sizeof(*bs->words);
        bs->words = (uint64_t*)CB_REALLOC(bs->words, bytes);
        cb_alloc_check(bs->words, bytes);
        bs->word_capacity = new_capacity;
    }
    // 新增的字清零（缩小时只清多出来的字，便于再次扩大时是干净的）
    for (size_t i = bs->word_count; i < need_words; ++i) bs->words[i] = 0;
    for (size_t i = need_words; i < bs->word_count; ++i) bs->words[i] = 0;
    bs->word_count = need_words;
    bs->bits = bits;
}

CBDEF void cb_bitset_set(CB_Bitset* bs, size_t index)
{
    CB_ASSERT(index < bs->bits);
    bs->words[index / CB_BITSET_WORD_BITS] |= (uint64_t)1 << (index % CB_BITSET_WORD_BITS);
}

CBDEF void cb_bitset_unset(CB_Bitset* bs, size_t index)
{
    CB_ASSERT(index < bs->bits);
    bs->words[index / CB_BITSET_WORD_BITS] &= ~((uint64_t)1 << (index % CB_BITSET_WORD_BITS));
}

CBDEF void cb_bitset_toggle(CB_Bitset* bs, size_t index)
{
    CB_ASSERT(index < bs->bits);
    bs->words[index / CB_BITSET_WORD_BITS] ^= (uint64_t)1 << (index % CB_BITSET_WORD_BITS);
}

CBDEF bool cb_bitset_test(const CB_Bitset* bs, size_t index)
{
    CB_ASSERT(index < bs->bits);
    return (bs->words[index / CB_BITSET_WORD_BITS] & ((uint64_t)1 << (index % CB_BITSET_WORD_BITS))) != 0;
}

CBDEF void cb_bitset_clear_all(CB_Bitset* bs)
{
    if (bs->word_count > 0) memset(bs->words, 0, bs->word_count * sizeof(*bs->words));
}

CBDEF size_t cb_bitset_count(const CB_Bitset* bs, bool target)
{
    size_t n = 0;
    for (size_t i = 0; i < bs->bits; ++i) {
        if (cb_bitset_test(bs, i) == target) n += 1;
    }
    return n;
}

CBDEF size_t cb_bitset_find(const CB_Bitset* bs, bool target)
{
    for (size_t i = 0; i < bs->bits; ++i) {
        if (cb_bitset_test(bs, i) == target) return i;
    }
    return (size_t)-1;
}

CBDEF void cb_bitset_free(CB_Bitset* bs)
{
    CB_FREE(bs->words);
    bs->words = NULL;
    bs->word_count = 0;
    bs->word_capacity = 0;
    bs->bits = 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Ring Buffer
////////////////////////////////////////////////////////////////////////////////////////////////////
// 字节环形缓冲，FIFO。容量固定、不自动增长：写满就只写能写下的部分并返回实写字节数。

typedef struct {
    unsigned char* data;
    size_t capacity;
    size_t read_pos;
    size_t write_pos;
    size_t count; // 当前可读字节数
} CB_Ring;

// ---- 声明 ----
CBDEF bool cb_ring_init(CB_Ring* ring, size_t capacity);
CBDEF void cb_ring_free(CB_Ring* ring);
// 还能写入多少字节
CBDEF size_t cb_ring_space(const CB_Ring* ring);
// 写入，返回实际写入的字节数
CBDEF size_t cb_ring_write(CB_Ring* ring, const void* data, size_t size);
// 读出，返回实际读出的字节数
CBDEF size_t cb_ring_read(CB_Ring* ring, void* out, size_t size);
// 只看不取，返回实际拷贝的字节数
CBDEF size_t cb_ring_peek(const CB_Ring* ring, void* out, size_t size);
// 丢弃全部数据（不释放内存）
CBDEF void cb_ring_clear(CB_Ring* ring);

// ---- 定义 ----
CBDEF bool cb_ring_init(CB_Ring* ring, size_t capacity)
{
    memset(ring, 0, sizeof(*ring));
    if (capacity == 0) return false;
    ring->data = (unsigned char*)CB_REALLOC(NULL, capacity);
    cb_alloc_check(ring->data, capacity);
    ring->capacity = capacity;
    return true;
}

CBDEF void cb_ring_free(CB_Ring* ring)
{
    CB_FREE(ring->data);
    memset(ring, 0, sizeof(*ring));
}

CBDEF size_t cb_ring_space(const CB_Ring* ring)
{
    return ring->capacity - ring->count;
}

CBDEF size_t cb_ring_write(CB_Ring* ring, const void* data, size_t size)
{
    const unsigned char* src = (const unsigned char*)data;
    size_t written = 0;
    while (written < size && ring->count < ring->capacity) {
        size_t chunk = ring->capacity - ring->write_pos;
        size_t room = ring->capacity - ring->count;
        if (chunk > room) chunk = room;
        if (chunk > size - written) chunk = size - written;

        memcpy(ring->data + ring->write_pos, src + written, chunk);
        ring->write_pos = (ring->write_pos + chunk) % ring->capacity;
        ring->count += chunk;
        written += chunk;
    }
    return written;
}

CBDEF size_t cb_ring_read(CB_Ring* ring, void* out, size_t size)
{
    size_t got = cb_ring_peek(ring, out, size);
    ring->read_pos = (ring->read_pos + got) % ring->capacity;
    ring->count -= got;
    return got;
}

CBDEF size_t cb_ring_peek(const CB_Ring* ring, void* out, size_t size)
{
    unsigned char* dst = (unsigned char*)out;
    size_t done = 0;
    size_t pos = ring->read_pos;
    while (done < size && done < ring->count) {
        size_t chunk = ring->capacity - pos;
        if (chunk > ring->count - done) chunk = ring->count - done;
        if (chunk > size - done) chunk = size - done;

        memcpy(dst + done, ring->data + pos, chunk);
        pos = (pos + chunk) % ring->capacity;
        done += chunk;
    }
    return done;
}

CBDEF void cb_ring_clear(CB_Ring* ring)
{
    ring->read_pos = 0;
    ring->write_pos = 0;
    ring->count = 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Sort
////////////////////////////////////////////////////////////////////////////////////////////////////
// 作用于任何 {items,count,capacity} 动态数组。
// cb_da_sort 走 qsort：快，但不稳定（相等元素的相对顺序不保证）。
// cb_da_sort_insertion 稳定，且对"几乎有序"的数组极快，适合小数组。

typedef int (*CB_Compare_Func)(const void* a, const void* b);

// ---- 声明 ----
// 稳定插入排序。为了让函数能按字节搬动元素，需要元素大小。
CBDEF void cb__insertion_sort(void* items, size_t count, size_t elem_size, CB_Compare_Func cmp);

#define cb_da_sort(da, cmp) qsort((da)->items, (da)->count, sizeof(*(da)->items), (cmp))
#define cb_da_sort_insertion(da, cmp) \
    cb__insertion_sort((da)->items, (da)->count, sizeof(*(da)->items), (cmp))

// ---- 定义 ----
CBDEF void cb__insertion_sort(void* items, size_t count, size_t elem_size, CB_Compare_Func cmp)
{
    if (count < 2) return;

    CB_Arena_Mark mark = cb_temp_save();
    unsigned char* tmp = (unsigned char*)cb_temp_alloc(elem_size);
    unsigned char* base = (unsigned char*)items;

    for (size_t i = 1; i < count; ++i) {
        memcpy(tmp, base + i * elem_size, elem_size);
        size_t j = i;
        while (j > 0 && cmp(base + (j - 1) * elem_size, tmp) > 0) {
            memcpy(base + j * elem_size, base + (j - 1) * elem_size, elem_size);
            j -= 1;
        }
        memcpy(base + j * elem_size, tmp, elem_size);
    }

    cb_temp_rewind(mark);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// StringBuilder
////////////////////////////////////////////////////////////////////////////////////////////////////
typedef struct {
    char* items;
    size_t count;
    size_t capacity;
} CB_String_Builder;

// StringBuilder 的**核心约定**：items 永远是一个合法的 C 字符串。
// 每次追加都会在 items[count] 补 '\0'，容量按 count+1 算（count 不含这个终止符），
// 所以 cb_sb_to_sv / printf("%s") / strstr 任何时刻都安全，调用方不需要记得补 NUL。
// 想在内容里塞 '\0'，用 cb_sb_append(&sb, '\0')——那是内容，不是终止符。
CBDEF void cb__sb_terminate(CB_String_Builder* sb);

// TODO: may need to add a buffer version, more safe for memory.
CBDEF bool cb_read_entire_file(const char* path, CB_String_Builder* sb);
CBDEF int cb_sb_appendf(CB_String_Builder* sb, const char* fmt, ...) CB_PRINTF_FORMAT(2, 3);
// 用 0 把 sb 补齐到 word_size 的整数倍（构造二进制格式时用）。
// 例：内容 "aaaaa" 按 4 对齐 → "aaaaa000"。
CBDEF void cb_sb_pad_align(CB_String_Builder* sb, size_t size);

// 追加一段定长缓冲
#define cb_sb_append_buf(sb, buf, size)         \
    do {                                        \
        cb_da_append_many((sb), (buf), (size)); \
        cb__sb_terminate(sb);                   \
    } while (0)

// 追加一个 StringView
#define cb_sb_append_sv(sb, sv) cb_sb_append_buf((sb), (sv).data, (sv).count)

// 追加一个 C 字符串
#define cb_sb_append_cstr(sb, cstr)                    \
    do {                                               \
        const char* cb__s = (cstr);                    \
        cb_sb_append_buf((sb), cb__s, strlen(cb__s));  \
    } while (0)

// 追加一个字符
#define cb_sb_append(sb, ch)                 \
    do {                                     \
        char cb__ch = (char)(ch);            \
        cb_sb_append_buf((sb), &cb__ch, 1);  \
    } while (0)

// 释放内存并把字段清零，避免留下悬垂指针
#define cb_sb_free(sb)          \
    do {                        \
        CB_FREE((sb).items);    \
        (sb).items = NULL;      \
        (sb).count = 0;         \
        (sb).capacity = 0;      \
    } while (0)

CBDEF bool cb_read_entire_file(const char* path, CB_String_Builder* sb)
{
    bool result = true;

    FILE* file = fopen(path, "rb");
    size_t new_count = 0;
    long long filesize = 0;

    if (file == NULL) cb_return_defer(false);

#ifdef _WIN32
    filesize = _filelengthi64(_fileno(file));
#else
    {
        struct stat statbuf;
        if (fstat(fileno(file), &statbuf) < 0) cb_return_defer(false);
        filesize = (long long)statbuf.st_size;
    }
#endif
    if (filesize < 0) cb_return_defer(false);

    new_count = sb->count + (size_t)filesize;
    if (new_count + 1 > sb->capacity) {
        sb->items = CB_DECLTYPE_CAST(sb->items) CB_REALLOC(sb->items, new_count + 1);
        cb_alloc_check(sb->items, new_count + 1);
        sb->capacity = new_count + 1;
    }

    if (fread(sb->items + sb->count, (size_t)filesize, 1, file) != 1 && filesize > 0) {
        // ferror 不设置 errno，下面 defer 里的错误信息可能不准确
        cb_return_defer(false);
    }
    sb->count = new_count;
    sb->items[sb->count] = '\0'; // 读进来的内容直接就能当 C 字符串用

defer:
    if (!result) cb_log(CB_ERROR, "Could not read file %s: %s", path, strerror(errno));
    if (file != NULL) fclose(file);
    return result;
}

CBDEF int cb_sb_appendf(CB_String_Builder* sb, const char* fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    int n = vsnprintf(NULL, 0, fmt, args);
    va_end(args);

    cb_da_reserve(sb, sb->count + n + 1);
    char* dest = sb->items + sb->count;
    va_start(args, fmt);
    vsnprintf(dest, n + 1, fmt, args);
    va_end(args);

    sb->count += n;

    return n;
}

CBDEF void cb__sb_terminate(CB_String_Builder* sb)
{
    cb_da_reserve(sb, sb->count + 1);
    sb->items[sb->count] = '\0';
}

CBDEF void cb_sb_pad_align(CB_String_Builder* sb, size_t size)
{
    size_t rem = sb->count % size;
    if (rem == 0) return;
    for (size_t i = 0; i < size - rem; ++i) {
        cb_sb_append(sb, 0);
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// StringView
////////////////////////////////////////////////////////////////////////////////////////////////////
typedef struct {
    const char* data;
    size_t count;
} CB_String_View;

// printf 打印 StringView 用的宏
#ifndef CB_SV_FMT
// 编译期构造 String_View 字面量，比 cb_sv_from_cstr("...") 少一次 strlen。
// designator 必须按 CB_String_View 的声明顺序（data 在前、count 在后）：C 允许乱序，
// C++ 是硬错误 "designator order for field does not match declaration order"。
#define CB_SVLIT(lit) (CB_CLIT(CB_String_View){.data = (lit), .count = sizeof(lit) - 1})
// 静态初始化版本（MSVC 的 /TC 模式不接受上面那种复合字面量写法时用它）
#define CB_SVLIT_STATIC(lit) {.data = (lit), .count = sizeof(lit) - 1}

#define CB_SV_FMT "%.*s"
#endif // CB_SV_FMT
#ifndef CB_SV_ARG
#define CB_SV_ARG(sv) (int)(sv).count, (sv).data
#endif // CB_SV_ARG
// USAGE:
//   CB_String_View name = ...;
//   printf("Name: "CB_SV_FMT"\n", CB_SV_ARG(name));

// StringView 复制到 temp storage
CBDEF const char* cb_sv_to_temp_cstr(CB_String_View sv);

CBDEF bool cb_sv_eq(CB_String_View a, CB_String_View b);

CBDEF CB_String_View cb_sv_from_parts(const char* data, size_t count);
CBDEF CB_String_View cb_sv_from_cstr(const char* cstr);

// StringBuilder 转 StringView
#define cb_sb_to_sv(sb) cb_sv_from_parts((sb).items, (sb).count)

// 切到 p(当前字符) 返回假为止，返回切下来的前缀，sv 前移到剩下的部分。
// 例：CB_String_View sv = cb_sv_from_cstr("  123abc456");
//     cb_sv_chop_by_func(&sv, isspace) → "  "，sv = "123abc456"
//     cb_sv_chop_by_func(&sv, isdigit) → "123"，sv = "abc456"
// 前移视图。空视图的 data 允许是 NULL，而 NULL + 0 在 C 标准里是 UB
//（UBSan: "applying zero offset to null pointer"，clang 18 会报），所以 n == 0 直接跳过。
#define cb__sv_advance(sv, n)  \
    do {                       \
        if ((n) > 0) {         \
            (sv)->data += (n); \
            (sv)->count -= (n);\
        }                      \
    } while (0)

CBDEF CB_String_View cb_sv_chop_by_func(CB_String_View* sv, int (*p)(int x));
// 切到 delim 之前，返回切下来的部分，并丢弃这个 delim 字符。
// 例：CB_String_View sv = cb_sv_from_cstr("  1223abc456");
//     cb_sv_chop_by_delim(&sv, '2') → "  1"，sv = "23abc456"
CBDEF CB_String_View cb_sv_chop_by_delim(CB_String_View* sv, char delim);
CBDEF CB_String_View cb_sv_chop_by_delim_r(CB_String_View* sv, char delim);
CBDEF CB_String_View cb_sv_chop_left(CB_String_View* sv, size_t n);
CBDEF CB_String_View cb_sv_chop_right(CB_String_View* sv, size_t n);

// 有该前缀/后缀返回 true，否则返回 false
CBDEF bool cb_sv_ends_with(CB_String_View sv, CB_String_View suffix);
CBDEF bool cb_sv_ends_with_cstr(CB_String_View sv, const char* suffix);
CBDEF bool cb_sv_starts_with(CB_String_View sv, CB_String_View prefix);
CBDEF bool cb_sv_starts_with_cstr(CB_String_View sv, const char* prefix);

// 有该前缀就切掉并返回 true，否则返回 false
CBDEF bool cb_sv_chop_prefix(CB_String_View* sv, CB_String_View prefix);
// 以该后缀结尾就切掉并返回 true，否则返回 false
CBDEF bool cb_sv_chop_suffix(CB_String_View* sv, CB_String_View suffix);

// 返回去掉首尾空白后的新视图，不修改原视图
CBDEF CB_String_View cb_sv_trim_left(CB_String_View sv);
CBDEF CB_String_View cb_sv_trim_right(CB_String_View sv);
CBDEF CB_String_View cb_sv_trim(CB_String_View sv);

CBDEF int cb_sv_find(CB_String_View* sv, char ch);
CBDEF int cb_sv_find_sv(CB_String_View* sv, CB_String_View target, size_t start_offset);

CBDEF const char* cb_sv_to_temp_cstr(CB_String_View sv)
{
    return cb_temp_strndup(sv.data, sv.count);
}

CBDEF bool cb_sv_eq(CB_String_View a, CB_String_View b)
{
    if (a.count != b.count) {
        return false;
    } else if (a.count == 0) {
        // 两个空视图：data 允许是 NULL，memcmp(NULL, NULL, 0) 是 UB，单独挡住
        return true;
    } else {
        return memcmp(a.data, b.data, a.count) == 0;
    }
}

CBDEF CB_String_View cb_sv_from_parts(const char* data, size_t count)
{
    CB_String_View sv;
    sv.count = count;
    sv.data = data;
    return sv;
}

CBDEF CB_String_View cb_sv_from_cstr(const char* cstr)
{
    return cb_sv_from_parts(cstr, strlen(cstr));
}

CBDEF CB_String_View cb_sv_chop_by_func(CB_String_View* sv, int (*p)(int x))
{
    size_t i = 0;
    while (i < sv->count && p(sv->data[i])) {
        i += 1;
    }

    CB_String_View result = cb_sv_from_parts(sv->data, i);
    cb__sv_advance(sv, i);

    return result;
}

CBDEF CB_String_View cb_sv_chop_by_delim(CB_String_View* sv, char delim)
{
    size_t i = 0;
    while (i < sv->count && sv->data[i] != delim) {
        i += 1;
    }

    CB_String_View result = cb_sv_from_parts(sv->data, i);

    cb__sv_advance(sv, i < sv->count ? i + 1 : i);

    return result;
}

// 从右边找【最后一个】delim：返回 delim 之前的部分，sv 变成 delim 之后的部分；
// 没有 delim 时返回整个 sv 并把 sv 清空。
CBDEF CB_String_View cb_sv_chop_by_delim_r(CB_String_View* sv, char delim)
{
    size_t i = sv->count;
    while (i > 0 && sv->data[i - 1] != delim) i -= 1;

    if (i == 0) {
        // 没有分隔符：整体返回，原视图清空
        CB_String_View whole = cb_sv_from_parts(sv->data, sv->count);
        cb__sv_advance(sv, sv->count);
        return whole;
    }

    size_t delim_index = i - 1; // i 是"分隔符下标 + 1"
    CB_String_View result = cb_sv_from_parts(sv->data, delim_index);
    cb__sv_advance(sv, delim_index + 1);
    return result;
}

CBDEF CB_String_View cb_sv_chop_left(CB_String_View* sv, size_t n)
{
    if (n > sv->count) {
        n = sv->count;
    }

    CB_String_View result = cb_sv_from_parts(sv->data, n);
    cb__sv_advance(sv, n);

    return result;
}

CBDEF CB_String_View cb_sv_chop_right(CB_String_View* sv, size_t n)
{
    if (n > sv->count) {
        n = sv->count;
    }

    // n == 0 时不要写 sv->data + sv->count - 0（空视图的 data 可能是 NULL）
    CB_String_View result = cb_sv_from_parts(n > 0 ? sv->data + sv->count - n : sv->data, n);
    sv->count -= n;

    return result;
}

CBDEF bool cb_sv_ends_with(CB_String_View sv, CB_String_View suffix)
{
    if (suffix.count > sv.count) {
        return false;
    }
    // 空后缀：任何视图都以它结尾。单独挡住这一支也避免对 NULL 做指针运算
    // （sv.count == 0 时 sv.data 允许是 NULL，而 NULL + 0 按标准是 UB）。
    if (suffix.count == 0) {
        return true;
    }

    CB_String_View sv_tail = {
        .data = sv.data + sv.count - suffix.count,
        .count = suffix.count,
    };
    return cb_sv_eq(sv_tail, suffix);
}

CBDEF bool cb_sv_ends_with_cstr(CB_String_View sv, const char* suffix)
{
    return cb_sv_ends_with(sv, cb_sv_from_cstr(suffix));
}

CBDEF bool cb_sv_starts_with(CB_String_View sv, CB_String_View prefix)
{
    if (prefix.count > sv.count) {
        return false;
    }

    CB_String_View actual_prefix = cb_sv_from_parts(sv.data, prefix.count);
    return cb_sv_eq(prefix, actual_prefix);
}

CBDEF bool cb_sv_starts_with_cstr(CB_String_View sv, const char* prefix)
{
    return cb_sv_starts_with(sv, cb_sv_from_cstr(prefix));
}

CBDEF bool cb_sv_chop_prefix(CB_String_View* sv, CB_String_View prefix)
{
    if (cb_sv_starts_with(*sv, prefix)) {
        cb_sv_chop_left(sv, prefix.count);
        return true;
    }
    return false;
}

CBDEF bool cb_sv_chop_suffix(CB_String_View* sv, CB_String_View suffix)
{
    if (cb_sv_ends_with(*sv, suffix)) {
        cb_sv_chop_right(sv, suffix.count);
        return true;
    }
    return false;
}

CBDEF CB_String_View cb_sv_trim_left(CB_String_View sv)
{
    size_t i = 0;
    while (i < sv.count && isspace(sv.data[i])) {
        i += 1;
    }

    return cb_sv_from_parts(i > 0 ? sv.data + i : sv.data, sv.count - i);
}

CBDEF CB_String_View cb_sv_trim_right(CB_String_View sv)
{
    size_t i = 0;
    while (i < sv.count && isspace(sv.data[sv.count - 1 - i])) {
        i += 1;
    }

    return cb_sv_from_parts(sv.data, sv.count - i);
}

CBDEF CB_String_View cb_sv_trim(CB_String_View sv)
{
    return cb_sv_trim_right(cb_sv_trim_left(sv));
}

// TODO: add find reverse version?
CBDEF int cb_sv_find(CB_String_View* sv, char ch)
{
    for (size_t i = 0; i < sv->count; i++) {
        if (sv->data[i] == ch) return i;
    }
    return -1;
}

CBDEF int cb_sv_find_sv(CB_String_View* sv, CB_String_View target, size_t start_offset)
{
    if (sv->count <= 0 || target.count <= 0) return -1;
    if (start_offset >= sv->count - target.count) return -1;
    if (target.count > sv->count - start_offset) return -1;

    size_t limit = sv->count - target.count + 1;
    for (size_t i = start_offset; i < limit; i++) {
        if (memcmp(sv->data + i, target.data, target.count) == 0) return i;
    }
    return -1;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// UTF-8 Support
////////////////////////////////////////////////////////////////////////////////////////////////////
static const uint8_t cb_bytes_for_utf8[] = {
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2, 2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3, 4,4,4,4,4,4,4,4,5,5,5,5,6,6,6,6,
};

CBDEF size_t cb_sv_utf8_len(CB_String_View sv, size_t* bytes_overrun);

CBDEF size_t cb_sv_utf8_len(CB_String_View sv, size_t* bytes_overrun)
{
    size_t i = 0;
    size_t n = 0;
    while (true) {
        if (i >= sv.count) {
            if (bytes_overrun) *bytes_overrun = i - sv.count;
            return n;
        }
        i += cb_bytes_for_utf8[(uint8_t)sv.data[i]];
        n += 1;
    }
    CB_UNREACHABLE("cb_sv_utf8_len");
    return 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// StringView Tools（大小写 / 数字解析 / split-join）
////////////////////////////////////////////////////////////////////////////////////////////////////
// ---- 声明 ----
// 大小写转换（结果在 temp storage 上）
CBDEF char* cb_sv_to_temp_upper(CB_String_View sv);
CBDEF char* cb_sv_to_temp_lower(CB_String_View sv);
CBDEF bool cb_sv_eq_ignore_case(CB_String_View a, CB_String_View b);

// 数字解析：严格解析，允许首尾空白；成功返回 true 并写 *out，失败返回 false 且不动 *out。
// 支持 0x/0X 十六进制、0b/0B 二进制、0o/0O 八进制前缀；整数不识别小数点与指数。
CBDEF bool cb_sv_to_i64(CB_String_View sv, int64_t* out);
CBDEF bool cb_sv_to_u64(CB_String_View sv, uint64_t* out);
CBDEF bool cb_sv_to_f64(CB_String_View sv, double* out);

// 逐个取出被 delim 分隔的片段，sv 会被就地消耗。
// 例：while (cb_sv_split_next(&sv, ',', &part)) { ... }
CBDEF bool cb_sv_split_next(CB_String_View* sv, char delim, CB_String_View* out);
// 用 sep 把 parts 连接起来追加到 sb（不追加结尾分隔符）
CBDEF void cb_sb_append_join(CB_String_Builder* sb, const CB_String_View* parts, size_t count, CB_String_View sep);

// utf8：解码一个码点
CBDEF bool cb_utf8_decode(const char* data, size_t size, uint32_t* out_codepoint, size_t* out_length);
// 编码一个码点，写入 out（至少 4 字节），返回写入长度；非法码点返回 0
CBDEF size_t cb_utf8_encode(uint32_t codepoint, char out[4]);
// 从 sv 头部取一个码点并前移 sv
CBDEF bool cb_sv_utf8_next(CB_String_View* sv, uint32_t* out_codepoint);
// 校验整段是否为合法 utf8；非法时通过 out_bad_offset 给出出错的下标
CBDEF bool cb_utf8_validate(CB_String_View sv, size_t* out_bad_offset);

// ---- 定义 ----
CBDEF char* cb_sv_to_temp_upper(CB_String_View sv)
{
    char* result = cb_temp_strndup(sv.data, sv.count);
    for (size_t i = 0; i < sv.count; ++i) result[i] = (char)toupper((unsigned char)result[i]);
    return result;
}

CBDEF char* cb_sv_to_temp_lower(CB_String_View sv)
{
    char* result = cb_temp_strndup(sv.data, sv.count);
    for (size_t i = 0; i < sv.count; ++i) result[i] = (char)tolower((unsigned char)result[i]);
    return result;
}

CBDEF bool cb_sv_eq_ignore_case(CB_String_View a, CB_String_View b)
{
    if (a.count != b.count) return false;
    for (size_t i = 0; i < a.count; ++i) {
        if (tolower((unsigned char)a.data[i]) != tolower((unsigned char)b.data[i])) return false;
    }
    return true;
}

// 内部：去掉首尾空白、解析可选符号与进制前缀
CBDEF bool cb__sv_number_parts(CB_String_View sv, CB_String_View* out_digits, int* out_base, bool* out_negative)
{
    sv = cb_sv_trim(sv);
    if (sv.count == 0) return false;

    bool negative = false;
    if (sv.data[0] == '+' || sv.data[0] == '-') {
        negative = sv.data[0] == '-';
        sv.data += 1;
        sv.count -= 1;
    }
    if (sv.count == 0) return false;

    int base = 10;
    if (sv.count >= 2 && sv.data[0] == '0') {
        char c = sv.data[1];
        if (c == 'x' || c == 'X') {
            base = 16;
        } else if (c == 'b' || c == 'B') {
            base = 2;
        } else if (c == 'o' || c == 'O') {
            base = 8;
        }
        if (base != 10) {
            sv.data += 2;
            sv.count -= 2;
            if (sv.count == 0) return false;
        }
    }

    *out_digits = sv;
    *out_base = base;
    *out_negative = negative;
    return true;
}

CBDEF bool cb__sv_digit_value(char c, int base, int* out)
{
    int v;
    if (c >= '0' && c <= '9') {
        v = c - '0';
    } else if (c >= 'a' && c <= 'z') {
        v = c - 'a' + 10;
    } else if (c >= 'A' && c <= 'Z') {
        v = c - 'A' + 10;
    } else {
        return false;
    }
    if (v >= base) return false;
    *out = v;
    return true;
}

CBDEF bool cb_sv_to_u64(CB_String_View sv, uint64_t* out)
{
    CB_String_View digits = CB_ZERO;
    int base = 10;
    bool negative = false;
    if (!cb__sv_number_parts(sv, &digits, &base, &negative)) return false;
    if (negative) return false; // 无符号不接受负号

    uint64_t value = 0;
    for (size_t i = 0; i < digits.count; ++i) {
        int d;
        if (digits.data[i] == '_') continue; // 允许 1_000_000 这种写法
        if (!cb__sv_digit_value(digits.data[i], base, &d)) return false;
        if (value > (UINT64_MAX - (uint64_t)d) / (uint64_t)base) return false; // 溢出
        value = value * (uint64_t)base + (uint64_t)d;
    }
    *out = value;
    return true;
}

CBDEF bool cb_sv_to_i64(CB_String_View sv, int64_t* out)
{
    CB_String_View digits = CB_ZERO;
    int base = 10;
    bool negative = false;
    if (!cb__sv_number_parts(sv, &digits, &base, &negative)) return false;

    uint64_t limit = negative ? (uint64_t)INT64_MAX + 1u : (uint64_t)INT64_MAX;
    uint64_t magnitude = 0;
    for (size_t i = 0; i < digits.count; ++i) {
        int d;
        if (digits.data[i] == '_') continue;
        if (!cb__sv_digit_value(digits.data[i], base, &d)) return false;
        if (magnitude > (limit - (uint64_t)d) / (uint64_t)base) return false; // 溢出
        magnitude = magnitude * (uint64_t)base + (uint64_t)d;
    }

    if (negative && magnitude == (uint64_t)INT64_MAX + 1u) {
        *out = INT64_MIN;
    } else if (negative) {
        *out = -(int64_t)magnitude;
    } else {
        *out = (int64_t)magnitude;
    }
    return true;
}

CBDEF bool cb_sv_to_f64(CB_String_View sv, double* out)
{
    sv = cb_sv_trim(sv);
    if (sv.count == 0) return false;

    // 浮点的语法规则繁琐，交给 strtod，先复制成 NUL 结尾的临时串
    char* cstr = cb_temp_strndup(sv.data, sv.count);
    errno = 0;
    char* end = NULL;
    double value = strtod(cstr, &end);
    if (end == cstr) return false;            // 一个字符都没解析出来
    if (end != cstr + sv.count) return false; // 有残留，说明不是纯数字
    if (errno == ERANGE) return false;        // 溢出/下溢
    *out = value;
    return true;
}

CBDEF bool cb_sv_split_next(CB_String_View* sv, char delim, CB_String_View* out)
{
    if (sv->count == 0) return false;
    *out = cb_sv_chop_by_delim(sv, delim);
    return true;
}

CBDEF void cb_sb_append_join(CB_String_Builder* sb, const CB_String_View* parts, size_t count, CB_String_View sep)
{
    for (size_t i = 0; i < count; ++i) {
        if (i > 0) cb_sb_append_sv(sb, sep);
        cb_sb_append_sv(sb, parts[i]);
    }
}

CBDEF bool cb_utf8_decode(const char* data, size_t size, uint32_t* out_codepoint, size_t* out_length)
{
    if (size == 0) return false;

    const unsigned char* bytes = (const unsigned char*)data;
    unsigned char first = bytes[0];

    // 续字节 0x80-0xBF 不能当首字节，必须显式挡掉：cb_bytes_for_utf8[0x80..0xBF] 是 1
    // （表值不能改成 0——cb_sv_utf8_len 里 i += 0 会死循环），不挡的话孤立的续字节
    // 会被当成合法的单字节字符 U+0080。
    if (first >= 0x80 && first <= 0xBF) return false;

    size_t length = cb_bytes_for_utf8[first];
    if (length == 0 || length > size || length > 4) return false;

    uint32_t codepoint = 0;
    if (length == 1) {
        codepoint = first;
    } else {
        static const uint32_t first_mask[] = {0, 0x7F, 0x1F, 0x0F, 0x07};
        static const uint32_t min_value[] = {0, 0, 0x80, 0x800, 0x10000};

        codepoint = first & first_mask[length];
        for (size_t i = 1; i < length; ++i) {
            if ((bytes[i] & 0xC0) != 0x80) return false; // 后续字节必须是 10xxxxxx
            codepoint = (codepoint << 6) | (uint32_t)(bytes[i] & 0x3F);
        }
        if (codepoint < min_value[length]) return false;              // 过长编码（overlong）
        if (codepoint > 0x10FFFF) return false;                       // 超出 Unicode 范围
        if (codepoint >= 0xD800 && codepoint <= 0xDFFF) return false; // 代理区
    }

    if (out_codepoint) *out_codepoint = codepoint;
    if (out_length) *out_length = length;
    return true;
}

CBDEF size_t cb_utf8_encode(uint32_t codepoint, char out[4])
{
    if (codepoint > 0x10FFFF) return 0;
    if (codepoint >= 0xD800 && codepoint <= 0xDFFF) return 0;

    if (codepoint < 0x80) {
        out[0] = (char)codepoint;
        return 1;
    }
    if (codepoint < 0x800) {
        out[0] = (char)(0xC0 | (codepoint >> 6));
        out[1] = (char)(0x80 | (codepoint & 0x3F));
        return 2;
    }
    if (codepoint < 0x10000) {
        out[0] = (char)(0xE0 | (codepoint >> 12));
        out[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        out[2] = (char)(0x80 | (codepoint & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (codepoint >> 18));
    out[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
    out[3] = (char)(0x80 | (codepoint & 0x3F));
    return 4;
}

CBDEF bool cb_sv_utf8_next(CB_String_View* sv, uint32_t* out_codepoint)
{
    size_t length = 0;
    if (!cb_utf8_decode(sv->data, sv->count, out_codepoint, &length)) return false;
    sv->data += length;
    sv->count -= length;
    return true;
}

CBDEF bool cb_utf8_validate(CB_String_View sv, size_t* out_bad_offset)
{
    size_t offset = 0;
    while (offset < sv.count) {
        size_t length = 0;
        if (!cb_utf8_decode(sv.data + offset, sv.count - offset, NULL, &length)) {
            if (out_bad_offset) *out_bad_offset = offset;
            return false;
        }
        offset += length;
    }
    return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// HashMap
////////////////////////////////////////////////////////////////////////////////////////////////////
// 开放寻址 + 线性探测。键会被复制进 map，所以调用方不必关心字符串的生命周期。
//
// 后端：
//   - 默认走 CB_REALLOC / CB_FREE
//   - cb_map_init_arena(&m, &arena, n) 之后全部内存从 arena 取。
//     注意 arena 不支持单独释放，重哈希产生的旧槽位会留到 cb_arena_reset 一次性回收。
//
// 用法：
//   CB_Map m = CB_ZERO;
//   cb_map_put_cstr(&m, "answer", (void*)(intptr_t)42);
//   void* v = NULL;
//   if (cb_map_get_cstr(&m, "answer", &v)) { ... }
//   cb_map_foreach(&m, it) { printf("%.*s\n", (int)it.key.count, it.key.data); }
//   cb_map_free(&m);

#ifndef CB_MAP_INIT_CAPACITY
#define CB_MAP_INIT_CAPACITY 16
#endif // !CB_MAP_INIT_CAPACITY

#define CB_MAP_EMPTY 0
#define CB_MAP_USED 1
#define CB_MAP_TOMBSTONE 2

typedef struct {
    CB_String_View key; // key.data 指向 map 自己拷贝的一份
    void* value;
    uint64_t hash; // 缓存哈希，重哈希时不必重算
    uint8_t state;
} CB_Map_Slot;

typedef struct {
    CB_Map_Slot* slots;
    size_t capacity; // 槽位数，始终是 2 的幂；0 表示尚未分配
    size_t count;    // 活跃条目数
    size_t tombstones;
    CB_Arena* arena; // 非 NULL 时所有内存从 arena 取
} CB_Map;

typedef struct {
    CB_Map* map;
    size_t index;
    CB_String_View key; // 当前条目（由 cb_map_next 填充）
    void* value;
} CB_Map_Iter;

// ---- 哈希 ----
CBDEF uint64_t cb_hash_bytes(const void* data, size_t size);
CBDEF uint64_t cb_hash_u64(uint64_t value);

// ---- 声明 ----
CBDEF void cb_map_init_capacity(CB_Map* map, size_t capacity);
// arena 后端：map 的所有内存都从 arena 取。arena 必须活得比 map 久。
CBDEF void cb_map_init_arena(CB_Map* map, CB_Arena* arena, size_t capacity);
// 插入或覆盖（键会被复制）。value 允许为 NULL。
CBDEF void cb_map_put(CB_Map* map, CB_String_View key, void* value);
CBDEF void cb_map_put_cstr(CB_Map* map, const char* key, void* value);
// 查不到返回 false 且不动 *out。out 允许为 NULL（只判断存在性）。
CBDEF bool cb_map_get(const CB_Map* map, CB_String_View key, void** out);
CBDEF bool cb_map_get_cstr(const CB_Map* map, const char* key, void** out);
CBDEF bool cb_map_has(const CB_Map* map, CB_String_View key);
CBDEF bool cb_map_del(CB_Map* map, CB_String_View key);
CBDEF size_t cb_map_count(const CB_Map* map);
// 清空条目但保留已分配容量
CBDEF void cb_map_clear(CB_Map* map);
CBDEF void cb_map_free(CB_Map* map);
CBDEF CB_Map_Iter cb_map_iter(CB_Map* map);
CBDEF bool cb_map_next(CB_Map_Iter* it);
#define cb_map_foreach(map, it) \
    for (CB_Map_Iter it = cb_map_iter(map); cb_map_next(&it);)

// ---- 定义 ----
CBDEF uint64_t cb_hash_bytes(const void* data, size_t size)
{
    // FNV-1a 64 位：短键表现好、实现简单
    const unsigned char* p = (const unsigned char*)data;
    uint64_t hash = 1469598103934665603ull;
    for (size_t i = 0; i < size; ++i) {
        hash ^= (uint64_t)p[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

CBDEF uint64_t cb_hash_u64(uint64_t value)
{
    // splitmix64 的收尾混合：把顺序整数打散，避免扎堆
    value += 0x9E3779B97F4A7C15ull;
    value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ull;
    value = (value ^ (value >> 27)) * 0x94D049BB133111EBull;
    return value ^ (value >> 31);
}

CBDEF void* cb__map_alloc(CB_Map* map, size_t size)
{
    if (map->arena != NULL) return cb_arena_alloc(map->arena, size);
    void* p = CB_REALLOC(NULL, size);
    cb_alloc_check(p, size);
    return p;
}

CBDEF void cb__map_dealloc(CB_Map* map, void* ptr)
{
    if (map->arena != NULL) return; // arena 不支持单独释放
    CB_FREE(ptr);
}

CBDEF CB_Map_Slot* cb__map_find_slot(const CB_Map* map, CB_String_View key, uint64_t hash, bool for_insert)
{
    if (map->capacity == 0) return NULL;

    size_t mask = map->capacity - 1;
    size_t i = (size_t)hash & mask;
    CB_Map_Slot* first_tombstone = NULL;

    for (size_t probe = 0; probe < map->capacity; ++probe) {
        CB_Map_Slot* slot = &map->slots[i];
        if (slot->state == CB_MAP_EMPTY) {
            if (for_insert) return first_tombstone != NULL ? first_tombstone : slot;
            return NULL;
        }
        if (slot->state == CB_MAP_TOMBSTONE) {
            if (first_tombstone == NULL) first_tombstone = slot;
        } else if (slot->hash == hash && cb_sv_eq(slot->key, key)) {
            return slot;
        }
        i = (i + 1) & mask;
    }
    return for_insert ? first_tombstone : NULL;
}

// 一次性重哈希到指定容量（必须是 2 的幂，且大于现有容量）
CBDEF void cb__map_resize_to(CB_Map* map, size_t new_capacity)
{
    CB_Map_Slot* new_slots = (CB_Map_Slot*)cb__map_alloc(map, new_capacity * sizeof(CB_Map_Slot));
    memset(new_slots, 0, new_capacity * sizeof(CB_Map_Slot));

    CB_Map_Slot* old_slots = map->slots;
    size_t old_capacity = map->capacity;

    map->slots = new_slots;
    map->capacity = new_capacity;
    map->count = 0;
    map->tombstones = 0;

    for (size_t i = 0; i < old_capacity; ++i) {
        if (old_slots[i].state != CB_MAP_USED) continue;
        // 键内存不复制，直接把指针搬过去
        CB_Map_Slot* dst = cb__map_find_slot(map, old_slots[i].key, old_slots[i].hash, true);
        *dst = old_slots[i];
        map->count += 1;
    }

    if (map->arena == NULL) CB_FREE(old_slots);
}

// 扩容一步到位（反复 grow 会在 arena 后端留下一串废弃槽位数组）
CBDEF void cb__map_grow(CB_Map* map)
{
    cb__map_resize_to(map, map->capacity == 0 ? CB_MAP_INIT_CAPACITY : map->capacity * 2);
}

// 预分配。map 必须先是零初始化的（CB_ZERO）。
CBDEF void cb_map_init_capacity(CB_Map* map, size_t capacity)
{
    CB_ASSERT(map->slots == NULL && map->capacity == 0 && "map 必须先零初始化，且不能重复 init");
    if (capacity == 0) return;

    // 向上取到 2 的幂，并保证负载因子不超过 3/4
    size_t wanted = capacity * 4 / 3 + 1;
    size_t power = CB_MAP_INIT_CAPACITY;
    while (power < wanted) power *= 2;
    cb__map_resize_to(map, power);
}

// arena 后端。arena 必须活得比 map 久；map 的槽位与键都从这里分配。
CBDEF void cb_map_init_arena(CB_Map* map, CB_Arena* arena, size_t capacity)
{
    map->arena = arena; // 必须先设，否则初始槽位会走 malloc
    cb_map_init_capacity(map, capacity);
}

CBDEF void cb_map_put(CB_Map* map, CB_String_View key, void* value)
{
    if (map->capacity == 0) cb__map_grow(map);
    // 负载因子（含墓碑）到 3/4 就扩容，保证探测链不会太长
    if ((map->count + map->tombstones + 1) * 4 >= map->capacity * 3) cb__map_grow(map);

    uint64_t hash = cb_hash_bytes(key.data, key.count);
    CB_Map_Slot* slot = cb__map_find_slot(map, key, hash, true);
    CB_ASSERT(slot != NULL);

    if (slot->state == CB_MAP_USED) {
        slot->value = value; // 键已存在：只覆盖值，不重复拷贝键
        return;
    }
    if (slot->state == CB_MAP_TOMBSTONE) map->tombstones -= 1;

    char* key_copy = (char*)cb__map_alloc(map, key.count + 1);
    memcpy(key_copy, key.data, key.count);
    key_copy[key.count] = '\0';

    slot->key = cb_sv_from_parts(key_copy, key.count);
    slot->value = value;
    slot->hash = hash;
    slot->state = CB_MAP_USED;
    map->count += 1;
}

CBDEF void cb_map_put_cstr(CB_Map* map, const char* key, void* value)
{
    cb_map_put(map, cb_sv_from_cstr(key), value);
}

CBDEF bool cb_map_get(const CB_Map* map, CB_String_View key, void** out)
{
    if (map->capacity == 0) return false;
    uint64_t hash = cb_hash_bytes(key.data, key.count);
    CB_Map_Slot* slot = cb__map_find_slot(map, key, hash, false);
    if (slot == NULL) return false;
    if (out != NULL) *out = slot->value;
    return true;
}

CBDEF bool cb_map_get_cstr(const CB_Map* map, const char* key, void** out)
{
    return cb_map_get(map, cb_sv_from_cstr(key), out);
}

CBDEF bool cb_map_has(const CB_Map* map, CB_String_View key)
{
    return cb_map_get(map, key, NULL);
}

CBDEF size_t cb_map_count(const CB_Map* map)
{
    return map->count;
}

CBDEF bool cb_map_del(CB_Map* map, CB_String_View key)
{
    if (map->capacity == 0) return false;
    uint64_t hash = cb_hash_bytes(key.data, key.count);
    CB_Map_Slot* slot = cb__map_find_slot(map, key, hash, false);
    if (slot == NULL) return false;

    cb__map_dealloc(map, (void*)slot->key.data);
    slot->key = cb_sv_from_parts(NULL, 0);
    slot->value = NULL;
    slot->hash = 0;
    slot->state = CB_MAP_TOMBSTONE;
    map->count -= 1;
    map->tombstones += 1;
    return true;
}

CBDEF void cb_map_clear(CB_Map* map)
{
    for (size_t i = 0; i < map->capacity; ++i) {
        if (map->slots[i].state == CB_MAP_USED) {
            cb__map_dealloc(map, (void*)map->slots[i].key.data);
        }
    }
    if (map->capacity > 0) memset(map->slots, 0, map->capacity * sizeof(CB_Map_Slot));
    map->count = 0;
    map->tombstones = 0;
}

CBDEF void cb_map_free(CB_Map* map)
{
    cb_map_clear(map);
    if (map->arena == NULL) CB_FREE(map->slots);
    map->slots = NULL;
    map->capacity = 0;
    map->count = 0;
    map->tombstones = 0;
    map->arena = NULL; // 摘掉 arena，map 变量可以复用
}

CBDEF CB_Map_Iter cb_map_iter(CB_Map* map)
{
    CB_Map_Iter it = CB_ZERO;
    it.map = map;
    it.index = 0;
    return it;
}

CBDEF bool cb_map_next(CB_Map_Iter* it)
{
    while (it->index < it->map->capacity) {
        CB_Map_Slot* slot = &it->map->slots[it->index++];
        if (slot->state == CB_MAP_USED) {
            it->key = slot->key;
            it->value = slot->value;
            return true;
        }
    }
    return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// HashMap：uint64 键版本
////////////////////////////////////////////////////////////////////////////////////////////////////
// 与 CB_Map 同样的开放寻址实现，只是键是整数、哈希与比较更直接。
// 键内存不需要单独分配，因此比字符串键版本更省。

typedef struct {
    uint64_t key;
    void* value;
    uint64_t hash;
    uint8_t state;
} CB_Map_U64_Slot;

typedef struct {
    CB_Map_U64_Slot* slots;
    size_t capacity;
    size_t count;
    size_t tombstones;
    CB_Arena* arena;
} CB_Map_U64;

typedef struct {
    CB_Map_U64* map;
    size_t index;
    uint64_t key;
    void* value;
} CB_Map_U64_Iter;

// ---- 声明 ----
CBDEF void cb_map_u64_init_capacity(CB_Map_U64* map, size_t capacity);
CBDEF void cb_map_u64_init_arena(CB_Map_U64* map, CB_Arena* arena, size_t capacity);
CBDEF void cb_map_u64_put(CB_Map_U64* map, uint64_t key, void* value);
CBDEF bool cb_map_u64_get(const CB_Map_U64* map, uint64_t key, void** out);
CBDEF bool cb_map_u64_has(const CB_Map_U64* map, uint64_t key);
CBDEF bool cb_map_u64_del(CB_Map_U64* map, uint64_t key);
CBDEF size_t cb_map_u64_count(const CB_Map_U64* map);
CBDEF void cb_map_u64_clear(CB_Map_U64* map);
CBDEF void cb_map_u64_free(CB_Map_U64* map);
CBDEF CB_Map_U64_Iter cb_map_u64_iter(CB_Map_U64* map);
CBDEF bool cb_map_u64_next(CB_Map_U64_Iter* it);
#define cb_map_u64_foreach(map, it) \
    for (CB_Map_U64_Iter it = cb_map_u64_iter(map); cb_map_u64_next(&it);)

// ---- 定义 ----
CBDEF void* cb__map_u64_alloc(CB_Map_U64* map, size_t size)
{
    if (map->arena != NULL) return cb_arena_alloc(map->arena, size);
    void* p = CB_REALLOC(NULL, size);
    cb_alloc_check(p, size);
    return p;
}

CBDEF CB_Map_U64_Slot* cb__map_u64_find_slot(const CB_Map_U64* map, uint64_t key, uint64_t hash, bool for_insert)
{
    if (map->capacity == 0) return NULL;

    size_t mask = map->capacity - 1;
    size_t i = (size_t)hash & mask;
    CB_Map_U64_Slot* first_tombstone = NULL;

    for (size_t probe = 0; probe < map->capacity; ++probe) {
        CB_Map_U64_Slot* slot = &map->slots[i];
        if (slot->state == CB_MAP_EMPTY) {
            if (for_insert) return first_tombstone != NULL ? first_tombstone : slot;
            return NULL;
        }
        if (slot->state == CB_MAP_TOMBSTONE) {
            if (first_tombstone == NULL) first_tombstone = slot;
        } else if (slot->hash == hash && slot->key == key) {
            return slot;
        }
        i = (i + 1) & mask;
    }
    return for_insert ? first_tombstone : NULL;
}

CBDEF void cb__map_u64_resize_to(CB_Map_U64* map, size_t new_capacity)
{
    CB_Map_U64_Slot* new_slots = (CB_Map_U64_Slot*)cb__map_u64_alloc(map, new_capacity * sizeof(CB_Map_U64_Slot));
    memset(new_slots, 0, new_capacity * sizeof(CB_Map_U64_Slot));

    CB_Map_U64_Slot* old_slots = map->slots;
    size_t old_capacity = map->capacity;

    map->slots = new_slots;
    map->capacity = new_capacity;
    map->count = 0;
    map->tombstones = 0;

    for (size_t i = 0; i < old_capacity; ++i) {
        if (old_slots[i].state != CB_MAP_USED) continue;
        CB_Map_U64_Slot* dst = cb__map_u64_find_slot(map, old_slots[i].key, old_slots[i].hash, true);
        *dst = old_slots[i];
        map->count += 1;
    }

    if (map->arena == NULL) CB_FREE(old_slots);
}

CBDEF void cb__map_u64_grow(CB_Map_U64* map)
{
    cb__map_u64_resize_to(map, map->capacity == 0 ? CB_MAP_INIT_CAPACITY : map->capacity * 2);
}

CBDEF void cb_map_u64_init_capacity(CB_Map_U64* map, size_t capacity)
{
    CB_ASSERT(map->slots == NULL && map->capacity == 0 && "map 必须先零初始化，且不能重复 init");
    if (capacity == 0) return;

    size_t wanted = capacity * 4 / 3 + 1;
    size_t power = CB_MAP_INIT_CAPACITY;
    while (power < wanted) power *= 2;
    cb__map_u64_resize_to(map, power);
}

CBDEF void cb_map_u64_init_arena(CB_Map_U64* map, CB_Arena* arena, size_t capacity)
{
    map->arena = arena; // 必须先设，否则初始槽位会走 malloc
    cb_map_u64_init_capacity(map, capacity);
}

CBDEF void cb_map_u64_put(CB_Map_U64* map, uint64_t key, void* value)
{
    if (map->capacity == 0) cb__map_u64_grow(map);
    if ((map->count + map->tombstones + 1) * 4 >= map->capacity * 3) cb__map_u64_grow(map);

    uint64_t hash = cb_hash_u64(key);
    CB_Map_U64_Slot* slot = cb__map_u64_find_slot(map, key, hash, true);
    CB_ASSERT(slot != NULL);

    if (slot->state == CB_MAP_USED) {
        slot->value = value;
        return;
    }
    if (slot->state == CB_MAP_TOMBSTONE) map->tombstones -= 1;

    slot->key = key;
    slot->value = value;
    slot->hash = hash;
    slot->state = CB_MAP_USED;
    map->count += 1;
}

CBDEF bool cb_map_u64_get(const CB_Map_U64* map, uint64_t key, void** out)
{
    if (map->capacity == 0) return false;
    uint64_t hash = cb_hash_u64(key);
    CB_Map_U64_Slot* slot = cb__map_u64_find_slot(map, key, hash, false);
    if (slot == NULL) return false;
    if (out != NULL) *out = slot->value;
    return true;
}

CBDEF bool cb_map_u64_has(const CB_Map_U64* map, uint64_t key)
{
    return cb_map_u64_get(map, key, NULL);
}

CBDEF size_t cb_map_u64_count(const CB_Map_U64* map)
{
    return map->count;
}

CBDEF bool cb_map_u64_del(CB_Map_U64* map, uint64_t key)
{
    if (map->capacity == 0) return false;
    uint64_t hash = cb_hash_u64(key);
    CB_Map_U64_Slot* slot = cb__map_u64_find_slot(map, key, hash, false);
    if (slot == NULL) return false;

    slot->key = 0;
    slot->value = NULL;
    slot->hash = 0;
    slot->state = CB_MAP_TOMBSTONE;
    map->count -= 1;
    map->tombstones += 1;
    return true;
}

CBDEF void cb_map_u64_clear(CB_Map_U64* map)
{
    if (map->capacity > 0) memset(map->slots, 0, map->capacity * sizeof(CB_Map_U64_Slot));
    map->count = 0;
    map->tombstones = 0;
}

CBDEF void cb_map_u64_free(CB_Map_U64* map)
{
    if (map->arena == NULL) CB_FREE(map->slots);
    map->slots = NULL;
    map->capacity = 0;
    map->count = 0;
    map->tombstones = 0;
    map->arena = NULL;
}

CBDEF CB_Map_U64_Iter cb_map_u64_iter(CB_Map_U64* map)
{
    CB_Map_U64_Iter it = CB_ZERO;
    it.map = map;
    it.index = 0;
    return it;
}

CBDEF bool cb_map_u64_next(CB_Map_U64_Iter* it)
{
    while (it->index < it->map->capacity) {
        CB_Map_U64_Slot* slot = &it->map->slots[it->index++];
        if (slot->state == CB_MAP_USED) {
            it->key = slot->key;
            it->value = slot->value;
            return true;
        }
    }
    return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// File System
////////////////////////////////////////////////////////////////////////////////////////////////////
#ifdef _WIN32
// 参考 https://stackoverflow.com/a/75644008（FormatMessageW 用 4096 * sizeof(WCHAR) 的栈缓冲）
#ifndef CB_WIN32_ERR_MSG_SIZE
#define CB_WIN32_ERR_MSG_SIZE (4 * 1024)
#endif // CB_WIN32_ERR_MSG_SIZE

CBDEF char* cb_win32_error_message(DWORD err)
{
    static char win32ErrMsg[CB_WIN32_ERR_MSG_SIZE] = CB_ZERO;
    DWORD errMsgSize = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, err, LANG_USER_DEFAULT, win32ErrMsg,
                                      CB_WIN32_ERR_MSG_SIZE, NULL);

    if (errMsgSize == 0) {
        if (GetLastError() != ERROR_MR_MID_NOT_FOUND) {
            if (sprintf(win32ErrMsg, "Could not get error message for 0x%lX", err) > 0) {
                return (char*)&win32ErrMsg;
            } else {
                return NULL;
            }
        } else {
            if (sprintf(win32ErrMsg, "Invalid Windows Error code (0x%lX)", err) > 0) {
                return (char*)&win32ErrMsg;
            } else {
                return NULL;
            }
        }
    }

    while (errMsgSize > 1 && isspace(win32ErrMsg[errMsgSize - 1])) {
        win32ErrMsg[--errMsgSize] = '\0';
    }

    return win32ErrMsg;
}
#endif // _WIN32

CBDEF const char* cb_path_name(const char* path);
CBDEF bool cb_rename(const char* old_path, const char* new_path);
CBDEF int cb_file_exists(const char* file_path);
CBDEF const char* cb_get_current_dir_temp(void);
CBDEF bool cb_set_current_dir(const char* path);

CBDEF char* cb_temp_dir_name(const char* path);
CBDEF char* cb_temp_file_name(const char* path);
CBDEF char* cb_temp_file_ext(const char* path);
CBDEF char* cb_temp_running_executable_path(void);

// 文件类型只区分 错误 / 普通文件 / 目录 / 符号链接 / 其它 五种，
// 更细的类型（FIFO / socket / 设备 / junction）没有实现。

typedef enum {
    CB_FILE_ERROR = -1,
    CB_FILE_REGULAR = 0,
    CB_FILE_DIRECTORY,
    CB_FILE_SYMLINK,
    CB_FILE_OTHER,
} CB_File_Type;

typedef enum {
    CB_WALK_CONT,
    CB_WALK_SKIP,
    CB_WALK_STOP,
} CB_Walk_Action;

typedef struct {
    const char* path;
    CB_File_Type type;
    size_t level;
    void* data;
    CB_Walk_Action* action;
} CB_Walk_Entry;

typedef bool (*CB_Walk_Func)(CB_Walk_Entry entry);

// 只写关心的字段即可：cb_walk_dir(root, fn, .post_order = true) / (.data = &x)，
// 剩下的字段由 CB__DEFAULT 给的默认值兜底（见 General 一节）。
// 结构体必须带标签名：C++ 里"匿名 struct + 默认成员初始化器"会被
// -Wnon-c-typedef-for-linkage 报出来。
typedef struct CB_Walk_Dir_Opt {
    void* data CB__DEFAULT(nullptr);
    bool post_order CB__DEFAULT(false);
} CB_Walk_Dir_Opt;

CBDEF bool cb_delete_walk_entry(CB_Walk_Entry entry);
CBDEF bool cb__walk_dir_opt_impl(CB_String_Builder* file_path, CB_Walk_Func func, size_t level, bool* stop, CB_Walk_Dir_Opt opt);
CBDEF bool cb_walk_dir_opt(const char* root, CB_Walk_Func func, CB_Walk_Dir_Opt opt);
#define cb_walk_dir(root, func, ...) cb_walk_dir_opt((root), (func), CB_CLIT(CB_Walk_Dir_Opt){__VA_ARGS__})

typedef struct {
    char* name;
    bool error;

    struct {
#ifdef _WIN32
        WIN32_FIND_DATA win32_data;
        HANDLE win32_hFind;
        bool win32_init;
#else
        DIR* posix_dir;
        struct dirent* posix_ent;
#endif // _WIN32
    } cb__private;
} CB_Dir_Entry;

// 打开目录准备迭代。失败返回 false（错误会自动经 cb_log 打印）。
CBDEF bool cb_dir_entry_open(const char* dir_path, CB_Dir_Entry* dir);
// 取下一个条目。返回 false 表示出错或者已经取完（出错时 dir->error 为 true）。
CBDEF bool cb_dir_entry_next(CB_Dir_Entry* dir);
CBDEF void cb_dir_entry_close(CB_Dir_Entry dir);

typedef struct {
    const char** items;
    size_t count;
    size_t capacity;
} CB_File_Paths;


// 递归创建目录（mkdir -p）：中间层不存在会自动建，路径已存在且是目录也算成功。
CBDEF bool cb_mkdir_if_not_exists(const char* path);
CBDEF bool cb_copy_file(const char* src_path, const char* dst_path);
CBDEF bool cb_copy_directory_recursively(const char* src_path, const char* dst_path);
CBDEF bool cb_delete_directory_recursively(const char* dir_path);
// 列出目录下的条目名（不含 "." 与 ".."），内存来自 temp storage。
CBDEF bool cb_read_entire_dir(const char* parent, CB_File_Paths* children);
CBDEF bool cb_write_entire_file(const char* path, const void* data, size_t size);
CBDEF CB_File_Type cb_get_file_type(const char* path);
CBDEF bool cb_delete_file(const char* path);

CBDEF bool cb_delete_walk_entry(CB_Walk_Entry entry)
{
    return cb_delete_file(entry.path);
}

CBDEF bool cb__walk_dir_opt_impl(CB_String_Builder* file_path, CB_Walk_Func func, size_t level, bool* stop, CB_Walk_Dir_Opt opt)
{
    CB_ASSERT(file_path->count > 0 && "file_path was probably not properly NULL-terminated");
    bool result = true;

    CB_Dir_Entry dir = CB_ZERO;
    size_t saved_file_path_count = file_path->count;
    CB_Walk_Action action = CB_WALK_CONT;

    CB_File_Type file_type = cb_get_file_type(file_path->items);
    if (file_type < 0) cb_return_defer(false);

    // 前序：先处理自己，再递归子项
    if (!opt.post_order) {
        if (!func(CB_CLIT(CB_Walk_Entry){
                .path = file_path->items,
                .type = file_type,
                .level = level,
                .data = opt.data,
                .action = &action,
            })) cb_return_defer(false);

        switch (action) {
        case CB_WALK_CONT:
            break;
        case CB_WALK_STOP:
            *stop = true;
            cb_return_defer(true); // STOP 也要结束遍历，直接返回，不贯穿到 SKIP
        case CB_WALK_SKIP:
            cb_return_defer(true);
        default:
            CB_UNREACHABLE("CB_Walk_Action");
        }
    }

    if (file_type == CB_FILE_DIRECTORY) {
        if (!cb_dir_entry_open(file_path->items, &dir)) cb_return_defer(false);
        while (true) {
            // 下一个条目
            if (!cb_dir_entry_next(&dir)) {
                if (!dir.error) break;
                cb_return_defer(false);
            }

            // 跳过 . 与 ..
            if (strcmp(dir.name, ".") == 0) continue;
            if (strcmp(dir.name, "..") == 0) continue;

            // 回到父路径长度再拼子项。不是 saved - 1：StringBuilder 的 count 不含终止符
            // （items[count] 才是 '\0'）。
            file_path->count = saved_file_path_count;
#ifdef _WIN32
            cb_sb_appendf(file_path, "\\%s", dir.name);
#else
            cb_sb_appendf(file_path, "/%s", dir.name);
#endif // _WIN32

            // 递归子目录
            if (!cb__walk_dir_opt_impl(file_path, func, level + 1, stop, opt)) cb_return_defer(false);
            if (*stop) cb_return_defer(true);
        }
        // 递归期间子项把 items[saved] 覆盖成了 '/'，这里补回终止符
        file_path->count = saved_file_path_count;
        cb__sb_terminate(file_path);
    }

    // Post-order walking：先递归处理完子项，最后才处理自己
    if (opt.post_order) {
        if (!func(CB_CLIT(CB_Walk_Entry){
                .path = file_path->items,
                .type = file_type,
                .level = level,
                .data = opt.data,
                .action = &action,
            })) cb_return_defer(false);

        switch (action) {
        case CB_WALK_CONT:
            break;
        case CB_WALK_STOP:
            *stop = true;
            cb_return_defer(true); // STOP 也要结束遍历，直接返回，不贯穿到 SKIP
        case CB_WALK_SKIP:
            cb_return_defer(true);
        default:
            CB_UNREACHABLE("CB_Walk_Action");
        }
    }

defer:
    // 把 file_path 恢复原样
    file_path->count = saved_file_path_count;
    cb_da_last(file_path) = '\0';
    cb_dir_entry_close(dir);
    return result;
}

CBDEF bool cb_walk_dir_opt(const char* root, CB_Walk_Func func, CB_Walk_Dir_Opt opt)
{
    CB_String_Builder file_path = CB_ZERO;

    cb_sb_appendf(&file_path, "%s", root);

    bool stop = false;
    bool ok = cb__walk_dir_opt_impl(&file_path, func, 0, &stop, opt);
    CB_FREE(file_path.items);
    return ok;
}

CBDEF const char* cb_path_name(const char* path)
{
#ifdef _WIN32
    const char* p1 = strrchr(path, '/');
    const char* p2 = strrchr(path, '\\');
    const char* p = (p1 > p2) ? p1 : p2; // NULL is ignored if the other search is successful
    return p ? p + 1 : path;
#else
    const char* p = strrchr(path, '/');
    return p ? p + 1 : path;
#endif // _WIN32
}

CBDEF bool cb_rename(const char* old_path, const char* new_path)
{
#ifdef CB_ENABLE_ECHO
    cb_log(CB_INFO, "Renaming %s -> %s", old_path, new_path);
#endif // CB_ENABLE_ECHO
#ifdef _WIN32
    if (!MoveFileEx(old_path, new_path, MOVEFILE_REPLACE_EXISTING)) {
        cb_log(CB_ERROR, "Could not rename %s to %s: %s", old_path, new_path, cb_win32_error_message(GetLastError()));
        return false;
    }
#else
    if (rename(old_path, new_path) < 0) {
        cb_log(CB_ERROR, "Could not rename %s to %s: %s", old_path, new_path, strerror(errno));
        return false;
    }
#endif // _WIN32
    return true;
}

// 不存在返回 0
CBDEF int cb_file_exists(const char* file_path)
{
#ifdef _WIN32
    return GetFileAttributesA(file_path) != INVALID_FILE_ATTRIBUTES;
#else
    return access(file_path, F_OK) == 0;
#endif // _WIN32
}

CBDEF const char* cb_get_current_dir_temp(void)
{
#ifdef _WIN32
    DWORD nBufferLength = GetCurrentDirectory(0, NULL);
    if (nBufferLength == 0) {
        cb_log(CB_ERROR, "Could not get current directory: %s", cb_win32_error_message(GetLastError()));
        return NULL;
    }

    char* buffer = (char*)cb_temp_alloc(nBufferLength);
    if (GetCurrentDirectory(nBufferLength, buffer) == 0) {
        cb_log(CB_ERROR, "Could not get current directory: %s", cb_win32_error_message(GetLastError()));
        return NULL;
    }

    return buffer;
#else
    // PATH_MAX 在 POSIX 里是可选的，这里用自己的兜底值，失败时按需重试。
    char* buffer = (char*)cb_temp_alloc(CB_PATH_MAX);
    while (getcwd(buffer, CB_PATH_MAX) == NULL) {
        if (errno != ERANGE) {
            cb_log(CB_ERROR, "Could not get current directory: %s", strerror(errno));
            return NULL;
        }
        // 路径比缓冲区长：换一块更大的再试（旧块留在 temp 里）
        buffer = (char*)cb_temp_alloc(CB_PATH_MAX * 2);
        if (getcwd(buffer, CB_PATH_MAX * 2) != NULL) break;
        cb_log(CB_ERROR, "Could not get current directory: %s", strerror(errno));
        return NULL;
    }

    return buffer;
#endif // _WIN32
}

CBDEF bool cb_set_current_dir(const char* path)
{
#ifdef _WIN32
    if (!SetCurrentDirectoryA(path)) {
        cb_log(CB_ERROR, "Could not set current directory to %s: %s", path, cb_win32_error_message(GetLastError()));
        return false;
    }
    return true;
#else
    if (chdir(path) < 0) {
        cb_log(CB_ERROR, "Could not set current directory to %s: %s", path, strerror(errno));
        return false;
    }
    return true;
#endif // _WIN32
}

CBDEF char* cb_temp_dir_name(const char* path)
{
#ifdef _WIN32
    if (!path) path = "";
    char* drive = (char*)cb_temp_alloc(_MAX_DRIVE);
    char* dir = (char*)cb_temp_alloc(_MAX_DIR);
    // https://learn.microsoft.com/en-us/previous-versions/visualstudio/visual-studio-2010/8e46eyt7(v=vs.100)
    errno_t ret = _splitpath_s(path, drive, _MAX_DRIVE, dir, _MAX_DIR, NULL, 0, NULL, 0);
    CB_ASSERT(ret == 0);
    return cb_temp_sprintf("%s%s", drive, dir);
#else
    // 取自 musl 的 dirname 实现：libc 各家对 dirname(3) 是否修改入参没有共识。
    if (!path || !*path) return cb_temp_strdup(".");
    size_t i = strlen(path) - 1;
    for (; path[i] == '/'; i--)
        if (!i) return cb_temp_strdup("/");
    for (; path[i] != '/'; i--)
        if (!i) return cb_temp_strdup(".");
    for (; path[i] == '/'; i--)
        if (!i) return cb_temp_strdup("/");
    return cb_temp_strndup(path, i + 1);
#endif // _WIN32
}

CBDEF char* cb_temp_file_name(const char* path)
{
#ifdef _WIN32
    if (!path) path = ""; // Treating NULL as empty.
    char* fname = (char*)cb_temp_alloc(_MAX_FNAME);
    char* ext = (char*)cb_temp_alloc(_MAX_EXT);
    // https://learn.microsoft.com/en-us/previous-versions/visualstudio/visual-studio-2010/8e46eyt7(v=vs.100)
    errno_t ret = _splitpath_s(path, NULL, 0, NULL, 0, fname, _MAX_FNAME, ext, _MAX_EXT);
    CB_ASSERT(ret == 0);
    return cb_temp_sprintf("%s%s", fname, ext);
#else
    // 取自 musl 的 basename 实现：libc 各家对 basename(3) 是否修改入参没有共识。
    if (!path || !*path) return cb_temp_strdup(".");
    char* s = cb_temp_strdup(path);
    size_t i = strlen(s) - 1;
    for (; i && s[i] == '/'; i--)
        s[i] = 0;
    for (; i && s[i - 1] != '/'; i--)
        ;
    return s + i;
#endif // _WIN32
}

CBDEF char* cb_temp_file_ext(const char* path)
{
#ifdef _WIN32
    if (!path) path = ""; // Treating NULL as empty.
    char* ext = (char*)cb_temp_alloc(_MAX_EXT);
    // https://learn.microsoft.com/en-us/previous-versions/visualstudio/visual-studio-2010/8e46eyt7(v=vs.100)
    errno_t ret = _splitpath_s(path, NULL, 0, NULL, 0, NULL, 0, ext, _MAX_EXT);
    CB_ASSERT(ret == 0);
    return ext;
#else
    return strrchr(cb_temp_file_name(path), '.');
#endif // _WIN32
}

CBDEF char* cb_temp_running_executable_path(void)
{
#if defined(__linux__)
    char buf[4096];
    int length = readlink("/proc/self/exe", buf, CB_ARRAY_LEN(buf));
    if (length < 0) return cb_temp_strdup("");
    return cb_temp_strndup(buf, length);
#elif defined(_WIN32)
    char buf[MAX_PATH];
    int length = GetModuleFileNameA(NULL, buf, MAX_PATH);
    return cb_temp_strndup(buf, length);
#elif defined(__APPLE__)
    char buf[4096];
    uint32_t size = CB_ARRAY_LEN(buf);
    if (_NSGetExecutablePath(buf, &size) != 0) return cb_temp_strdup("");
    int length = strlen(buf);
    return cb_temp_strndup(buf, length);
#elif defined(__FreeBSD__)
    char buf[4096];
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1};
    size_t length = sizeof(buf);
    if (sysctl(mib, 4, buf, &length, NULL, 0) < 0) return cb_temp_strdup("");
    return cb_temp_strndup(buf, length);
#elif defined(__HAIKU__)
    int cookie = 0;
    image_info info;
    while (get_next_image_info(B_CURRENT_TEAM, &cookie, &info) == B_OK)
        if (info.type == B_APP_IMAGE)
            break;
    return cb_temp_strndup(info.name, strlen(info.name));
#else
    fprintf(stderr, "%s:%d: TODO: cb_temp_running_executable_path is not implemented for this platform\n", __FILE__, __LINE__);
    return cb_temp_strdup("");
#endif
}

CBDEF bool cb_dir_entry_open(const char* dir_path, CB_Dir_Entry* dir)
{
    memset(dir, 0, sizeof(*dir));
#ifdef _WIN32
    CB_Arena_Mark temp_mark = cb_temp_save();
    char* buffer = cb_temp_sprintf("%s\\*", dir_path);
    dir->cb__private.win32_hFind = FindFirstFile(buffer, &dir->cb__private.win32_data);
    cb_temp_rewind(temp_mark);

    if (dir->cb__private.win32_hFind == INVALID_HANDLE_VALUE) {
        cb_log(CB_ERROR, "Could not open directory %s: %s", dir_path, cb_win32_error_message(GetLastError()));
        dir->error = true;
        return false;
    }
#else
    dir->cb__private.posix_dir = opendir(dir_path);
    if (dir->cb__private.posix_dir == NULL) {
        cb_log(CB_ERROR, "Could not open directory %s: %s", dir_path, strerror(errno));
        dir->error = true;
        return false;
    }
#endif // _WIN32
    return true;
}

CBDEF bool cb_dir_entry_next(CB_Dir_Entry* dir)
{
#ifdef _WIN32
    if (!dir->cb__private.win32_init) {
        dir->cb__private.win32_init = true;
        dir->name = dir->cb__private.win32_data.cFileName;
        return true;
    }

    if (!FindNextFile(dir->cb__private.win32_hFind, &dir->cb__private.win32_data)) {
        if (GetLastError() == ERROR_NO_MORE_FILES) return false;
        cb_log(CB_ERROR, "Could not read next directory entry: %s", cb_win32_error_message(GetLastError()));
        dir->error = true;
        return false;
    }
    dir->name = dir->cb__private.win32_data.cFileName;
#else
    errno = 0;
    dir->cb__private.posix_ent = readdir(dir->cb__private.posix_dir);
    if (dir->cb__private.posix_ent == NULL) {
        if (errno == 0) return false;
        cb_log(CB_ERROR, "Could not read next directory entry: %s", strerror(errno));
        dir->error = true;
        return false;
    }
    dir->name = dir->cb__private.posix_ent->d_name;
#endif // _WIN32
    return true;
}

CBDEF void cb_dir_entry_close(CB_Dir_Entry dir)
{
#ifdef _WIN32
    FindClose(dir.cb__private.win32_hFind);
#else
    if (dir.cb__private.posix_dir) closedir(dir.cb__private.posix_dir);
#endif // _WIN32
}

// copy_file_range() 需要 Linux 4.5+
CBDEF bool cb_copy_file(const char* src_path, const char* dst_path)
{
#ifdef CB_ENABLE_ECHO
    cb_log(CB_INFO, "Copying '%s' -> '%s'", src_path, dst_path);
#endif // CB_ENABLE_ECHO

#ifdef _WIN32
    if (!CopyFile(src_path, dst_path, FALSE)) {
        cb_log(CB_ERROR, "Could not copy file: '%s'", cb_win32_error_message(GetLastError()));
        return false;
    }
    return true;
#else
    int src_fd = -1;
    int dst_fd = -1;
    size_t buf_size = 32 * 1024;
    char* buf = (char*)CB_REALLOC(NULL, buf_size);
    cb_alloc_check(buf, buf_size);
    bool result = true;

    src_fd = open(src_path, O_RDONLY);
    if (src_fd < 0) {
        cb_log(CB_ERROR, "Could not open file '%s': %s", src_path, strerror(errno));
        cb_return_defer(false);
    }
    struct stat src_stat;
    if (fstat(src_fd, &src_stat) < 0) {
        cb_log(CB_ERROR, "Could not get mode of file '%s': %s", src_path, strerror(errno));
        cb_return_defer(false);
    }

    dst_fd = open(dst_path, O_CREAT | O_TRUNC | O_WRONLY, src_stat.st_mode);
    if (dst_fd < 0) {
        cb_log(CB_ERROR, "Could not create file '%s': %s", dst_path, strerror(errno));
        cb_return_defer(false);
    }

    while (true) {
        ssize_t n = read(src_fd, buf, buf_size);
        if (n == 0) break;
        if (n < 0) {
            cb_log(CB_ERROR, "Could not read from file '%s': %s", src_path, strerror(errno));
            cb_return_defer(false);
        }
        char* buf2 = buf;
        while (n > 0) {
            ssize_t m = write(dst_fd, buf2, n);
            if (m < 0) {
                cb_log(CB_ERROR, "Could not write to file '%s': %s", dst_path, strerror(errno));
                cb_return_defer(false);
            }
            n -= m;
            buf2 += m;
        }
    }

defer:
    CB_FREE(buf);
    close(src_fd);
    close(dst_fd);
    return result;
#endif
}

CBDEF bool cb_copy_directory_recursively(const char* src_path, const char* dst_path)
{
    bool result = true;
    CB_File_Paths children = CB_ZERO;
    CB_String_Builder src_sb = CB_ZERO;
    CB_String_Builder dst_sb = CB_ZERO;
    CB_Arena_Mark temp_checkpoint = cb_temp_save();

    CB_File_Type type = cb_get_file_type(src_path);
    if (type < 0) return false;

    switch (type) {
    case CB_FILE_DIRECTORY: {
        if (!cb_mkdir_if_not_exists(dst_path)) cb_return_defer(false);
        if (!cb_read_entire_dir(src_path, &children)) cb_return_defer(false);

        for (size_t i = 0; i < children.count; ++i) {
            if (strcmp(children.items[i], ".") == 0) continue;
            if (strcmp(children.items[i], "..") == 0) continue;

            src_sb.count = 0;
            cb_sb_append_cstr(&src_sb, src_path);
            cb_sb_append_cstr(&src_sb, "/");
            cb_sb_append_cstr(&src_sb, children.items[i]);

            dst_sb.count = 0;
            cb_sb_append_cstr(&dst_sb, dst_path);
            cb_sb_append_cstr(&dst_sb, "/");
            cb_sb_append_cstr(&dst_sb, children.items[i]);

            if (!cb_copy_directory_recursively(src_sb.items, dst_sb.items)) {
                cb_return_defer(false);
            }
        }
    } break;

    case CB_FILE_REGULAR: {
        if (!cb_copy_file(src_path, dst_path)) cb_return_defer(false);
    } break;

    case CB_FILE_SYMLINK: {
        cb_log(CB_WARN, "TODO: cb_copy_directory_recursively not support symlinks yet");
    } break;

    case CB_FILE_ERROR:
    case CB_FILE_OTHER: {
        cb_log(CB_ERROR, "Unsupported type of file %s", src_path);
        cb_return_defer(false);
    } break;

    default:
        CB_UNREACHABLE("cb_copy_directory_recursively");
    }

defer:
    cb_temp_rewind(temp_checkpoint);
    cb_da_free(src_sb);
    cb_da_free(dst_sb);
    cb_da_free(children);
    return result;
}

CBDEF bool cb_delete_directory_recursively(const char* dir_path)
{
    // 不用指定初始化器写法：C++ 下部分指定会触发 -Wmissing-designated-field-initializers
    CB_Walk_Dir_Opt opt = CB_ZERO;
    opt.post_order = true;
    return cb_walk_dir_opt(dir_path, cb_delete_walk_entry, opt);
}

// 例：parent = "~/dir/" 时 children 里依次是 "dir2"、"file1"、"file2"（"." 与 ".." 已跳过）。
CBDEF bool cb_read_entire_dir(const char* parent, CB_File_Paths* children)
{
    if (strlen(parent) == 0) {
        cb_log(CB_ERROR, "Cannot read empty path");
        return false;
    }
    bool result = true;
    CB_Dir_Entry dir = CB_ZERO;
    if (!cb_dir_entry_open(parent, &dir)) cb_return_defer(false);
    while (cb_dir_entry_next(&dir)) {
        // 跳过 "." 与 ".."：它们是噪音，还会让"照着列表递归"的代码无限递归
        if (strcmp(dir.name, ".") == 0 || strcmp(dir.name, "..") == 0) continue;
        cb_da_append(children, cb_temp_strdup(dir.name));
    }
    if (dir.error) cb_return_defer(false);

defer:
    cb_dir_entry_close(dir);
    return result;
}

CBDEF bool cb_write_entire_file(const char* path, const void* data, size_t size)
{
    bool result = true;

    const char* buf = NULL;
    FILE* f = fopen(path, "wb");
    if (f == NULL) {
        cb_log(CB_ERROR, "Could not open file %s for writing: %s\n", path, strerror(errno));
        cb_return_defer(false);
    }

    buf = (const char*)data;
    while (size > 0) {
        size_t n = fwrite(buf, 1, size, f);
        if (ferror(f)) {
            cb_log(CB_ERROR, "Could not write into file %s: %s\n", path, strerror(errno));
            cb_return_defer(false);
        }
        size -= n;
        buf += n;
    }

defer:
    if (f) fclose(f);
    return result;
}

CBDEF CB_File_Type cb_get_file_type(const char* path)
{
#ifdef _WIN32
    DWORD attr = GetFileAttributesA(path);
    if (attr == INVALID_FILE_ATTRIBUTES) {
        cb_log(CB_ERROR, "Could not get file attrbutes of %s: %s", path, cb_win32_error_message(GetLastError()));
        return CB_FILE_ERROR;
    }

    // 顺序要紧：符号链接（含目录链接/junction）同时带 DIRECTORY 与 REPARSE_POINT，
    // 先判 REPARSE_POINT 才能与 POSIX 下 lstat 的语义一致（返回链接本身，不返回目标）。
    if (attr & FILE_ATTRIBUTE_REPARSE_POINT) return CB_FILE_SYMLINK;
    if (attr & FILE_ATTRIBUTE_DIRECTORY) return CB_FILE_DIRECTORY;
    return CB_FILE_REGULAR;
#else
    struct stat statbuf;
    if (lstat(path, &statbuf) < 0) {
        cb_log(CB_ERROR, "Could not get stat of %s: %s", path, strerror(errno));
        return CB_FILE_ERROR;
    }

    if (S_ISREG(statbuf.st_mode)) return CB_FILE_REGULAR;
    if (S_ISDIR(statbuf.st_mode)) return CB_FILE_DIRECTORY;
    if (S_ISLNK(statbuf.st_mode)) return CB_FILE_SYMLINK;
    return CB_FILE_OTHER;
#endif // _WIN32
}

CBDEF bool cb_delete_file(const char* path)
{
#ifdef CB_ENABLE_ECHO
    cb_log(CB_INFO, "deleting %s", path);
#endif // !CB_ENABLE_ECHO

#ifdef _WIN32
    CB_File_Type type = cb_get_file_type(path);
    switch (type) {
    case CB_FILE_ERROR:
        // 取不到类型（例如文件不存在）：与 POSIX 分支 remove() 失败的行为保持一致
        return false;
    case CB_FILE_DIRECTORY:
        if (!RemoveDirectoryA(path)) {
            cb_log(CB_ERROR, "Could not delete directory %s: %s", path, cb_win32_error_message(GetLastError()));
            return false;
        }
        break;
    case CB_FILE_REGULAR:
    case CB_FILE_SYMLINK:
    case CB_FILE_OTHER:
        if (!DeleteFileA(path)) {
            cb_log(CB_ERROR, "Could not delete file %s: %s", path, cb_win32_error_message(GetLastError()));
            return false;
        }
        break;
    default:
        CB_UNREACHABLE("CB_File_Type");
    }
    return true;
#else
    if (remove(path) < 0) {
        cb_log(CB_ERROR, "Could not delete file %s: %s", path, strerror(errno));
        return false;
    }
    return true;
#endif // _WIN32
}


////////////////////////////////////////////////////////////////////////////////////////////////////
// 路径处理
////////////////////////////////////////////////////////////////////////////////////////////////////
// 结果都分配在 temp storage 上。这些函数只做字符串层面的处理，不访问文件系统
// （cb_path_absolute 例外：它需要读取当前工作目录）。

// ---- 声明 ----
// 是否绝对路径（POSIX: 以 / 开头；Windows: 以 / 或 \ 开头，或形如 C:/）
CBDEF bool cb_path_is_absolute(const char* path);
// 拼接两段路径，自动处理分隔符（不会出现重复分隔符）。
// 若 b 是绝对路径，则忽略 a 直接返回 b（与 os.path.join 的惯例一致）。
CBDEF char* cb_path_join(const char* a, const char* b);
// 折叠重复分隔符、解析 "." 与 ".."。不解析符号链接、不访问文件系统。
// 相对路径保持相对；根目录上的 ".." 会被丢弃（"/.." -> "/"）。
CBDEF char* cb_path_normalize(const char* path);
// 相对路径转绝对路径（当前工作目录 + normalize）
CBDEF char* cb_path_absolute(const char* path);
// 替换扩展名。new_ext 可以带或不带前导点；传 NULL 或 "" 表示去掉扩展名。
// 只处理最后一个路径分量里的最后一个点之后的部分。
CBDEF char* cb_path_replace_ext(const char* path, const char* new_ext);

// ---- 定义 ----
CBDEF bool cb_path_is_sep(char c)
{
#ifdef _WIN32
    return c == '/' || c == '\\';
#else
    return c == '/';
#endif // _WIN32
}

#ifdef _WIN32
#define CB_PATH_SEP '\\'
#else
#define CB_PATH_SEP '/'
#endif // _WIN32

CBDEF bool cb_path_is_absolute(const char* path)
{
    if (path == NULL || path[0] == '\0') return false;
    if (cb_path_is_sep(path[0])) return true;
#ifdef _WIN32
    // Windows 盘符路径：字母 + ':' + 分隔符
    if (isalpha((unsigned char)path[0]) && path[1] == ':' && cb_path_is_sep(path[2])) return true;
#endif // _WIN32
    return false;
}

CBDEF char* cb_path_join(const char* a, const char* b)
{
    if (a == NULL || a[0] == '\0') return cb_temp_strdup(b == NULL ? "" : b);
    if (b == NULL || b[0] == '\0') return cb_temp_strdup(a);
    if (cb_path_is_absolute(b)) return cb_temp_strdup(b);

    // 走到这里 b 一定是相对路径。无论 a 末尾是哪种分隔符，输出统一用平台原生分隔符。
    size_t a_len = strlen(a);
    if (cb_path_is_sep(a[a_len - 1])) {
        return cb_temp_sprintf("%.*s%c%s", (int)(a_len - 1), a, CB_PATH_SEP, b);
    }
    return cb_temp_sprintf("%s%c%s", a, CB_PATH_SEP, b);
}

// 按任意路径分隔符切分：Windows 上 '/' 与 '\\' 都是分隔符，只按 CB_PATH_SEP 切不开。
CBDEF CB_String_View cb__sv_chop_by_path_sep(CB_String_View* sv)
{
    size_t i = 0;
    while (i < sv->count && !cb_path_is_sep(sv->data[i])) i += 1;

    CB_String_View result = cb_sv_from_parts(sv->data, i);
    cb__sv_advance(sv, i < sv->count ? i + 1 : i);
    return result;
}

CBDEF char* cb_path_normalize(const char* path)
{
    if (path == NULL || path[0] == '\0') return cb_temp_strdup(".");

    CB_String_Builder sb = CB_ZERO;
    bool absolute = cb_path_is_sep(path[0]);
    if (absolute) cb_sb_append(&sb, CB_PATH_SEP);

    // 用"分量栈"来解析，避免在字符串上做复杂的回退
    CB_File_Paths parts = CB_ZERO;

    CB_String_View rest = cb_sv_from_cstr(path);
    while (rest.count > 0) {
        CB_String_View part = cb__sv_chop_by_path_sep(&rest);
        if (part.count == 0) continue;                      // 重复分隔符
        if (cb_sv_eq(part, cb_sv_from_cstr("."))) continue; // 当前目录

        if (cb_sv_eq(part, cb_sv_from_cstr(".."))) {
            if (parts.count > 0) {
                parts.count -= 1; // 可以回退
            } else if (!absolute) {
                // 相对路径开头的 ".." 必须保留
                cb_da_append(&parts, cb_temp_strndup(part.data, part.count));
            }
            continue;
        }
        cb_da_append(&parts, cb_temp_strndup(part.data, part.count));
    }

    for (size_t i = 0; i < parts.count; ++i) {
        if (i > 0) cb_sb_append(&sb, CB_PATH_SEP);
        cb_sb_append_cstr(&sb, parts.items[i]);
    }
    // 空路径：绝对路径给根目录，相对路径给 "."
    if (sb.count == 0) {
        cb_sb_append(&sb, absolute ? CB_PATH_SEP : '.');
    }

    char* result = cb_temp_strdup(sb.items);
    cb_sb_free(sb);
    cb_da_free(parts);
    return result;
}

CBDEF char* cb_path_absolute(const char* path)
{
    if (path == NULL || path[0] == '\0') path = ".";
    if (cb_path_is_absolute(path)) return cb_path_normalize(path);

    const char* cwd = cb_get_current_dir_temp();
    if (cwd == NULL) return cb_path_normalize(path);
    return cb_path_normalize(cb_path_join(cwd, path));
}

CBDEF char* cb_path_replace_ext(const char* path, const char* new_ext)
{
    if (path == NULL) return cb_temp_strdup("");

    // 只在最后一个路径分量里找最后一个点
    size_t last_sep = 0;
    for (size_t i = 0; path[i] != '\0'; ++i) {
        if (cb_path_is_sep(path[i])) last_sep = i + 1;
    }

    size_t dot = (size_t)-1;
    for (size_t i = last_sep; path[i] != '\0'; ++i) {
        if (path[i] == '.') dot = i;
    }

    size_t base_len = (dot == (size_t)-1) ? strlen(path) : dot;

    if (new_ext == NULL || new_ext[0] == '\0') return cb_temp_strndup(path, base_len);

    const char* ext = new_ext[0] == '.' ? new_ext + 1 : new_ext;
    return cb_temp_sprintf("%.*s.%s", (int)base_len, path, ext);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// File System 扩展：元信息 / 递归建目录 / 原子写 / 符号链接 / glob / mmap
////////////////////////////////////////////////////////////////////////////////////////////////////

#ifdef _WIN32
#define CB_PROCESS_ID() ((unsigned long)GetCurrentProcessId())
#else
#define CB_PROCESS_ID() ((unsigned long)getpid())
#endif // _WIN32

// ---- 声明 ----
// 文件字节数。失败返回 (size_t)-1。目录的大小因平台而异，不要依赖它。
CBDEF size_t cb_file_size(const char* path);
// 最后修改时间（Unix 时间戳，秒）。失败返回 -1。
CBDEF int64_t cb_file_mtime(const char* path);

// 原子写：先写同目录下的临时文件，再 rename 覆盖目标。
// 同一文件系统内 rename 是原子的，因此不会出现"目标文件被写坏一半"的中间状态。
CBDEF bool cb_write_entire_file_atomic(const char* path, const void* data, size_t size);

// 创建符号链接。Windows 上需要开发者模式或管理员权限。
CBDEF bool cb_create_symlink(const char* target, const char* link_path);
// 读取符号链接指向的目标（temp 分配）。失败返回 NULL。
CBDEF char* cb_read_symlink(const char* path);

// 通配符匹配：* 匹配任意串，? 匹配单字符，[abc] / [a-z] / [!abc] 字符类。
CBDEF bool cb_glob_match(const char* pattern, const char* text);
// 列出 dir 下匹配 pattern 的条目（只匹配单层，不递归）。
// 追加到 out 的是"dir/名字"完整路径，内存来自 temp storage。
CBDEF bool cb_glob(const char* dir, const char* pattern, CB_File_Paths* out);

// 只读内存映射。适合大文件零拷贝读取。
typedef struct {
    void* data;
    size_t size;
#ifdef _WIN32
    HANDLE file_handle;
    HANDLE mapping_handle;
#else
    int fd;
#endif // _WIN32
} CB_Mmap;

CBDEF bool cb_mmap_open(const char* path, CB_Mmap* out);
CBDEF void cb_mmap_close(CB_Mmap* mmap);

// ---- 定义 ----
CBDEF size_t cb_file_size(const char* path)
{
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA info;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &info)) return (size_t)-1;
    return ((size_t)info.nFileSizeHigh << 32) | (size_t)info.nFileSizeLow;
#else
    struct stat statbuf;
    if (stat(path, &statbuf) < 0) return (size_t)-1;
    return (size_t)statbuf.st_size;
#endif // _WIN32
}

CBDEF int64_t cb_file_mtime(const char* path)
{
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA info;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &info)) return -1;
    ULARGE_INTEGER t;
    t.LowPart = info.ftLastWriteTime.dwLowDateTime;
    t.HighPart = info.ftLastWriteTime.dwHighDateTime;
    // FILETIME 是"1601-01-01 起的 100ns 数"，先平移到 Unix 纪元再换算成秒
    return (int64_t)((t.QuadPart - 116444736000000000ull) / 10000000ull);
#else
    struct stat statbuf;
    if (stat(path, &statbuf) < 0) return -1;
    return (int64_t)statbuf.st_mtime;
#endif // _WIN32
}

// 实现放在这一节（声明在 File System 一节）：它要用 cb_path_is_sep，而本文件每节
// 是"声明->定义"，被调用的函数必须出现在前面。
CBDEF bool cb_mkdir_if_not_exists(const char* path)
{
    if (path == NULL || path[0] == '\0') return false;

    // 一次遍历：每遇到一个分隔符就把攒出来的前缀建一次（不要递归调用自己，会无限递归）。
    bool result = true;
    CB_String_Builder partial = CB_ZERO;

    for (size_t i = 0;; ++i) {
        char c = path[i];
        if (c == '\0' || cb_path_is_sep(c)) {
            if (partial.count > 0) {
                // partial.items 已经是合法的 C 字符串（StringBuilder 的不变量），直接用

                // "C:" 这类盘符前缀不能 mkdir（它是"驱动器当前目录"），跳过
                bool is_drive = partial.count == 2 && partial.items[1] == ':';
                if (!is_drive) {
#ifdef _WIN32
                    int rc = _mkdir(partial.items);
#else
                    int rc = mkdir(partial.items, 0755);
#endif
                    if (rc < 0 && errno != EEXIST) {
                        cb_log(CB_ERROR, "Could not create directory '%s': %s", partial.items,
                               strerror(errno));
                        cb_return_defer(false);
                    }
#ifdef CB_ENABLE_ECHO
                    if (rc == 0) cb_log(CB_INFO, "Created directory '%s'", partial.items);
#endif
                }

            }
            if (c == '\0') break;
        }
        cb_sb_append(&partial, c);
    }

defer:
    cb_sb_free(partial);
    return result;
}

CBDEF bool cb_write_entire_file_atomic(const char* path, const void* data, size_t size)
{
    static size_t counter = 0;
    // 临时文件与目标同目录，保证 rename 不跨文件系统
    char* tmp_path = cb_temp_sprintf("%s.tmp%lu_%zu", path, CB_PROCESS_ID(), counter++);

    if (!cb_write_entire_file(tmp_path, data, size)) return false;

    if (!cb_rename(tmp_path, path)) {
        cb_delete_file(tmp_path); // 别留下垃圾文件
        return false;
    }
    return true;
}

CBDEF bool cb_create_symlink(const char* target, const char* link_path)
{
#ifdef _WIN32
    DWORD flags = SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
    if (cb_get_file_type(target) == CB_FILE_DIRECTORY) flags |= SYMBOLIC_LINK_FLAG_DIRECTORY;
    if (!CreateSymbolicLinkA(link_path, target, flags)) {
        cb_log(CB_ERROR, "Could not create symlink %s -> %s: %s", link_path, target, cb_win32_error_message(GetLastError()));
        return false;
    }
    return true;
#else
    if (symlink(target, link_path) < 0) {
        cb_log(CB_ERROR, "Could not create symlink %s -> %s: %s", link_path, target, strerror(errno));
        return false;
    }
    return true;
#endif // _WIN32
}

CBDEF char* cb_read_symlink(const char* path)
{
#ifdef _WIN32
    // Windows 的符号链接是"重解析点"，要读目标得走 DeviceIoControl(FSCTL_GET_REPARSE_POINT)。
    // 这里定义自己的紧凑结构，避免依赖 ddk/ntifs.h 里的 REPARSE_DATA_BUFFER。
    typedef struct {
        ULONG tag;
        USHORT data_length;
        USHORT reserved;
        USHORT substitute_offset;
        USHORT substitute_length;
        USHORT print_offset;
        USHORT print_length;
        // 注意：符号链接在这里还有一个 ULONG flags，挂载点没有，
        // 所以 PathBuffer 的起点要按 tag 区分（见下面的 path_buffer_offset）。
    } CB__Reparse_Header;

    HANDLE handle = CreateFileA(path, 0,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                NULL, OPEN_EXISTING,
                                FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (handle == INVALID_HANDLE_VALUE) {
        cb_log(CB_ERROR, "Could not open %s: %s", path, cb_win32_error_message(GetLastError()));
        return NULL;
    }

    enum { CB__REPARSE_BUFFER_SIZE = 16384 }; // MAXIMUM_REPARSE_DATA_BUFFER_SIZE
    unsigned char buffer[CB__REPARSE_BUFFER_SIZE];
    DWORD bytes_returned = 0;
    if (!DeviceIoControl(handle, FSCTL_GET_REPARSE_POINT, NULL, 0,
                         buffer, (DWORD)sizeof(buffer), &bytes_returned, NULL)) {
        cb_log(CB_ERROR, "Could not read reparse point of %s: %s", path, cb_win32_error_message(GetLastError()));
        CloseHandle(handle);
        return NULL;
    }
    CloseHandle(handle);

    CB__Reparse_Header* header = (CB__Reparse_Header*)buffer;
    size_t path_buffer_offset;
    if (header->tag == IO_REPARSE_TAG_SYMLINK) {
        path_buffer_offset = 20; // 比挂载点多一个 ULONG flags
    } else if (header->tag == IO_REPARSE_TAG_MOUNT_POINT) {
        path_buffer_offset = 16;
    } else {
        cb_log(CB_ERROR, "%s 不是符号链接（reparse tag = 0x%lX）", path, (unsigned long)header->tag);
        return NULL;
    }

    if (path_buffer_offset + header->substitute_offset + header->substitute_length > bytes_returned) {
        cb_log(CB_ERROR, "重解析点数据不完整：%s", path);
        return NULL;
    }

    const WCHAR* wide = (const WCHAR*)(buffer + path_buffer_offset + header->substitute_offset);
    int wide_len = (int)(header->substitute_length / sizeof(WCHAR));

    // 目标通常带 "\??\" 前缀（NT 对象路径），去掉它才是普通路径
    if (wide_len >= 4 && wide[0] == L'\\' && wide[1] == L'?' && wide[2] == L'?' && wide[3] == L'\\') {
        wide += 4;
        wide_len -= 4;
    }

    int utf8_len = WideCharToMultiByte(CP_UTF8, 0, wide, wide_len, NULL, 0, NULL, NULL);
    if (utf8_len <= 0) {
        cb_log(CB_ERROR, "Could not convert symlink target of %s to UTF-8", path);
        return NULL;
    }
    char* result = (char*)cb_temp_alloc((size_t)utf8_len + 1);
    WideCharToMultiByte(CP_UTF8, 0, wide, wide_len, result, utf8_len, NULL, NULL);
    result[utf8_len] = '\0';
    return result;
#else
    size_t capacity = 256;
    for (;;) {
        char* buffer = (char*)cb_temp_alloc(capacity);
        ssize_t n = readlink(path, buffer, capacity);
        if (n < 0) {
            cb_log(CB_ERROR, "Could not read symlink %s: %s", path, strerror(errno));
            return NULL;
        }
        if ((size_t)n < capacity) { // readlink 不会补 NUL，这里自己补
            buffer[n] = '\0';
            return buffer;
        }
        capacity *= 2; // 被截断了，换更大的缓冲区
    }
#endif // _WIN32
}

CBDEF bool cb_glob_match(const char* pattern, const char* text)
{
    // 迭代 + 回溯处理 '*'，不用递归（避免深路径爆栈）
    const char* p = pattern;
    const char* t = text;
    const char* star_p = NULL;
    const char* star_t = NULL;

    while (*t != '\0') {
        if (*p == '*') {
            star_p = p++;
            star_t = t;
        } else if (*p == '?') {
            p += 1;
            t += 1;
        } else if (*p == '[') {
            const char* cls = p + 1;
            bool negate = false;
            if (*cls == '!' || *cls == '^') {
                negate = true;
                cls += 1;
            }
            bool matched = false;
            bool first = true;
            while (*cls != '\0' && (*cls != ']' || first)) {
                first = false;
                if (cls[1] == '-' && cls[2] != '\0' && cls[2] != ']') {
                    if ((unsigned char)*t >= (unsigned char)cls[0] && (unsigned char)*t <= (unsigned char)cls[2]) matched = true;
                    cls += 3;
                } else {
                    if (*cls == *t) matched = true;
                    cls += 1;
                }
            }
            if (*cls != ']') return false; // 字符类没闭合，按不匹配处理

            if (matched == negate) { // 没匹配上，尝试用上一个 '*' 回退
                if (star_p == NULL) return false;
                p = star_p + 1;
                t = ++star_t;
                continue;
            }
            p = cls + 1;
            t += 1;
        } else {
            if (*p != *t) {
                if (star_p == NULL) return false;
                p = star_p + 1;
                t = ++star_t;
                continue;
            }
            p += 1;
            t += 1;
        }
    }

    while (*p == '*') p += 1;
    return *p == '\0';
}

CBDEF bool cb_glob(const char* dir, const char* pattern, CB_File_Paths* out)
{
    bool result = true;
    CB_File_Paths children = CB_ZERO;

    if (!cb_read_entire_dir(dir, &children)) cb_return_defer(false);

    for (size_t i = 0; i < children.count; ++i) {
        const char* name = children.items[i];
        if (!cb_glob_match(pattern, name)) continue;
        cb_da_append(out, cb_path_join(dir, name));
    }

defer:
    cb_da_free(children);
    return result;
}

CBDEF bool cb_mmap_open(const char* path, CB_Mmap* out)
{
    memset(out, 0, sizeof(*out));
#ifndef _WIN32
    out->fd = -1;
#endif

#ifdef _WIN32
    HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        cb_log(CB_ERROR, "Could not open file %s: %s", path, cb_win32_error_message(GetLastError()));
        return false;
    }

    LARGE_INTEGER file_size;
    if (!GetFileSizeEx(file, &file_size)) {
        cb_log(CB_ERROR, "Could not get size of %s: %s", path, cb_win32_error_message(GetLastError()));
        CloseHandle(file);
        return false;
    }

    out->file_handle = file;
    if (file_size.QuadPart == 0) return true; // 空文件无法映射，返回空视图

    HANDLE mapping = CreateFileMappingA(file, NULL, PAGE_READONLY, 0, 0, NULL);
    if (mapping == NULL) {
        cb_log(CB_ERROR, "Could not create mapping for %s: %s", path, cb_win32_error_message(GetLastError()));
        CloseHandle(file);
        out->file_handle = NULL;
        return false;
    }

    void* data = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    if (data == NULL) {
        cb_log(CB_ERROR, "Could not map view of %s: %s", path, cb_win32_error_message(GetLastError()));
        CloseHandle(mapping);
        CloseHandle(file);
        out->file_handle = NULL;
        return false;
    }

    out->mapping_handle = mapping;
    out->data = data;
    out->size = (size_t)file_size.QuadPart;
    return true;
#else
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        cb_log(CB_ERROR, "Could not open file %s: %s", path, strerror(errno));
        return false;
    }

    struct stat statbuf;
    if (fstat(fd, &statbuf) < 0) {
        cb_log(CB_ERROR, "Could not get size of %s: %s", path, strerror(errno));
        close(fd);
        return false;
    }

    out->fd = fd;
    if (statbuf.st_size == 0) return true; // 长度 0 的 mmap 会 EINVAL，直接给空视图

    void* data = mmap(NULL, (size_t)statbuf.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (data == MAP_FAILED) {
        cb_log(CB_ERROR, "Could not mmap %s: %s", path, strerror(errno));
        close(fd);
        out->fd = -1;
        return false;
    }

    out->data = data;
    out->size = (size_t)statbuf.st_size;
    return true;
#endif // _WIN32
}

CBDEF void cb_mmap_close(CB_Mmap* mmap)
{
#ifdef _WIN32
    if (mmap->data != NULL) UnmapViewOfFile(mmap->data);
    if (mmap->mapping_handle != NULL) CloseHandle(mmap->mapping_handle);
    if (mmap->file_handle != NULL) CloseHandle(mmap->file_handle);
#else
    if (mmap->data != NULL && mmap->size > 0) munmap(mmap->data, mmap->size);
    if (mmap->fd >= 0) close(mmap->fd);
#endif // _WIN32
    memset(mmap, 0, sizeof(*mmap));
#ifndef _WIN32
    mmap->fd = -1;
#endif
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Math & Bits
////////////////////////////////////////////////////////////////////////////////////////////////////
// cb_min / cb_max / cb_clamp：每个参数只求值一次，且返回类型 = 第一个参数的类型。
//
//   C   ：_Generic 按第一个参数的类型分派到同类型的 static inline 实现，
//         只覆盖下面那张表里的 15 个标准算术类型（char 是独立类型，必须单列）；
//         枚举 / 指针 / 结构体要先显式转换成表里的类型，否则编译不过。
//   C++ ：模板，没有这个限制（枚举、指针都能用）。
//
// 注意（C）：第二个及之后的参数会先隐式转成第一个参数的类型再比较：cb_min(2, 1.9)
// 走 int 版、结果是 1；想让小数参与比较就把小数写在第一个：cb_min(1.9, 2) = 1.9。
// 这是"返回类型 = 第一个参数的类型"的直接后果，不是 bug。

#ifdef __cplusplus

template <typename T>
struct cb__remove_ref {
    typedef T type;
};
template <typename T>
struct cb__remove_ref<T&> {
    typedef T type;
};

// a < b ? a : b 对左值给出的是左值，decltype 得到 T&，直接当返回类型就是返回悬垂引用
// （参数是按值传进来的局部量），所以要去掉引用。
#define CB__MINMAX_RET(expr) typename cb__remove_ref<decltype(expr)>::type

template <typename T, typename U>
constexpr auto cb_min(T a, U b) -> CB__MINMAX_RET(a < b ? a : b)
{
    return a < b ? a : b;
}

template <typename T, typename U>
constexpr auto cb_max(T a, U b) -> CB__MINMAX_RET(a > b ? a : b)
{
    return a > b ? a : b;
}

template <typename T, typename U, typename V>
constexpr auto cb_clamp(T x, U lo, V hi) -> CB__MINMAX_RET(cb_min(cb_max(x, lo), hi))
{
    return cb_min(cb_max(x, lo), hi);
}

#else // !__cplusplus

// 盖章宏：一次生成 min/max/clamp 三个同类型实现
#define CB__DEFINE_MINMAX(type, tag)                                    \
    CBDEF type cb__min_##tag(type a, type b) { return a < b ? a : b; }  \
    CBDEF type cb__max_##tag(type a, type b) { return a > b ? a : b; }  \
    CBDEF type cb__clamp_##tag(type x, type lo, type hi)                \
    {                                                                   \
        return cb__min_##tag(cb__max_##tag(x, lo), hi);                 \
    }

CB__DEFINE_MINMAX(_Bool, bool)
CB__DEFINE_MINMAX(char, char)
CB__DEFINE_MINMAX(signed char, schar)
CB__DEFINE_MINMAX(unsigned char, uchar)
CB__DEFINE_MINMAX(short, short)
CB__DEFINE_MINMAX(unsigned short, ushort)
CB__DEFINE_MINMAX(int, int)
CB__DEFINE_MINMAX(unsigned int, uint)
CB__DEFINE_MINMAX(long, long)
CB__DEFINE_MINMAX(unsigned long, ulong)
CB__DEFINE_MINMAX(long long, llong)
CB__DEFINE_MINMAX(unsigned long long, ullong)
CB__DEFINE_MINMAX(float, float)
CB__DEFINE_MINMAX(double, double)
CB__DEFINE_MINMAX(long double, ldouble)

#define cb_min(a, b)          \
    _Generic((a),             \
        _Bool: cb__min_bool,  \
        char: cb__min_char,   \
        signed char: cb__min_schar,   \
        unsigned char: cb__min_uchar, \
        short: cb__min_short,         \
        unsigned short: cb__min_ushort, \
        int: cb__min_int,             \
        unsigned int: cb__min_uint,   \
        long: cb__min_long,           \
        unsigned long: cb__min_ulong, \
        long long: cb__min_llong,     \
        unsigned long long: cb__min_ullong, \
        float: cb__min_float,         \
        double: cb__min_double,       \
        long double: cb__min_ldouble)((a), (b))

#define cb_max(a, b)          \
    _Generic((a),             \
        _Bool: cb__max_bool,  \
        char: cb__max_char,   \
        signed char: cb__max_schar,   \
        unsigned char: cb__max_uchar, \
        short: cb__max_short,         \
        unsigned short: cb__max_ushort, \
        int: cb__max_int,             \
        unsigned int: cb__max_uint,   \
        long: cb__max_long,           \
        unsigned long: cb__max_ulong, \
        long long: cb__max_llong,     \
        unsigned long long: cb__max_ullong, \
        float: cb__max_float,         \
        double: cb__max_double,       \
        long double: cb__max_ldouble)((a), (b))

#define cb_clamp(x, lo, hi)            \
    _Generic((x),                      \
        _Bool: cb__clamp_bool,         \
        char: cb__clamp_char,          \
        signed char: cb__clamp_schar,  \
        unsigned char: cb__clamp_uchar, \
        short: cb__clamp_short,        \
        unsigned short: cb__clamp_ushort, \
        int: cb__clamp_int,            \
        unsigned int: cb__clamp_uint,  \
        long: cb__clamp_long,          \
        unsigned long: cb__clamp_ulong, \
        long long: cb__clamp_llong,    \
        unsigned long long: cb__clamp_ullong, \
        float: cb__clamp_float,        \
        double: cb__clamp_double,      \
        long double: cb__clamp_ldouble)((x), (lo), (hi))

#endif // __cplusplus

// 把 value 向上/向下对齐到 alignment（必须是 2 的幂）
#define cb_align_up(value, alignment) (((value) + (alignment) - 1) & ~((alignment) - 1))
#define cb_align_down(value, alignment) ((value) & ~((alignment) - 1))

// ---- 声明 ----
CBDEF bool cb_is_pow2(uint64_t value);
// 向上取到最近的 2 的幂；0 与 1 都返回 1，溢出返回 0
CBDEF uint64_t cb_next_pow2(uint64_t value);
// 二进制里 1 的个数
CBDEF int cb_popcount64(uint64_t value);
// 末尾/开头连续 0 的个数。注意 cb_ctz64(0) == 64、cb_clz64(0) == 64。
CBDEF int cb_ctz64(uint64_t value);
CBDEF int cb_clz64(uint64_t value);
CBDEF uint64_t cb_rotl64(uint64_t value, int amount);
CBDEF uint64_t cb_rotr64(uint64_t value, int amount);
// 字节序
CBDEF bool cb_is_little_endian(void);
CBDEF uint32_t cb_bswap32(uint32_t value);
CBDEF uint64_t cb_bswap64(uint64_t value);

// ---- 定义 ----
CBDEF bool cb_is_pow2(uint64_t value)
{
    return value != 0 && (value & (value - 1)) == 0;
}

CBDEF uint64_t cb_next_pow2(uint64_t value)
{
    if (value <= 1) return 1;
    if (value > (uint64_t)1 << 63) return 0; // 溢出
    value -= 1;
    value |= value >> 1;
    value |= value >> 2;
    value |= value >> 4;
    value |= value >> 8;
    value |= value >> 16;
    value |= value >> 32;
    return value + 1;
}

CBDEF int cb_popcount64(uint64_t value)
{
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_popcountll(value);
#else
    int count = 0;
    while (value != 0) {
        value &= value - 1;
        count += 1;
    }
    return count;
#endif
}

CBDEF int cb_ctz64(uint64_t value)
{
    if (value == 0) return 64;
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_ctzll(value);
#else
    int count = 0;
    while ((value & 1) == 0) {
        value >>= 1;
        count += 1;
    }
    return count;
#endif
}

CBDEF int cb_clz64(uint64_t value)
{
    if (value == 0) return 64;
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_clzll(value);
#else
    int count = 0;
    while ((value & ((uint64_t)1 << 63)) == 0) {
        value <<= 1;
        count += 1;
    }
    return count;
#endif
}

CBDEF uint64_t cb_rotl64(uint64_t value, int amount)
{
    amount &= 63;
    if (amount == 0) return value;
    return (value << amount) | (value >> (64 - amount));
}

CBDEF uint64_t cb_rotr64(uint64_t value, int amount)
{
    amount &= 63;
    if (amount == 0) return value;
    return (value >> amount) | (value << (64 - amount));
}

CBDEF bool cb_is_little_endian(void)
{
    const uint16_t probe = 1;
    return *(const uint8_t*)&probe == 1;
}

CBDEF uint32_t cb_bswap32(uint32_t value)
{
    return ((value & 0x000000FFu) << 24) | ((value & 0x0000FF00u) << 8) |
           ((value & 0x00FF0000u) >> 8) | ((value & 0xFF000000u) >> 24);
}

CBDEF uint64_t cb_bswap64(uint64_t value)
{
    return ((uint64_t)cb_bswap32((uint32_t)(value & 0xFFFFFFFFu)) << 32) |
           (uint64_t)cb_bswap32((uint32_t)(value >> 32));
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Time & Date
////////////////////////////////////////////////////////////////////////////////////////////////////
// ---- 声明 ----
// 当前 UTC 时间戳（秒）
CBDEF int64_t cb_time_now(void);
// Unix 时间戳转 ISO8601 UTC，如 "2025-09-13T03:26:40Z"（temp 分配）
CBDEF char* cb_time_to_iso8601(int64_t unix_seconds);
// 人性化时长：0.42 -> "420ms"，12.5 -> "12.5s"，185 -> "3m5s"，7325 -> "2h2m"
CBDEF char* cb_duration_to_str(double seconds);

// ---- 定义 ----
CBDEF int64_t cb_time_now(void)
{
    return (int64_t)time(NULL);
}

CBDEF char* cb_time_to_iso8601(int64_t unix_seconds)
{
    time_t t = (time_t)unix_seconds;
    struct tm tm_utc;
#ifdef _WIN32
    if (gmtime_s(&tm_utc, &t) != 0) return NULL;
#else
    if (gmtime_r(&t, &tm_utc) == NULL) return NULL;
#endif // _WIN32

    return cb_temp_sprintf("%04d-%02d-%02dT%02d:%02d:%02dZ",
                           tm_utc.tm_year + 1900, tm_utc.tm_mon + 1, tm_utc.tm_mday,
                           tm_utc.tm_hour, tm_utc.tm_min, tm_utc.tm_sec);
}

CBDEF char* cb_duration_to_str(double seconds)
{
    if (seconds < 0) seconds = 0;
    if (seconds < 1.0) return cb_temp_sprintf("%.0fms", seconds * 1000.0);

    if (seconds < 60.0) {
        // 12.5s（整秒时不带小数点）
        double rounded = (double)(int64_t)(seconds * 10.0 + 0.5) / 10.0;
        if (rounded == (double)(int64_t)rounded) return cb_temp_sprintf("%llds", (long long)(int64_t)rounded);
        return cb_temp_sprintf("%.1fs", rounded);
    }

    int64_t total = (int64_t)(seconds + 0.5);
    int64_t hours = total / 3600;
    int64_t minutes = (total % 3600) / 60;
    int64_t secs = total % 60;

    if (hours > 0) return cb_temp_sprintf("%lldh%lldm", (long long)hours, (long long)minutes);
    return cb_temp_sprintf("%lldm%llds", (long long)minutes, (long long)secs);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Random
////////////////////////////////////////////////////////////////////////////////////////////////////
// xoshiro256**：质量比 rand() 高，且结果跨平台一致（便于复现）。
// 注意：它适合模拟、采样、洗牌，**不适合**密码学用途。

typedef struct {
    uint64_t state[4];
} CB_Rng;

// ---- 声明 ----
CBDEF void cb_rng_seed(CB_Rng* rng, uint64_t seed);
CBDEF uint64_t cb_rng_next(CB_Rng* rng);
// [0, 1) 的浮点数
CBDEF double cb_rng_double(CB_Rng* rng);
// [0, bound) 的均匀整数；bound 为 0 时返回 0
CBDEF uint64_t cb_rng_range(CB_Rng* rng, uint64_t bound);
// 原地洗牌（Fisher-Yates），元素按字节搬动
CBDEF void cb_rng_shuffle(CB_Rng* rng, void* items, size_t count, size_t elem_size);

// ---- 定义 ----
CBDEF uint64_t cb__rng_splitmix64(uint64_t* state)
{
    uint64_t z = (*state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

CBDEF void cb_rng_seed(CB_Rng* rng, uint64_t seed)
{
    // 用 splitmix64 把单个种子扩散成四个状态字；种子为 0 也能得到非零状态
    uint64_t state = seed;
    for (int i = 0; i < 4; ++i) rng->state[i] = cb__rng_splitmix64(&state);
}

CBDEF uint64_t cb_rng_next(CB_Rng* rng)
{
    uint64_t* s = rng->state;
    uint64_t result = cb_rotl64(s[1] * 5, 7) * 9;

    uint64_t t = s[1] << 17;
    s[2] ^= s[0];
    s[3] ^= s[1];
    s[1] ^= s[2];
    s[0] ^= s[3];
    s[2] ^= t;
    s[3] = cb_rotl64(s[3], 45);

    return result;
}

CBDEF double cb_rng_double(CB_Rng* rng)
{
    // 取高 53 位，得到 [0,1) 上的均匀分布
    return (double)(cb_rng_next(rng) >> 11) * (1.0 / 9007199254740992.0);
}

CBDEF uint64_t cb_rng_range(CB_Rng* rng, uint64_t bound)
{
    if (bound == 0) return 0;
    // 拒绝采样，避免取模带来的偏斜
    uint64_t limit = UINT64_MAX - (UINT64_MAX % bound) - 1;
    uint64_t value;
    do {
        value = cb_rng_next(rng);
    } while (value > limit);
    return value % bound;
}

CBDEF void cb_rng_shuffle(CB_Rng* rng, void* items, size_t count, size_t elem_size)
{
    if (count < 2 || elem_size == 0) return;

    CB_Arena_Mark mark = cb_temp_save();
    unsigned char* tmp = (unsigned char*)cb_temp_alloc(elem_size);
    unsigned char* base = (unsigned char*)items;

    for (size_t i = count - 1; i > 0; --i) {
        size_t j = (size_t)cb_rng_range(rng, (uint64_t)i + 1);
        if (i == j) continue;
        memcpy(tmp, base + i * elem_size, elem_size);
        memcpy(base + i * elem_size, base + j * elem_size, elem_size);
        memcpy(base + j * elem_size, tmp, elem_size);
    }

    cb_temp_rewind(mark);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Runtime Environment
////////////////////////////////////////////////////////////////////////////////////////////////////
// ---- 声明 ----
// 读环境变量（结果在 temp storage 上）。不存在返回 NULL（与 getenv 一致）。
CBDEF char* cb_env_get(const char* name);
CBDEF bool cb_env_set(const char* name, const char* value);
// stdout 是否是终端（用于决定要不要上色）
CBDEF bool cb_stdout_is_tty(void);
// 终端宽度（取不到时返回 80）
CBDEF int cb_terminal_width(void);
// 是否该输出 ANSI 颜色：终端 + 未设置 NO_COLOR + 未显式关闭
CBDEF bool cb_color_enabled(void);

// ---- 定义 ----
CBDEF char* cb_env_get(const char* name)
{
    const char* value = getenv(name);
    if (value == NULL) return NULL;
    return cb_temp_strdup(value);
}

CBDEF bool cb_env_set(const char* name, const char* value)
{
#ifdef _WIN32
    if (_putenv_s(name, value) != 0) {
        cb_log(CB_ERROR, "Could not set env %s", name);
        return false;
    }
    return true;
#else
    if (setenv(name, value, 1) != 0) {
        cb_log(CB_ERROR, "Could not set env %s: %s", name, strerror(errno));
        return false;
    }
    return true;
#endif // _WIN32
}

CBDEF bool cb_stdout_is_tty(void)
{
#ifdef _WIN32
    return _isatty(_fileno(stdout)) != 0;
#else
    return isatty(STDOUT_FILENO) != 0;
#endif // _WIN32
}

CBDEF int cb_terminal_width(void)
{
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info)) {
        int width = (int)(info.srWindow.Right - info.srWindow.Left + 1);
        if (width > 0) return width;
    }
#else
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) return (int)ws.ws_col;
#endif // _WIN32
    return 80;
}

CBDEF bool cb_color_enabled(void)
{
    if (getenv("NO_COLOR") != NULL) return false; // https://no-color.org/
    return cb_stdout_is_tty();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Hex Dump
////////////////////////////////////////////////////////////////////////////////////////////////////
// 把二进制内容按 16 字节一行打到 stderr（偏移 + 十六进制 + ASCII 列）。
// 输出到 stderr 是为了不污染程序正常的 stdout。
CBDEF void cb_dump_hex(const void* data, size_t size)
{
    const unsigned char* bytes = (const unsigned char*)data;
    for (size_t offset = 0; offset < size; offset += 16) {
        fprintf(stderr, "%08zx  ", offset);
        for (size_t i = 0; i < 16; ++i) {
            if (offset + i < size) fprintf(stderr, "%02x ", bytes[offset + i]);
            else fprintf(stderr, "   ");
            if (i == 7) fprintf(stderr, " ");
        }
        fprintf(stderr, " |");
        for (size_t i = 0; i < 16 && offset + i < size; ++i) {
            unsigned char c = bytes[offset + i];
            fprintf(stderr, "%c", (c >= 32 && c < 127) ? c : '.');
        }
        fprintf(stderr, "|\n");
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// CLI Args
////////////////////////////////////////////////////////////////////////////////////////////////////
// 极简参数解析。规则：
//   --key=value   选项，带值
//   --key         开关
//   -k            开关（等价于 --k）
//   -k=value      选项，带值
//   --            之后的全部当作位置参数
//   其它          位置参数
// 需要"--key value"这种空格分隔写法的场景，请把值写成 --key=value。

typedef struct {
    const char* name;  // 去掉前导 - / --
    const char* value; // 开关为 NULL
} CB_Arg_Entry;

typedef struct {
    CB_Arg_Entry* items;
    size_t count;
    size_t capacity;
} CB_Arg_List;

typedef struct {
    CB_Arg_List options;
    CB_File_Paths positionals;
} CB_Args;

// ---- 声明 ----
// argc/argv 传 main 的原始值（argv[0] 会被当成程序名跳过）
CBDEF void cb_args_parse(CB_Args* args, int argc, char** argv);
// 是否存在该开关/选项
CBDEF bool cb_args_has(const CB_Args* args, const char* name);
// 取选项的值；不存在返回 fallback
CBDEF const char* cb_args_get(const CB_Args* args, const char* name, const char* fallback);
// 最近一次出现的值（同名多次出现时后者覆盖前者，这里也给"取第一次"的版本）
CBDEF const char* cb_args_get_first(const CB_Args* args, const char* name);
CBDEF size_t cb_args_positional_count(const CB_Args* args);
CBDEF const char* cb_args_positional(const CB_Args* args, size_t index);
CBDEF void cb_args_free(CB_Args* args);

// ---- 定义 ----
CBDEF void cb_args_parse(CB_Args* args, int argc, char** argv)
{
    bool only_positionals = false;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];

        if (only_positionals) {
            cb_da_append(&args->positionals, cb_temp_strdup(arg));
            continue;
        }
        if (strcmp(arg, "--") == 0) {
            only_positionals = true;
            continue;
        }
        if (arg[0] != '-' || arg[1] == '\0') { // "-" 单独出现也算位置参数
            cb_da_append(&args->positionals, cb_temp_strdup(arg));
            continue;
        }

        const char* name = arg + 1;
        if (name[0] == '-') name += 1; // --key -> key
        if (name[0] == '\0') {         // 单纯的一个 "--"以外的空名字
            cb_da_append(&args->positionals, cb_temp_strdup(arg));
            continue;
        }

        CB_Arg_Entry entry = CB_ZERO;
        const char* eq = strchr(name, '=');
        if (eq != NULL) {
            entry.name = cb_temp_strndup(name, (size_t)(eq - name));
            entry.value = cb_temp_strdup(eq + 1);
        } else {
            entry.name = cb_temp_strdup(name);
            entry.value = NULL;
        }
        cb_da_append(&args->options, entry);
    }
}

CBDEF bool cb_args_has(const CB_Args* args, const char* name)
{
    for (size_t i = 0; i < args->options.count; ++i) {
        if (strcmp(args->options.items[i].name, name) == 0) return true;
    }
    return false;
}

CBDEF const char* cb_args_get_first(const CB_Args* args, const char* name)
{
    for (size_t i = 0; i < args->options.count; ++i) {
        if (strcmp(args->options.items[i].name, name) == 0) return args->options.items[i].value;
    }
    return NULL;
}

CBDEF const char* cb_args_get(const CB_Args* args, const char* name, const char* fallback)
{
    // 同名多次出现时取最后一次
    const char* found = NULL;
    for (size_t i = 0; i < args->options.count; ++i) {
        if (strcmp(args->options.items[i].name, name) == 0) found = args->options.items[i].value;
    }
    return found != NULL ? found : fallback;
}

CBDEF size_t cb_args_positional_count(const CB_Args* args)
{
    return args->positionals.count;
}

CBDEF const char* cb_args_positional(const CB_Args* args, size_t index)
{
    if (index >= args->positionals.count) return NULL;
    return args->positionals.items[index];
}

CBDEF void cb_args_free(CB_Args* args)
{
    cb_da_free(args->options);
    cb_da_free(args->positionals);
    args->options.items = NULL;
    args->options.count = 0;
    args->options.capacity = 0;
    args->positionals.items = NULL;
    args->positionals.count = 0;
    args->positionals.capacity = 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Process & FD
////////////////////////////////////////////////////////////////////////////////////////////////////
#ifdef _WIN32
typedef HANDLE CB_Proc;
#define CB_INVALID_PROC INVALID_HANDLE_VALUE
typedef HANDLE CB_FD;
#define CB_INVALID_FD INVALID_HANDLE_VALUE
#else
typedef int CB_Proc;
#define CB_INVALID_PROC (-1)
typedef int CB_FD;
#define CB_INVALID_FD (-1)
#endif // _WIN32

CBDEF CB_FD cb_fd_open_read(const char* path);
CBDEF CB_FD cb_fd_open_write(const char* path);
CBDEF void cb_fd_close(CB_FD fd);

typedef struct {
    CB_FD read;
    CB_FD write;
} CB_Pipe;

CBDEF bool cb_pipe_create(CB_Pipe* pp);

typedef struct {
    CB_Proc* items;
    size_t count;
    size_t capacity;
} CB_Procs;

CBDEF int cb__proc_wait_async(CB_Proc proc, int ms);
// 等到该进程结束
CBDEF bool cb_proc_wait(CB_Proc proc);
// 等到所有进程结束
CBDEF bool cb_procs_wait(CB_Procs procs);
// 等待全部结束并把 procs 清空（可继续复用这个数组）
CBDEF bool cb_procs_wait_and_reset(CB_Procs* procs);

CBDEF CB_FD cb_fd_open_read(const char* path)
{
#ifdef _WIN32
    // https://docs.microsoft.com/en-us/windows/win32/fileio/opening-a-file-for-reading-or-writing
    SECURITY_ATTRIBUTES saAttr = CB_ZERO;
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;

    CB_FD result = CreateFile(
        path,
        GENERIC_READ,
        0,
        &saAttr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_READONLY,
        NULL);

    if (result == INVALID_HANDLE_VALUE) {
        cb_log(CB_ERROR, "Could not open file %s: %s", path, cb_win32_error_message(GetLastError()));
        return CB_INVALID_FD;
    }

    return result;
#else
    CB_FD result = open(path, O_RDONLY);
    if (result < 0) {
        cb_log(CB_ERROR, "Could not open file %s: %s", path, strerror(errno));
        return CB_INVALID_FD;
    }
    return result;
#endif // _WIN32
}

CBDEF CB_FD cb_fd_open_write(const char* path)
{
#ifdef _WIN32
    SECURITY_ATTRIBUTES saAttr = CB_ZERO;
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;

    CB_FD result = CreateFile(
        path,                  // name of the write
        GENERIC_WRITE,         // open for writing
        0,                     // do not share
        &saAttr,               // default security
        CREATE_ALWAYS,         // create always
        FILE_ATTRIBUTE_NORMAL, // normal file
        NULL);                 // no attr. template

    if (result == INVALID_HANDLE_VALUE) {
        cb_log(CB_ERROR, "Could not open file %s: %s", path, cb_win32_error_message(GetLastError()));
        return CB_INVALID_FD;
    }

    return result;
#else
    CB_FD result = open(path,
            O_WRONLY | O_CREAT | O_TRUNC,
            S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (result < 0) {
        cb_log(CB_ERROR, "Could not open file %s: %s", path, strerror(errno));
        return CB_INVALID_FD;
    }
    return result;
#endif // _WIN32
}

CBDEF void cb_fd_close(CB_FD fd)
{
#ifdef _WIN32
    CloseHandle(fd);
#else
    close(fd);
#endif // _WIN32
}

CBDEF bool cb_pipe_create(CB_Pipe* pp)
{
#ifdef _WIN32
    // https://docs.microsoft.com/en-us/windows/win32/ProcThread/creating-a-child-process-with-redirected-input-and-output
    SECURITY_ATTRIBUTES saAttr = CB_ZERO;
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;

    if (!CreatePipe(&pp->read, &pp->write, &saAttr, 0)) {
        cb_log(CB_ERROR, "Could not create pipe: %s", cb_win32_error_message(GetLastError()));
        return false;
    }

    return true;
#else
    int pipefd[2];
    if (pipe(pipefd) < 0) {
        cb_log(CB_ERROR, "Could not create pipe: %s\n", strerror(errno));
        return false;
    }

    pp->read = pipefd[0];
    pp->write = pipefd[1];

    return true;
#endif // _WIN32
}

CBDEF int cb__proc_wait_async(CB_Proc proc, int ms)
{
    if (proc == CB_INVALID_PROC) return 0;

#ifdef _WIN32
    DWORD result = WaitForSingleObject(proc, ms);
    if (result == WAIT_TIMEOUT) return 0;

    if (result == WAIT_FAILED) {
        cb_log(CB_ERROR, "Could not wait on child process: %s", cb_win32_error_message(GetLastError()));
        return -1;
    }

    DWORD exit_status;
    if (!GetExitCodeProcess(proc, &exit_status)) {
        cb_log(CB_ERROR, "Could not get process exit code: %s", cb_win32_error_message(GetLastError()));
        return -1;
    }

    if (exit_status != 0) {
        cb_log(CB_ERROR, "Command exited with exit code %lu", exit_status);
        return -1;
    }

    CloseHandle(proc);
    return 1;
#else
    long ns = ms * 1000 * 1000;
    struct timespec duration = {
        .tv_sec = ns / (1000 * 1000 * 1000),
        .tv_nsec = ns % (1000 * 1000 * 1000),
    };

    int wstatus = 0;
    pid_t pid = waitpid(proc, &wstatus, WNOHANG);
    if (pid < 0) {
        cb_log(CB_ERROR, "Could not wait on command (pid %d): %s", proc, strerror(errno));
        return -1;
    }

    if (pid == 0) {
        nanosleep(&duration, NULL);
        return 0;
    }

    if (WIFEXITED(wstatus)) {
        int exit_status = WEXITSTATUS(wstatus);
        if (exit_status != 0) {
            cb_log(CB_ERROR, "Command exited with exit code %d", exit_status);
            return -1;
        }

        return 1;
    }

    if (WIFSIGNALED(wstatus)) {
        cb_log(CB_ERROR, "Command process was terminated by signl %d", WTERMSIG(wstatus));
        return -1;
    }

    nanosleep(&duration, NULL);
    return 0;
#endif // _WIN32
}

CBDEF bool cb_proc_wait(CB_Proc proc)
{
    if (proc == CB_INVALID_PROC) return false;

#ifdef _WIN32
    DWORD result = WaitForSingleObject(proc, INFINITE);
    if (result == WAIT_FAILED) {
        cb_log(CB_ERROR, "Could not wait on child process: %s", cb_win32_error_message(GetLastError()));
        return false;
    }

    DWORD exit_status;
    if (!GetExitCodeProcess(proc, &exit_status)) {
        cb_log(CB_ERROR, "Could not get process exit code: %s", cb_win32_error_message(GetLastError()));
        return false;
    }

    if (exit_status != 0) {
        cb_log(CB_ERROR, "Command exited with exit code %lu", exit_status);
        return false;
    }

    CloseHandle(proc);

    return true;
#else
    while (true) {
        int wstatus = 0;
        if (waitpid(proc, &wstatus, 0) < 0) {
            cb_log(CB_ERROR, "Could not wait on command (pid %d): %s", proc, strerror(errno));
            return false;
        }

        if (WIFEXITED(wstatus)) {
            int exit_status = WEXITSTATUS(wstatus);
            if (exit_status != 0) {
                cb_log(CB_ERROR, "Command exited with exit code %d", exit_status);
                return false;
            }
            break;
        }

        if (WIFSIGNALED(wstatus)) {
            cb_log(CB_ERROR, "Command process was terminated by signal %d", WTERMSIG(wstatus));
            return false;
        }
    }

    return true;
#endif // _WIN32
}

CBDEF bool cb_procs_wait(CB_Procs procs)
{
    bool success = true;
    for (size_t i = 0; i < procs.count; ++i) {
        success = cb_proc_wait(procs.items[i]) && success;
    }
    return success;
}

CBDEF bool cb_procs_wait_and_reset(CB_Procs* procs)
{
    bool success = cb_procs_wait(*procs);
    procs->count = 0;
    return success;
}


////////////////////////////////////////////////////////////////////////////////////////////////////
// Cmd
////////////////////////////////////////////////////////////////////////////////////////////////////
typedef struct {
    const char** items;
    size_t count;
    size_t capacity;
} CB_Cmd;

// cb_cmd_run_opt() 的选项。
typedef struct CB_Cmd_Opt {
    // 异步执行：把 CB_Proc 追加到给定的 CB_Procs 数组
    CB_Procs* async CB__DEFAULT(nullptr);
    // .async 列表的并发上限；0 表示 cb_nprocs()
    size_t max_procs CB__DEFAULT(0);
    // 执行后不重置命令
    bool dont_reset CB__DEFAULT(false);
    // 把 stdin 重定向到文件
    const char* stdin_path CB__DEFAULT(nullptr);
    // 把 stdout 重定向到文件
    const char* stdout_path CB__DEFAULT(nullptr);
    // 把 stderr 重定向到文件
    const char* stderr_path CB__DEFAULT(nullptr);
} CB_Cmd_Opt;

CBDEF void cb__cmd_append(CB_Cmd* cmd, size_t n, const char** args);
#ifdef __cplusplus
template <typename... Args>
CBDEF void cb__cpp_cmd_append_wrapper(CB_Cmd* cmd, Args... strs)
{
    const char* args[] = {strs...};
    cb__cmd_append(cmd, sizeof(args) / sizeof(args[0]), args);
}
#define cb_cmd_append(cmd, ...) cb__cpp_cmd_append_wrapper(cmd, __VA_ARGS__)
#else
#define cb_cmd_append(cmd, ...) \
    cb__cmd_append(cmd, sizeof((const char*[]){__VA_ARGS__}) / sizeof(const char*), (const char*[]){__VA_ARGS__})
#endif // __cplusplus

// 把 other 的全部参数追加到 cmd 后面
#define cb_cmd_extend(cmd, other_cmd) \
    cb_da_append_many((cmd), (other_cmd)->items, (other_cmd)->count)
// 释放 cmd 占用的内存（并清空句柄）
#define cb_cmd_free(cmd) \
    do {                     \
        cb_da_free(cmd);     \
        (cmd).items = NULL;  \
        (cmd).count = 0;     \
        (cmd).capacity = 0;  \
    } while (0)

CBDEF void cb_cmd_to_sb(CB_Cmd cmd, CB_String_Builder* sb);
CBDEF int cb_nprocs(void);
CBDEF CB_Proc cb__cmd_start_process(CB_Cmd cmd, CB_FD* fdin, CB_FD* fdout, CB_FD* fderr);
CBDEF bool cb_cmd_run_opt(CB_Cmd* cmd, CB_Cmd_Opt opt);
CBDEF bool cb__cmd_run_opt_with_location(CB_Cmd* cmd, const char* file, int line, CB_Cmd_Opt opt);
#define cb_cmd_run(cmd, ...) cb__cmd_run_opt_with_location((cmd), __FILE__, __LINE__, CB_CLIT(CB_Cmd_Opt){__VA_ARGS__})

typedef struct {
    CB_FD fdin;
    CB_Cmd cmd;
    bool err2out;
} CB_Pipes;

CBDEF void cb__cmd_append(CB_Cmd* cmd, size_t n, const char** args)
{
    for (size_t i = 0; i < n; ++i) {
        cb_da_append(cmd, args[i]);
    }
}

CBDEF void cb_cmd_to_sb(CB_Cmd cmd, CB_String_Builder* sb)
{
    for (size_t i = 0; i < cmd.count; ++i) {
        const char* arg = cmd.items[i];
        if (arg == NULL) break;
        if (i > 0) cb_sb_append_cstr(sb, " ");
        if (!strchr(arg, ' ')) {
            cb_sb_append_cstr(sb, arg);
        } else {
            cb_sb_append(sb, '\'');
            cb_sb_append_cstr(sb, arg);
            cb_sb_append(sb, '\'');
        }
    }
}

#ifdef _WIN32
// 把命令渲染成 Windows 能接受的命令行字符串。
// 规则见 https://learn.microsoft.com/en-gb/archive/blogs/twistylittlepassagesallalike/everyone-quotes-command-line-arguments-the-wrong-way
CBDEF void cb__win32_cmd_quote(CB_Cmd cmd, CB_String_Builder* quoted)
{
    for (size_t i = 0; i < cmd.count; ++i) {
        const char* arg = cmd.items[i];
        if (arg == NULL) break;
        size_t len = strlen(arg);
        if (i > 0) cb_sb_append(quoted, ' ');
        if (len != 0 && NULL == strpbrk(arg, " \t\n\v\"")) {
            // 不需要加引号
            cb_sb_append_buf(quoted, arg, len);
        } else {
            // 需要转义：参数里的双引号，以及双引号前的连续反斜杠
            size_t backslashes = 0;
            cb_sb_append(quoted, '\"');
            for (size_t j = 0; j < len; ++j) {
                char x = arg[j];
                if (x == '\\') {
                    backslashes += 1;
                } else {
                    if (x == '\"') {
                        // 转义已有的反斜杠和这个双引号
                        for (size_t k = 0; k < 1 + backslashes; ++k) {
                            cb_sb_append(quoted, '\\');
                        }
                    }
                    backslashes = 0;
                }
                cb_sb_append(quoted, x);
            }
            // 收尾前的反斜杠也要转义
            for (size_t k = 0; k < backslashes; ++k) {
                cb_sb_append(quoted, '\\');
            }
            cb_sb_append(quoted, '\"');
        }
    }
}
#endif // _WIN32

CBDEF int cb_nprocs(void)
{
#ifdef _WIN32
    SYSTEM_INFO siSysInfo;
    GetSystemInfo(&siSysInfo);
    return siSysInfo.dwNumberOfProcessors;
#else
    return sysconf(_SC_NPROCESSORS_ONLN);
#endif // _WIN32
}

CBDEF CB_Proc cb__cmd_start_process(CB_Cmd cmd, CB_FD* fdin, CB_FD* fdout, CB_FD* fderr)
{
    if (cmd.count < 1) {
        cb_log(CB_ERROR, "Could not run empty command");
        return CB_INVALID_PROC;
    }

#ifdef CB_ENABLE_ECHO
    CB_String_Builder sb = CB_ZERO;
    cb_cmd_to_sb(cmd, &sb);
    cb_log(CB_INFO, "CMD: %s", sb.items);
    cb_sb_free(sb);
    memset(&sb, 0, sizeof(sb));
#endif // !CB_ENABLE_ECHO

#ifdef _WIN32
    // https://docs.microsoft.com/en-us/windows/win32/procthread/creating-a-child-process-with-redirected-input-and-output
    STARTUPINFO siStartInfo;
    ZeroMemory(&siStartInfo, sizeof(siStartInfo));
    siStartInfo.cb = sizeof(STARTUPINFO);
    // NOTE: 给 std 句柄传 NULL 理论上没问题（见 GetStdHandle 的 attach/detach 行为）
    // TODO: check for errors in GetStdHandle
    siStartInfo.hStdError = fderr ? *fderr : GetStdHandle(STD_ERROR_HANDLE);
    siStartInfo.hStdOutput = fdout ? *fdout : GetStdHandle(STD_OUTPUT_HANDLE);
    siStartInfo.hStdInput = fdin ? *fdin : GetStdHandle(STD_INPUT_HANDLE);
    siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

    PROCESS_INFORMATION piProcInfo;
    ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));

    CB_String_Builder quoted = CB_ZERO;
    cb__win32_cmd_quote(cmd, &quoted);
    BOOL bSuccess = CreateProcessA(NULL, quoted.items, NULL, NULL, TRUE, 0, NULL, NULL, &siStartInfo, &piProcInfo);
    cb_sb_free(quoted);

    if (!bSuccess) {
        cb_log(CB_ERROR, "Could not create child process for %s: %s", cmd.items[0], cb_win32_error_message(GetLastError()));
        return CB_INVALID_PROC;
    }

    CloseHandle(piProcInfo.hThread);

    return piProcInfo.hProcess;
#else
    pid_t cpid = fork();
    if (cpid < 0) {
        cb_log(CB_ERROR, "Could not fork child process: %s", strerror(errno));
        return CB_INVALID_PROC;
    }

    if (cpid == 0) {
        if (fdin) {
            if (dup2(*fdin, STDIN_FILENO) < 0) {
                cb_log(CB_ERROR, "Could not setup stdin for child process: %s", strerror(errno));
                exit(1);
            }
        }
        if (fdout) {
            if (dup2(*fdout, STDOUT_FILENO) < 0) {
                cb_log(CB_ERROR, "Could not setup stdout for child process: %s", strerror(errno));
                exit(1);
            }
        }
        if (fderr) {
            if (dup2(*fderr, STDERR_FILENO) < 0) {
                cb_log(CB_ERROR, "Could not setup stderr for child process: %s", strerror(errno));
                exit(1);
            }
        }

        // NOTE: 子进程里这一点内存泄漏无所谓（一次性的）
        CB_Cmd cmd_null = CB_ZERO;
        cb_da_append_many(&cmd_null, cmd.items, cmd.count);
        cb_da_append(&cmd_null, (const char*)NULL);

        if (execvp(cmd.items[0], (char* const*)cmd_null.items) < 0) {
            cb_log(CB_ERROR, "Could not exec child process for %s: %s", cmd.items[0], strerror(errno));
            exit(1);
        }
        CB_UNREACHABLE("cb__cmd_start_process");
    }

    return cpid;
#endif // _WIN32
}

CBDEF bool cb_cmd_run_opt(CB_Cmd* cmd, CB_Cmd_Opt opt)
{
    bool result = true;
    CB_FD fdin = CB_INVALID_FD;
    CB_FD fdout = CB_INVALID_FD;
    CB_FD fderr = CB_INVALID_FD;
    CB_FD* opt_fdin = NULL;
    CB_FD* opt_fdout = NULL;
    CB_FD* opt_fderr = NULL;
    CB_Proc proc = CB_INVALID_PROC;

    size_t max_procs = opt.max_procs > 0 ? opt.max_procs : (size_t)cb_nprocs() + 1;

    if (opt.async && max_procs > 0) {
        while (opt.async->count >= max_procs) {
            for (size_t i = 0; i < opt.async->count; ++i) {
                int ret = cb__proc_wait_async(opt.async->items[i], i);
                if (ret < 0) cb_return_defer(false);
                if (ret) {
                    cb_da_remove_unordered(opt.async, i);
                    break;
                }
            }
        }
    }

    if (opt.stdin_path) {
        fdin = cb_fd_open_read(opt.stdin_path);
        if (fdin == CB_INVALID_FD) cb_return_defer(false);
        opt_fdin = &fdin;
    }
    if (opt.stdout_path) {
        fdout = cb_fd_open_write(opt.stdout_path);
        if (fdout == CB_INVALID_FD) cb_return_defer(false);
        opt_fdout = &fdout;
    }
    if (opt.stderr_path) {
        fderr = cb_fd_open_write(opt.stderr_path);
        if (fderr == CB_INVALID_FD) cb_return_defer(false);
        opt_fderr = &fderr;
    }
    proc = cb__cmd_start_process(*cmd, opt_fdin, opt_fdout, opt_fderr);

    if (opt.async) {
        if (proc == CB_INVALID_PROC) cb_return_defer(false);
        cb_da_append(opt.async, proc);
    } else {
        if (!cb_proc_wait(proc)) cb_return_defer(false);
    }

defer:
    if (opt_fdin) cb_fd_close(*opt_fdin);
    if (opt_fdout) cb_fd_close(*opt_fdout);
    if (opt_fderr) cb_fd_close(*opt_fderr);
    if (!opt.dont_reset) cmd->count = 0;
    return result;
}

CBDEF bool cb__cmd_run_opt_with_location(CB_Cmd* cmd, const char* file, int line, CB_Cmd_Opt opt)
{
    bool ok = cb_cmd_run_opt(cmd, opt);
#ifdef CB_TRACE_CMD_RUN_FAIL_LOCATION
    if (!ok) {
        cb_log(CB_ERROR, "%s:%d: ERROR: cmd_run failed", file, line);
    }
#else
    (void)(file);
    (void)(line);
#endif // CB_TRACE_CMD_RUN_FAIL_LOCATION
    return ok;
}


////////////////////////////////////////////////////////////////////////////////////////////////////
// Cmd Chain：把多条命令用管道串起来
////////////////////////////////////////////////////////////////////////////////////////////////////
// 用法（等价于 foo | bar | baz > out.txt）：
//   CB_Chain chain = CB_ZERO;  CB_Cmd cmd = CB_ZERO;
//   if (!cb_chain_begin(&chain)) return 1;
//   cb_cmd_append(&cmd, "foo"); if (!cb_chain_cmd(&chain, &cmd)) return 1;
//   cb_cmd_append(&cmd, "bar"); if (!cb_chain_cmd(&chain, &cmd)) return 1;
//   cb_cmd_append(&cmd, "baz"); if (!cb_chain_cmd(&chain, &cmd)) return 1;
//   if (!cb_chain_end(&chain, .stdout_path = "out.txt")) return 1;
//
// Chain 里唯一动态分配的是 .cmd，需要自己回收时 cb_da_free(chain.cmd) 即可。

typedef struct {
    // 上一条命令的输出端，会作为下一条命令的输入
    CB_FD fdin;
    // 最近一次 cb_chain_cmd() 累积的命令
    CB_Cmd cmd;
    // 最近一次 cb_chain_cmd() 的 .err2out
    bool err2out;
} CB_Chain;

typedef struct CB_Chain_Begin_Opt {
    const char* stdin_path CB__DEFAULT(nullptr);
} CB_Chain_Begin_Opt;

typedef struct CB_Chain_Cmd_Opt {
    bool err2out CB__DEFAULT(false);
    bool dont_reset CB__DEFAULT(false);
} CB_Chain_Cmd_Opt;

typedef struct CB_Chain_End_Opt {
    CB_Procs* async CB__DEFAULT(nullptr);
    size_t max_procs CB__DEFAULT(0);
    const char* stdout_path CB__DEFAULT(nullptr);
    const char* stderr_path CB__DEFAULT(nullptr);
} CB_Chain_End_Opt;

// 小工具：记录需要在收尾时关闭的 fd（一条链最多用到 3 个，留 5 个位置足够）
typedef struct {
    CB_FD items[5];
    size_t count;
} CB_Fd_List;

// ---- 声明 ----
CBDEF bool cb_chain_begin_opt(CB_Chain* chain, CB_Chain_Begin_Opt opt);
CBDEF bool cb_chain_cmd_opt(CB_Chain* chain, CB_Cmd* cmd, CB_Chain_Cmd_Opt opt);
CBDEF bool cb_chain_end_opt(CB_Chain* chain, CB_Chain_End_Opt opt);

#define cb_chain_begin(chain, ...) cb_chain_begin_opt((chain), CB_CLIT(CB_Chain_Begin_Opt){__VA_ARGS__})
#define cb_chain_cmd(chain, cmd, ...) cb_chain_cmd_opt((chain), (cmd), CB_CLIT(CB_Chain_Cmd_Opt){__VA_ARGS__})
#define cb_chain_end(chain, ...) cb_chain_end_opt((chain), CB_CLIT(CB_Chain_End_Opt){__VA_ARGS__})

// ---- 定义 ----
CBDEF void cb__fd_list_push(CB_Fd_List* list, CB_FD fd)
{
    if (list->count < CB_ARRAY_LEN(list->items)) list->items[list->count++] = fd;
}

CBDEF void cb__fd_list_close_all(CB_Fd_List* list)
{
    for (size_t i = 0; i < list->count; ++i) cb_fd_close(list->items[i]);
    list->count = 0;
}

CBDEF CB_FD cb__fd_stdout(void)
{
#ifdef _WIN32
    return GetStdHandle(STD_OUTPUT_HANDLE);
#else
    return STDOUT_FILENO;
#endif // _WIN32
}

CBDEF bool cb_chain_begin_opt(CB_Chain* chain, CB_Chain_Begin_Opt opt)
{
    chain->cmd.count = 0;
    chain->err2out = false;
    chain->fdin = CB_INVALID_FD;
    if (opt.stdin_path != NULL) {
        chain->fdin = cb_fd_open_read(opt.stdin_path);
        if (chain->fdin == CB_INVALID_FD) return false;
    }
    return true;
}

CBDEF bool cb_chain_cmd_opt(CB_Chain* chain, CB_Cmd* cmd, CB_Chain_Cmd_Opt opt)
{
    bool result = true;
    CB_Pipe pp = CB_ZERO;
    CB_Fd_List fds = CB_ZERO;

    CB_ASSERT(cmd->count > 0);

    if (chain->cmd.count != 0) { // 不是链上的第一条：先把上一条挂上管道跑起来
        CB_FD* pfdin = NULL;
        if (chain->fdin != CB_INVALID_FD) {
            cb__fd_list_push(&fds, chain->fdin);
            pfdin = &chain->fdin;
        }
        if (!cb_pipe_create(&pp)) cb_return_defer(false);
        cb__fd_list_push(&fds, pp.write);

        CB_FD* pfdout = &pp.write;
        CB_FD* pfderr = chain->err2out ? pfdout : NULL;

        CB_Proc proc = cb__cmd_start_process(chain->cmd, pfdin, pfdout, pfderr);
        chain->cmd.count = 0;
        if (proc == CB_INVALID_PROC) {
            cb__fd_list_push(&fds, pp.read);
            cb_return_defer(false);
        }
        chain->fdin = pp.read;
    }

    cb_da_append_many(&chain->cmd, cmd->items, cmd->count);
    chain->err2out = opt.err2out;

defer:
    cb__fd_list_close_all(&fds);
    if (!opt.dont_reset) cmd->count = 0;
    return result;
}

CBDEF bool cb_chain_end_opt(CB_Chain* chain, CB_Chain_End_Opt opt)
{
    bool result = true;
    CB_FD* pfdin = NULL;
    CB_Fd_List fds = CB_ZERO;

    if (chain->fdin != CB_INVALID_FD) {
        cb__fd_list_push(&fds, chain->fdin);
        pfdin = &chain->fdin;
    }

    if (chain->cmd.count != 0) { // 链非空
        size_t max_procs = opt.max_procs > 0 ? opt.max_procs : (size_t)cb_nprocs() + 1;

        if (opt.async != NULL && max_procs > 0) {
            // 达到并发上限就先等一个空位出来
            while (opt.async->count >= max_procs) {
                for (size_t i = 0; i < opt.async->count; ++i) {
                    int ret = cb__proc_wait_async(opt.async->items[i], 1);
                    if (ret < 0) cb_return_defer(false);
                    if (ret) {
                        cb_da_remove_unordered(opt.async, i);
                        break;
                    }
                }
            }
        }

        CB_FD fdout = cb__fd_stdout();
        if (opt.stdout_path != NULL) {
            fdout = cb_fd_open_write(opt.stdout_path);
            if (fdout == CB_INVALID_FD) cb_return_defer(false);
            cb__fd_list_push(&fds, fdout);
        }

        CB_FD fderr = CB_INVALID_FD;
        CB_FD* pfderr = NULL;
        if (chain->err2out) pfderr = &fdout;
        if (opt.stderr_path != NULL) {
            if (pfderr == NULL) {
                fderr = cb_fd_open_write(opt.stderr_path);
                if (fderr == CB_INVALID_FD) cb_return_defer(false);
                cb__fd_list_push(&fds, fderr);
                pfderr = &fderr;
            } else {
                // 最后一条命令设了 err2out：它的 stderr 已经并进 stdout，
                // 所以 stderr 文件应该是空的（这里显式建一个空文件，语义才一致）
                CB_ASSERT(chain->err2out);
                if (!cb_write_entire_file(opt.stderr_path, NULL, 0)) cb_return_defer(false);
            }
        }

        CB_Proc proc = cb__cmd_start_process(chain->cmd, pfdin, &fdout, pfderr);
        chain->cmd.count = 0;

        if (opt.async != NULL) {
            if (proc == CB_INVALID_PROC) cb_return_defer(false);
            cb_da_append(opt.async, proc);
        } else {
            if (!cb_proc_wait(proc)) cb_return_defer(false);
        }
    }

defer:
    cb__fd_list_close_all(&fds);
    return result;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// C Builder
////////////////////////////////////////////////////////////////////////////////////////////////////
#ifndef CB_SELF_REBUILD
#if defined(_WIN32)
#if defined(__clang__)
#if defined(__cplusplus)
#define CB_SELF_REBUILD(binary_path, source_path) "clang", "-x", "c++", "-o", binary_path, source_path
#else
#define CB_SELF_REBUILD(binary_path, source_path) "clang", "-x", "c", "-o", binary_path, source_path
#endif
#elif defined(__GNUC__)
#if defined(__cplusplus)
#define CB_SELF_REBUILD(binary_path, source_path) "gcc", "-x", "c++", "-o", binary_path, source_path
#else
#define CB_SELF_REBUILD(binary_path, source_path) "gcc", "-x", "c", "-o", binary_path, source_path
#endif
#elif defined(_MSC_VER)
#define CB_SELF_REBUILD(binary_path, source_path) "cl.exe", cb_temp_sprintf("/Fe:%s", (binary_path)), source_path
#elif defined(__TINYC__)
#define CB_SELF_REBUILD(binary_path, source_path) "tcc", "-o", binary_path, source_path
#endif
#else
#if defined(__cplusplus)
#define CB_SELF_REBUILD(binary_path, source_path) "cc", "-x", "c++", "-o", binary_path, source_path
#else
#define CB_SELF_REBUILD(binary_path, source_path) "cc", "-x", "c", "-o", binary_path, source_path
#endif
#endif
#endif

CBDEF int cb_needs_rebuild(const char* binary_path, const char** source_paths, size_t source_paths_count);
CBDEF void cb__self_rebuild(int argc, char** argv, const char* source_path, ...);
#define CB_SELF_REBUILD_PLUS(argc, argv, ...) cb__self_rebuild(argc, argv, __FILE__, __VA_ARGS__, NULL)

CBDEF int cb_needs_rebuild(const char* binary_path, const char** source_paths, size_t source_paths_count)
{
#ifdef _WIN32
    BOOL bSuccess;

    HANDLE output_path_fd = CreateFile(binary_path, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_READONLY, NULL);
    if (output_path_fd == INVALID_HANDLE_VALUE) {
        // NOTE: 输出不存在，那必然需要重新构建
        if (GetLastError() == ERROR_FILE_NOT_FOUND) return 1;
        cb_log(CB_ERROR, "Could not open file %s: %s", binary_path, cb_win32_error_message(GetLastError()));
        return -1;
    }
    FILETIME output_path_time;
    bSuccess = GetFileTime(output_path_fd, NULL, NULL, &output_path_time);
    CloseHandle(output_path_fd);
    if (!bSuccess) {
        cb_log(CB_ERROR, "Could not get time of %s: %s", binary_path, cb_win32_error_message(GetLastError()));
        return -1;
    }

    for (size_t i = 0; i < source_paths_count; ++i) {
        const char* input_path = source_paths[i];
        HANDLE input_path_fd = CreateFile(input_path, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_READONLY, NULL);
        if (input_path_fd == INVALID_HANDLE_VALUE) {
            // NOTE: 源文件不存在是错误，构建本来就依赖它
            cb_log(CB_ERROR, "Could not open file %s: %s", input_path, cb_win32_error_message(GetLastError()));
            return -1;
        }
        FILETIME input_path_time;
        bSuccess = GetFileTime(input_path_fd, NULL, NULL, &input_path_time);
        CloseHandle(input_path_fd);
        if (!bSuccess) {
            cb_log(CB_ERROR, "Could not get time of %s: %s", input_path, cb_win32_error_message(GetLastError()));
            return -1;
        }

        // NOTE: 只要有一个源文件比输出新，就必须重新构建
        if (CompareFileTime(&input_path_time, &output_path_time) == 1) return 1;
    }

    return 0;
#else
    struct stat statbuf = CB_ZERO;

    if (stat(binary_path, &statbuf) < 0) {
        // NOTE: 输出不存在就必须重建
        if (errno == ENOENT) return 1;
        cb_log(CB_ERROR, "Could not stat %s: %s", binary_path, strerror(errno));
        return -1;
    }
    time_t binary_path_time = statbuf.st_mtime;

    for (size_t i = 0; i < source_paths_count; ++i) {
        const char* input_path = source_paths[i];
        if (stat(input_path, &statbuf) < 0) {
            // NOTE: 输入不存在是错误（构建本来就依赖它）
            cb_log(CB_ERROR, "Could not stat %s: %s", input_path, strerror(errno));
            return -1;
        }
        time_t source_path_time = statbuf.st_mtime;
        // NOTE: 只要有一个源文件比输出新，就必须重建
        if (source_path_time > binary_path_time) return 1;
    }

    return 0;
#endif // _WIN32
}

// 思路取自 nob.h（后者取自 https://github.com/zhiayang/nabs）
CBDEF void cb__self_rebuild(int argc, char** argv, const char* source_path, ...)
{
    const char* binary_path = cb_shift(argv, argc);
#ifdef _WIN32
    // Windows 上可执行文件通常不带扩展名（是 ./cb 而不是 ./cb.exe），改扩展名时要补上。
    if (!cb_sv_ends_with_cstr(cb_sv_from_cstr(binary_path), ".exe")) {
        binary_path = cb_temp_sprintf("%s.exe", binary_path);
    }
#endif

    CB_File_Paths source_paths = CB_ZERO;
    cb_da_append(&source_paths, source_path);
    va_list args;
    va_start(args, source_path);
    while (1) {
        const char* path = va_arg(args, const char*);
        if (path == NULL) break;
        cb_da_append(&source_paths, path);
    }
    va_end(args);

    int should_rebuild = cb_needs_rebuild(binary_path, source_paths.items, source_paths.count);
    if (should_rebuild < 0) exit(1); // error, return -1
    if (!should_rebuild) {           // dont needed to rebuild
        CB_FREE(source_paths.items);
        return;
    }

    CB_Cmd cmd = CB_ZERO;
    const char* old_binary_path = cb_temp_sprintf("%s.old", binary_path);

    if (!cb_rename(binary_path, old_binary_path)) exit(1);
    cb_cmd_append(&cmd, CB_SELF_REBUILD(binary_path, source_path));
    CB_Cmd_Opt opt = CB_ZERO;
    if (!cb_cmd_run_opt(&cmd, opt)) {
        cb_rename(old_binary_path, binary_path);
        exit(1);
    }

#ifndef CB_DONT_DELETE_OLD_CB
    cb_delete_file(old_binary_path);
#endif // CB_EXPERIMENTAL_DELETE_OLD

    cb_cmd_append(&cmd, binary_path);
    cb_da_append_many(&cmd, argv, argc);
    if (!cb_cmd_run_opt(&cmd, opt)) exit(1);
    exit(0);
}

// 每个宏都要有自己的 #ifndef 守卫：这一整块只是"默认值"，调用方（比如项目的构建脚本）
// 可以在 #include "cb.h" 之前定义自己的 cb_cc / cb_cc_flags 把它顶掉。
// TODO: we should test on C++ compilers too
#ifndef cb_cc_flags
#if defined(__cplusplus)
#if defined(_MSC_VER)
#define cb_cc_flags(cmd) cb_cmd_append(cmd, "/std:c++20", "/TP", "/W4", "/nologo", "/D_CRT_SECURE_NO_WARNINGS", "-I.")
#else
#ifndef cb_cc
#define cb_cc(cmd) cb_cmd_append(cmd, "cc", "-x", "c++")
#endif
#define cb_cc_flags(cmd) cb_cmd_append(cmd, "-Wall", "-Wextra", "-Wno-missing-field-initializers", "-Wswitch-enum", "-ggdb", "-I.");
#endif
#else // __cplusplus
#if defined(_MSC_VER)
#define cb_cc_flags(cmd) cb_cmd_append(cmd, "/TC", "/W4", "/nologo", "/D_CRT_SECURE_NO_WARNINGS", "-I.")
#elif defined(__APPLE__) || defined(__MACH__)
// "-std=c11" / "-D_POSIX_C_SOURCE=200809L" 在 macOS 上会藏掉需要的符号（原因不明）
#define cb_cc_flags(cmd) cb_cmd_append(cmd, "-Wall", "-Wextra", "-Wswitch-enum", "-I.")
#elif defined(__FreeBSD__)
// "-D_POSIX_C_SOURCE=200809L" 在 FreeBSD 上会藏掉需要的符号
#define cb_cc_flags(cmd) cb_cmd_append(cmd, "-Wall", "-Wextra", "-Wswitch-enum", "-std=c11", "-ggdb", "-I.");
#else
#define cb_cc_flags(cmd) cb_cmd_append(cmd, "-Wall", "-Wextra", "-Wswitch-enum", "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-ggdb", "-I.");
#endif
#endif // __cplusplus
#endif // !cb_cc_flags

#ifndef cb_cc
#if _WIN32
#if defined(__GNUC__)
#define cb_cc(cmd) cb_cmd_append(cmd, "cc")
#elif defined(__clang__)
#define cb_cc(cmd) cb_cmd_append(cmd, "clang")
#elif defined(_MSC_VER)
#define cb_cc(cmd) cb_cmd_append(cmd, "cl.exe")
#elif defined(__TINYC__)
#define cb_cc(cmd) cb_cmd_append(cmd, "tcc")
#endif
#else
#define cb_cc(cmd) cb_cmd_append(cmd, "cc")
#endif
#endif // cb_cc

#ifndef cb_cc_flags
#if defined(_MSC_VER) && !defined(__clang__)
#define cb_cc_flags(cmd) cb_cmd_append(cmd, "/W4", "/nologo", "/D_CRT_SECURE_NO_WARNINGS")
#else
#define cb_cc_flags(cmd) cb_cmd_append(cmd, "-Wall", "-Wextra")
#endif
#endif // cb_cc_flags

#ifndef cb_cc_output
#if defined(_MSC_VER) && !defined(__clang__)
#define cb_cc_output(cmd, output_path) cb_cmd_append(cmd, cb_temp_sprintf("/Fe:%s", (output_path)), cb_temp_sprintf("/Fo:%s", (output_path)))
#else
#define cb_cc_output(cmd, output_path) cb_cmd_append(cmd, "-o", (output_path))
#endif
#endif // cb_cc_output

#ifndef cb_cc_inputs
#define cb_cc_inputs(cmd, ...) cb_cmd_append(cmd, __VA_ARGS__)
#endif // cb_cc_inputs

#endif // STD_CB_H

// >>> CB_STRIP_PREFIX 生成区开始（tools/gen-docs.py 生成，不要手改）
////////////////////////////////////////////////////////////////////////////////////////////////////
// CB_STRIP_PREFIX：去前缀别名
////////////////////////////////////////////////////////////////////////////////////////////////////
// 默认不生效。在 #include "cb.h" 之前 #define CB_STRIP_PREFIX，就能写
// temp_sprintf / String_View，而不是 cb_temp_sprintf / CB_String_View。
//
// 为什么放在文件最末尾：实现区用的都是带前缀的名字，别名区如果放在前面会互相干扰。
// 自带 include guard，header-only 模式下被多个 .c 各自 include 也只展开一次。
//
// 刻意不生成别名的名字（去掉前缀会与标准库 / POSIX / 系统库 / 第三方库撞车）：
//   ERROR            撞 mingw <wingdi.h> 的 #define ERROR 0
//   PATH_MAX         撞 limits.h 的 PATH_MAX（CB_PATH_MAX 本身就是从它兜底来的）
//   clamp            撞 C++ std::clamp
//   glob             撞 POSIX glob.h 的 glob()
//   log              撞 libm 的 log()
//   max              撞 Windows 的 max 宏、C++ std::max
//   min              撞 Windows 的 min 宏、C++ std::min
//   rename           撞 stdio.h 的 rename()
//   rotl64           撞 C23 <stdbit.h> / glibc 的 rotl64
//   rotr64           撞 C23 <stdbit.h> / glibc 的 rotr64
//   swap             撞 C++ std::swap
//
// 名单不是凭印象列的：拿 libc/libm/libpthread、mingw 导入库的真实导出符号，
// 以及 clang/mingw -dM -E 的全部系统宏扫出来比对，命中的就是上面这些名字。
//
// 本区由 tools/gen-docs.py 生成，不要手改；./cb docs --check 会校验它与 cb.h 是否同步。
#ifndef CB_STRIP_PREFIX_GUARD_
#define CB_STRIP_PREFIX_GUARD_
#  ifdef CB_STRIP_PREFIX
// ---- 版本 ----
#    define VERSION_MAJOR CB_VERSION_MAJOR
#    define VERSION_MINOR CB_VERSION_MINOR
#    define VERSION_PATCH CB_VERSION_PATCH
#    define VERSION_STRING CB_VERSION_STRING
// ---- General ----
#    define ASSERT CB_ASSERT
#    define PRINTF_FORMAT CB_PRINTF_FORMAT
#    define SHARED_STATE CB_SHARED_STATE
// ---- 内存分配收口 ----
#    define CLIT CB_CLIT
#    define DECLTYPE_CAST CB_DECLTYPE_CAST
#    define FREE CB_FREE
#    define FREE_RAW CB_FREE_RAW
#    define OOM CB_OOM
#    define REALLOC CB_REALLOC
#    define REALLOC_RAW CB_REALLOC_RAW
#    define alloc_check cb_alloc_check
#    define alloc_live_count cb_alloc_live_count
#    define alloc_live_size cb_alloc_live_size
#    define alloc_report cb_alloc_report
#    define shift cb_shift
// ---- Logger / Panic ----
#    define ARRAY_GET CB_ARRAY_GET
#    define ARRAY_LEN CB_ARRAY_LEN
#    define DEPRECATED CB_DEPRECATED
#    define INFO CB_INFO
#    define LOG_AT CB_LOG_AT
#    define Log_Level CB_Log_Level
#    define NO_LOGS CB_NO_LOGS
#    define PANIC_BACKTRACE CB_PANIC_BACKTRACE
#    define TODO CB_TODO
#    define UNREACHABLE CB_UNREACHABLE
#    define UNUSED CB_UNUSED
#    define WARN CB_WARN
#    define ZERO CB_ZERO
#    define cancer_log_handler cb_cancer_log_handler
#    define default_log_handler cb_default_log_handler
#    define get_log_handler cb_get_log_handler
#    define log_at cb_log_at
#    define null_log_handler cb_null_log_handler
#    define return_defer cb_return_defer
#    define set_log_handler cb_set_log_handler
// ---- Timer ----
#    define TIMER_END CB_TIMER_END
#    define TIMER_MAX_DEPTH CB_TIMER_MAX_DEPTH
#    define TIMER_START CB_TIMER_START
#    define Timer CB_Timer
#    define Timer_Frame CB_Timer_Frame
#    define Timer_Stat CB_Timer_Stat
#    define get_time_ms cb_get_time_ms
#    define get_time_us cb_get_time_us
#    define nanos_since_unspecified_epoch cb_nanos_since_unspecified_epoch
#    define timer_begin cb_timer_begin
#    define timer_end cb_timer_end
#    define timer_end_print cb_timer_end_print
#    define timer_end_stat cb_timer_end_stat
#    define timer_fprint_stats cb_timer_fprint_stats
#    define timer_get_stat cb_timer_get_stat
#    define timer_print_stats cb_timer_print_stats
#    define timer_reset cb_timer_reset
// ---- Arena / Temp Storage ----
#    define ARENA_ALIGN CB_ARENA_ALIGN
#    define ARENA_REGION_INIT_CAPACITY CB_ARENA_REGION_INIT_CAPACITY
#    define Arena CB_Arena
#    define Arena_Mark CB_Arena_Mark
#    define Arena_Region CB_Arena_Region
#    define TEMP_CAPACITY CB_TEMP_CAPACITY
#    define THREAD_LOCAL CB_THREAD_LOCAL
#    define arena_alloc cb_arena_alloc
#    define arena_alloc_aligned cb_arena_alloc_aligned
#    define arena_free cb_arena_free
#    define arena_realloc cb_arena_realloc
#    define arena_reset cb_arena_reset
#    define arena_rewind cb_arena_rewind
#    define arena_save cb_arena_save
#    define arena_sprintf cb_arena_sprintf
#    define arena_strdup cb_arena_strdup
#    define arena_strndup cb_arena_strndup
#    define arena_vsprintf cb_arena_vsprintf
#    define temp_alloc cb_temp_alloc
#    define temp_reset cb_temp_reset
#    define temp_rewind cb_temp_rewind
#    define temp_save cb_temp_save
#    define temp_sprintf cb_temp_sprintf
#    define temp_strdup cb_temp_strdup
#    define temp_strndup cb_temp_strndup
#    define temp_vsprintf cb_temp_vsprintf
// ---- Dynamic Array ----
#    define DA_INIT_CAP CB_DA_INIT_CAP
#    define DArray CB_DArray
#    define da_append cb_da_append
#    define da_append_many cb_da_append_many
#    define da_clear cb_da_clear
#    define da_first cb_da_first
#    define da_foreach cb_da_foreach
#    define da_foreach_rev cb_da_foreach_rev
#    define da_free cb_da_free
#    define da_insert cb_da_insert
#    define da_last cb_da_last
#    define da_pop cb_da_pop
#    define da_remove_ordered cb_da_remove_ordered
#    define da_remove_unordered cb_da_remove_unordered
#    define da_reserve cb_da_reserve
#    define da_resize cb_da_resize
// ---- Bitset ----
#    define BITSET_WORD_BITS CB_BITSET_WORD_BITS
#    define Bitset CB_Bitset
#    define bitset_clear_all cb_bitset_clear_all
#    define bitset_count cb_bitset_count
#    define bitset_find cb_bitset_find
#    define bitset_free cb_bitset_free
#    define bitset_resize cb_bitset_resize
#    define bitset_set cb_bitset_set
#    define bitset_test cb_bitset_test
#    define bitset_toggle cb_bitset_toggle
#    define bitset_unset cb_bitset_unset
// ---- Ring Buffer ----
#    define Ring CB_Ring
#    define ring_clear cb_ring_clear
#    define ring_free cb_ring_free
#    define ring_init cb_ring_init
#    define ring_peek cb_ring_peek
#    define ring_read cb_ring_read
#    define ring_space cb_ring_space
#    define ring_write cb_ring_write
// ---- Sort ----
#    define Compare_Func CB_Compare_Func
#    define da_sort cb_da_sort
#    define da_sort_insertion cb_da_sort_insertion
// ---- StringBuilder ----
#    define String_Builder CB_String_Builder
#    define read_entire_file cb_read_entire_file
#    define sb_append cb_sb_append
#    define sb_append_buf cb_sb_append_buf
#    define sb_append_cstr cb_sb_append_cstr
#    define sb_append_sv cb_sb_append_sv
#    define sb_appendf cb_sb_appendf
#    define sb_free cb_sb_free
#    define sb_pad_align cb_sb_pad_align
// ---- StringView ----
#    define SVLIT CB_SVLIT
#    define SVLIT_STATIC CB_SVLIT_STATIC
#    define SV_ARG CB_SV_ARG
#    define SV_FMT CB_SV_FMT
#    define String_View CB_String_View
#    define sb_to_sv cb_sb_to_sv
#    define sv_chop_by_delim cb_sv_chop_by_delim
#    define sv_chop_by_delim_r cb_sv_chop_by_delim_r
#    define sv_chop_by_func cb_sv_chop_by_func
#    define sv_chop_left cb_sv_chop_left
#    define sv_chop_prefix cb_sv_chop_prefix
#    define sv_chop_right cb_sv_chop_right
#    define sv_chop_suffix cb_sv_chop_suffix
#    define sv_ends_with cb_sv_ends_with
#    define sv_ends_with_cstr cb_sv_ends_with_cstr
#    define sv_eq cb_sv_eq
#    define sv_find cb_sv_find
#    define sv_find_sv cb_sv_find_sv
#    define sv_from_cstr cb_sv_from_cstr
#    define sv_from_parts cb_sv_from_parts
#    define sv_starts_with cb_sv_starts_with
#    define sv_starts_with_cstr cb_sv_starts_with_cstr
#    define sv_to_temp_cstr cb_sv_to_temp_cstr
#    define sv_trim cb_sv_trim
#    define sv_trim_left cb_sv_trim_left
#    define sv_trim_right cb_sv_trim_right
// ---- UTF-8 Support ----
#    define sv_utf8_len cb_sv_utf8_len
// ---- StringView Tools（大小写 / 数字解析 / split-join） ----
#    define sb_append_join cb_sb_append_join
#    define sv_eq_ignore_case cb_sv_eq_ignore_case
#    define sv_split_next cb_sv_split_next
#    define sv_to_f64 cb_sv_to_f64
#    define sv_to_i64 cb_sv_to_i64
#    define sv_to_temp_lower cb_sv_to_temp_lower
#    define sv_to_temp_upper cb_sv_to_temp_upper
#    define sv_to_u64 cb_sv_to_u64
#    define sv_utf8_next cb_sv_utf8_next
#    define utf8_decode cb_utf8_decode
#    define utf8_encode cb_utf8_encode
#    define utf8_validate cb_utf8_validate
// ---- HashMap ----
#    define MAP_EMPTY CB_MAP_EMPTY
#    define MAP_INIT_CAPACITY CB_MAP_INIT_CAPACITY
#    define MAP_TOMBSTONE CB_MAP_TOMBSTONE
#    define MAP_USED CB_MAP_USED
#    define Map CB_Map
#    define Map_Iter CB_Map_Iter
#    define Map_Slot CB_Map_Slot
#    define hash_bytes cb_hash_bytes
#    define hash_u64 cb_hash_u64
#    define map_clear cb_map_clear
#    define map_count cb_map_count
#    define map_del cb_map_del
#    define map_foreach cb_map_foreach
#    define map_free cb_map_free
#    define map_get cb_map_get
#    define map_get_cstr cb_map_get_cstr
#    define map_has cb_map_has
#    define map_init_arena cb_map_init_arena
#    define map_init_capacity cb_map_init_capacity
#    define map_iter cb_map_iter
#    define map_next cb_map_next
#    define map_put cb_map_put
#    define map_put_cstr cb_map_put_cstr
// ---- HashMap：uint64 键版本 ----
#    define Map_U64 CB_Map_U64
#    define Map_U64_Iter CB_Map_U64_Iter
#    define Map_U64_Slot CB_Map_U64_Slot
#    define map_u64_clear cb_map_u64_clear
#    define map_u64_count cb_map_u64_count
#    define map_u64_del cb_map_u64_del
#    define map_u64_foreach cb_map_u64_foreach
#    define map_u64_free cb_map_u64_free
#    define map_u64_get cb_map_u64_get
#    define map_u64_has cb_map_u64_has
#    define map_u64_init_arena cb_map_u64_init_arena
#    define map_u64_init_capacity cb_map_u64_init_capacity
#    define map_u64_iter cb_map_u64_iter
#    define map_u64_next cb_map_u64_next
#    define map_u64_put cb_map_u64_put
// ---- File System ----
#    define FILE_DIRECTORY CB_FILE_DIRECTORY
#    define FILE_ERROR CB_FILE_ERROR
#    define FILE_OTHER CB_FILE_OTHER
#    define FILE_REGULAR CB_FILE_REGULAR
#    define FILE_SYMLINK CB_FILE_SYMLINK
#    define File_Paths CB_File_Paths
#    define File_Type CB_File_Type
#    define WALK_CONT CB_WALK_CONT
#    define WALK_SKIP CB_WALK_SKIP
#    define WALK_STOP CB_WALK_STOP
#    define WIN32_ERR_MSG_SIZE CB_WIN32_ERR_MSG_SIZE
#    define Walk_Action CB_Walk_Action
#    define Walk_Dir_Opt CB_Walk_Dir_Opt
#    define Walk_Entry CB_Walk_Entry
#    define Walk_Func CB_Walk_Func
#    define copy_directory_recursively cb_copy_directory_recursively
#    define copy_file cb_copy_file
#    define delete_directory_recursively cb_delete_directory_recursively
#    define delete_file cb_delete_file
#    define delete_walk_entry cb_delete_walk_entry
#    define dir_entry_close cb_dir_entry_close
#    define dir_entry_next cb_dir_entry_next
#    define dir_entry_open cb_dir_entry_open
#    define file_exists cb_file_exists
#    define get_current_dir_temp cb_get_current_dir_temp
#    define get_file_type cb_get_file_type
#    define mkdir_if_not_exists cb_mkdir_if_not_exists
#    define path_name cb_path_name
#    define read_entire_dir cb_read_entire_dir
#    define set_current_dir cb_set_current_dir
#    define temp_dir_name cb_temp_dir_name
#    define temp_file_ext cb_temp_file_ext
#    define temp_file_name cb_temp_file_name
#    define temp_running_executable_path cb_temp_running_executable_path
#    define walk_dir cb_walk_dir
#    define walk_dir_opt cb_walk_dir_opt
#    define win32_error_message cb_win32_error_message
#    define write_entire_file cb_write_entire_file
// ---- 路径处理 ----
#    define PATH_SEP CB_PATH_SEP
#    define path_absolute cb_path_absolute
#    define path_is_absolute cb_path_is_absolute
#    define path_is_sep cb_path_is_sep
#    define path_join cb_path_join
#    define path_normalize cb_path_normalize
#    define path_replace_ext cb_path_replace_ext
// ---- File System 扩展：元信息 / 递归建目录 / 原子写 / 符号链接 / glob / mmap ----
#    define Mmap CB_Mmap
#    define PROCESS_ID CB_PROCESS_ID
#    define create_symlink cb_create_symlink
#    define file_mtime cb_file_mtime
#    define file_size cb_file_size
#    define glob_match cb_glob_match
#    define mmap_close cb_mmap_close
#    define mmap_open cb_mmap_open
#    define read_symlink cb_read_symlink
#    define write_entire_file_atomic cb_write_entire_file_atomic
// ---- Math & Bits ----
#    define align_down cb_align_down
#    define align_up cb_align_up
#    define bswap32 cb_bswap32
#    define bswap64 cb_bswap64
#    define clz64 cb_clz64
#    define ctz64 cb_ctz64
#    define is_little_endian cb_is_little_endian
#    define is_pow2 cb_is_pow2
#    define next_pow2 cb_next_pow2
#    define popcount64 cb_popcount64
// ---- Time & Date ----
#    define duration_to_str cb_duration_to_str
#    define time_now cb_time_now
#    define time_to_iso8601 cb_time_to_iso8601
// ---- Random ----
#    define Rng CB_Rng
#    define rng_double cb_rng_double
#    define rng_next cb_rng_next
#    define rng_range cb_rng_range
#    define rng_seed cb_rng_seed
#    define rng_shuffle cb_rng_shuffle
// ---- Runtime Environment ----
#    define color_enabled cb_color_enabled
#    define env_get cb_env_get
#    define env_set cb_env_set
#    define stdout_is_tty cb_stdout_is_tty
#    define terminal_width cb_terminal_width
// ---- Hex Dump ----
#    define dump_hex cb_dump_hex
// ---- CLI Args ----
#    define Arg_Entry CB_Arg_Entry
#    define Arg_List CB_Arg_List
#    define Args CB_Args
#    define args_free cb_args_free
#    define args_get cb_args_get
#    define args_get_first cb_args_get_first
#    define args_has cb_args_has
#    define args_parse cb_args_parse
#    define args_positional cb_args_positional
#    define args_positional_count cb_args_positional_count
// ---- Process & FD ----
#    define INVALID_FD CB_INVALID_FD
#    define INVALID_PROC CB_INVALID_PROC
#    define Pipe CB_Pipe
#    define Procs CB_Procs
#    define fd_close cb_fd_close
#    define fd_open_read cb_fd_open_read
#    define fd_open_write cb_fd_open_write
#    define pipe_create cb_pipe_create
#    define proc_wait cb_proc_wait
#    define procs_wait cb_procs_wait
#    define procs_wait_and_reset cb_procs_wait_and_reset
// ---- Cmd ----
#    define Cmd CB_Cmd
#    define Cmd_Opt CB_Cmd_Opt
#    define Pipes CB_Pipes
#    define cmd_append cb_cmd_append
#    define cmd_extend cb_cmd_extend
#    define cmd_free cb_cmd_free
#    define cmd_run cb_cmd_run
#    define cmd_run_opt cb_cmd_run_opt
#    define cmd_to_sb cb_cmd_to_sb
#    define nprocs cb_nprocs
// ---- Cmd Chain：把多条命令用管道串起来 ----
#    define Chain CB_Chain
#    define Chain_Begin_Opt CB_Chain_Begin_Opt
#    define Chain_Cmd_Opt CB_Chain_Cmd_Opt
#    define Chain_End_Opt CB_Chain_End_Opt
#    define Fd_List CB_Fd_List
#    define chain_begin cb_chain_begin
#    define chain_begin_opt cb_chain_begin_opt
#    define chain_cmd cb_chain_cmd
#    define chain_cmd_opt cb_chain_cmd_opt
#    define chain_end cb_chain_end
#    define chain_end_opt cb_chain_end_opt
// ---- C Builder ----
#    define SELF_REBUILD CB_SELF_REBUILD
#    define SELF_REBUILD_PLUS CB_SELF_REBUILD_PLUS
#    define cc cb_cc
#    define cc_flags cb_cc_flags
#    define cc_inputs cb_cc_inputs
#    define cc_output cb_cc_output
#    define needs_rebuild cb_needs_rebuild
#  endif // CB_STRIP_PREFIX
#endif // CB_STRIP_PREFIX_GUARD_
// <<< CB_STRIP_PREFIX 生成区结束
