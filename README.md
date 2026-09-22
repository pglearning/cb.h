# cb.h

单文件、零依赖的 **C99** 库：既是**构建工具**（用 C 写构建脚本，不需要 Make / CMake / shell），
也是一套日常够用的工具箱（容器、字符串、UTF-8、文件系统、路径、进程与管道、命令行解析、日志、计时）。

原型与设计参考：[tsoding/nob.h](https://github.com/tsoding/nob.h)。

## 用法：stb 风格单头文件

**恰好一个 `.c`** 定义 `CB_IMPLEMENTATION` 之后再 include，其余 `.c` 只 include。
单文件程序可以额外 `#define CBDEF static inline`，让编译器丢掉没用到的函数。

```c
// build.c —— 就是那个"恰好一个 .c"
#define CB_IMPLEMENTATION
#define CB_ENABLE_ECHO                 // 打印实际执行的命令与文件操作（默认静默）
#include "cb.h"

int main(int argc, char** argv)
{
    CB_SELF_REBUILD(argc, argv, "cb.h");   // 改了 cb.h 或本文件就自动重建自己

    if (!cb_mkdir_if_not_exists("./bin")) return 1;

    CB_Cmd cmd = CB_ZERO;
    cb_cc(&cmd);                           // 编译器
    cb_cc_flags(&cmd);                     // 平台/语言对应的告警与标准旗标
    cb_cc_output(&cmd, "./bin/app");
    cb_cc_inputs(&cmd, "./src/main.c");
    return cb_cmd_run(&cmd, .dont_reset = false) ? 0 : 1;
}
```

```console
$ cc -std=c99 -D_POSIX_C_SOURCE=200112L -I. build.c -o ./build_script
$ ./build_script               # 产出 ./bin/app
```

严格 `-std=c99` 时需要 `-D_POSIX_C_SOURCE=200112L`（否则 `lstat` / `readlink` /
`clock_gettime` / `PATH_MAX` 不可见）；用编译器默认方言或 `-std=gnu99` 则不需要。
macOS 与 FreeBSD 不传这个宏，原因写在 cb.h 的 **Build Flags** 一节。

三个容易踩的点：

- 输出名不要叫 `build`：项目里通常同时有 `build/` 目录，`cc -o build` 会报
  `cannot open output file build: 是一个目录`。
- **链接库要排在源码之后**：`cb_cc_inputs()` 放 `.c`，`.so` / `.a` / `-lfoo` 用
  `cb_cmd_append()` 追加在它们**后面**。Ubuntu 的 gcc/clang 默认带 `--as-needed`，
  库若排在源码前面，链接器处理到它时还没有任何未定义符号，会直接把它丢掉，然后在源码处
  报 `undefined reference`；本地编译器默认不带 `--as-needed`，于是本地能过、CI 挂
  （`examples/01_multi_project.c` 就是这么踩的）。
- `cb_cmd_run(cmd, ...)` 与 `CB_SELF_REBUILD(argc, argv, ...)` 的 `...` 在严格 C99 下
  **至少要有一个实参**（C99 变参宏的硬性要求），所以要么写 `.dont_reset = false` 这类
  选项，要么像 `CB_SELF_REBUILD(argc, argv, "cb.h")` 那样至少列一个文件。

## 测试：`cb.c` 就是 cb.h 自己的测试器

与 nob.c 同一套流程：编译每个测试到 `build/tests/<名字>`，在独立沙箱目录里运行，
把 stdout 与 `tests/<名字>.stdout.txt` 逐字节比对。

```console
$ cc -I. cb.c -o cb        # 首次 clone 后没有 cb 二进制，先构建一次
$ ./cb test                # 之后改了 cb.c 或 cb.h，它会先自举重建再跑
$ ./cb test arena map      # 只跑指定测试
$ ./cb record arena        # 重录 golden（tests/arena.stdout.txt）
$ ./cb list                # 列出测试
$ ./cb help                # 列出命令
```

15 个测试覆盖 arena / temp、动态数组 / 位图 / 环形缓冲 / 哈希表、字符串与 UTF-8、
文件系统与路径、命令与管道（含 chain）、构建 API、CLI 参数等。
Windows 分支用 `tools/verify-windows.sh`（mingw-w64 交叉编译 + wine 实际运行）验证，
内存与未定义行为用 `tools/verify-sanitizers.sh`（ASan + UBSan + LSan，加 `--examples`
连示例一起查），C++ 模式（clang++ / g++ × c++17 / c++20）用 `tools/verify-cxx-tests.sh`，
示例本身用 `tools/verify-examples.sh`（11 个示例 × 两遍：默认链接、
以及给 `cc` 垫上 `-Wl,--as-needed` 复现 Ubuntu 的默认行为；`CC=` 与 `CB_LANG=c++` 可切编译器与语言）。

## 示例

`examples/` 下 11 个可独立编译运行的程序：多项目构建脚本、单目标构建、两阶段生成、
构建 API 全选项、容器、字符串、文件系统、内存、日志、工具箱、编译开关总览。
清单与构建命令见 [`examples/README.md`](examples/README.md)。

## 编译开关

全部开关（行为开关、容量微调、`CB_OOM(size)` 等）与默认值列在 cb.h 开头的 **NOTE** 里，
都在 `#include "cb.h"` 之前定义才生效，每个都有 `#ifndef` 守卫。

## 目录

| 路径 | 说明 |
| --- | --- |
| `cb.h` | 库本体：每一节都是「声明 + 紧跟自己的 `#ifdef CB_IMPLEMENTATION` 定义块」，别名区在文件末尾 |
| `cb.c` | cb.h 自己的测试器（nob.c 式：`test` / `record` / `list` / `help`） |
| `tests/` | 15 个测试与它们的 golden 输出（`*.win32.stdout.txt` 是平台差异覆盖） |
| `examples/` | 11 个示例程序 |
| `tools/` | 三条验证脚本（windows-cross / sanitizers / cxx-tests） |
| `_ref/nob.h` | 参照用的 nob.h 原型 |
| `_backup/` | 重写前的初版与备份 |

## 许可

见 [LICENSE.md](LICENSE.md)。
