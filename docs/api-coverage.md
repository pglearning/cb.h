# cb.h 公共接口覆盖对照表
> 由 `tools/gen-docs.py` 自动生成：对每个公共接口，在 `examples/`、`tests/`、`bench/` 与 `cb.c` 里搜索它的**实际使用**。
> 所以这张表是可验证的，不是声称。重新生成：`tools/gen-docs.py`

| 类别 | 数量 |
|---|---|
| 在示例/测试/基准/cb.c 里被实际使用 | **307** |
| 配置常量（编译期开关，只被库自己读） | **18** |
| 公共类型（作为其它接口的参数/字段） | **14** |
| 内部辅助（函数/宏，只被库自己调） | **7** |
| 没有任何使用者的公共接口 | **0** |
| 合计 | **346** |

---

## 被实际使用的接口

| 节 | 接口 | 在哪里被用到 |
|---|---|---|
| General | `CB_ASSERT` | `examples/03_multi_project.c`、`examples/12_logging.c` |
| 内存分配收口 | `CB_REALLOC_RAW` | `examples/11_memory.c` |
| 内存分配收口 | `CB_FREE_RAW` | `examples/11_memory.c` |
| 内存分配收口 | `cb_alloc_report` | `examples/11_memory.c` |
| 内存分配收口 | `cb_alloc_live_count` | `examples/11_memory.c` |
| 内存分配收口 | `cb_alloc_live_size` | `examples/11_memory.c` |
| 内存分配收口 | `CB_REALLOC` | `examples/11_memory.c` |
| 内存分配收口 | `CB_FREE` | `examples/11_memory.c` |
| 内存分配收口 | `CB_OOM` | `examples/11_memory.c`、`examples/12_logging.c` |
| 内存分配收口 | `cb_swap` | `examples/13_toolbox.c` |
| 内存分配收口 | `cb_shift` | `examples/02_single_project.c`、`examples/03_multi_project.c`、`examples/04_two_stage.c`、`examples/13_toolbox.c`、`cb.c` |
| Logger / Panic | `CB_ZERO` | `examples/02_single_project.c`、`examples/03_multi_project.c`、`examples/04_two_stage.c`、`examples/05_build_api.c`、`examples/06_containers.c`、`examples/07_strings.c`、`examples/08_utf8.c`、`examples/09_paths.c`、`examples/10_filesystem.c`、`examples/11_memory.c`、`examples/13_toolbox.c`、`tests/chain.c`、`tests/error_paths.c`、`tests/fs.c`、`tests/map.c`、`tests/nob_parity.c`、`tests/stdlib.c`、`tests/string_builder.c`、`tests/string_view.c`、`tests/toolbox.c`、`bench/bench.c` |
| Logger / Panic | `CB_ARRAY_LEN` | `examples/02_single_project.c`、`examples/03_multi_project.c`、`examples/04_two_stage.c`、`examples/05_build_api.c`、`examples/06_containers.c`、`examples/07_strings.c`、`examples/08_utf8.c`、`examples/09_paths.c`、`examples/13_toolbox.c`、`tests/dynamic_array.c`、`tests/nob_parity.c`、`tests/stdlib.c`、`tests/toolbox.c`、`cb.c` |
| Logger / Panic | `CB_ARRAY_GET` | `examples/13_toolbox.c` |
| Logger / Panic | `CB_UNUSED` | `examples/05_build_api.c`、`examples/13_toolbox.c`、`tests/map.c`、`tests/nob_parity.c`、`tests/stdlib.c`、`bench/bench.c` |
| Logger / Panic | `cb_return_defer` | `examples/02_single_project.c`、`examples/03_multi_project.c`、`examples/04_two_stage.c`、`cb.c` |
| Logger / Panic | `CB_INFO` | `examples/01_hello.c`、`examples/02_single_project.c`、`examples/03_multi_project.c`、`examples/04_two_stage.c`、`examples/12_logging.c`、`examples/13_toolbox.c`、`cb.c` |
| Logger / Panic | `CB_WARN` | `examples/01_hello.c`、`examples/04_two_stage.c`、`examples/12_logging.c` |
| Logger / Panic | `CB_ERROR` | `examples/01_hello.c`、`examples/05_build_api.c`、`examples/12_logging.c`、`tests/read_entire_dir.c`、`tests/strip_prefix.c`、`bench/bench.c`、`cb.c` |
| Logger / Panic | `CB_NO_LOGS` | `examples/05_build_api.c`、`examples/10_filesystem.c`、`examples/12_logging.c`、`tests/error_paths.c`、`tests/fs.c`、`tests/nob_parity.c` |
| Logger / Panic | `CB_Log_Level` | `examples/05_build_api.c`、`examples/10_filesystem.c`、`examples/12_logging.c`、`tests/fs.c`、`tests/nob_parity.c` |
| Logger / Panic | `cb_log` | `examples/01_hello.c`、`examples/02_single_project.c`、`examples/03_multi_project.c`、`examples/04_two_stage.c`、`examples/05_build_api.c`、`examples/12_logging.c`、`tests/read_entire_dir.c`、`cb.c` |
| Logger / Panic | `cb_log_at` | `examples/13_toolbox.c` |
| Logger / Panic | `CB_LOG_AT` | `examples/01_hello.c`、`examples/12_logging.c`、`examples/13_toolbox.c` |
| Logger / Panic | `cb_set_log_handler` | `examples/02_single_project.c`、`examples/03_multi_project.c`、`examples/04_two_stage.c`、`examples/05_build_api.c`、`examples/12_logging.c`、`examples/13_toolbox.c`、`bench/bench.c`、`cb.c` |
| Logger / Panic | `cb_get_log_handler` | `examples/12_logging.c` |
| Logger / Panic | `cb_default_log_handler` | `examples/02_single_project.c`、`examples/03_multi_project.c`、`examples/04_two_stage.c`、`examples/05_build_api.c`、`examples/12_logging.c`、`examples/13_toolbox.c`、`bench/bench.c`、`cb.c` |
| Logger / Panic | `cb_cancer_log_handler` | `examples/12_logging.c` |
| Logger / Panic | `cb_null_log_handler` | `examples/12_logging.c` |
| Logger / Panic | `CB_PANIC_BACKTRACE` | `examples/12_logging.c` |
| Logger / Panic | `CB_TODO` | `examples/12_logging.c` |
| Logger / Panic | `CB_UNREACHABLE` | `examples/12_logging.c` |
| Timer | `CB_Timer_Stat` | `examples/13_toolbox.c` |
| Timer | `cb_get_time_ms` | `examples/13_toolbox.c` |
| Timer | `cb_get_time_us` | `examples/13_toolbox.c`、`bench/bench.c` |
| Timer | `cb_timer_begin` | `examples/13_toolbox.c` |
| Timer | `cb_timer_end` | `examples/13_toolbox.c` |
| Timer | `cb_timer_end_stat` | `examples/13_toolbox.c`、`bench/bench.c` |
| Timer | `cb_timer_end_print` | `examples/13_toolbox.c` |
| Timer | `cb_timer_get_stat` | `examples/13_toolbox.c` |
| Timer | `cb_timer_fprint_stats` | `bench/bench.c` |
| Timer | `cb_timer_print_stats` | `examples/13_toolbox.c`、`bench/bench.c` |
| Timer | `cb_timer_reset` | `examples/13_toolbox.c` |
| Timer | `CB_TIMER_START` | `examples/13_toolbox.c`、`bench/bench.c` |
| Timer | `CB_TIMER_END` | `examples/13_toolbox.c` |
| Timer | `cb_nanos_since_unspecified_epoch` | `examples/13_toolbox.c`、`tests/nob_parity.c`、`bench/bench.c` |
| Arena / Temp Storage | `CB_TEMP_CAPACITY` | `examples/11_memory.c` |
| Arena / Temp Storage | `CB_ARENA_ALIGN` | `tests/arena.c` |
| Arena / Temp Storage | `CB_Arena` | `examples/06_containers.c`、`examples/11_memory.c`、`tests/arena.c`、`tests/map.c`、`bench/bench.c` |
| Arena / Temp Storage | `CB_Arena_Mark` | `examples/11_memory.c`、`tests/arena.c`、`tests/temp_storage.c`、`bench/bench.c`、`cb.c` |
| Arena / Temp Storage | `cb_arena_alloc` | `examples/11_memory.c`、`tests/arena.c`、`bench/bench.c` |
| Arena / Temp Storage | `cb_arena_alloc_aligned` | `examples/11_memory.c`、`tests/arena.c` |
| Arena / Temp Storage | `cb_arena_realloc` | `examples/11_memory.c` |
| Arena / Temp Storage | `cb_arena_strdup` | `examples/11_memory.c`、`bench/bench.c` |
| Arena / Temp Storage | `cb_arena_strndup` | `examples/11_memory.c`、`tests/arena.c` |
| Arena / Temp Storage | `cb_arena_sprintf` | `examples/11_memory.c`、`tests/arena.c`、`bench/bench.c` |
| Arena / Temp Storage | `cb_arena_vsprintf` | `examples/11_memory.c` |
| Arena / Temp Storage | `cb_arena_save` | `examples/11_memory.c`、`tests/arena.c`、`bench/bench.c` |
| Arena / Temp Storage | `cb_arena_rewind` | `examples/11_memory.c`、`tests/arena.c`、`bench/bench.c` |
| Arena / Temp Storage | `cb_arena_reset` | `examples/11_memory.c`、`tests/arena.c` |
| Arena / Temp Storage | `cb_arena_free` | `examples/06_containers.c`、`examples/11_memory.c`、`tests/arena.c`、`tests/map.c`、`bench/bench.c` |
| Arena / Temp Storage | `cb_temp_alloc` | `tests/map.c`、`tests/temp_storage.c`、`bench/bench.c` |
| Arena / Temp Storage | `cb_temp_strdup` | `examples/11_memory.c`、`tests/arena.c`、`tests/temp_storage.c` |
| Arena / Temp Storage | `cb_temp_strndup` | `examples/11_memory.c`、`tests/temp_storage.c` |
| Arena / Temp Storage | `cb_temp_sprintf` | `examples/02_single_project.c`、`examples/03_multi_project.c`、`examples/04_two_stage.c`、`examples/11_memory.c`、`tests/map.c`、`tests/temp_storage.c`、`bench/bench.c`、`cb.c` |
| Arena / Temp Storage | `cb_temp_vsprintf` | `examples/11_memory.c` |
| Arena / Temp Storage | `cb_temp_reset` | `examples/11_memory.c`、`tests/temp_storage.c` |
| Arena / Temp Storage | `cb_temp_save` | `examples/11_memory.c`、`tests/arena.c`、`tests/temp_storage.c`、`bench/bench.c`、`cb.c` |
| Arena / Temp Storage | `cb_temp_rewind` | `examples/11_memory.c`、`tests/arena.c`、`tests/temp_storage.c`、`bench/bench.c`、`cb.c` |
| Dynamic Array | `CB_DArray` | `tests/dynamic_array.c`、`tests/stdlib.c`、`tests/strip_prefix.c` |
| Dynamic Array | `cb_da_free` | `examples/02_single_project.c`、`examples/03_multi_project.c`、`examples/05_build_api.c`、`examples/06_containers.c`、`examples/09_paths.c`、`tests/chain.c`、`tests/dynamic_array.c`、`tests/fs.c`、`tests/nob_parity.c`、`tests/stdlib.c`、`bench/bench.c` |
| Dynamic Array | `cb_da_reserve` | `examples/06_containers.c`、`bench/bench.c` |
| Dynamic Array | `cb_da_append` | `examples/03_multi_project.c`、`examples/06_containers.c`、`tests/dynamic_array.c`、`tests/stdlib.c`、`bench/bench.c`、`cb.c` |
| Dynamic Array | `cb_da_append_many` | `examples/03_multi_project.c`、`examples/06_containers.c`、`tests/dynamic_array.c`、`tests/stdlib.c`、`bench/bench.c` |
| Dynamic Array | `cb_da_resize` | `examples/06_containers.c` |
| Dynamic Array | `cb_da_pop` | `examples/06_containers.c`、`tests/dynamic_array.c` |
| Dynamic Array | `cb_da_first` | `examples/06_containers.c`、`tests/dynamic_array.c` |
| Dynamic Array | `cb_da_last` | `examples/06_containers.c`、`tests/dynamic_array.c` |
| Dynamic Array | `cb_da_remove_unordered` | `examples/06_containers.c`、`tests/dynamic_array.c`、`bench/bench.c` |
| Dynamic Array | `cb_da_foreach` | `examples/06_containers.c`、`tests/dynamic_array.c`、`bench/bench.c`、`cb.c` |
| Dynamic Array | `cb_da_clear` | `examples/06_containers.c`、`tests/stdlib.c` |
| Dynamic Array | `cb_da_insert` | `examples/06_containers.c`、`tests/stdlib.c`、`bench/bench.c` |
| Dynamic Array | `cb_da_remove_ordered` | `examples/06_containers.c`、`tests/stdlib.c` |
| Dynamic Array | `cb_da_foreach_rev` | `examples/06_containers.c`、`tests/stdlib.c` |
| Bitset | `CB_Bitset` | `examples/06_containers.c`、`tests/stdlib.c`、`bench/bench.c` |
| Bitset | `cb_bitset_resize` | `examples/06_containers.c`、`tests/stdlib.c`、`bench/bench.c` |
| Bitset | `cb_bitset_set` | `examples/06_containers.c`、`tests/stdlib.c`、`bench/bench.c` |
| Bitset | `cb_bitset_unset` | `examples/06_containers.c`、`tests/stdlib.c`、`bench/bench.c` |
| Bitset | `cb_bitset_toggle` | `examples/06_containers.c`、`tests/stdlib.c` |
| Bitset | `cb_bitset_test` | `examples/06_containers.c`、`tests/stdlib.c`、`bench/bench.c` |
| Bitset | `cb_bitset_clear_all` | `examples/06_containers.c`、`tests/stdlib.c` |
| Bitset | `cb_bitset_count` | `examples/06_containers.c`、`tests/stdlib.c`、`bench/bench.c` |
| Bitset | `cb_bitset_find` | `examples/06_containers.c`、`tests/stdlib.c`、`bench/bench.c` |
| Bitset | `cb_bitset_free` | `examples/06_containers.c`、`tests/stdlib.c`、`bench/bench.c` |
| Ring Buffer | `CB_Ring` | `examples/06_containers.c`、`tests/stdlib.c`、`bench/bench.c` |
| Ring Buffer | `cb_ring_init` | `examples/06_containers.c`、`tests/stdlib.c`、`bench/bench.c` |
| Ring Buffer | `cb_ring_free` | `examples/06_containers.c`、`tests/stdlib.c`、`bench/bench.c` |
| Ring Buffer | `cb_ring_space` | `examples/06_containers.c`、`tests/stdlib.c` |
| Ring Buffer | `cb_ring_write` | `examples/06_containers.c`、`tests/stdlib.c`、`bench/bench.c` |
| Ring Buffer | `cb_ring_read` | `examples/06_containers.c`、`tests/stdlib.c`、`bench/bench.c` |
| Ring Buffer | `cb_ring_peek` | `examples/06_containers.c`、`tests/stdlib.c` |
| Ring Buffer | `cb_ring_clear` | `examples/06_containers.c`、`tests/stdlib.c` |
| Sort | `cb_da_sort` | `examples/06_containers.c`、`tests/stdlib.c`、`bench/bench.c` |
| Sort | `cb_da_sort_insertion` | `examples/06_containers.c`、`tests/stdlib.c`、`bench/bench.c` |
| StringBuilder | `CB_String_Builder` | `examples/02_single_project.c`、`examples/03_multi_project.c`、`examples/04_two_stage.c`、`examples/05_build_api.c`、`examples/07_strings.c`、`examples/10_filesystem.c`、`examples/11_memory.c`、`tests/chain.c`、`tests/error_paths.c`、`tests/fs.c`、`tests/stdlib.c`、`tests/string_builder.c`、`tests/string_view.c`、`bench/bench.c`、`cb.c` |
| StringBuilder | `cb_read_entire_file` | `examples/05_build_api.c`、`examples/10_filesystem.c`、`tests/chain.c`、`tests/error_paths.c`、`tests/fs.c`、`tests/string_builder.c`、`bench/bench.c`、`cb.c` |
| StringBuilder | `cb_sb_appendf` | `examples/07_strings.c`、`examples/11_memory.c`、`tests/string_builder.c`、`tests/string_view.c`、`bench/bench.c` |
| StringBuilder | `cb_sb_pad_align` | `examples/07_strings.c`、`tests/string_builder.c` |
| StringBuilder | `cb_sb_append_buf` | `examples/07_strings.c`、`tests/string_builder.c` |
| StringBuilder | `cb_sb_append_sv` | `examples/07_strings.c`、`tests/string_builder.c` |
| StringBuilder | `cb_sb_append_cstr` | `examples/02_single_project.c`、`examples/03_multi_project.c`、`examples/04_two_stage.c`、`examples/07_strings.c`、`tests/stdlib.c`、`tests/string_builder.c`、`bench/bench.c` |
| StringBuilder | `cb_sb_append` | `examples/07_strings.c`、`tests/string_builder.c` |
| StringBuilder | `cb_sb_free` | `examples/02_single_project.c`、`examples/03_multi_project.c`、`examples/04_two_stage.c`、`examples/05_build_api.c`、`examples/07_strings.c`、`examples/10_filesystem.c`、`examples/11_memory.c`、`tests/chain.c`、`tests/error_paths.c`、`tests/fs.c`、`tests/stdlib.c`、`tests/string_builder.c`、`tests/string_view.c`、`bench/bench.c` |
| StringView | `CB_String_View` | `examples/07_strings.c`、`examples/08_utf8.c`、`tests/bytes_for_utf8.c`、`tests/nob_parity.c`、`tests/stdlib.c`、`tests/string_view.c`、`tests/strip_prefix.c`、`bench/bench.c`、`cb.c` |
| StringView | `CB_SVLIT` | `examples/06_containers.c`、`examples/07_strings.c`、`examples/08_utf8.c`、`tests/error_paths.c`、`tests/nob_parity.c`、`tests/stdlib.c`、`tests/string_builder.c`、`tests/string_view.c`、`tests/strip_prefix.c`、`bench/bench.c` |
| StringView | `CB_SVLIT_STATIC` | `tests/nob_parity.c` |
| StringView | `CB_SV_FMT` | `examples/05_build_api.c`、`examples/07_strings.c`、`examples/10_filesystem.c`、`tests/bytes_for_utf8.c`、`tests/nob_parity.c`、`tests/stdlib.c`、`tests/string_builder.c`、`tests/string_view.c`、`cb.c` |
| StringView | `CB_SV_ARG` | `examples/05_build_api.c`、`examples/07_strings.c`、`examples/10_filesystem.c`、`tests/bytes_for_utf8.c`、`tests/nob_parity.c`、`tests/stdlib.c`、`tests/string_builder.c`、`tests/string_view.c`、`cb.c` |
| StringView | `cb_sv_to_temp_cstr` | `examples/07_strings.c`、`tests/stdlib.c` |
| StringView | `cb_sv_eq` | `examples/07_strings.c`、`tests/nob_parity.c`、`tests/string_view.c`、`tests/strip_prefix.c`、`bench/bench.c`、`cb.c` |
| StringView | `cb_sv_from_parts` | `examples/07_strings.c`、`examples/08_utf8.c`、`tests/error_paths.c`、`tests/map.c`、`tests/stdlib.c`、`tests/string_view.c` |
| StringView | `cb_sv_from_cstr` | `examples/07_strings.c`、`tests/bytes_for_utf8.c`、`tests/map.c`、`tests/nob_parity.c`、`tests/stdlib.c`、`tests/string_view.c`、`bench/bench.c` |
| StringView | `cb_sb_to_sv` | `examples/05_build_api.c`、`examples/07_strings.c`、`examples/10_filesystem.c`、`tests/string_builder.c`、`tests/string_view.c`、`bench/bench.c`、`cb.c` |
| StringView | `cb_sv_chop_by_func` | `examples/07_strings.c`、`tests/string_view.c` |
| StringView | `cb_sv_chop_by_delim` | `examples/07_strings.c`、`tests/string_view.c`、`bench/bench.c`、`cb.c` |
| StringView | `cb_sv_chop_by_delim_r` | `examples/07_strings.c`、`tests/stdlib.c`、`tests/string_view.c` |
| StringView | `cb_sv_chop_left` | `examples/07_strings.c`、`tests/bytes_for_utf8.c`、`tests/string_view.c` |
| StringView | `cb_sv_chop_right` | `examples/07_strings.c`、`tests/string_view.c` |
| StringView | `cb_sv_ends_with` | `examples/07_strings.c`、`tests/string_view.c` |
| StringView | `cb_sv_ends_with_cstr` | `examples/07_strings.c`、`tests/string_view.c` |
| StringView | `cb_sv_starts_with` | `examples/07_strings.c`、`tests/string_view.c` |
| StringView | `cb_sv_starts_with_cstr` | `examples/07_strings.c`、`tests/string_view.c` |
| StringView | `cb_sv_chop_prefix` | `examples/07_strings.c`、`tests/string_view.c` |
| StringView | `cb_sv_chop_suffix` | `examples/07_strings.c`、`tests/string_view.c` |
| StringView | `cb_sv_trim_left` | `examples/07_strings.c`、`tests/string_view.c` |
| StringView | `cb_sv_trim_right` | `examples/07_strings.c`、`tests/string_view.c` |
| StringView | `cb_sv_trim` | `examples/07_strings.c`、`tests/string_view.c`、`bench/bench.c` |
| StringView | `cb_sv_find` | `examples/07_strings.c`、`tests/string_view.c`、`bench/bench.c` |
| StringView | `cb_sv_find_sv` | `examples/07_strings.c`、`tests/string_view.c` |
| UTF-8 Support | `cb_sv_utf8_len` | `examples/08_utf8.c`、`bench/bench.c` |
| StringView Tools（大小写 / 数字解析 / split-join） | `cb_sv_to_temp_upper` | `examples/07_strings.c`、`tests/stdlib.c`、`bench/bench.c` |
| StringView Tools（大小写 / 数字解析 / split-join） | `cb_sv_to_temp_lower` | `examples/07_strings.c`、`tests/stdlib.c` |
| StringView Tools（大小写 / 数字解析 / split-join） | `cb_sv_eq_ignore_case` | `examples/07_strings.c`、`tests/stdlib.c`、`bench/bench.c` |
| StringView Tools（大小写 / 数字解析 / split-join） | `cb_sv_to_i64` | `examples/07_strings.c`、`tests/error_paths.c`、`tests/stdlib.c`、`bench/bench.c` |
| StringView Tools（大小写 / 数字解析 / split-join） | `cb_sv_to_u64` | `examples/07_strings.c`、`tests/error_paths.c`、`tests/stdlib.c` |
| StringView Tools（大小写 / 数字解析 / split-join） | `cb_sv_to_f64` | `examples/07_strings.c`、`tests/error_paths.c`、`tests/stdlib.c`、`bench/bench.c` |
| StringView Tools（大小写 / 数字解析 / split-join） | `cb_sv_split_next` | `examples/07_strings.c`、`tests/stdlib.c`、`tests/string_view.c` |
| StringView Tools（大小写 / 数字解析 / split-join） | `cb_sb_append_join` | `examples/07_strings.c`、`tests/stdlib.c` |
| StringView Tools（大小写 / 数字解析 / split-join） | `cb_utf8_decode` | `examples/08_utf8.c`、`tests/stdlib.c`、`bench/bench.c` |
| StringView Tools（大小写 / 数字解析 / split-join） | `cb_utf8_encode` | `examples/08_utf8.c`、`tests/error_paths.c`、`tests/stdlib.c`、`bench/bench.c` |
| StringView Tools（大小写 / 数字解析 / split-join） | `cb_sv_utf8_next` | `examples/08_utf8.c`、`tests/stdlib.c`、`bench/bench.c` |
| StringView Tools（大小写 / 数字解析 / split-join） | `cb_utf8_validate` | `examples/08_utf8.c`、`tests/error_paths.c`、`tests/stdlib.c`、`bench/bench.c` |
| HashMap | `CB_Map` | `examples/06_containers.c`、`tests/map.c`、`bench/bench.c` |
| HashMap | `CB_Map_Iter` | `examples/06_containers.c` |
| HashMap | `cb_hash_bytes` | `examples/06_containers.c`、`bench/bench.c` |
| HashMap | `cb_hash_u64` | `examples/06_containers.c`、`bench/bench.c` |
| HashMap | `cb_map_init_capacity` | `examples/06_containers.c`、`tests/map.c` |
| HashMap | `cb_map_init_arena` | `examples/06_containers.c`、`tests/map.c` |
| HashMap | `cb_map_put` | `tests/map.c` |
| HashMap | `cb_map_put_cstr` | `examples/06_containers.c`、`tests/map.c`、`bench/bench.c` |
| HashMap | `cb_map_get` | `tests/map.c` |
| HashMap | `cb_map_get_cstr` | `examples/06_containers.c`、`tests/map.c`、`bench/bench.c` |
| HashMap | `cb_map_has` | `examples/06_containers.c`、`tests/map.c` |
| HashMap | `cb_map_del` | `examples/06_containers.c`、`tests/map.c`、`bench/bench.c` |
| HashMap | `cb_map_count` | `examples/06_containers.c`、`tests/map.c` |
| HashMap | `cb_map_clear` | `examples/06_containers.c`、`tests/map.c` |
| HashMap | `cb_map_free` | `examples/06_containers.c`、`tests/map.c`、`bench/bench.c` |
| HashMap | `cb_map_iter` | `examples/06_containers.c` |
| HashMap | `cb_map_next` | `examples/06_containers.c` |
| HashMap | `cb_map_foreach` | `examples/06_containers.c`、`tests/map.c`、`bench/bench.c` |
| HashMap：uint64 键版本 | `CB_Map_U64` | `examples/06_containers.c`、`tests/map.c`、`bench/bench.c` |
| HashMap：uint64 键版本 | `CB_Map_U64_Iter` | `examples/06_containers.c` |
| HashMap：uint64 键版本 | `cb_map_u64_init_capacity` | `examples/06_containers.c` |
| HashMap：uint64 键版本 | `cb_map_u64_init_arena` | `examples/06_containers.c` |
| HashMap：uint64 键版本 | `cb_map_u64_put` | `examples/06_containers.c`、`tests/map.c`、`bench/bench.c` |
| HashMap：uint64 键版本 | `cb_map_u64_get` | `examples/06_containers.c`、`tests/map.c`、`bench/bench.c` |
| HashMap：uint64 键版本 | `cb_map_u64_has` | `tests/map.c` |
| HashMap：uint64 键版本 | `cb_map_u64_del` | `tests/map.c` |
| HashMap：uint64 键版本 | `cb_map_u64_count` | `examples/06_containers.c` |
| HashMap：uint64 键版本 | `cb_map_u64_clear` | `examples/06_containers.c` |
| HashMap：uint64 键版本 | `cb_map_u64_free` | `examples/06_containers.c`、`tests/map.c`、`bench/bench.c` |
| HashMap：uint64 键版本 | `cb_map_u64_iter` | `examples/06_containers.c` |
| HashMap：uint64 键版本 | `cb_map_u64_next` | `examples/06_containers.c` |
| HashMap：uint64 键版本 | `cb_map_u64_foreach` | `examples/06_containers.c`、`tests/map.c` |
| File System | `cb_win32_error_message` | `tests/error_paths.c` |
| File System | `cb_path_name` | `examples/09_paths.c` |
| File System | `cb_rename` | `examples/10_filesystem.c`、`tests/error_paths.c` |
| File System | `cb_file_exists` | `examples/02_single_project.c`、`examples/03_multi_project.c`、`examples/04_two_stage.c`、`examples/10_filesystem.c`、`tests/chain.c`、`tests/error_paths.c`、`bench/bench.c`、`cb.c` |
| File System | `cb_get_current_dir_temp` | `examples/09_paths.c`、`cb.c` |
| File System | `cb_set_current_dir` | `examples/03_multi_project.c`、`examples/09_paths.c`、`tests/error_paths.c`、`cb.c` |
| File System | `cb_temp_dir_name` | `examples/09_paths.c` |
| File System | `cb_temp_file_name` | `examples/09_paths.c` |
| File System | `cb_temp_file_ext` | `examples/09_paths.c` |
| File System | `cb_temp_running_executable_path` | `examples/10_filesystem.c` |
| File System | `CB_FILE_ERROR` | `examples/10_filesystem.c`、`tests/error_paths.c`、`tests/fs.c` |
| File System | `CB_FILE_REGULAR` | `examples/10_filesystem.c`、`tests/fs.c` |
| File System | `CB_FILE_DIRECTORY` | `examples/03_multi_project.c`、`examples/10_filesystem.c`、`tests/fs.c` |
| File System | `CB_FILE_SYMLINK` | `examples/10_filesystem.c`、`tests/fs.c` |
| File System | `CB_WALK_CONT` | `examples/10_filesystem.c` |
| File System | `CB_WALK_SKIP` | `examples/03_multi_project.c`、`examples/10_filesystem.c` |
| File System | `CB_WALK_STOP` | `examples/10_filesystem.c` |
| File System | `CB_Walk_Entry` | `examples/03_multi_project.c`、`examples/10_filesystem.c`、`bench/bench.c` |
| File System | `CB_Walk_Dir_Opt` | `examples/10_filesystem.c` |
| File System | `cb_delete_walk_entry` | `examples/10_filesystem.c` |
| File System | `cb_walk_dir_opt` | `examples/10_filesystem.c` |
| File System | `cb_walk_dir` | `examples/03_multi_project.c`、`examples/10_filesystem.c`、`bench/bench.c` |
| File System | `cb_dir_entry_open` | `examples/10_filesystem.c` |
| File System | `cb_dir_entry_next` | `examples/10_filesystem.c` |
| File System | `cb_dir_entry_close` | `examples/10_filesystem.c` |
| File System | `CB_File_Paths` | `examples/03_multi_project.c`、`examples/09_paths.c`、`tests/fs.c`、`tests/read_entire_dir.c`、`bench/bench.c` |
| File System | `cb_mkdir_if_not_exists` | `examples/02_single_project.c`、`examples/03_multi_project.c`、`examples/04_two_stage.c`、`examples/05_build_api.c`、`examples/10_filesystem.c`、`tests/error_paths.c`、`tests/fs.c`、`tests/read_entire_dir.c`、`bench/bench.c`、`cb.c` |
| File System | `cb_copy_file` | `examples/10_filesystem.c`、`tests/error_paths.c`、`cb.c` |
| File System | `cb_copy_directory_recursively` | `examples/10_filesystem.c` |
| File System | `cb_delete_directory_recursively` | `examples/02_single_project.c`、`examples/03_multi_project.c`、`examples/04_two_stage.c`、`examples/10_filesystem.c`、`tests/error_paths.c`、`bench/bench.c`、`cb.c` |
| File System | `cb_read_entire_dir` | `examples/10_filesystem.c`、`tests/error_paths.c`、`tests/fs.c`、`tests/read_entire_dir.c` |
| File System | `cb_write_entire_file` | `examples/02_single_project.c`、`examples/03_multi_project.c`、`examples/04_two_stage.c`、`examples/05_build_api.c`、`examples/10_filesystem.c`、`tests/chain.c`、`tests/error_paths.c`、`tests/fs.c`、`tests/nob_parity.c`、`tests/string_builder.c`、`bench/bench.c` |
| File System | `cb_get_file_type` | `examples/10_filesystem.c`、`tests/error_paths.c`、`tests/fs.c` |
| File System | `cb_delete_file` | `tests/error_paths.c`、`tests/read_entire_dir.c`、`tests/string_builder.c`、`cb.c` |
| 路径处理 | `cb_path_is_absolute` | `examples/09_paths.c`、`tests/stdlib.c`、`bench/bench.c` |
| 路径处理 | `cb_path_join` | `examples/03_multi_project.c`、`examples/09_paths.c`、`tests/stdlib.c`、`bench/bench.c` |
| 路径处理 | `cb_path_normalize` | `examples/03_multi_project.c`、`examples/09_paths.c`、`tests/stdlib.c`、`bench/bench.c` |
| 路径处理 | `cb_path_absolute` | `examples/09_paths.c`、`tests/stdlib.c`、`bench/bench.c` |
| 路径处理 | `cb_path_replace_ext` | `examples/09_paths.c`、`tests/stdlib.c`、`bench/bench.c` |
| 路径处理 | `cb_path_is_sep` | `examples/09_paths.c` |
| 路径处理 | `CB_PATH_SEP` | `examples/09_paths.c` |
| File System 扩展：元信息 / 递归建目录 / 原子写 / 符号链接 / glob / mmap | `cb_file_size` | `examples/05_build_api.c`、`examples/10_filesystem.c`、`tests/chain.c`、`tests/error_paths.c`、`tests/fs.c`、`bench/bench.c` |
| File System 扩展：元信息 / 递归建目录 / 原子写 / 符号链接 / glob / mmap | `cb_file_mtime` | `examples/10_filesystem.c`、`tests/error_paths.c`、`tests/fs.c` |
| File System 扩展：元信息 / 递归建目录 / 原子写 / 符号链接 / glob / mmap | `cb_write_entire_file_atomic` | `examples/10_filesystem.c`、`tests/error_paths.c`、`tests/fs.c`、`bench/bench.c` |
| File System 扩展：元信息 / 递归建目录 / 原子写 / 符号链接 / glob / mmap | `cb_create_symlink` | `examples/10_filesystem.c`、`tests/fs.c` |
| File System 扩展：元信息 / 递归建目录 / 原子写 / 符号链接 / glob / mmap | `cb_read_symlink` | `examples/10_filesystem.c`、`tests/error_paths.c`、`tests/fs.c` |
| File System 扩展：元信息 / 递归建目录 / 原子写 / 符号链接 / glob / mmap | `cb_glob_match` | `examples/09_paths.c`、`tests/fs.c`、`bench/bench.c` |
| File System 扩展：元信息 / 递归建目录 / 原子写 / 符号链接 / glob / mmap | `cb_glob` | `examples/09_paths.c`、`tests/error_paths.c`、`tests/fs.c`、`bench/bench.c` |
| File System 扩展：元信息 / 递归建目录 / 原子写 / 符号链接 / glob / mmap | `CB_Mmap` | `examples/10_filesystem.c`、`tests/error_paths.c`、`tests/fs.c`、`bench/bench.c` |
| File System 扩展：元信息 / 递归建目录 / 原子写 / 符号链接 / glob / mmap | `cb_mmap_open` | `examples/10_filesystem.c`、`tests/error_paths.c`、`tests/fs.c`、`bench/bench.c` |
| File System 扩展：元信息 / 递归建目录 / 原子写 / 符号链接 / glob / mmap | `cb_mmap_close` | `examples/10_filesystem.c`、`tests/error_paths.c`、`tests/fs.c`、`bench/bench.c` |
| Math & Bits | `cb_min` | `examples/13_toolbox.c`、`tests/strip_prefix.c`、`tests/toolbox.c` |
| Math & Bits | `cb_max` | `examples/13_toolbox.c`、`tests/toolbox.c` |
| Math & Bits | `cb_clamp` | `examples/13_toolbox.c`、`tests/toolbox.c` |
| Math & Bits | `cb_align_up` | `examples/13_toolbox.c`、`tests/toolbox.c` |
| Math & Bits | `cb_align_down` | `examples/13_toolbox.c`、`tests/toolbox.c` |
| Math & Bits | `cb_is_pow2` | `examples/13_toolbox.c`、`tests/toolbox.c` |
| Math & Bits | `cb_next_pow2` | `examples/13_toolbox.c`、`tests/toolbox.c` |
| Math & Bits | `cb_popcount64` | `examples/13_toolbox.c`、`tests/toolbox.c`、`bench/bench.c` |
| Math & Bits | `cb_ctz64` | `examples/13_toolbox.c`、`tests/toolbox.c`、`bench/bench.c` |
| Math & Bits | `cb_clz64` | `examples/13_toolbox.c`、`tests/toolbox.c`、`bench/bench.c` |
| Math & Bits | `cb_rotl64` | `examples/13_toolbox.c`、`tests/toolbox.c`、`bench/bench.c` |
| Math & Bits | `cb_rotr64` | `tests/toolbox.c` |
| Math & Bits | `cb_is_little_endian` | `examples/13_toolbox.c`、`tests/toolbox.c` |
| Math & Bits | `cb_bswap32` | `examples/13_toolbox.c`、`tests/toolbox.c` |
| Math & Bits | `cb_bswap64` | `tests/toolbox.c` |
| Time & Date | `cb_time_now` | `examples/13_toolbox.c`、`tests/toolbox.c` |
| Time & Date | `cb_time_to_iso8601` | `examples/13_toolbox.c`、`tests/toolbox.c`、`bench/bench.c` |
| Time & Date | `cb_duration_to_str` | `examples/13_toolbox.c`、`tests/toolbox.c`、`bench/bench.c` |
| Random | `CB_Rng` | `examples/13_toolbox.c`、`tests/toolbox.c`、`bench/bench.c` |
| Random | `cb_rng_seed` | `examples/13_toolbox.c`、`tests/toolbox.c`、`bench/bench.c` |
| Random | `cb_rng_next` | `examples/13_toolbox.c`、`tests/toolbox.c`、`bench/bench.c` |
| Random | `cb_rng_double` | `examples/13_toolbox.c`、`tests/toolbox.c` |
| Random | `cb_rng_range` | `examples/13_toolbox.c`、`tests/toolbox.c`、`bench/bench.c` |
| Random | `cb_rng_shuffle` | `examples/13_toolbox.c`、`tests/toolbox.c` |
| Runtime Environment | `cb_env_get` | `examples/13_toolbox.c`、`tests/toolbox.c` |
| Runtime Environment | `cb_env_set` | `examples/13_toolbox.c`、`tests/toolbox.c` |
| Runtime Environment | `cb_stdout_is_tty` | `examples/13_toolbox.c`、`tests/toolbox.c` |
| Runtime Environment | `cb_terminal_width` | `examples/13_toolbox.c`、`tests/toolbox.c` |
| Runtime Environment | `cb_color_enabled` | `examples/05_build_api.c`、`examples/13_toolbox.c`、`tests/toolbox.c` |
| Hex Dump | `cb_dump_hex` | `examples/13_toolbox.c`、`tests/toolbox.c` |
| CLI Args | `CB_Args` | `examples/13_toolbox.c`、`tests/error_paths.c`、`tests/toolbox.c`、`bench/bench.c` |
| CLI Args | `cb_args_parse` | `examples/13_toolbox.c`、`tests/toolbox.c`、`bench/bench.c` |
| CLI Args | `cb_args_has` | `examples/13_toolbox.c`、`tests/error_paths.c`、`tests/toolbox.c` |
| CLI Args | `cb_args_get` | `examples/13_toolbox.c`、`tests/toolbox.c` |
| CLI Args | `cb_args_get_first` | `tests/toolbox.c` |
| CLI Args | `cb_args_positional_count` | `examples/13_toolbox.c`、`tests/toolbox.c` |
| CLI Args | `cb_args_positional` | `examples/13_toolbox.c`、`tests/toolbox.c` |
| CLI Args | `cb_args_free` | `examples/13_toolbox.c`、`tests/toolbox.c`、`bench/bench.c` |
| Process & FD | `CB_INVALID_FD` | `examples/05_build_api.c` |
| Process & FD | `cb_fd_open_read` | `examples/05_build_api.c` |
| Process & FD | `cb_fd_open_write` | `examples/05_build_api.c` |
| Process & FD | `cb_fd_close` | `examples/05_build_api.c` |
| Process & FD | `CB_Pipe` | `examples/05_build_api.c` |
| Process & FD | `cb_pipe_create` | `examples/05_build_api.c` |
| Process & FD | `CB_Procs` | `examples/02_single_project.c`、`examples/03_multi_project.c`、`examples/05_build_api.c`、`tests/nob_parity.c` |
| Process & FD | `cb_proc_wait` | `examples/05_build_api.c` |
| Process & FD | `cb_procs_wait` | `examples/05_build_api.c` |
| Process & FD | `cb_procs_wait_and_reset` | `examples/02_single_project.c`、`examples/03_multi_project.c`、`examples/05_build_api.c`、`tests/nob_parity.c` |
| Cmd | `CB_Cmd` | `examples/02_single_project.c`、`examples/03_multi_project.c`、`examples/04_two_stage.c`、`examples/05_build_api.c`、`tests/chain.c`、`tests/nob_parity.c`、`bench/bench.c`、`cb.c` |
| Cmd | `cb_cmd_append` | `examples/02_single_project.c`、`examples/03_multi_project.c`、`examples/04_two_stage.c`、`examples/05_build_api.c`、`tests/chain.c`、`tests/nob_parity.c`、`bench/bench.c`、`cb.c` |
| Cmd | `cb_cmd_extend` | `examples/05_build_api.c`、`tests/nob_parity.c` |
| Cmd | `cb_cmd_free` | `examples/02_single_project.c`、`examples/03_multi_project.c`、`examples/04_two_stage.c`、`examples/05_build_api.c`、`tests/chain.c`、`tests/nob_parity.c`、`bench/bench.c` |
| Cmd | `cb_cmd_to_sb` | `examples/05_build_api.c`、`bench/bench.c` |
| Cmd | `cb_nprocs` | `examples/02_single_project.c`、`examples/03_multi_project.c`、`examples/05_build_api.c` |
| Cmd | `cb_cmd_run` | `examples/02_single_project.c`、`examples/03_multi_project.c`、`examples/04_two_stage.c`、`examples/05_build_api.c`、`tests/nob_parity.c`、`bench/bench.c`、`cb.c` |
| Cmd Chain：把多条命令用管道串起来 | `CB_Chain` | `examples/05_build_api.c`、`tests/chain.c` |
| Cmd Chain：把多条命令用管道串起来 | `CB_Chain_Begin_Opt` | `examples/05_build_api.c` |
| Cmd Chain：把多条命令用管道串起来 | `CB_Chain_Cmd_Opt` | `examples/05_build_api.c` |
| Cmd Chain：把多条命令用管道串起来 | `CB_Chain_End_Opt` | `examples/05_build_api.c` |
| Cmd Chain：把多条命令用管道串起来 | `cb_chain_begin_opt` | `examples/05_build_api.c` |
| Cmd Chain：把多条命令用管道串起来 | `cb_chain_cmd_opt` | `examples/05_build_api.c` |
| Cmd Chain：把多条命令用管道串起来 | `cb_chain_end_opt` | `examples/05_build_api.c` |
| Cmd Chain：把多条命令用管道串起来 | `cb_chain_begin` | `examples/05_build_api.c`、`tests/chain.c` |
| Cmd Chain：把多条命令用管道串起来 | `cb_chain_cmd` | `examples/05_build_api.c`、`tests/chain.c` |
| Cmd Chain：把多条命令用管道串起来 | `cb_chain_end` | `examples/05_build_api.c`、`tests/chain.c` |
| C Builder | `CB_SELF_REBUILD` | `examples/05_build_api.c` |
| C Builder | `cb_needs_rebuild` | `examples/02_single_project.c`、`examples/03_multi_project.c`、`examples/04_two_stage.c`、`examples/05_build_api.c`、`tests/nob_parity.c` |
| C Builder | `CB_SELF_REBUILD_PLUS` | `examples/02_single_project.c`、`examples/03_multi_project.c`、`examples/04_two_stage.c`、`examples/05_build_api.c`、`cb.c` |
| C Builder | `cb_cc_flags` | `examples/05_build_api.c`、`cb.c` |
| C Builder | `cb_cc` | `examples/05_build_api.c`、`cb.c` |
| C Builder | `cb_cc_output` | `examples/05_build_api.c`、`cb.c` |
| C Builder | `cb_cc_inputs` | `examples/05_build_api.c`、`cb.c` |

