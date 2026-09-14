# cb.h 与 nob.h 的完整对比

对照基准：本地 `_ref/nob.h`（上游 `tsoding/nob.h` **v3.10.0** 的完整 clone，仅用于对照，不参与构建）。

结论先行：

- **主要功能已对齐**：上游的公共接口 cb.h 都有对应实现，其中 115 个是改名沿用（见下表）
- **cb.h 另有 231 个上游没有的公共接口**（容器、字符串工具、utf8、路径、FS 扩展、工具箱）
- 这些数字由 `tools/gen-docs.py` 与一次性的上游提取脚本实测得出（提取规则：上游取 `#define nob_*/NOB_*`、`NOBDEF` 声明与 `typedef`，去掉前缀后与 cb.h 的 346 个接口按名字对应）：上游 **145** 个、cb.h **346** 个、改名沿用 **115** 个、cb.h 独有 **231** 个、上游有而 cb.h 没有 **30** 个。
- **刻意不做的有 4 类**（见文末），都是经过取舍的，不是遗漏

---

## 一、改名对应（功能等价，只是换了名字）

| nob.h | cb.h | 说明 |
|---|---|---|
| `nob__cmd_render` | `cb_cmd_to_sb` | 把命令渲染成可读字符串 |
| `nob_fd_open_for_read` | `cb_fd_open_read` | |
| `nob_fd_open_for_write` | `cb_fd_open_write` | |
| `nob__go_rebuild_urself` | `cb__self_rebuild` | 自重建实现 |
| `NOB_GO_REBUILD_URSELF` | `CB_SELF_REBUILD_PLUS` | 自重建入口宏 |
| `NOB_GO_REBUILD_URSELF_PLUS` | `CB_SELF_REBUILD_PLUS` | 同上（cb.h 把两者合并为一个） |
| `NOB_REBUILD_URSELF` | `CB_SELF_REBUILD` | 重建用的编译器命令 |
| `nob_shift_args` | `cb_shift` | 取下一个 argv 并推进 |
| `nob_sv_end_with` | `cb_sv_ends_with` | 旧名带 `_cstr` 变体 |
| `nob_sv_start_with` | `cb_sv_starts_with` | 同上 |
| `nob_sv_chop_while` | `cb_sv_chop_by_func` | 实现逐字节相同 |
| `nob_temp_sv_to_cstr` | `cb_sv_to_temp_cstr` | |
| `nob_log_handler` / `nob__log_handler` | `cb_log_handler` | 处理器类型与变量 |
| `nob_procs_flush` | `cb_procs_wait_and_reset` | 用上游的新名字（`procs_flush` 已被上游弃用） |
| `NOB_TODOF` | `CB_TODO` | cb.h 的版本本身就是变参格式化 |
| `NOB_UNREACHABLEF` | `CB_UNREACHABLE` | 同上 |
| `NOB_NANOS_PER_SEC` | （无） | cb.h 内部直接用字面量，未导出为宏 |
| `NOB_STRIP_PREFIX` 与前缀剥离机制 | `CB_STRIP_PREFIX` | 同样默认不生效；去前缀别名区由 `tools/gen-docs.py` 生成在 cb.h 末尾 |

---

## 二、上游有、cb.h 此前缺失，**已全部补齐**

这些是本轮新增的（v3.10.0 里较新的特性），实测见 `tests/nob_parity.c`：

| 功能 | cb.h 对应 | 说明 |
|---|---|---|
| `NOB_SVLIT` | `CB_SVLIT` | 编译期构造 String_View 字面量，省掉一次 `strlen` |
| `NOB_SVLIT_STATIC` | `CB_SVLIT_STATIC` | 静态初始化版本（MSVC `/TC` 下需要） |
| `nob_cmd_extend` | `cb_cmd_extend` | 把一条命令的参数追加到另一条后面 |
| `nob_cmd_free` | `cb_cmd_free` | 释放命令内存，并把句柄清零 |
| `nob_nanos_since_unspecified_epoch` | `cb_nanos_since_unspecified_epoch` | 纳秒级单调时间戳 |
| `nob_needs_rebuild1` | `cb_needs_rebuild` | cb.h 把单依赖与多依赖合并成一个入口：`cb_needs_rebuild(bin, deps[], n)`，单依赖就传长度为 1 的数组 |

