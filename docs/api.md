# cb.h API 速查表
> 由 `tools/gen-docs.py` 从 `cb.h` 的声明里自动提取（只列公共接口，内部 `cb__*` 已排除；同名只列一次）。重新生成：`tools/gen-docs.py`
>
> - 每个接口的详细语义写在 `cb.h` 里它的声明上方
> - 分模块的使用说明与可运行示例见 [guide.md](guide.md)
> - 每个接口在哪个示例/测试里被真正用到见 [api-coverage.md](api-coverage.md)
> - 与上游 nob.h 的差异见 [nob-comparison.md](nob-comparison.md)

共 **346** 个公共接口：函数 201 个、宏 105 个、类型 40 个。

### 版本
- `CB_VERSION_MAJOR`（宏）
- `CB_VERSION_MINOR`（宏）
- `CB_VERSION_PATCH`（宏）
- `CB_VERSION_STRING`（宏）

### Platform Header
- `CB_PATH_MAX`（宏）

### General
- `CB_ASSERT`（宏）
- `CB_PRINTF_FORMAT`（宏）
- `CB_SHARED_STATE`（宏）

### 内存分配收口
- `CB_REALLOC_RAW`（宏）
- `CB_FREE_RAW`（宏）
- `cb_alloc_report()`
- `cb_alloc_live_count()`
- `cb_alloc_live_size()`
- `CB_REALLOC`（宏）
- `CB_FREE`（宏）
- `CB_OOM`（宏）
- `cb_alloc_check`（宏）
- `CB_DECLTYPE_CAST`（宏）
- `cb_swap`（宏）
- `cb_shift`（宏）
- `CB_CLIT`（宏）

### Logger / Panic
- `CB_DEPRECATED`（宏）
- `CB_ZERO`（宏）
- `CB_ARRAY_LEN`（宏）
- `CB_ARRAY_GET`（宏）
- `CB_UNUSED`（宏）
- `cb_return_defer`（宏）
- `CB_INFO`（常量）
- `CB_WARN`（常量）
- `CB_ERROR`（常量）
- `CB_NO_LOGS`（常量）
- `CB_Log_Level`（类型）
- `cb_log()`
- `cb_log_at()`
- `CB_LOG_AT`（宏）
- `cb_set_log_handler()`
- `cb_get_log_handler()`
- `cb_default_log_handler()`
- `cb_cancer_log_handler()`
- `cb_null_log_handler()`
- `CB_PANIC_BACKTRACE`（宏）
- `CB_TODO`（宏）
- `CB_UNREACHABLE`（宏）

### Timer
- `CB_TIMER_MAX_DEPTH`（宏）
- `CB_Timer_Stat`（类型）
- `CB_Timer_Frame`（类型）
- `CB_Timer`（类型）
- `cb_get_time_ms()`
- `cb_get_time_us()`
- `cb_timer_begin()`
- `cb_timer_end()`
- `cb_timer_end_stat()`
- `cb_timer_end_print()`
- `cb_timer_get_stat()`
- `cb_timer_fprint_stats()`
- `cb_timer_print_stats()`
- `cb_timer_reset()`
- `CB_TIMER_START`（宏）
- `CB_TIMER_END`（宏）
- `cb_nanos_since_unspecified_epoch()`

### Arena / Temp Storage
- `CB_ARENA_REGION_INIT_CAPACITY`（宏）
- `CB_TEMP_CAPACITY`（宏）
- `CB_ARENA_ALIGN`（宏）
- `CB_THREAD_LOCAL`（宏）
- `CB_Arena_Region`（类型）
- `CB_Arena`（类型）
- `CB_Arena_Mark`（类型）
- `cb_arena_alloc()`
- `cb_arena_alloc_aligned()`
- `cb_arena_realloc()`
- `cb_arena_strdup()`
- `cb_arena_strndup()`
- `cb_arena_sprintf()`
- `cb_arena_vsprintf()`
- `cb_arena_save()`
- `cb_arena_rewind()`
- `cb_arena_reset()`
- `cb_arena_free()`
- `cb_temp_alloc()`
- `cb_temp_strdup()`
- `cb_temp_strndup()`
- `cb_temp_sprintf()`
- `cb_temp_vsprintf()`
- `cb_temp_reset()`
- `cb_temp_save()`
- `cb_temp_rewind()`

### Dynamic Array
- `CB_DArray`（类型）
- `CB_DA_INIT_CAP`（宏）
- `cb_da_free`（宏）
- `cb_da_reserve`（宏）
- `cb_da_append`（宏）
- `cb_da_append_many`（宏）
- `cb_da_resize`（宏）
- `cb_da_pop`（宏）
- `cb_da_first`（宏）
- `cb_da_last`（宏）
- `cb_da_remove_unordered`（宏）
- `cb_da_foreach`（宏）
- `cb_da_clear`（宏）
- `cb_da_insert`（宏）
- `cb_da_remove_ordered`（宏）
- `cb_da_foreach_rev`（宏）

