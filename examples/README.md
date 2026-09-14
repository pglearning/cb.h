# 示例

13 个可直接编译运行的示例，覆盖 cb.h 的全部功能。全部一起跑：

```console
$ ./cb examples
```

也可以单独跑（**注意要在仓库根目录下运行**，示例里的路径都相对于当前工作目录）：

```console
$ cc -o ex01 examples/01_hello.c            && ./ex01
$ cc -o ex02 examples/02_single_project.c   && ./ex02 build
```

---

## 构建类（cb.h 的主用途）

| 示例 | 对标上游 | 内容 |
|---|---|---|
| [`01_hello.c`](01_hello.c) | — | 最小可用：include + 日志。只有 4 行有效代码 |
| [`02_single_project.c`](02_single_project.c) | `001_basic_usage` | **单项目构建**：自重建、并行编译、增量跳过、链接、运行 |
| [`03_multi_project.c`](03_multi_project.c) | `015_walk_dirs` | **多项目构建**：项目表 + 依赖顺序 + 静态库 + `walk_dir` 发现子项目 |
| [`04_two_stage.c`](04_two_stage.c) | `010_nob_two_stage` | **两阶段构建**：先生成 `config.h`，再按配置构建；改配置会触发重编 |
| [`05_build_api.c`](05_build_api.c) | `005_parallel_build` | **构建 API 全量**：Cmd / FD / Pipe / Proc / Procs / Chain 的每个函数与选项 |

> 上面 02–05 这 4 个构建示例都是**自包含**的：它们会在 `build/examples/<名字>/` 下生成一个演示项目再构建它，
> 所以不需要额外的示例文件，在哪儿跑都行。真实项目里把生成那一步删掉即可。

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

---

## 几个示例的特殊用法

**示例 02/03/04** 接受 `build` 与 `clean`：

```console
$ cc -o ex02 examples/02_single_project.c
$ ./ex02 build     # 构建（第二次运行会走增量跳过）
$ ./ex02 clean     # 递归删除产物
```

**示例 04** 首次运行会生成配置文件，改它再跑就能看到配置生效：

```console
$ ./ex04                         # 生成 build/examples/twostage/build/config.h
$ vi build/examples/twostage/build/config.h   # 改 GREETING
$ ./ex04                         # 自动重新编译并生效
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

---

## 哪些函数在哪里演示

每个公共函数至少在一个示例或测试里出现过。完整对照表见
[`../docs/api-coverage.md`](../docs/api-coverage.md)。

分模块的详细说明（怎么用 + 为什么这样设计）见 [`../docs/guide.md`](../docs/guide.md)。
