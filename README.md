# cb.h

一个 header-only 的 C 库：既是**构建工具**（用 C 写构建脚本，不需要任何构建系统），
也是**个人标准库**（日常项目里反复要用的容器、字符串、文件系统、进程工具）。

单文件、零依赖、C11。`#include "cb.h"` 之后所有功能立即可用，**不需要 `#define CB_IMPLEMENTATION`**。

Original author: https://github.com/tsoding/nob.h

```console
$ cc -o cb cb.c && ./cb test
```

## 快速上手

当作构建脚本用：

```c
// build.c
#include "cb.h"

int main(int argc, char** argv)
{
    CB_SELF_REBUILD_PLUS(argc, argv, "cb.h");   // 改了这个脚本会自动重编自己

    CB_Cmd cmd = CB_ZERO;
    cb_cmd_append(&cmd, "cc", "-Wall", "-Wextra", "-o", "app", "main.c");
    if (!cb_cmd_run(&cmd)) return 1;
    return 0;
}
```

```console
$ cc -o build build.c
$ ./build
```

当作普通库用：直接 `#include "cb.h"` 调用任意接口即可。

## 能力概览

`cb.h` 顶部有一份按文件顺序排列的 30 节目录，常用几块：

- **Arena / Temp Storage** — 分块增长的作用域分配器；`cb_temp_*` 是它的线程局部实例
- **Dynamic Array / Bitset / Ring Buffer / Sort / HashMap** — 容器与算法
- **StringView / StringBuilder / StringView Tools / UTF-8 Support** — 字符串与 utf8
- **File System / 路径处理 / File System 扩展** — 目录遍历、原子写、符号链接、glob、mmap
- **Process & FD / Cmd / Cmd Chain** — 起进程、重定向、并发限流、管道串联
- **Math & Bits / Time & Date / Random / Runtime Environment / Hex Dump / CLI Args** — 日常小工具

编译期开关（`CB_STRIP_PREFIX`、`CB_SHARED_STATE`、`CB_OOM`、`CB_ALLOC_TRACK` 等）与
使用约束都写在 `cb.h` 头部，用之前扫一眼即可。

## 文档

| 文档 | 内容 |
|---|---|
| **[docs/guide.md](docs/guide.md)** | 分模块使用指南：每节讲"怎么用"，含大量可抄的代码片段 |
| **[docs/api.md](docs/api.md)** | API 速查表：346 个公共接口（201 函数 + 105 宏 + 40 类型），按分节列出 |
| **[docs/api-coverage.md](docs/api-coverage.md)** | 覆盖对照表：每个接口在哪个示例/测试里被真正用到（当前无使用者的接口数 = 0） |
| **[examples/](examples/)** | 13 个可直接编译运行的示例，覆盖全部功能 |
| **[bench/](bench/)** | 性能基准，覆盖 8 个模块 |

## 示例

```console
$ ./cb examples      # 编译并运行全部 13 个示例
```

**构建类**（cb.h 的主用途）：

| 示例 | 内容 |
|---|---|
| [`01_hello.c`](examples/01_hello.c) | 最小可用：include + 日志 |
| [`02_single_project.c`](examples/02_single_project.c) | **单项目构建**：自重建、并行编译、增量跳过、链接、运行 |
| [`03_multi_project.c`](examples/03_multi_project.c) | **多项目构建**：项目表 + 依赖顺序 + 静态库 + 遍历目录发现子项目 |
| [`04_two_stage.c`](examples/04_two_stage.c) | **两阶段构建**：先生成 `config.h` 再按配置构建，改配置即触发重编 |
| [`05_build_api.c`](examples/05_build_api.c) | **构建 API 全量**：Cmd / FD / Pipe / Proc / Procs / Chain 的每个函数与选项 |

**标准库类**：`06_containers`（容器与算法）、`07_strings`（字符串）、`08_utf8`、
`09_paths`（路径与 glob）、`10_filesystem`、`11_memory`、`12_logging`、`13_toolbox`。

构建类示例都是自包含的：会在 `build/examples/<名字>/` 下先造一个演示项目再构建它，
真实项目里把造项目那几步删掉即可。详见 [examples/README.md](examples/README.md)。

## 构建与测试

```console
$ cc -o cb cb.c
$ ./cb              # 不带参数 = 跑 test
$ ./cb test         # 正确性测试（先做 clang++/g++ 兼容性检查，再逐个与 golden 比对）
$ ./cb examples     # 编译并运行全部示例，输出默认可见
$ ./cb bench        # 性能基准（./cb bench containers 只跑一组）
$ ./cb docs         # 重新生成 docs/api.md 与 docs/api-coverage.md
$ ./cb docs --check # 校验文档是否最新、有没有"没人用的公共接口"
$ ./cb record       # 重新录制 golden
$ ./cb clean        # 删除 build/
$ ./cb list         # 列出测试
$ ./cb help         # 或 -h / --help
```

输出默认全打，没有 `-v` 这类开关：

```console
$ ./cb test
[INFO] ---- ./build/./tests/bytes_for_utf8 finished ----
[INFO] ---- ./build/./tests/read_entire_dir finished ----
...
[INFO] 15/15 test(s) passed
```

15 个测试全部是 **golden 输出比对**：测试只把实际观察到的值 `printf` 出来，
`./cb test` 拿它和 `tests/<名字>.stdout.txt` 比对。失败时给出最多 10 行差异、
两个 golden 的路径，以及保留下来的沙箱目录（成功时沙箱会删掉）。

`docs/api.md` 与 `docs/api-coverage.md` 由脚本从 `cb.h` 生成，不要手改。

## 平台支持

| 平台 | 状态 |
|---|---|
| Linux + clang / gcc | ✅ C（gnu11 / gnu17 / gnu23 / c11）× C++（c++17 / c++20 / c++23），0 错误 0 警告 |
| ASan + UBSan + LSan | ✅ 15 个测试与 13 个示例全部干净 |
| Windows（mingw-w64 + wine） | ✅ 交叉编译 0 警告，15 个测试全部通过 |
| macOS / FreeBSD / Haiku / MSVC | ⚠️ 保留了条件编译分支，未实测 |

```console
$ tools/verify-sanitizers.sh            # 全部测试在 ASan+UBSan+LSan 下跑（--examples 连示例一起扫）
$ tools/verify-windows.sh               # mingw-w64 交叉编译 + wine 实跑，逐字节比对 golden
$ tools/verify-cxx-tests.sh             # clang++/g++ × c++17/c++20 构建 cb 并跑全部测试（要求 0 告警）
```
