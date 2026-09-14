// 示例 07：字符串 —— String_View 切片、String_Builder、大小写与忽略大小写比较、
//            严格数字解析、split / join
//
// 编译运行（在仓库根目录）：
//     cc -o /tmp/ex07 examples/07_strings.c && /tmp/ex07

#include "../cb.h"

static void title(const char* text)
{
    printf("\n== %s ==\n", text);
}

// ---------------------------------------------------------------- String_View
static void demo_string_view(void)
{
    title("String_View：不拥有内存的 (指针, 长度) 切片");

    // CB_SVLIT 在编译期构造，省掉一次 strlen
    CB_String_View sv = CB_SVLIT("  key = value  ");
    printf("原始: |" CB_SV_FMT "|  字节数 = %zu\n", CB_SV_ARG(sv), sv.count);

    // 打印必须用 CB_SV_FMT / CB_SV_ARG（它不是 NUL 结尾的 C 字符串）
    CB_String_View trimmed = cb_sv_trim(sv);                       // 两端去空白
    printf("trim: |" CB_SV_FMT "|\n", CB_SV_ARG(trimmed));
    printf("trim_left:  |" CB_SV_FMT "|\n", CB_SV_ARG(cb_sv_trim_left(sv)));
    printf("trim_right: |" CB_SV_FMT "|\n", CB_SV_ARG(cb_sv_trim_right(sv)));

    // chop 系列会就地消耗（修改传入的 sv），返回值是被切下来的部分
    CB_String_View rest = trimmed;
    CB_String_View left = cb_sv_chop_by_delim(&rest, '=');         // 切到 '=' 为止（不含）
    printf("chop_by_delim('='): 左=|" CB_SV_FMT "| 右=|" CB_SV_FMT "|\n",
           CB_SV_ARG(left), CB_SV_ARG(rest));

    // chop_by_delim_r 从右边找分隔符（取最后一个）
    rest = trimmed;
    CB_String_View right = cb_sv_chop_by_delim_r(&rest, '=');
    printf("chop_by_delim_r('='): 左=|" CB_SV_FMT "| 右=|" CB_SV_FMT "|\n",
           CB_SV_ARG(right), CB_SV_ARG(rest));

    CB_String_View abc = CB_SVLIT("abcdef");
    printf("chop_left(2)  = |" CB_SV_FMT "| 剩下 |" CB_SV_FMT "|\n",
           CB_SV_ARG(cb_sv_chop_left(&abc, 2)), CB_SV_ARG(abc));
    printf("chop_right(2) = |" CB_SV_FMT "| 剩下 |" CB_SV_FMT "|\n",
           CB_SV_ARG(cb_sv_chop_right(&abc, 2)), CB_SV_ARG(abc));

    // 按谓词切分（例如切掉开头的空白）
    CB_String_View spaced = CB_SVLIT("   indented");
    printf("chop_by_func(isspace) = |" CB_SV_FMT "| 剩下 |" CB_SV_FMT "|\n",
           CB_SV_ARG(cb_sv_chop_by_func(&spaced, isspace)), CB_SV_ARG(spaced));

    // 前缀 / 后缀
    CB_String_View path = CB_SVLIT("src/main.c");
    printf("starts_with(\"src/\") = %s\n", cb_sv_starts_with(path, CB_SVLIT("src/")) ? "真" : "假");
    printf("starts_with_cstr(\"lib/\") = %s\n", cb_sv_starts_with_cstr(path, "lib/") ? "真" : "假");
    printf("ends_with(\".c\") = %s\n", cb_sv_ends_with(path, CB_SVLIT(".c")) ? "真" : "假");
    printf("ends_with_cstr(\".h\") = %s\n", cb_sv_ends_with_cstr(path, ".h") ? "真" : "假");

    CB_String_View chopped = path;
    bool had = cb_sv_chop_prefix(&chopped, CB_SVLIT("src/"));      // 有前缀就切掉并返回 true
    printf("chop_prefix(\"src/\") = %s，剩下 |" CB_SV_FMT "|\n", had ? "真" : "假", CB_SV_ARG(chopped));
    had = cb_sv_chop_suffix(&chopped, CB_SVLIT(".c"));
    printf("chop_suffix(\".c\")  = %s，剩下 |" CB_SV_FMT "|\n", had ? "真" : "假", CB_SV_ARG(chopped));

    // 相等与查找
    printf("eq(abc, abc) = %s\n", cb_sv_eq(CB_SVLIT("abc"), CB_SVLIT("abc")) ? "真" : "假");
    CB_String_View hay = CB_SVLIT("hello world");
    printf("find('w') = %d\n", cb_sv_find(&hay, 'w'));
    printf("find_sv(\"lo\") = %d\n", cb_sv_find_sv(&hay, CB_SVLIT("lo"), 0));

    // 需要 NUL 结尾的 C 字符串时（结果在 temp 上）
    printf("to_temp_cstr = %s\n", cb_sv_to_temp_cstr(CB_SVLIT("not-nul-terminated")));

    // 从 C 字符串 / (指针, 长度) 构造
    CB_String_View from_cstr = cb_sv_from_cstr("from cstr");
    CB_String_View from_parts = cb_sv_from_parts("from parts", 4);
    printf("from_cstr = |" CB_SV_FMT "|  from_parts = |" CB_SV_FMT "|\n",
           CB_SV_ARG(from_cstr), CB_SV_ARG(from_parts));
}

