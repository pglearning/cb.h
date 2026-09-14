# cb.h 使用指南

本文按模块讲"怎么用、以及为什么这样设计"。每个模块都配了可运行的示例，
都在 [`examples/`](../examples/) 下，可以直接：

```console
$ ./cb examples     # 编译并运行全部示例
```

更细的接口清单见 [`api.md`](api.md)，与上游 nob.h 的差异见 [`nob-comparison.md`](nob-comparison.md)，
每个接口在哪个示例里被用到见 [`api-coverage.md`](api-coverage.md)。

每个模块对应哪个示例：

| 模块 | 示例 |
|---|---|
| 构建脚本（单项目 / 多项目 / 两阶段 / API 全量） | `01_hello` `02_single_project` `03_multi_project` `04_two_stage` `05_build_api` |
| 容器与算法 | `06_containers` |
| 字符串 | `07_strings` |
| utf8 | `08_utf8` |
| 路径与 glob | `09_paths` |
| 文件系统 | `10_filesystem` |
| 内存 | `11_memory` |
| 日志与 panic | `12_logging` |
| 工具箱 | `13_toolbox` |
| 编译期开关 | `14_config_switches` |

性能基准与示例/测试是分开的：`./cb bench`（源码在 [`bench/`](../bench/)，说明见它的 README）。

---

## 0. 三条必须先知道的约定

**① `cb.h` 不必是第一个被 include 的头文件**

```c
#include "cb.h"     // 推荐放最前面，但已经不是必须
#include <stdio.h>  // 先包含系统头也能编译
```

cb.h 会在内部定义 `_POSIX_C_SOURCE`，而 glibc 的 `features.h` 一辈子只处理一次，所以系统头
先被包含时那个定义就失效。以前必须靠"把 cb.h 放第一个"或 `-D_POSIX_C_SOURCE=200809L` 绕过去；
现在 cb.h 会在系统头之后再按 glibc 自己的 `__USE_*` 守门宏，把真正缺的那几个 POSIX 原型补上：

`clock_gettime` / `CLOCK_MONOTONIC` / `fileno` / `readlink` / `symlink` / `lstat` / `gmtime_r` /
`setenv` / `nanosleep`。其它平台、其它 libc 一个字都不加（所以不会和别处的声明打架）。

**② 不需要 `#define CB_IMPLEMENTATION`**

cb.h 是纯 header-only：`CBDEF` 固定展开为 `static inline`，所有函数体就在头文件里。
任何 `.c` 只要 include 就能用全部功能。

**③ 状态默认"每 TU 一份"，需要时可共享**

日志级别、temp 栈、timer 统计默认都是每个 TU 一份 `static`：零依赖、可以随便单编译，
代价是在 A.c 设的 `cb_minimal_log_level`，B.c 看不见。

要让多个 TU 共享同一份状态，就打开可选共享模式（默认行为一个字不变）：

```c
// a.c —— 恰好一个 TU 给出定义
#define CB_SHARED_STATE_IMPL   // 自动蕴含 CB_SHARED_STATE
#include "cb.h"
```
```c
// b.c —— 其余 TU 只声明
#define CB_SHARED_STATE
#include "cb.h"
```
```console
$ cc -c a.c && cc -c b.c && cc a.o b.o -o app
```

共享的是：`cb_minimal_log_level`、`cb_log_handler`、`cb_timer`、temp 栈（`_Thread_local`，
所以是"每线程一份、所有 TU 共用同一份"）、以及 `CB_ALLOC_TRACK` 的计数。
跨 TU **读** temp 内存本来就是安全的；共享模式下 A 的 `cb_temp_reset` 才会回收 B 分配的 temp。

---

## 1. 构建脚本

cb.h 的主用途。核心是"用 C 写构建逻辑"，不需要 Make/CMake/Shell。

完整可运行示例：[`examples/01_hello.c`](../examples/01_hello.c)

### 1.1 自重建

```c
int main(int argc, char** argv)
{
    CB_SELF_REBUILD_PLUS(argc, argv, "cb.h");  // 自己或 cb.h 变新了就重新编译自己
    // ... 真正的构建逻辑
}
```

- 它比较**可执行文件**与**源文件**的修改时间，需要时重新编译并重新执行
- 注意路径是**相对于当前工作目录**解析的，不是相对于源文件所在目录
- `CB_DONT_DELETE_OLD_CB` 可以保留旧二进制（`.old`）便于排查

### 1.2 拼命令

```c
CB_Cmd cmd = CB_ZERO;
cb_cmd_append(&cmd, "cc", "-Wall", "-o", "app", "main.c");
if (!cb_cmd_run(&cmd)) return 1;
```

`cb_cmd_run` 是个变参宏，可选的具名参数（`CB_Cmd_Opt` 的字段）：

| 选项 | 作用 |
|---|---|
| `.async = &procs` | 异步启动，把进程句柄塞进 `procs` |
| `.max_procs = n` | 配合 `.async` 限制并发数（满了就先等一个结束） |
| `.stdout_path = "log.txt"` | stdout 重定向到文件 |
| `.stderr_path = "err.txt"` | stderr 重定向到文件 |
| `.stdin_path = "in.txt"` | stdin 来自文件 |
| `.dont_reset` | 跑完不把 `cmd.count` 清零（默认会清零以便复用） |

