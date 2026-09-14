# 性能基准测试

测量 cb.h 各模块关键操作的开销。**与正确性测试分开**：`./cb test` 只跑正确性，
性能数据在这里。

```console
$ ./cb bench                 # 全部组
$ ./cb bench containers      # 只跑一组
$ ./cb bench fs -v           # 带补充说明
$ ./cb bench > bench.txt     # 结果走 stdout，可以直接重定向保存
```

可用的组：

| 组 | 内容 |
|---|---|
| `memory` | Arena 分配 / 快照回滚 / strdup / sprintf，temp storage |
| `containers` | 动态数组、排序、位图、环形缓冲、哈希表（字符串键与整数键） |
| `strings` | String_View 各种操作、String_Builder、数字解析 |
| `utf8` | 校验、按码点计长、逐码点遍历、编解码 |
| `paths` | join / normalize / replace_ext / is_absolute / glob 匹配 |
| `fs` | 文件存在性/大小、读写、原子写、mmap、列目录、遍历 |
| `cmd` | 命令拼接/渲染、进程启动 |
| `toolbox` | 哈希、随机数、时间、位运算、CLI 解析 |

## 怎么读这张表

每一项都是「**跑 N 次操作的总耗时**」，N 写在名字里：

```
Name                       Count      Avg(us)      Min(us)      Max(us)    Total(us)
da_append x1M                  1      557.237      557.237      557.237      557.237
```

`da_append x1M` = 100 万次 `cb_da_append` 总共花了 557µs，即单次约 **0.56 ns**。
换算方式就是 `Total(us) / N`。

所有项都只测一次（Count=1），所以 Min/Max 与 Avg 相同——目前不做多次采样取最小值。

## 这不是严谨的 benchmark

别把它当权威数据，它只用来**发现数量级上的异常**：

- **没有预热，没有隔离 CPU，没有统计显著性分析**。同一台机器上重复跑，数字会有 10%~30% 的波动
- **-O2 编译**（基准必须在优化构建下才有意义），但这也意味着编译器优化会干扰测量
- 时间源是 `clock_gettime(CLOCK_MONOTONIC)`（Windows 是 `QueryPerformanceCounter`），
  单次调用本身就有几十纳秒开销，所以极快的操作测出来的是「操作 + 计时开销」的下限
- **文件系统与进程相关的项**受页缓存和系统负载影响很大，重复运行会明显变快
- 运行环境不同结果差异很大（CPU、内存、wine vs 原生 Windows）

## 防优化

纯函数（如 `cb_sv_eq`）的返回值如果没人用，编译器会把整个循环删掉，
于是你测到的是**空循环**的时间——出现「每纳秒几百次」这种明显不合理的数字时，
基本就是这个原因。本基准用两条纪律避免：

1. 结果必须写进 `volatile` 全局 `g_sink`（宏 `BENCH_SINK`），编译器不敢删
2. 输入从 `volatile` 指针读（宏 `BENCH_IN`），每次迭代强制重载，循环不变量没法提到循环外

读表时还要留意**循环本身有没有真的干活**：容量和 chunk 选错时，循环里的写入会全部
变成被拒绝的空操作，测出来的数字漂亮但没有任何意义。

## 结论

- Windows 下 `cb_get_time_us()` 约 53ns/次，Linux 约 22ns/次
- `da_append` 预分配比边插边扩容快约 3 倍（`cb_da_reserve` 值得用）
- `sv_to_i64` 比 `atoi` 慢（要做严格校验与溢出检测），但比 `strtoll` 的完整用法快