// ---------------------------------------------------------------- String_Builder
static void demo_string_builder(void)
{
    title("String_Builder：可增长缓冲");

    CB_String_Builder sb = CB_ZERO;

    cb_sb_append_cstr(&sb, "count=");       // 追加 C 字符串
    cb_sb_appendf(&sb, "%d", 42);           // 格式化追加（返回写入字符数）
    cb_sb_append(&sb, ' ');                 // 追加单个字符
    cb_sb_append_sv(&sb, CB_SVLIT("done")); // 追加 String_View
    printf("构造结果: |" CB_SV_FMT "|  长度 = %zu\n", CB_SV_ARG(cb_sb_to_sv(sb)), sb.count);

    // 追加一块有长度的缓冲（可以是二进制）
    const char tail[] = {'|', 'e', 'n', 'd', '|'};
    cb_sb_append_buf(&sb, tail, sizeof(tail));
    printf("追加裸缓冲后: |" CB_SV_FMT "|\n", CB_SV_ARG(cb_sb_to_sv(sb)));

    // 对齐填充：补 0 到指定边界
    size_t before = sb.count;
    cb_sb_pad_align(&sb, 8);
    printf("pad_align(8): %zu -> %zu\n", before, sb.count);

    cb_sb_free(sb);                          // 释放并把句柄清零
    printf("free 之后 items = %s，count = %zu\n", sb.items == NULL ? "NULL" : "非空", sb.count);
}

// ---------------------------------------------------------------- 大小写
static void demo_case(void)
{
    title("大小写转换与忽略大小写比较");

    printf("to_temp_upper(\"Hello World\") = %s\n", cb_sv_to_temp_upper(CB_SVLIT("Hello World")));
    printf("to_temp_lower(\"Hello World\") = %s\n", cb_sv_to_temp_lower(CB_SVLIT("Hello World")));
    printf("eq(\"Hello\",\"hello\")                = %s\n",
           cb_sv_eq(CB_SVLIT("Hello"), CB_SVLIT("hello")) ? "真" : "假");
    printf("eq_ignore_case(\"Hello\",\"hello\")    = %s\n",
           cb_sv_eq_ignore_case(CB_SVLIT("Hello"), CB_SVLIT("hello")) ? "真" : "假");
    printf("eq_ignore_case(\"Hello\",\"hell\")     = %s（长度不同）\n",
           cb_sv_eq_ignore_case(CB_SVLIT("Hello"), CB_SVLIT("hell")) ? "真" : "假");
}