```c
CB_Procs procs = CB_ZERO;
for (每个源文件) {
    cmd.count = 0;
    cb_cmd_append(&cmd, "cc", "-c", "-o", obj, src);
    if (!cb_cmd_run(&cmd, .async = &procs, .max_procs = (size_t)cb_nprocs())) return 1;
}
if (!cb_procs_wait_and_reset(&procs)) return 1;   // 等全部结束
```

### 1.3 增量构建

```c
const char* deps[] = {"main.c", "cb.h"};   // 这个目标依赖的全部输入
int needs = cb_needs_rebuild("app", deps, CB_ARRAY_LEN(deps));
if (needs < 0) return 1;      // 出错
if (needs == 0) continue;     // 已是最新，跳过
// 否则需要重新编译
```

### 1.4 管道串联

```c
CB_Chain chain = CB_ZERO;
CB_Cmd cmd = CB_ZERO;

cb_chain_begin(&chain);                          // 可选 .stdin_path
cb_cmd_append(&cmd, "cc", "--version");
cb_chain_cmd(&chain, &cmd);                      // 链上的第一段
cb_cmd_append(&cmd, "head", "-n", "1");
cb_chain_cmd(&chain, &cmd);                      // 上一段的输出接进来
cb_chain_end(&chain);                            // 可选 .stdout_path / .stderr_path / .async

cb_da_free(chain.cmd);
```

`cb_chain_cmd(&chain, &cmd, .err2out = true)` 会把该段的 stderr 并进 stdout。

### 1.5 C Builder：编译器与选项从哪来

不要在调用处硬写 `"cc"` 和一长串 flag，交给 cb.h 的四个宏，换平台 / 换语言模式时只改一处：

| 宏 | 作用 |
|---|---|
| `cb_cc(cmd)` | 追加编译器驱动命令 |
| `cb_cc_flags(cmd)` | 追加告警 / 语言标准 / 调试信息标志 |
| `cb_cc_output(cmd, path)` | 追加输出参数（Unix 是 `-o path`，MSVC 是 `/Fe:path` `/Fo:path`） |
| `cb_cc_inputs(cmd, ...)` | 追加输入文件（默认原样追加，留它是为了 MSVC 这类平台能统一改写） |

四个宏都自带默认值，直接就能用。默认值按平台与**构建脚本自己被编译时的**语言模式分
（`-I.` 是"当前目录当头文件搜索路径"，构建脚本通常就在项目根跑）：

| 场景 | `cb_cc` | `cb_cc_flags` |
|---|---|---|
| C（Linux 等） | `cc` | `-Wall -Wextra -Wswitch-enum -std=c11 -D_POSIX_C_SOURCE=200809L -ggdb -I.` |
| C（macOS） | `cc` | `-Wall -Wextra -Wswitch-enum -I.`（加 `-std=c11` / `-D_POSIX_C_SOURCE` 会藏掉需要的符号） |
| C（FreeBSD） | `cc` | `-Wall -Wextra -Wswitch-enum -std=c11 -ggdb -I.` |
| C（MSVC） | `cl.exe` | `/TC /W4 /nologo /D_CRT_SECURE_NO_WARNINGS -I.` |
| C++（非 MSVC） | `cc -x c++` | `-Wall -Wextra -Wno-missing-field-initializers -Wswitch-enum -ggdb -I.` |
| C++（MSVC） | `cl.exe` | `/std:c++20 /TP /W4 /nologo /D_CRT_SECURE_NO_WARNINGS -I.` |

默认值里**没有**项目自己的东西：没有 `-O2`（`./cb bench` 自己追加）、没有项目特定的 `-D`、
没有 `-std=c++17` 这类 C++ 标准版本、没有 `-l` 链接库、没有 `-rdynamic`。
所以真实项目的构建脚本通常会把 `cb_cc` / `cb_cc_flags` 整对顶掉。

**覆盖方式**：宏是 `#ifndef` 守卫的，在 `#include "cb.h"` **之前**定义自己的版本即可
（定义在 include 之后等于重定义宏，头文件里早已展开过默认值）：

```c
// 编译器与 flag 只在这一处定义，调用处不再出现 "cc" 与一长串选项
#define cb_cc(cmd)       cb_cmd_append(cmd, "cc")
#define cb_cc_flags(cmd) cb_cmd_append(cmd, "-Wall", "-Wextra", "-std=gnu11", "-I.", "-Iinclude")
#include "cb.h"
```

两个宏的覆盖是独立的：只定义 `cb_cc_flags` 时 `cb_cc` 仍是默认值，反之也一样。

**实例**：[`cb.c` 开头](../cb.c)就顶掉了整对（C++ 下带 `-x c++`），所以 `./cb` 自己、测试、
示例、基准用的是同一套选项。C 模式默认给 `-std=c11` 而不是 `-std=c99`：C11 是 cb.h 的基线，
库本身就要 `_Thread_local` / `alignof` 这些 C11 才有的东西。