---

## 配置常量（18 个）

编译期开关或常量，只被 cb.h 自己读取，不需要外部调用：

- `CB_VERSION_MAJOR`（版本）
- `CB_VERSION_MINOR`（版本）
- `CB_VERSION_PATCH`（版本）
- `CB_VERSION_STRING`（版本）
- `CB_PATH_MAX`（Platform Header）
- `CB_SHARED_STATE`（General）
- `CB_TIMER_MAX_DEPTH`（Timer）
- `CB_ARENA_REGION_INIT_CAPACITY`（Arena / Temp Storage）
- `CB_THREAD_LOCAL`（Arena / Temp Storage）
- `CB_DA_INIT_CAP`（Dynamic Array）
- `CB_BITSET_WORD_BITS`（Bitset）
- `CB_MAP_INIT_CAPACITY`（HashMap）
- `CB_MAP_EMPTY`（HashMap）
- `CB_MAP_USED`（HashMap）
- `CB_MAP_TOMBSTONE`（HashMap）
- `CB_WIN32_ERR_MSG_SIZE`（File System）
- `CB_FILE_OTHER`（File System）
- `CB_INVALID_PROC`（Process & FD）

---

## 公共类型（14 个）

作为其它公共接口的参数或字段出现，不需要在示例里单独调用：