// ---------------------------------------------------------------- 数字解析
static void demo_numbers(void)
{
    title("严格数字解析：拒绝空串、垃圾尾巴、溢出");

    const char* texts[] = {"42", "-7", "+8", "0x1F", "0b1010", "0o17",
                           "1_000_000", "  12  ", "12abc", "", "-",
                           "9223372036854775807", "9223372036854775808"};
    printf("%-22s %-10s %s\n", "输入", "结果", "值");
    for (size_t i = 0; i < CB_ARRAY_LEN(texts); ++i) {
        int64_t v = 0;
        bool ok = cb_sv_to_i64(cb_sv_from_cstr(texts[i]), &v);
        printf("%-22s %-10s %s%lld\n", texts[i], ok ? "成功" : "拒绝", ok ? "" : "-", ok ? (long long)v : 0);
    }

    uint64_t u = 0;
    cb_sv_to_u64(CB_SVLIT("18446744073709551615"), &u);
    printf("\ncb_sv_to_u64(UINT64_MAX) = %llu\n", (unsigned long long)u);
    printf("cb_sv_to_u64(\"-1\") 被拒 = %s\n", cb_sv_to_u64(CB_SVLIT("-1"), &u) ? "否" : "是");

    int64_t n = 0;
    printf("cb_sv_to_i64(2147483647)  = %s\n", cb_sv_to_i64(CB_SVLIT("2147483647"), &n) ? "成功" : "失败");
    printf("cb_sv_to_i64(2147483648)  = %s\n", cb_sv_to_i64(CB_SVLIT("2147483648"), &n) ? "成功" : "失败");
    printf("  -> %lld（int 装不下就用 int64，库不再单独提供 int 版本）\n", (long long)n);

    double d = 0;
    printf("cb_sv_to_f64(3.5)   = %s\n", cb_sv_to_f64(CB_SVLIT("3.5"), &d) ? "成功" : "失败");
    printf("cb_sv_to_f64(1e3)   = %g\n", cb_sv_to_f64(CB_SVLIT("1e3"), &d) ? d : 0.0);
    printf("cb_sv_to_f64(1.5x)  = %s\n", cb_sv_to_f64(CB_SVLIT("1.5x"), &d) ? "成功" : "被拒（有垃圾尾巴）");
}

// ---------------------------------------------------------------- split / join
static void demo_split_join(void)
{
    title("split / join");

    CB_String_View csv = CB_SVLIT("alpha,beta,,gamma");
    CB_String_View parts[8];
    size_t count = 0;
    CB_String_View part = CB_ZERO;

    while (cb_sv_split_next(&csv, ',', &part) && count < CB_ARRAY_LEN(parts)) {
        parts[count++] = part;
    }
    printf("按 ',' 切出 %zu 段（注意中间的空段也保留了）:\n", count);
    for (size_t i = 0; i < count; ++i) {
        printf("  [%zu] |" CB_SV_FMT "|\n", i, CB_SV_ARG(parts[i]));
    }

    CB_String_Builder joined = CB_ZERO;
    cb_sb_append_join(&joined, parts, count, CB_SVLIT(" | "));
    printf("用 \" | \" 拼回: |" CB_SV_FMT "|\n", CB_SV_ARG(cb_sb_to_sv(joined)));
    cb_sb_free(joined);

    // 只切第一段，余下留在原视图里
    CB_String_View kv = CB_SVLIT("key=value=extra");
    CB_String_View first = cb_sv_chop_by_delim(&kv, '=');
    printf("\nchop_by_delim 只切第一段: key=|" CB_SV_FMT "| 余下=|" CB_SV_FMT "|\n",
           CB_SV_ARG(first), CB_SV_ARG(kv));
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    demo_string_view();
    demo_string_builder();
    demo_case();
    demo_numbers();
    demo_split_join();

    printf("\n全部字符串示例执行完毕\n");
    return 0;
}