---

## 2. 文件系统

### 2.1 读写

```c
// 读：内容进 StringBuilder（末尾一定是 '\0'，也可以按 String_View 访问）
CB_String_Builder sb = CB_ZERO;
if (cb_read_entire_file("input.txt", &sb)) {
    CB_String_View sv = cb_sb_to_sv(sb);
    cb_sb_free(sb);
}

// 写
cb_write_entire_file("out.bin", data, size);

// 原子写：先写临时文件再 rename，避免写到一半崩溃毁掉原文件
cb_write_entire_file_atomic("config.txt", data, size);
```

### 2.2 目录

```c
cb_mkdir_if_not_exists("a/b/c");            // mkdir -p：中间层不存在会自动建
cb_delete_directory_recursively("build/");  // 递归删除
cb_copy_directory_recursively("src", "dst");
cb_delete_file("x.tmp");
cb_rename("a", "b");

CB_File_Paths entries = CB_ZERO;
if (cb_read_entire_dir("src", &entries)) {   // 不含 "." 与 ".."
    for (size_t i = 0; i < entries.count; ++i) puts(entries.items[i]);
    cb_da_free(entries);                     // 字符串本身在 temp 上
}
```

### 2.3 递归遍历

```c
bool on_entry(CB_Walk_Entry entry)
{
    printf("%*s%s\n", (int)entry.level * 2, "", entry.path);
    // 通过 *entry.action 控制：CB_WALK_CONT / CB_WALK_SKIP / CB_WALK_STOP
    return true;
}

cb_walk_dir("src", on_entry);                       // 前序
cb_walk_dir("build", on_entry, .post_order = true); // 后序（先子后父，适合删除）
```

`CB_Walk_Dir_Opt.data` 会原样传给回调的 `entry.data`，用于传上下文。

### 2.4 元信息与符号链接

```c
cb_file_size("x");          // 失败返回 (size_t)-1
cb_file_mtime("x");         // Unix 时间戳，失败返回 -1
cb_get_file_type("x");      // lstat，不跟随符号链接：
                            // CB_FILE_REGULAR / CB_FILE_DIRECTORY / CB_FILE_SYMLINK / CB_FILE_OTHER / CB_FILE_ERROR

cb_create_symlink("target", "link");
char* dest = cb_read_symlink("link");   // temp 上
```

### 2.5 glob 与 mmap

```c
if (cb_glob_match("*.c", name)) { ... }   // 纯匹配：* ? [a-z] [!abc]

CB_File_Paths hits = CB_ZERO;
cb_glob("src", "*.c", &hits);             // 只匹配单层，路径是 "src/xxx.c"
cb_da_free(hits);

CB_Mmap m = CB_ZERO;
if (cb_mmap_open("big.bin", &m)) {
    // m.data / m.size：只读映射，零拷贝
    cb_mmap_close(&m);
}
```

---

## 3. 路径

```c
cb_path_join("src", "main.c");        // "src/main.c"
cb_path_join("src", "/abs");          // "/abs"（b 是绝对路径时以 b 为准）
cb_path_normalize("a/./b/../c");      // "a/c"
cb_path_normalize("a//b///c");        // "a/b/c"
cb_path_absolute("x");                // 基于 cwd 的绝对路径
cb_path_is_absolute("/x");            // true
cb_path_replace_ext("a/b.c", "h");    // "a/b.h"
cb_path_replace_ext("a/b.c", NULL);   // "a/b"
```

三条行为约定：

1. **结果都在 temp storage 上**，不用你释放
2. **只做字符串处理，不访问文件系统**（唯一例外是 `cb_path_absolute`，它要读 cwd）
3. **输出统一用平台原生分隔符**（Windows 是 `\`，其余是 `/`），
   但**输入**两种分隔符都认（Windows 下 `a/b` 与 `a\b` 等价）

---

## 4. 内存

### 4.1 Arena：显式作用域分配器

```c
CB_Arena arena = CB_ZERO;

char* s = cb_arena_sprintf(&arena, "value=%d", 42);
int* xs = (int*)cb_arena_alloc(&arena, 100 * sizeof(int));

CB_Arena_Mark mark = cb_arena_save(&arena);   // 记检查点
// ... 随便分配 ...
cb_arena_rewind(&arena, mark);                // 回到检查点，之后的分配全部作废

cb_arena_reset(&arena);   // 清空内容，保留内存（下次复用，不还给系统）
cb_arena_free(&arena);    // 真正还给系统
```

要点：

- 分块增长（首块 64KB，不够自动追加），**永不"满"**
- 分配是纯指针加法；`cb_arena_realloc` 无法原地扩容，会新分配 + 拷贝并返回新指针
- 对齐粒度是 `max_align_t`；需要更高对齐用 `cb_arena_alloc_aligned(&a, size, 64)`
- arena **不支持单独释放**某个对象——这是刻意取舍，换来的是极快的分配

完整示例：[`examples/11_memory.c`](../examples/11_memory.c)

### 4.2 Temp Storage：不用管内存的临时分配

temp 就是 arena 的一个**线程局部**实例，没有句柄、不用初始化：

```c
char* path = cb_temp_sprintf("%s/%s", dir, name);   // 直接用
char* copy = cb_temp_strdup(src);