- `CB_Timer_Frame`（Timer）
- `CB_Timer`（Timer）
- `CB_Arena_Region`（Arena / Temp Storage）
- `CB_Compare_Func`（Sort）
- `CB_Map_Slot`（HashMap）
- `CB_Map_U64_Slot`（HashMap：uint64 键版本）
- `CB_File_Type`（File System）
- `CB_Walk_Action`（File System）
- `CB_Walk_Func`（File System）
- `CB_Arg_Entry`（CLI Args）
- `CB_Arg_List`（CLI Args）
- `CB_Cmd_Opt`（Cmd）
- `CB_Pipes`（Cmd）
- `CB_Fd_List`（Cmd Chain：把多条命令用管道串起来）

---

## 内部辅助（7 个）

技术上可见，但只有 cb.h 自己在用，不构成对外接口：

- `CB_PRINTF_FORMAT`（General）
- `cb_alloc_check`（内存分配收口）
- `CB_DECLTYPE_CAST`（内存分配收口）
- `CB_CLIT`（内存分配收口）
- `CB_DEPRECATED`（Logger / Panic）
- `CB_PROCESS_ID`（File System 扩展：元信息 / 递归建目录 / 原子写 / 符号链接 / glob / mmap）
- `cb_cmd_run_opt`（Cmd）

