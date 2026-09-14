# 示例

14 个可直接编译运行的示例，覆盖 cb.h 的全部功能。

## 怎么跑

全部一次跑完（用项目自己的 flags 编译，逐个运行，输出默认全可见）：

```console
$ ./cb examples
```

单独跑：**要在仓库根目录下运行**，示例里的路径都相对于当前工作目录。

```console
$ cc -std=c11 -o ex01 examples/01_hello.c            && ./ex01
$ cc -std=c11 -o ex02 examples/02_single_project.c   && ./ex02
```

两个示例需要额外的编译参数（`./cb examples` 已经替你加上）：

```console
$ cc -DCB_ALLOC_TRACK -o ex11 examples/11_memory.c && ./ex11   # 打开追踪才会打印泄漏报告
$ cc -rdynamic         -o ex12 examples/12_logging.c && ./ex12 # 调用栈里才有函数名
```

`14_config_switches` 的开关写在文件里（`#define CB_STRIP_PREFIX` / `#define CB_OOM`
以及三个容量开关），直接编译即可，不用在命令行重复定义。它的行为见下面"编译期开关一览"一节。

---

## 构建类（cb.h 的主用途）

| 示例 | 对标上游 | 内容 |
|---|---|---|
| [`01_hello.c`](01_hello.c) | — | 最小可用：include + 日志。只有 4 行有效代码 |
| [`02_single_project.c`](02_single_project.c) | `001_basic_usage` | **单项目构建**：编译一个真实源文件再运行它，就是 `cb.c` 里 `build_and_run` 的骨架 |
| [`03_multi_project.c`](03_multi_project.c) | — | **多项目构建**：项目表驱动多个目标，源文件 / 产物名 / 运行参数都写在表里 |
| [`04_two_stage.c`](04_two_stage.c) | `010_nob_two_stage` | **两阶段构建**：先生成 `config.h`，再按配置构建并运行；改配置重跑即生效 |
| [`05_build_api.c`](05_build_api.c) | `005_parallel_build` | **构建 API 全量**：Cmd / FD / Pipe / Proc / Procs / Chain 的每个函数与选项 |

> 02/03 构建的是仓库里真实存在的源文件（`cb.c`、`examples/01_hello.c`），04 会先生成
> `config.h`；产物都在 `build/examples/<名字>/` 下。它们的 `main` 就是 `cb.c` 的骨架：
> 固定几行设置 + 遍历 `project_names`，不带命令行分发。

## 标准库类

| 示例 | 内容 |
|---|---|
| [`06_containers.c`](06_containers.c) | 动态数组、排序、位图、环形缓冲、哈希表（字符串键 / 整数键） |
| [`07_strings.c`](07_strings.c) | String_View 切片、String_Builder、大小写、严格数字解析、split/join |
| [`08_utf8.c`](08_utf8.c) | 起始字节长度表、按码点计长、解码、编码、逐码点遍历、合法性校验 |
| [`09_paths.c`](09_paths.c) | join / normalize / absolute / replace_ext / 取路径各部分 / glob 匹配与列目录 |
| [`10_filesystem.c`](10_filesystem.c) | 读写、原子写、`mkdir -p`、复制、删除、目录遍历（前序/后序/SKIP/STOP）、元信息、符号链接、mmap |
| [`11_memory.c`](11_memory.c) | Arena、Temp Storage、内存追踪与泄漏定位、替换分配出口、OOM |
| [`12_logging.c`](12_logging.c) | 日志级别、`CB_LOG_AT`、三种内置处理器、自定义处理器、panic 与调用栈 |
| [`13_toolbox.c`](13_toolbox.c) | 数学位运算、时间日期、随机数、运行环境、hex dump、CLI 参数解析 |

## 编译期开关一览

cb.h 一共 17 个编译期开关，全部列在下面。它们**必须定义在 `#include "cb.h"` 之前**
（`#define` 与命令行 `-D` 等价），写在后面一点作用都没有。每个示例里出现的位置见最后一列。
这份清单的代码版是 [`14_config_switches.c`](14_config_switches.c)：头部同样列着 17 个，
并实测其中 5 个。

### A. 行为开关（7 个，默认全关）

| 开关 | 作用 | 在哪个示例里能看到 |
|---|---|---|
| `CB_ENABLE_ECHO` | 文件系统 / 命令等操作打印提示信息，能看到实际执行的 `CMD:` 行 | `02`/`03`/`04`/`05`/`10` 打开 |
| `CB_DONT_DELETE_OLD_CB` | 保留上一次编译出的旧二进制（自重建时不删旧的） | `02`/`03`/`04` 注释着的候选 |
| `CB_TRACE_CMD_RUN_FAIL_LOCATION` | 命令失败时打印它的调用点（`文件:行号`） | `02`/`03`/`04` 注释着的候选 |
| `CB_ALLOC_TRACK` | 每次分配带追踪头，可统计并用 `cb_alloc_report()` 报告泄漏 | `11`（`-DCB_ALLOC_TRACK` 打开）、`11` 注释着的候选 |
| `CB_STRIP_PREFIX` | 生成去 `cb_` / `CB_` 前缀的别名（别名区由 `tools/gen-docs.py` 生成） | `14` 打开并实测 |
| `CB_SHARED_STATE[_IMPL]` | 多 TU 共享全局状态（日志级别 / temp 栈 / timer 统计 / 追踪计数）；每个 TU 定义前者，恰好一个再定义 `_IMPL` | `14` 只作说明——要跨 TU 才有效果，单文件演示不了，用法见 [`../docs/guide.md`](../docs/guide.md) 第 0 节 ③ |
| `CB_OOM(size)` | 顶掉默认的 OOM 处理器（默认打印 `文件:行号` 后 abort） | `14` 打开并在子进程里真触发一次 |