CB_Arena_Mark mark = cb_temp_save();
while (有个循环反复要临时字符串) {
    char* scratch = cb_temp_sprintf("...");
    cb_temp_rewind(mark);   // 每轮回收，内存不随循环增长
}
cb_temp_reset();   // 复位整个 temp 栈（保留已分配块）
```

temp 是 `_Thread_local`、生命周期到进程结束，**没有释放接口**：不想要的内存用
`cb_temp_rewind` 回收，而不是 free。

**典型用法**：路径拼接、格式化消息、解析时的临时切片。
**不要**把 temp 指针存进长期存活的结构体里。

### 4.3 分配失败：`CB_OOM`

容器/arena/temp 的分配失败**无法优雅上报**（宏没有返回值可传），所以统一走死亡开关：

```
cb.h:861: OOM: 分配 65536 字节失败
```

它刻意**不用 `assert`**——`assert` 会被 `-DNDEBUG` 关掉，而构建工具通常正是 release 编译的，
关掉之后 OOM 会静默变成空指针崩溃，且崩溃点再也说不出原因。

能优雅失败的 API（读文件、起进程、cmd）仍然返回 `false`。

想自己接管（长驻程序里不要 `abort`）不用改 cb.h，`CB_OOM` 是 `#ifndef` 守卫的：

```c
static void my_oom(size_t size);      // 要先声明：cb.h 里的函数体会调用它
#define CB_OOM(size) my_oom(size)     // 必须在 #include "cb.h" 之前
#include "cb.h"
```

之后容器 / arena / temp 的分配失败都会走到 `my_oom(size)`。注意它**必须不返回**——
分配失败后调用方还会继续使用那个空指针。

### 4.4 内存追踪

```console
$ cc -DCB_ALLOC_TRACK -o app app.c && ./app
```

```c
cb_alloc_live_count();   // 当前存活块数
cb_alloc_live_size();    // 当前存活字节数
cb_alloc_report();       // 打印峰值 + 每处泄漏的 文件:行号
```

输出形如（`examples/11_memory.c` 故意漏掉一块 1234 字节）：

```
=== cb alloc report ===
live : 2 blocks, 66802 bytes
peak : 132370 bytes, 7 allocations total
  LEAK 1234 bytes from examples/11_memory.c:100
  LEAK 65568 bytes from <cb.h>（temp storage 的常驻块，见下）
```

注意：temp storage 是 `_Thread_local`、生命周期到进程结束，**故意不释放**——它会在报告里
显示成那块 64KB 的"泄漏"，这是设计而不是 bug（也没有接口能提前把它还给系统）。

也可以只替换分配出口而不开追踪：

```c
#define CB_REALLOC(ptr, size) my_realloc(ptr, size)
#define CB_FREE(ptr) my_free(ptr)
#include "cb.h"
```

全库只经这两个宏，替换它们就接管了所有内存行为。

---

## 5. 容器

### 5.1 动态数组

`cb_da_*` 系列宏对**任何具备 `items` / `count` / `capacity` 三个字段的结构体**都通用——
这是刻意的设计，你不需要为每种类型生成代码：

```c
typedef struct { int* items; size_t count; size_t capacity; } IntArray;
typedef struct { Command* items; size_t count; size_t capacity; } Commands;  // 也一样能用

IntArray xs = CB_ZERO;
cb_da_append(&xs, 42);
cb_da_append_many(&xs, raw, count);
cb_da_insert(&xs, 0, -1);
cb_da_remove_ordered(&xs, 2);   // 保序删除
cb_da_remove_unordered(&xs, 2); // 用末尾元素顶替，O(1)
cb_da_clear(&xs);               // 只清计数，保留内存
cb_da_free(xs);

printf("%d %d %zu\n", cb_da_first(&xs), cb_da_last(&xs), xs.count);
cb_da_foreach(int, it, &xs) printf("%d ", *it);
cb_da_foreach_rev(int, it, &xs) printf("%d ", *it);
```

`cb_da_append_many` 支持**自追加**（源就是自己），内部会处理 realloc 搬移与重叠。

### 5.2 位图 / 环形缓冲 / 哈希表 / 排序