### Bitset
- `CB_Bitset`（类型）
- `CB_BITSET_WORD_BITS`（宏）
- `cb_bitset_resize()`
- `cb_bitset_set()`
- `cb_bitset_unset()`
- `cb_bitset_toggle()`
- `cb_bitset_test()`
- `cb_bitset_clear_all()`
- `cb_bitset_count()`
- `cb_bitset_find()`
- `cb_bitset_free()`

### Ring Buffer
- `CB_Ring`（类型）
- `cb_ring_init()`
- `cb_ring_free()`
- `cb_ring_space()`
- `cb_ring_write()`
- `cb_ring_read()`
- `cb_ring_peek()`
- `cb_ring_clear()`

### Sort
- `CB_Compare_Func`（类型）
- `cb_da_sort`（宏）
- `cb_da_sort_insertion`（宏）

### StringBuilder
- `CB_String_Builder`（类型）
- `cb_read_entire_file()`
- `cb_sb_appendf()`
- `cb_sb_pad_align()`
- `cb_sb_append_buf`（宏）
- `cb_sb_append_sv`（宏）
- `cb_sb_append_cstr`（宏）
- `cb_sb_append`（宏）
- `cb_sb_free`（宏）

### StringView
- `CB_String_View`（类型）
- `CB_SVLIT`（宏）
- `CB_SVLIT_STATIC`（宏）
- `CB_SV_FMT`（宏）
- `CB_SV_ARG`（宏）
- `cb_sv_to_temp_cstr()`
- `cb_sv_eq()`
- `cb_sv_from_parts()`
- `cb_sv_from_cstr()`
- `cb_sb_to_sv`（宏）
- `cb_sv_chop_by_func()`
- `cb_sv_chop_by_delim()`
- `cb_sv_chop_by_delim_r()`
- `cb_sv_chop_left()`
- `cb_sv_chop_right()`
- `cb_sv_ends_with()`
- `cb_sv_ends_with_cstr()`
- `cb_sv_starts_with()`
- `cb_sv_starts_with_cstr()`
- `cb_sv_chop_prefix()`
- `cb_sv_chop_suffix()`
- `cb_sv_trim_left()`
- `cb_sv_trim_right()`
- `cb_sv_trim()`
- `cb_sv_find()`
- `cb_sv_find_sv()`

### UTF-8 Support
- `cb_sv_utf8_len()`

### StringView Tools（大小写 / 数字解析 / split-join）
- `cb_sv_to_temp_upper()`
- `cb_sv_to_temp_lower()`
- `cb_sv_eq_ignore_case()`
- `cb_sv_to_i64()`
- `cb_sv_to_u64()`
- `cb_sv_to_f64()`
- `cb_sv_split_next()`
- `cb_sb_append_join()`
- `cb_utf8_decode()`
- `cb_utf8_encode()`
- `cb_sv_utf8_next()`
- `cb_utf8_validate()`

### HashMap
- `CB_MAP_INIT_CAPACITY`（宏）
- `CB_MAP_EMPTY`（宏）
- `CB_MAP_USED`（宏）
- `CB_MAP_TOMBSTONE`（宏）
- `CB_Map_Slot`（类型）
- `CB_Map`（类型）
- `CB_Map_Iter`（类型）
- `cb_hash_bytes()`
- `cb_hash_u64()`
- `cb_map_init_capacity()`
- `cb_map_init_arena()`
- `cb_map_put()`
- `cb_map_put_cstr()`
- `cb_map_get()`
- `cb_map_get_cstr()`
- `cb_map_has()`
- `cb_map_del()`
- `cb_map_count()`
- `cb_map_clear()`
- `cb_map_free()`
- `cb_map_iter()`
- `cb_map_next()`
- `cb_map_foreach`（宏）

### HashMap：uint64 键版本
- `CB_Map_U64_Slot`（类型）
- `CB_Map_U64`（类型）
- `CB_Map_U64_Iter`（类型）
- `cb_map_u64_init_capacity()`
- `cb_map_u64_init_arena()`
- `cb_map_u64_put()`
- `cb_map_u64_get()`
- `cb_map_u64_has()`
- `cb_map_u64_del()`
- `cb_map_u64_count()`
- `cb_map_u64_clear()`
- `cb_map_u64_free()`
- `cb_map_u64_iter()`
- `cb_map_u64_next()`
- `cb_map_u64_foreach`（宏）