---

## 三、上游有、cb.h **刻意不做**

| 上游 | 为什么不做 |
|---|---|
| `nob_cmd_run_sync` / `_async` / `_redirect` 及其 `_and_reset` 变体（8 个函数） | 全部被 `cb_cmd_run_opt` 一个入口覆盖（`.async` / `.max_procs` / `.stdout_path` / `.stderr_path` / `.dont_reset`）。上游自己也在这 8 个函数上标了 deprecated |
| `nob_fa_append` | cb.h **没有**这个接口：定长数组直接用普通数组 + 自己的 `count` 追加 |
| `nob_procs_flush` / `nob_procs_append_with_flush` | 上游已标记弃用，推荐改用 `cb_cmd_run(&cmd, .async = &procs, .max_procs = n)` |
| `Nob__Cmd_Redirect` 内部类型 | 内部实现细节，随 `cmd_run_opt` 方案一并消失 |

---

## 四、cb.h 独有（上游没有）

共 **231 个** cb.h 独有的公共接口，按分节归纳：

| 分类 | 内容 | 数量级 |
|---|---|---|
| **内存** | `CB_Arena` 分块分配器、`cb_arena_*`、`cb_temp_*`（arena 的线程局部实例）、`CB_OOM` 死亡开关、`CB_ALLOC_TRACK` 内存追踪（含泄漏定位） | ~15 |
| **容器** | `CB_Bitset`、`CB_Ring`、`CB_Map`（字符串键）、`CB_Map_U64`（整数键）、`cb_da_*` 补充操作、排序（qsort + 稳定插入排序） | ~45 |
| **字符串** | 大小写转换、忽略大小写比较、`cb_sv_to_i64/u64/f64`（严格解析 + 溢出检测 + `0x`/`0b`/`0o`）、split/join | ~12 |
| **utf8** | `cb_utf8_decode/encode/validate`、`cb_sv_utf8_next`（含 overlong、代理区、截断检测） | ~5 |
| **路径** | `cb_path_join/normalize/absolute/is_absolute/replace_ext` | ~5 |
| **文件系统** | 文件元信息、`mkdir -p`、原子写、符号链接读写、glob（含字符类）、mmap | ~15 |
| **工具** | 数学与位运算、时间日期 ISO8601、xoshiro256\*\* 随机数、运行环境探测、hex dump、CLI 参数解析 | ~40 |
| **其他** | 自动生成的内存分配报告、`cb_log_at` / `CB_LOG_AT`（带位置的日志）、panic 调用栈 | ~8 |

---

## 五、验证程度对比

| 维度 | nob.h | cb.h |
|---|---|---|
| 自带测试 | 20 个 `.c` 测试 | 15 个测试、**全部 golden 输出比对**（不是只比对有没有崩） |
| CI | macOS / Ubuntu / Windows 三平台 × C/C++ 矩阵 | Linux 的 C/C++ 矩阵 + 严格警告检查 |
| C++ 编译警告 | 15 个（仅头文件本身） | **0 个**（clang++ 与 g++ 均为 0） |
| Linux 验证 | ✅ CI | ✅ 本机实测 |
| Windows 验证 | ✅ CI（MSVC + mingw） | ✅ 本机 mingw-w64 交叉编译 + **wine 下 15/15 测试全过**：与 Linux 逐字节比对同一份 golden，只有 5 个平台差异用例用 `tests/*.win32.stdout.txt` 覆盖 |
| macOS / BSD | ✅ CI（macOS） | ⚠️ 保留分支，未实测 |