```c
// 位图
CB_Bitset bs = CB_ZERO;
cb_bitset_resize(&bs, 1000);
cb_bitset_set(&bs, 42); cb_bitset_test(&bs, 42); cb_bitset_count(&bs, true);
cb_bitset_find(&bs, false);   // 第一个 0 位（没有则返回 (size_t)-1）
cb_bitset_free(&bs);

// 环形缓冲（字节流 FIFO，容量固定，写满拒绝）
CB_Ring ring = CB_ZERO;
cb_ring_init(&ring, 4096);
cb_ring_write(&ring, data, n);   // 返回实际写入字节数
cb_ring_read(&ring, buf, n);     // 返回实际读出字节数
cb_ring_space(&ring); ring.count;
cb_ring_free(&ring);

// 哈希表：字符串键（键会被复制，调用方不必管字符串生命周期）
CB_Map m = CB_ZERO;
cb_map_put_cstr(&m, "answer", (void*)(intptr_t)42);
void* v = NULL;
if (cb_map_get_cstr(&m, "answer", &v)) { /* v == (void*)42 */ }
cb_map_has(&m, CB_SVLIT("answer"));
cb_map_del(&m, CB_SVLIT("answer"));
cb_map_foreach(&m, it) { /* it.key (String_View) / it.value */ }
cb_map_free(&m);

// 整数键版本（键内存不需要单独分配，更省）
CB_Map_U64 counts = CB_ZERO;
cb_map_u64_put(&counts, 12345, ptr);
cb_map_u64_free(&counts);

// 排序：cb_da_sort 走 qsort（快但不稳定）；cb_da_sort_insertion 稳定
cb_da_sort(&xs, cmp);
cb_da_sort_insertion(&xs, cmp);
```

哈希表可以接到 arena 上：

```c
CB_Arena arena = CB_ZERO;
CB_Map m = CB_ZERO;
cb_map_init_arena(&m, &arena, 0);   // 之后所有内存从 arena 取
```

---

## 6. 字符串与 utf8

### 6.1 String_View：零拷贝切片

```c
CB_String_View sv = CB_SVLIT("  key = value  ");   // 编译期字面量，无 strlen
printf("|" CB_SV_FMT "|\n", CB_SV_ARG(sv));        // 打印用这两个宏

CB_String_View t = cb_sv_trim(sv);
CB_String_View key = cb_sv_chop_by_delim(&t, '=');  // 就地消耗
cb_sv_starts_with(sv, CB_SVLIT("  ke"));
cb_sv_eq(a, b); cb_sv_eq_ignore_case(a, b);
cb_sv_find(&sv, 'x');
cb_sv_to_temp_cstr(sv);   // 需要 NUL 结尾 C 字符串时
```

### 6.2 String_Builder：可增长缓冲

```c
CB_String_Builder sb = CB_ZERO;
cb_sb_append_cstr(&sb, "count=");
cb_sb_appendf(&sb, "%d", 42);
cb_sb_append(&sb, '\n');
cb_sb_append_sv(&sb, some_sv);
printf("%s", sb.items);              // items[count] 一定是 '\0'，可以直接当 C 字符串
CB_String_View sv = cb_sb_to_sv(sb); // 不想走 strlen 时用 view 访问
cb_sb_free(sb);                      // 释放并把句柄清零
```

### 6.3 数字解析（严格 + 溢出检测）

```c
int64_t i; uint64_t u; double d;
cb_sv_to_i64(CB_SVLIT("0x1F"), &i);        // 31，支持 0x/0b/0o 与下划线分隔
cb_sv_to_u64(CB_SVLIT("18446744073709551615"), &u);
cb_sv_to_f64(CB_SVLIT("3.5"), &d);
// 失败返回 false 且不动 *out：空串、尾随垃圾、溢出都会被拒绝
```

### 6.4 split / join

```c
CB_String_View csv = CB_SVLIT("a,b,,c");
CB_String_View part;
while (cb_sv_split_next(&csv, ',', &part)) { /* 依次拿到 a b "" c */ }

CB_String_Builder sb = CB_ZERO;
cb_sb_append_join(&sb, parts, count, CB_SVLIT(" | "));
```

### 6.5 utf8

```c
CB_String_View walk = text;
uint32_t cp;
while (cb_sv_utf8_next(&walk, &cp)) { /* 逐码点 */ }

uint32_t codepoint; size_t length;
cb_utf8_decode(data, size, &codepoint, &length);

char enc[4];
size_t n = cb_utf8_encode(0x4E2D, enc);   // 非法码点返回 0

size_t bad_offset;
cb_utf8_validate(text, &bad_offset);      // 检测 overlong / 代理区 / 截断
```

---

## 7. 日志、断言与错误处理

```c
cb_set_log_handler(cb_default_log_handler);   // 也可用 cb_cancer_log_handler（彩色）
cb_minimal_log_level = CB_INFO;               // 低于该级别的日志被丢弃

cb_log(CB_INFO, "普通日志：不带位置");
CB_LOG_AT(CB_ERROR, "自己的代码里想带位置时用它");   // 输出 [ERROR] mycode.c:42: ...
```

**为什么分两个**：`__FILE__`/`__LINE__` 展开在宏**被书写**的位置。库函数体里写的 `cb_log`
只会报出 cb.h 自己的行号，对定位你的问题没有帮助；而 `CB_LOG_AT` 写在你那一行，位置就精确指向你。

```c
CB_TODO("这条路还没实现");          // 打印位置后 abort
CB_UNREACHABLE("不可能到这里");     // 同上
CB_ASSERT(cond && "说明");          // 会被 -DNDEBUG 关掉，用于"逻辑上不可能"
```