### File System
- `CB_WIN32_ERR_MSG_SIZE`（宏）
- `cb_win32_error_message()`
- `cb_path_name()`
- `cb_rename()`
- `cb_file_exists()`
- `cb_get_current_dir_temp()`
- `cb_set_current_dir()`
- `cb_temp_dir_name()`
- `cb_temp_file_name()`
- `cb_temp_file_ext()`
- `cb_temp_running_executable_path()`
- `CB_FILE_ERROR`（常量）
- `CB_FILE_REGULAR`（常量）
- `CB_FILE_DIRECTORY`（常量）
- `CB_FILE_SYMLINK`（常量）
- `CB_FILE_OTHER`（常量）
- `CB_File_Type`（类型）
- `CB_WALK_CONT`（常量）
- `CB_WALK_SKIP`（常量）
- `CB_WALK_STOP`（常量）
- `CB_Walk_Action`（类型）
- `CB_Walk_Entry`（类型）
- `CB_Walk_Func`（类型）
- `CB_Walk_Dir_Opt`（类型）
- `cb_delete_walk_entry()`
- `cb_walk_dir_opt()`
- `cb_walk_dir`（宏）
- `cb_dir_entry_open()`
- `cb_dir_entry_next()`
- `cb_dir_entry_close()`
- `CB_File_Paths`（类型）
- `cb_mkdir_if_not_exists()`
- `cb_copy_file()`
- `cb_copy_directory_recursively()`
- `cb_delete_directory_recursively()`
- `cb_read_entire_dir()`
- `cb_write_entire_file()`
- `cb_get_file_type()`
- `cb_delete_file()`

### 路径处理
- `cb_path_is_absolute()`
- `cb_path_join()`
- `cb_path_normalize()`
- `cb_path_absolute()`
- `cb_path_replace_ext()`
- `cb_path_is_sep()`
- `CB_PATH_SEP`（宏）

### File System 扩展：元信息 / 递归建目录 / 原子写 / 符号链接 / glob / mmap
- `CB_PROCESS_ID`（宏）
- `cb_file_size()`
- `cb_file_mtime()`
- `cb_write_entire_file_atomic()`
- `cb_create_symlink()`
- `cb_read_symlink()`
- `cb_glob_match()`
- `cb_glob()`
- `CB_Mmap`（类型）
- `cb_mmap_open()`
- `cb_mmap_close()`

### Math & Bits
- `cb_min`（宏）
- `cb_max`（宏）
- `cb_clamp`（宏）
- `cb_align_up`（宏）
- `cb_align_down`（宏）
- `cb_is_pow2()`
- `cb_next_pow2()`
- `cb_popcount64()`
- `cb_ctz64()`
- `cb_clz64()`
- `cb_rotl64()`
- `cb_rotr64()`
- `cb_is_little_endian()`
- `cb_bswap32()`
- `cb_bswap64()`

### Time & Date
- `cb_time_now()`
- `cb_time_to_iso8601()`
- `cb_duration_to_str()`

### Random
- `CB_Rng`（类型）
- `cb_rng_seed()`
- `cb_rng_next()`
- `cb_rng_double()`
- `cb_rng_range()`
- `cb_rng_shuffle()`

### Runtime Environment
- `cb_env_get()`
- `cb_env_set()`
- `cb_stdout_is_tty()`
- `cb_terminal_width()`
- `cb_color_enabled()`

### Hex Dump
- `cb_dump_hex()`

### CLI Args
- `CB_Arg_Entry`（类型）
- `CB_Arg_List`（类型）
- `CB_Args`（类型）
- `cb_args_parse()`
- `cb_args_has()`
- `cb_args_get()`
- `cb_args_get_first()`
- `cb_args_positional_count()`
- `cb_args_positional()`
- `cb_args_free()`

### Process & FD
- `CB_INVALID_PROC`（宏）
- `CB_INVALID_FD`（宏）
- `cb_fd_open_read()`
- `cb_fd_open_write()`
- `cb_fd_close()`
- `CB_Pipe`（类型）
- `cb_pipe_create()`
- `CB_Procs`（类型）
- `cb_proc_wait()`
- `cb_procs_wait()`
- `cb_procs_wait_and_reset()`

### Cmd
- `CB_Cmd`（类型）
- `CB_Cmd_Opt`（类型）
- `cb_cmd_append`（宏）
- `cb_cmd_extend`（宏）
- `cb_cmd_free`（宏）
- `cb_cmd_to_sb()`
- `cb_nprocs()`
- `cb_cmd_run_opt()`
- `cb_cmd_run`（宏）
- `CB_Pipes`（类型）

### Cmd Chain：把多条命令用管道串起来
- `CB_Chain`（类型）
- `CB_Chain_Begin_Opt`（类型）
- `CB_Chain_Cmd_Opt`（类型）
- `CB_Chain_End_Opt`（类型）
- `CB_Fd_List`（类型）
- `cb_chain_begin_opt()`
- `cb_chain_cmd_opt()`
- `cb_chain_end_opt()`
- `cb_chain_begin`（宏）
- `cb_chain_cmd`（宏）
- `cb_chain_end`（宏）

### C Builder
- `CB_SELF_REBUILD`（宏）
- `cb_needs_rebuild()`
- `CB_SELF_REBUILD_PLUS`（宏）
- `cb_cc_flags`（宏）
- `cb_cc`（宏）
- `cb_cc_output`（宏）
- `cb_cc_inputs`（宏）