### B. 容量 / 参数微调（9 个，括号里是默认值）

| 开关 | 作用 | 在哪个示例里能看到 |
|---|---|---|
| `CB_PATH_MAX` | 路径缓冲上限（`PATH_MAX`，系统没有时 4096） | `14` 打印它的值 |
| `CB_TIMER_MAX_DEPTH` | 计时器嵌套深度上限（64，统计表本身按需增长） | `14` 清单里说明 |
| `CB_ARENA_REGION_INIT_CAPACITY` | arena / temp 首块容量（64KB，写满自动追加新块） | `11` 打开（改成 8KB）、`14` 实测（改成 1KB） |
| `CB_ARENA_ALIGN` | arena 分配对齐（`alignof(max_align_t)`） | `14` 清单里说明 |
| `CB_THREAD_LOCAL` | 线程局部存储说明符（`_Thread_local`，C++ 下 `thread_local`；单线程可定义成空） | `14` 清单里说明 |
| `CB_DA_INIT_CAP` | 动态数组首次扩容的容量（256） | `14` 实测（改成 4）、`11` 注释着的候选 |
| `CB_BITSET_WORD_BITS` | 位图一个字多少位（64，内部按 `uint64_t` 字存） | `14` 清单里说明（同上，直接 `#define`） |
| `CB_MAP_INIT_CAPACITY` | HashMap 初始桶数（16，到负载上限后按 2 倍增长） | `14` 实测（改成 4）、`11` 注释着的候选 |
| `CB_WIN32_ERR_MSG_SIZE` | Windows 错误信息缓冲大小（4KB） | `14` 清单里说明（只在 Windows 下生效） |

### C. 还有一个（1 个）

| 开关 | 作用 | 在哪个示例里能看到 |
|---|---|---|
| `CB_WARN_DEPRECATED` | 让 `CB_DEPRECATED` 真的展开成编译器的 deprecated 属性（默认关：标了也不报警告） | `14` 清单里说明 |

> 另有几个开关写在 cb.h 各自的小节里，不在上面的 17 个里：`CB_REALLOC` / `CB_FREE` /
> `CB_REALLOC_RAW` / `CB_FREE_RAW`（内存分配收口，"替换分配出口"一节）、`CB_TEMP_CAPACITY`
> （Arena / Temp Storage，默认跟随 `CB_ARENA_REGION_INIT_CAPACITY`）、`CB_ASSERT` /
> `CB_PANIC_BACKTRACE`（Logger / Panic）。
>
> 哪些接口在哪个示例/测试里被真正用到，见 [`../docs/api-coverage.md`](../docs/api-coverage.md)。

---

## 几个示例的特殊用法

**示例 02/03/04** 不带参数，跑起来就是"构建 + 运行"：

```console
$ cc -o ex02 examples/02_single_project.c && ./ex02   # 编译 cb.c 再运行它
$ cc -o ex04 examples/04_two_stage.c      && ./ex04   # 先生成 config.h，再按它构建
```

**示例 04** 的配置只在第一次运行时生成，之后可以编辑，改完重跑就能看到配置生效：

```console
$ ./ex04                                      # 生成 build/examples/twostage/config.h
$ vi build/examples/twostage/config.h         # 改 GREETING
$ ./ex04                                      # 按新配置重新编译并运行
```

**示例 11** 想看到内存泄漏报告要开追踪：

```console
$ cc -DCB_ALLOC_TRACK -o ex11 examples/11_memory.c && ./ex11
```

它会故意泄漏一块内存，报告里会精确指出泄漏所在的 `文件:行号`。
（`./cb examples` 跑的就是带 `-DCB_ALLOC_TRACK` 的版本。）

**示例 12** 可以演示真实 panic（会 abort，属正常）：

```console
$ cc -rdynamic -o ex12 examples/12_logging.c
$ ./ex12 panic     # 打印位置 + 调用栈后 abort
$ ./ex12 assert    # 演示 CB_ASSERT 失败
```

**示例 13** 可以传真实参数看 CLI 解析：

```console
$ ./ex13 --verbose --out=build/app -j file1.txt file2.txt
```

**示例 14** 是全部 17 个开关的清单，并实测其中 5 个：`CB_STRIP_PREFIX`、`CB_OOM` 和三个容量开关
（`CB_ARENA_REGION_INIT_CAPACITY` / `CB_DA_INIT_CAP` / `CB_MAP_INIT_CAPACITY`，都改成很小的值，
运行时会打印容量怎么长）。触发分配失败的那一段放在子进程里，好让 `./cb examples` 整体仍然以 0 结束：
自定义处理器打印一行并以 42 退出，父进程检查退出码后继续往下跑。

---

## 哪些函数在哪里演示

每个公共函数至少在一个示例或测试里出现过。完整对照表见
[`../docs/api-coverage.md`](../docs/api-coverage.md)。

分模块的详细说明（怎么用 + 为什么这样设计）见 [`../docs/guide.md`](../docs/guide.md)。