panic 时 Linux/glibc 下会额外打印调用栈；链接时加 `-rdynamic` 才能看到函数名。
`#define CB_PANIC_BACKTRACE 0` 可关闭。

**错误处理约定**：能恢复的失败一律返回 `bool`（同时 `cb_log(CB_ERROR, ...)`），
不可恢复的分配失败走 `CB_OOM`。不引入错误码体系。

---

## 8. 计时

```c
CB_TIMER_START("整体");
CB_TIMER_START("某个阶段");
do_work();
double us = cb_timer_end_stat();   // 返回耗时并累加统计，也可用 CB_TIMER_END() 只取值
cb_timer_end_stat();
cb_timer_print_stats();            // 打印统计表（走 stderr）

cb_nanos_since_unspecified_epoch(); // 纳秒级单调时间戳，只应拿两次的差值
```

- 时间源：Windows 用 `QueryPerformanceCounter`，其余平台 `clock_gettime(CLOCK_MONOTONIC)`
- 全部逻辑在 C11 内联函数里（宏只是薄包装），能单步调试，MSVC 也能编译
- 输出一律走 **stderr**：计时是诊断信息，混进 stdout 会污染程序输出与回归测试
- 嵌套超过 `CB_TIMER_MAX_DEPTH`（默认 64）会明确报错，不再静默丢弃

---

## 9. 工具箱

```c
// 数学与位运算（每个参数只求值一次；返回类型 = 第一个参数的类型）
cb_min(a, b); cb_max(a, b); cb_clamp(x, lo, hi);
cb_align_up(v, 8); cb_align_down(v, 8);
cb_is_pow2(v); cb_next_pow2(v); cb_popcount64(v); cb_ctz64(v); cb_clz64(v);
cb_rotl64(v, n); cb_bswap64(v);

// 时间
cb_time_now(); cb_time_to_iso8601(ts); cb_duration_to_str(seconds);

// 随机数（xoshiro256**，同种子跨平台可复现；不适合密码学）
CB_Rng rng = CB_ZERO;
cb_rng_seed(&rng, 42);
cb_rng_next(&rng); cb_rng_double(&rng); cb_rng_range(&rng, 100);
cb_rng_shuffle(&rng, items, count, elem_size);

// 运行环境
cb_env_get("PATH");          // temp 上；不存在返回 NULL
cb_env_set("K", "V");
cb_stdout_is_tty(); cb_terminal_width(); cb_color_enabled();  // 尊重 NO_COLOR

// 调试
cb_dump_hex(data, size);     // 16 字节一行，打到 stderr

// CLI 解析
CB_Args args = CB_ZERO;
cb_args_parse(&args, argc, argv);
cb_args_has(&args, "verbose");
cb_args_get(&args, "out", "默认值");
cb_args_positional_count(&args); cb_args_positional(&args, 0);
cb_args_free(&args);
```

`cb_args_parse` 的规则：`--key=value` 是选项、`--key` 是开关、`-k=value` 也认、
`--` 之后全是位置参数、其余是位置参数。同名选项重复出现时 `cb_args_get` 取最后一次。

---

## 10. 测试与调试

```console
$ ./cb             # 不带参数 = 跑 test
$ ./cb test        # 跑正确性测试并与 golden 比对（含 clang++/g++ 双编译器 C++ 检查）
$ ./cb bench       # 跑性能基准（./cb bench containers 只跑一组）
$ ./cb record      # 重新录制 golden
$ ./cb examples    # 编译并运行全部示例
$ ./cb clean       # 删除 build/
$ ./cb list        # 列出测试
$ ./cb help        # 或 -h / --help
```

**三条约定**：

1. **信息默认全打，没有 `-v` 这种开关**。所有输出走 `cb_log(CB_INFO, ...)`，
   想少看就把 `cb_minimal_log_level` 调高——而不是让工具默认把信息藏起来。
2. **正确性与性能分开**。测试程序里没有任何计时插桩，`./cb test` 的输出是干净的；
   性能数据全部来自 `./cb bench`（见 [`../bench/README.md`](../bench/README.md)）。
3. **每个测试跑在自己的沙箱里**：`build/tests/<名字>.cwd`。测试进程的 cwd 就在里面，
   所以测试自己创建的文件不会污染仓库根目录。每次运行前清空重建，**成功后删除**（没东西可看），
   **失败时保留**并在输出里给出路径。

测试程序本身**只 `printf` 实际观察到的值**，不打印 `ok`/`FAILED`、也不自己判断通过与否——
**输出就是断言**，比对是构建脚本的事。所以新增测试时：写你要观察的值，
然后 `./cb record <名字>` 录下基线。golden 里不能出现时间戳、地址、目录列举顺序这类
不确定内容（项目自己的测试就踩过：计时表曾让 4 个测试每次必挂）。

跨平台差异用 `tests/<名字>.win32.stdout.txt` 覆盖：`tools/verify-windows.sh` 优先用它，
没有就和 Linux 用同一份 golden——后者是更强的断言，它证明该功能在两个平台上输出一致。

### 在 C++ 模式下跑测试

```console
$ tools/verify-cxx-tests.sh            # clang++/g++ × c++17/c++20 各构建一次 cb、各跑一遍全部测试
$ tools/verify-cxx-tests.sh --quick    # 只跑 g++ -std=c++17
```

`cb.c` 用 C++ 编译器构建时（CI 的 `linux` 任务就是这么做的：`$cc -x c++ -o cb cb.c`），
它给自己那套编译选项里带 `-x c++`，于是 `./cb test` 会把**全部 15 个测试当 C++ 编译**。
本地只跑 C 模式时这条路径看不见，它抓到的问题都是 C 下永远不报的：

| 问题 | 为什么 C 下不报 |
|---|---|
| 指定初始化器的 designator 顺序（`CB_SVLIT` 就踩过） | C 允许乱序；C++ 要求与结构体声明顺序一致，是硬错误 |
| `{0}` 初始化多成员结构体 | C 不告警；C++ 的 `-Wmissing-field-initializers` 会逐个成员报 |
| 取复合字面量的地址（`&(CB_Args)CB_ZERO`、`(char[4]){0}`） | C 下复合字面量是对象；C++ 下是右值/临时数组 |

所以每个 `tests/*.c` 都包含 `tests/test_diagnostics.h`——它按编译器分支压掉上面那两个
`-Wmissing-field-initializers` 系的告警，写法和 `cb.c` 顶部完全一致。CI 里对应 `cxx-tests` 任务。

### 在 Linux 上验证 Windows 分支

```console
$ pacman -S mingw-w64-gcc wine      # 一次性安装
$ tools/verify-windows.sh           # 交叉编译 + wine 实跑全部测试
```

这个脚本会用 mingw-w64 编译（C 与 C++ 两种模式），再用 wine 真正运行 15 个测试。
"只编译不运行"会漏掉很多东西——Windows 分支在真正被编译之前，
里面藏着 `shellapi.h` 顺序错误、调用了从未定义的函数、拼写错误等一堆问题。

脚本里设了 `WINEDLLOVERRIDES="mscoree,mshtml="` 来关掉 Wine 的
wine-mono / wine-gecko 安装提示。cb.h 的测试只依赖 `KERNEL32.dll` 与 UCRT，
不碰 .NET 或浏览器组件，所以关掉它们没有任何影响。

### 在检测器下跑测试（ASan / UBSan / LeakSanitizer）

```console
$ tools/verify-sanitizers.sh            # 全部测试
$ tools/verify-sanitizers.sh arena map  # 只跑指定的测试
$ tools/verify-sanitizers.sh --examples # 14 个示例也扫一遍
```

这是**独立于"测试全绿"的另一条证据链**：功能测试只证明"结果对不对"，
检测器才回答"有没有越界、有没有 UB、有没有泄漏"。

这个脚本不是摆设，第一次跑就抓到三个普通构建完全看不出来的真问题：

| 问题 | 为什么普通构建看不出来 | 现状 |
|---|---|---|
| `cb_da_append_many` 在 `n == 0` 时对 NULL 调 `memmove`，并做 NULL 指针算术 | 两者都是 UB，但 glibc 的 `memmove(dst, NULL, 0)` 恰好不会崩；`cb_cmd_extend(&空, &空)` 正走这条路 | 已修：`n == 0` 直接跳过 |
| `cb_read_entire_file` 没给结尾 `'\0'` 留位置（上游 nob.h 同样的毛病） | 越界的那一字节通常落在 malloc 的舍入余量里，读到的刚好是 0，于是"看起来能用" | 已修：按 `new_count + 1` 分配并写 `'\0'` |
| `cb_sv_eq` 对两个空视图调 `memcmp(NULL, NULL, 0)` | 空视图的 `data` 允许是 NULL（`CB_ZERO` 或 `cb_sv_from_parts(NULL, 0)`），而 `memcmp` 的参数按标准必须有效，哪怕长度为 0 | 已修：`count == 0` 直接返回 true |

三个都在 `tests/` 里留了回归点——`tests/string_builder.c` 会显式检查
`items[count] == '\0'` 与 `strlen(items) == count`，把它退回上游写法就会挂。
CI 里也有对应的 `sanitizers` 任务。

**关于 LeakSanitizer**：cb.h 的 temp storage 是 `_Thread_local`、生命周期到进程结束，
故意不释放——这是设计而不是泄漏，不要为它写 `free`。除此之外要求 0 泄漏。

写自己的 golden 测试时可以参考 `tests/` 下的写法：每个测试是一个独立程序，
在 `cb.c` 的 `test_names[]` 里登记，输出与 `tests/<名字>.stdout.txt` 比对。

**注意**：golden 里不要放耗时、地址、随机数这类不稳定内容——cb.h 自己的测试就踩过这个坑
（计时表曾让 4 个测试每次必挂）。诊断信息请走 stderr。

---

## 11. 编译期开关

下面 17 个开关都在 `#include "cb.h"` **之前**定义才生效（头文件里那份是 `#ifndef` 守卫的默认值，
定义晚了只是宏重定义）；只有标了**直接 `#define`** 的两个例外，它们要改头文件里那处定义本身。逐个开关
的可运行示例见 [`examples/14_config_switches.c`](../examples/14_config_switches.c)。

**① 行为开关（默认全关）**

| 宏 | 作用 | 默认值 |
|---|---|---|
| `CB_ENABLE_ECHO` | 文件系统 / 命令等操作打印提示信息（能看到实际执行的 `CMD:` 行） | 关 |
| `CB_DONT_DELETE_OLD_CB` | 自重建时保留上一次的旧二进制（`.old`）便于排查 | 关（旧的会被删掉） |
| `CB_TRACE_CMD_RUN_FAIL_LOCATION` | 命令失败时打印它的调用点（`文件:行号`） | 关 |
| `CB_ALLOC_TRACK` | 每次分配带追踪头，可 `cb_alloc_report()` 统计与报告泄漏（见 4.4） | 关 |
| `CB_STRIP_PREFIX` | 生成去 `cb_` / `CB_` 前缀的别名，写 `temp_sprintf` / `String_View` | 关 |
| `CB_SHARED_STATE` + `CB_SHARED_STATE_IMPL` | 多 TU 共享全局状态：所有 TU 定义前者、**恰好一个** TU 定义后者（见 0.③） | 关（每 TU 一份） |
| `CB_OOM(size)` | 顶掉默认的 OOM 处理器，必须不返回（见 4.3） | 打印后 `abort` |
| `CB_WARN_DEPRECATED` | 让 `CB_DEPRECATED` 真的展开成编译器的 deprecated 属性 | 关（标了也不报警告） |

**② 容量 / 参数微调（都是数值）**

| 宏 | 作用 | 默认值 |
|---|---|---|
| `CB_PATH_MAX` | 路径缓冲上限 | `PATH_MAX`；系统没定义时 4096 |
| `CB_TIMER_MAX_DEPTH` | 计时器嵌套深度上限（统计表本身按需增长，不是条目上限） | 64 |
| `CB_ARENA_REGION_INIT_CAPACITY` | arena / temp 首块容量（写满自动追加新块，不是总量上限） | 64KB |
| `CB_ARENA_ALIGN` | arena 分配对齐 | `alignof(max_align_t)` |
| `CB_THREAD_LOCAL` | 线程局部存储说明符，单线程程序定义成空可省掉 TLS 开销 | C 下 `_Thread_local`，C++ 下 `thread_local` |
| `CB_DA_INIT_CAP` | 动态数组首次扩容的容量 | 256 |
| `CB_BITSET_WORD_BITS` | 位图一个字多少位 | 64 |
| `CB_MAP_INIT_CAPACITY` | HashMap 初始桶数（到负载上限后按 2 倍增长） | 16 |
| `CB_WIN32_ERR_MSG_SIZE` | Windows 错误信息缓冲大小（非 Windows 无效果） | 4KB |

另外几个只影响单个模块的开关写在各自小节里，用法同样是"在 include 之前定义"：
`CB_REALLOC` / `CB_FREE`（内存分配收口，见 4.4）、`CB_TEMP_CAPACITY`（temp 首块容量，
默认跟 `CB_ARENA_REGION_INIT_CAPACITY` 走）、`CB_ASSERT`（断言实现，默认 `assert`）、
`CB_PANIC_BACKTRACE`（panic 时打印调用栈，默认 Linux/glibc 下开、其余平台关，见第 7 节）。

---

## 12. 设计取舍

完整的清单在 cb.h 头部的"设计取舍"一节，这里只列结论。

**设计取舍（不是 bug）**：

| 取舍 | 说明 |
|---|---|
| arena 不支持单独释放 | 只能整体 `cb_arena_reset` / `cb_arena_free`；要精确回收就用 realloc 后端 |
| 分配失败默认 abort | 见 4.3；能优雅失败的 API 仍返回 `false` |
| `C++` 下 `cb_return_defer` 有约定 | 作用域里要有名为 `result` 的变量 + `defer:` 标签；C++ 下所有声明要写在第一个 `goto` 之前 |
| `cb_da_*` 不做类型检查 | 换取"任意三字段结构体都能用"的通用性 |
| C 里 `cb_min` 类只覆盖 15 个标准算术类型 | 枚举/指针要显式转换；第二个及之后的参数会隐式转成第一个参数的类型（`cb_min(2, 1.9)` → 1；小数要写在第一个）；C++ 的模板版没有这些限制 |
| 丢弃 `cb_da_pop/first/last` 的返回值 | 写 `(void)cb_da_pop(&da);`，否则 gcc 16 会报 `-Wunused-value` |

**平台**：

| 平台 | 状态 |
|---|---|
| Linux (glibc) | 主要开发平台，clang / gcc 全绿 |
| Windows (mingw-w64 + wine) | 交叉编译 + wine 跑通全部测试；MSVC 未实测 |
| macOS / FreeBSD / Haiku | 保留了条件编译分支，未实测 |
