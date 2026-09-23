#include "test_diagnostics.h"
#include "cb.h"

#ifdef _WIN32
#define TEST_SEP "\\"
#else
#define TEST_SEP "/"
#endif

static void test_da(void)
{
    printf("\n== Dynamic Array ==\n");

    CB_DArray a = CB_ZERO;
    cb_da_append(&a, "b");
    cb_da_append(&a, "d");
    cb_da_insert(&a, 1, "c");
    printf("insert(1,\"c\") -> count = %zu, items[1] = %s\n", a.count, a.items[1]);

    cb_da_insert(&a, 0, "a");
    printf("insert(0,\"a\") -> count = %zu, items[0] = %s\n", a.count, a.items[0]);

    cb_da_remove_ordered(&a, 1);
    printf("remove_ordered(1) -> count = %zu, items[1] = %s\n", a.count, a.items[1]);

    CB_String_Builder rev = CB_ZERO;
    cb_da_foreach_rev(const char*, it, &a)
    {
        cb_sb_append_cstr(&rev, *it);
    }
    cb_sb_append_null(&rev);
    printf("foreach_rev -> %s\n", rev.items);
    cb_sb_free(rev);

    cb_da_clear(&a);
    printf("clear -> count = %zu, items != NULL = %d\n", a.count, (int)(a.items != NULL));

    size_t visited = 0;
    cb_da_foreach_rev(const char*, it2, &a) { CB_UNUSED(it2); visited += 1; }
    printf("空数组 foreach_rev 访问次数 = %zu\n", visited);

    cb_da_free(a);
}

static void test_bitset(void)
{
    printf("\n== Bitset ==\n");

    CB_Bitset bs = CB_ZERO;
    cb_bitset_resize(&bs, 100);
    printf("resize(100) -> bits = %zu\n", bs.bits);

    cb_bitset_set(&bs, 3);
    cb_bitset_set(&bs, 64);
    cb_bitset_set(&bs, 99);
    printf("set 3/64/99 -> test(3) = %d, test(64) = %d, test(99) = %d\n",
           (int)cb_bitset_test(&bs, 3), (int)cb_bitset_test(&bs, 64), (int)cb_bitset_test(&bs, 99));
    printf("未置位 test(4) = %d\n", (int)cb_bitset_test(&bs, 4));

    cb_bitset_unset(&bs, 64);
    printf("unset(64) -> test(64) = %d\n", (int)cb_bitset_test(&bs, 64));

    cb_bitset_toggle(&bs, 4);
    printf("toggle(4) -> test(4) = %d\n", (int)cb_bitset_test(&bs, 4));
    cb_bitset_toggle(&bs, 4);
    printf("再 toggle(4) -> test(4) = %d\n", (int)cb_bitset_test(&bs, 4));

    printf("count(true) = %zu\n", cb_bitset_count(&bs, true));
    printf("find(false) = %zu\n", cb_bitset_find(&bs, false));
    printf("find(true) = %zu\n", cb_bitset_find(&bs, true));

    cb_bitset_clear_all(&bs);
    printf("clear_all -> count(true) = %zu\n", cb_bitset_count(&bs, true));

    cb_bitset_set(&bs, 5);
    cb_bitset_resize(&bs, 500);
    printf("resize(500) -> test(5) = %d, test(499) = %d\n",
           (int)cb_bitset_test(&bs, 5), (int)cb_bitset_test(&bs, 499));

    cb_bitset_free(&bs);
    printf("free -> words == NULL = %d, bits = %zu\n", (int)(bs.words == NULL), bs.bits);
}

static void test_ring(void)
{
    printf("\n== Ring Buffer ==\n");

    CB_Ring ring = CB_ZERO;
    printf("init(8) = %d\n", (int)cb_ring_init(&ring, 8));
    printf("初始 space = %zu\n", cb_ring_space(&ring));

    printf("write(\"abcdef\", 6) = %zu\n", cb_ring_write(&ring, "abcdef", 6));

    char buf[16] = CB_ZERO;
    printf("peek(3) = %zu, buf = %s\n", cb_ring_peek(&ring, buf, 3), buf);
    printf("peek 之后可读 = %zu\n", ring.count);

    printf("read(4) = %zu, buf = %s\n", cb_ring_read(&ring, buf, 4), buf);
    printf("读出后可读 = %zu\n", ring.count);

    printf("绕回 write(\"123456\", 6) = %zu\n", cb_ring_write(&ring, "123456", 6));
    printf("满时 write(\"X\", 1) = %zu\n", cb_ring_write(&ring, "X", 1));

    printf("read(8) = %zu\n", cb_ring_read(&ring, buf, 8));
    buf[8] = '\0';
    printf("跨绕回点数据 = %s\n", buf);

    printf("空时 read(4) = %zu\n", cb_ring_read(&ring, buf, 4));

    cb_ring_clear(&ring);
    printf("clear -> count = %zu, read_pos = %zu\n", ring.count, ring.read_pos);

    cb_ring_free(&ring);
    printf("free -> data == NULL = %d, capacity = %zu\n", (int)(ring.data == NULL), ring.capacity);
}

static int cmp_int(const void* a, const void* b)
{
    int x = *(const int*)a;
    int y = *(const int*)b;
    return (x > y) - (x < y);
}

typedef struct {
    int* items;
    size_t count;
    size_t capacity;
} IntArray;

typedef struct {
    int key;
    int order;
} Pair;

typedef struct {
    Pair* items;
    size_t count;
    size_t capacity;
} PairArray;

static int cmp_pair_key(const void* a, const void* b)
{
    int x = ((const Pair*)a)->key;
    int y = ((const Pair*)b)->key;
    return (x > y) - (x < y);
}

static void test_sort(void)
{
    printf("\n== Sort ==\n");

    CB_Compare_Func sorter = cmp_int; // 排序 API 的比较函数类型
    printf("CB_Compare_Func 变量可用于 da_sort = %d\n", (int)(sorter != NULL));

    int nums[] = {5, 3, 9, 1, 7, 3};
    IntArray ia = CB_ZERO;
    cb_da_append_many(&ia, nums, CB_ARRAY_LEN(nums));
    cb_da_sort(&ia, cmp_int);
    printf("da_sort 升序 =");
    for (size_t i = 0; i < ia.count; ++i) printf(" %d", ia.items[i]);
    printf("\n");

    Pair pairs[] = {{2, 0}, {1, 1}, {2, 2}, {1, 3}, {2, 4}};
    PairArray pd = CB_ZERO;
    cb_da_append_many(&pd, pairs, CB_ARRAY_LEN(pairs));
    cb_da_sort_insertion(&pd, cmp_pair_key);
    printf("da_sort_insertion 稳定 -> order =");
    for (size_t i = 0; i < pd.count; ++i) printf(" %d", pd.items[i].order);
    printf("\n");

    PairArray empty = CB_ZERO;
    cb_da_sort_insertion(&empty, cmp_pair_key);
    printf("空数组排序后 count = %zu\n", empty.count);

    cb_da_free(ia);
    cb_da_free(pd);
}

static void test_case(void)
{
    printf("\n== 大小写 / 忽略大小写比较 ==\n");

    printf("to_temp_upper(\"aBc1\") = %s\n", cb_sv_to_temp_upper(cb_sv_from_cstr("aBc1")));
    printf("to_temp_lower(\"aBc1\") = %s\n", cb_sv_to_temp_lower(cb_sv_from_cstr("aBc1")));
    printf("eq_ignore_case(\"Hello\", \"hELLo\") = %d\n",
           (int)cb_sv_eq_ignore_case(cb_sv_from_cstr("Hello"), cb_sv_from_cstr("hELLo")));
    printf("eq_ignore_case(\"Hello\", \"Hell\") = %d\n",
           (int)cb_sv_eq_ignore_case(cb_sv_from_cstr("Hello"), cb_sv_from_cstr("Hell")));
}

static void test_numbers(void)
{
    printf("\n== 数字解析 ==\n");

    int64_t i = 0;
    uint64_t u = 0;
    double d = 0;

    {

        int cb__ok = (int)cb_sv_to_i64(cb_sv_from_cstr("42"), &i);
        printf("to_i64(\"42\") = %d, i = %lld\n", cb__ok, (long long)i);
    }
    {

        int cb__ok = (int)cb_sv_to_i64(cb_sv_from_cstr("-7"), &i);
        printf("to_i64(\"-7\") = %d, i = %lld\n", cb__ok, (long long)i);
    }
    {

        int cb__ok = (int)cb_sv_to_i64(cb_sv_from_cstr("  12  "), &i);
        printf("to_i64(\"  12  \") = %d, i = %lld\n", cb__ok, (long long)i);
    }
    {

        int cb__ok = (int)cb_sv_to_i64(cb_sv_from_cstr("1_000_000"), &i);
        printf("to_i64(\"1_000_000\") = %d, i = %lld\n", cb__ok, (long long)i);
    }
    {

        int cb__ok = (int)cb_sv_to_i64(cb_sv_from_cstr("0x1F"), &i);
        printf("to_i64(\"0x1F\") = %d, i = %lld\n", cb__ok, (long long)i);
    }
    {

        int cb__ok = (int)cb_sv_to_i64(cb_sv_from_cstr("-0x10"), &i);
        printf("to_i64(\"-0x10\") = %d, i = %lld\n", cb__ok, (long long)i);
    }
    {

        int cb__ok = (int)cb_sv_to_i64(cb_sv_from_cstr("0b1010"), &i);
        printf("to_i64(\"0b1010\") = %d, i = %lld\n", cb__ok, (long long)i);
    }
    {

        int cb__ok = (int)cb_sv_to_i64(cb_sv_from_cstr("0o17"), &i);
        printf("to_i64(\"0o17\") = %d, i = %lld\n", cb__ok, (long long)i);
    }

    {
        int cb__ok = (int)cb_sv_to_i64(cb_sv_from_cstr("9223372036854775807"), &i);
        printf("to_i64(INT64_MAX 字面量) = %d, i == INT64_MAX = %d\n", cb__ok, (int)(i == INT64_MAX));
    }
    {
        int cb__ok = (int)cb_sv_to_i64(cb_sv_from_cstr("-9223372036854775808"), &i);
        printf("to_i64(INT64_MIN 字面量) = %d, i == INT64_MIN = %d\n", cb__ok, (int)(i == INT64_MIN));
    }

    printf("to_i64(INT64_MAX+1 溢出) = %d\n",
           (int)cb_sv_to_i64(cb_sv_from_cstr("9223372036854775808"), &i));
    printf("to_i64(\"12abc\" 尾随垃圾) = %d\n",
           (int)cb_sv_to_i64(cb_sv_from_cstr("12abc"), &i));
    printf("to_i64(\"\" 空串) = %d\n", (int)cb_sv_to_i64(cb_sv_from_cstr(""), &i));
    printf("to_i64(\"-\" 只有符号) = %d\n", (int)cb_sv_to_i64(cb_sv_from_cstr("-"), &i));
    printf("to_i64(\"1.5\" 小数点) = %d\n", (int)cb_sv_to_i64(cb_sv_from_cstr("1.5"), &i));

    {
        int cb__ok = (int)cb_sv_to_u64(cb_sv_from_cstr("18446744073709551615"), &u);
        printf("to_u64(UINT64_MAX 字面量) = %d, u == UINT64_MAX = %d\n", cb__ok, (int)(u == UINT64_MAX));
    }
    printf("to_u64(\"-1\" 负号) = %d\n", (int)cb_sv_to_u64(cb_sv_from_cstr("-1"), &u));

    int64_t n = 0;
    {

        int cb__ok = (int)cb_sv_to_i64(cb_sv_from_cstr("2147483647"), &n);
        printf("to_i64(INT_MAX 字面量) = %d, n = %lld\n", cb__ok, (long long)n);
    }
    {

        int cb__ok = (int)cb_sv_to_i64(cb_sv_from_cstr("2147483648"), &n);
        printf("to_i64(INT_MAX+1 字面量) = %d, n = %lld\n", cb__ok, (long long)n);
    }
    printf("to_i64(int64 上界之外) = %d\n",
           (int)cb_sv_to_i64(cb_sv_from_cstr("9223372036854775808"), &n));

    {

        int cb__ok = (int)cb_sv_to_f64(cb_sv_from_cstr("3.5"), &d);
        printf("to_f64(\"3.5\") = %d, d = %g\n", cb__ok, d);
    }
    {

        int cb__ok = (int)cb_sv_to_f64(cb_sv_from_cstr("1e3"), &d);
        printf("to_f64(\"1e3\") = %d, d = %g\n", cb__ok, d);
    }
    {

        int cb__ok = (int)cb_sv_to_f64(cb_sv_from_cstr("-0.25"), &d);
        printf("to_f64(\"-0.25\") = %d, d = %g\n", cb__ok, d);
    }
    printf("to_f64(\"abc\" 非数字) = %d\n", (int)cb_sv_to_f64(cb_sv_from_cstr("abc"), &d));
    printf("to_f64(\"1.5x\" 尾随垃圾) = %d\n", (int)cb_sv_to_f64(cb_sv_from_cstr("1.5x"), &d));
}

static void test_split_join(void)
{
    printf("\n== split / join ==\n");

    CB_String_View sv = cb_sv_from_cstr("a,bb,,ccc");
    CB_String_View parts[4];
    size_t count = 0;
    CB_String_View part = CB_ZERO;
    while (cb_sv_split_next(&sv, ',', &part) && count < 4) parts[count++] = part;

    printf("split 段数 = %zu\n", count);
    for (size_t i = 0; i < count; ++i) {
        printf("  parts[%zu] = |%s|\n", i, cb_sv_to_temp_cstr(parts[i]));
    }

    CB_String_Builder sb = CB_ZERO;
    cb_sb_append_join(&sb, parts, count, cb_sv_from_cstr("|"));
    cb_sb_append_null(&sb);
    printf("join(\"|\") = %s\n", sb.items);
    cb_sb_free(sb);
}

static void test_chop_by_delim_r(void)
{
    printf("\n== 回归：cb_sv_chop_by_delim_r ==\n");

    CB_String_View sv = CB_SVLIT("a=b=c");
    CB_String_View left = cb_sv_chop_by_delim_r(&sv, '=');
    printf("多个分隔符 -> left = |" CB_SV_FMT "|, 剩下 = |" CB_SV_FMT "|\n",
           CB_SV_ARG(left), CB_SV_ARG(sv));

    CB_String_View no_sep = CB_SVLIT("nosep");
    CB_String_View whole = cb_sv_chop_by_delim_r(&no_sep, '=');
    printf("无分隔符 -> whole = |" CB_SV_FMT "|, 原视图 count = %zu\n",
           CB_SV_ARG(whole), no_sep.count);

    CB_String_View lead = CB_SVLIT("=x");
    CB_String_View empty = cb_sv_chop_by_delim_r(&lead, '=');
    printf("分隔符在开头 -> empty count = %zu, 剩下 = |" CB_SV_FMT "|\n",
           empty.count, CB_SV_ARG(lead));

    char* buf = (char*)malloc(3);
    memcpy(buf, "abc", 3);
    CB_String_View tight = cb_sv_from_parts(buf, 3);
    CB_String_View got = cb_sv_chop_by_delim_r(&tight, '=');
    printf("视图贴到缓冲区末尾 -> got count = %zu, tight count = %zu\n", got.count, tight.count);
    free(buf);
}

static void test_utf8(void)
{
    printf("\n== utf8 ==\n");

    const char* zh = "\xE4\xB8\xAD";
    uint32_t cp = 0;
    size_t len = 0;

    {
        int ok = (int)cb_utf8_decode(zh, 3, &cp, &len);
        printf("decode(\"中\") = %d, cp = U+%04X, len = %zu\n", ok, cp, len);
    }
    {
        int ok = (int)cb_utf8_decode("A", 1, &cp, &len);
        printf("decode(\"A\") = %d, cp = 0x%02X, len = %zu\n", ok, cp, len);
    }

    char enc[4] = CB_ZERO;
    {
        size_t n = cb_utf8_encode(0x4E2D, enc);
        printf("encode(U+4E2D) = %zu, 往返一致 = %d\n", n, (int)(memcmp(enc, zh, 3) == 0));
    }
    printf("encode(U+1F600 emoji) = %zu\n", cb_utf8_encode(0x1F600, enc));
    printf("encode(U+110000 超范围) = %zu\n", cb_utf8_encode(0x110000, enc));
    printf("encode(U+D800 代理区) = %zu\n", cb_utf8_encode(0xD800, enc));

    {
        CB_String_View sv = cb_sv_from_cstr("a中b");
        for (int i = 1; i <= 3; ++i) {
            int ok = (int)cb_sv_utf8_next(&sv, &cp);
            printf("utf8_next 第 %d 个码点: %d, cp = 0x%X\n", i, ok, cp);
        }
        int more = (int)cb_sv_utf8_next(&sv, &cp);
        printf("迭代到尾部: %d, 剩余字节 = %zu\n", more, sv.count);
    }

    printf("validate(\"a中b\") = %d\n", (int)cb_utf8_validate(cb_sv_from_cstr("a中b"), NULL));
    {
        size_t bad = 0;
        int ok = (int)cb_utf8_validate(cb_sv_from_cstr("a\xFF" "b"), &bad);
        printf("validate(\"a\\xFFb\") = %d, bad 下标 = %zu\n", ok, bad);
    }
    printf("validate(截断的 3 字节序列) = %d\n",
           (int)cb_utf8_validate(cb_sv_from_parts("\xE4\xB8", 2), NULL));
    printf("validate(overlong \"\\xC0\\x80\") = %d\n",
           (int)cb_utf8_validate(cb_sv_from_parts("\xC0\x80", 2), NULL));
}

static void test_path(void)
{
    printf("\n== 路径处理 ==\n");

    printf("path_is_absolute(\"/x/y\") = %d\n", (int)cb_path_is_absolute("/x/y"));
    printf("path_is_absolute(\"x/y\") = %d\n", (int)cb_path_is_absolute("x/y"));

    printf("path_join(\"a\", \"b\") = %s\n", cb_path_join("a", "b"));
    printf("path_join(\"a/\", \"b\") = %s\n", cb_path_join("a/", "b"));
    printf("path_join(\"/\", \"b\") = %s\n", cb_path_join("/", "b"));
    printf("path_join(\"a\", \"/b\") = %s\n", cb_path_join("a", "/b"));
    printf("path_join(\"\", \"b\") = %s\n", cb_path_join("", "b"));

    printf("path_normalize(\"/a/b/../c\") = %s\n", cb_path_normalize("/a/b/../c"));
    printf("path_normalize(\"a/./b//c\") = %s\n", cb_path_normalize("a/./b//c"));
    printf("path_normalize(\"../a\") = %s\n", cb_path_normalize("../a"));
    printf("path_normalize(\"/..\") = %s\n", cb_path_normalize("/.."));
    printf("path_normalize(\"a/b/../../c\") = %s\n", cb_path_normalize("a/b/../../c"));
    printf("path_normalize(\"\") = %s\n", cb_path_normalize(""));
    printf("path_normalize(\"a/\") = %s\n", cb_path_normalize("a/"));

    printf("path_replace_ext(\"a/b.c\", \"h\") = %s\n", cb_path_replace_ext("a/b.c", "h"));
    printf("path_replace_ext(\"a/b.c\", \".h\") = %s\n", cb_path_replace_ext("a/b.c", ".h"));
    printf("path_replace_ext(\"a/b\", \"h\") = %s\n", cb_path_replace_ext("a/b", "h"));
    printf("path_replace_ext(\"a/b.c\", NULL) = %s\n", cb_path_replace_ext("a/b.c", NULL));
    printf("path_replace_ext(\"a.b/c\", \"h\") = %s\n", cb_path_replace_ext("a.b/c", "h"));

    const char* abs = cb_path_absolute("x");
    printf("path_absolute(\"x\") 是绝对路径 = %d\n", (int)cb_path_is_absolute(abs));

    printf("path_absolute(\"/a/../b\") = %s\n", cb_path_absolute("/a/../b"));
}

// 路径名/临时名工具。cb_get_current_dir_temp 与 cb_temp_running_executable_path 返回的是
// 机器相关的绝对路径，所以只打印与机器无关的结论（是否绝对路径、basename）。
static void test_path_extras(void)
{
    printf("\n== 路径名与临时名工具 ==\n");

    printf("CB_PATH_SEP = %c，cb_path_is_sep(CB_PATH_SEP) = %d\n", CB_PATH_SEP,
           (int)cb_path_is_sep(CB_PATH_SEP));
    printf("cb_path_is_sep('/') = %d，cb_path_is_sep('\\\\') = %d（Windows 下两个都是分隔符）\n",
           (int)cb_path_is_sep('/'), (int)cb_path_is_sep('\\'));
    printf("cb_path_is_sep('x') = %d，cb_path_is_sep('\\0') = %d\n", (int)cb_path_is_sep('x'),
           (int)cb_path_is_sep('\0'));

    printf("cb_path_name(\"a/b/c.txt\") = %s\n", cb_path_name("a/b/c.txt"));
    printf("cb_path_name(\"c.txt\") = %s\n", cb_path_name("c.txt"));
    printf("cb_path_name(\"/x/y/\") = |%s|（结尾的分隔符后面没有名字）\n", cb_path_name("/x/y/"));

    printf("cb_temp_dir_name(\"a/b/c.txt\") = %s\n", cb_temp_dir_name("a/b/c.txt"));
    printf("cb_temp_dir_name(\"c.txt\") = %s\n", cb_temp_dir_name("c.txt"));
    printf("cb_temp_dir_name(\"\") = %s\n", cb_temp_dir_name(""));

    printf("cb_temp_file_name(\"a/b/c.txt\") = %s\n", cb_temp_file_name("a/b/c.txt"));
    printf("cb_temp_file_name(\"c.txt\") = %s\n", cb_temp_file_name("c.txt"));
    printf("cb_temp_file_name(\"/x/y/\") = |%s|\n", cb_temp_file_name("/x/y/"));

    {
        char* ext = cb_temp_file_ext("a/b/c.txt");
        char* last_ext = cb_temp_file_ext("archive.tar.gz");
        printf("cb_temp_file_ext(\"a/b/c.txt\") = %s\n", ext != NULL ? ext : "(NULL)");
        printf("cb_temp_file_ext(\"archive.tar.gz\") = %s（只取最后一个点之后）\n",
               last_ext != NULL ? last_ext : "(NULL)");
    }
    {
        char* no_ext = cb_temp_file_ext("a/b/noext");
        printf("cb_temp_file_ext(\"a/b/noext\") 非空 = %d\n", (int)(no_ext != NULL && no_ext[0] != '\0'));
    }

    const char* cwd = cb_get_current_dir_temp();
    printf("cb_get_current_dir_temp() 是绝对路径 = %d，当前目录存在 = %d\n",
           (int)cb_path_is_absolute(cwd), (int)(cb_file_exists(cwd) != 0));

    const char* exe = cb_temp_running_executable_path();
    const char* exe_base = cb_temp_file_name(exe);
    printf("cb_temp_running_executable_path() 的 basename = %s（不打印绝对路径）\n", exe_base);
    printf("basename 以 \"stdlib\" 开头 = %d\n",
           (int)cb_sv_starts_with_cstr(cb_sv_from_cstr(exe_base), "stdlib"));
}

// CB_Arg_Entry / CB_Arg_List 是 CB_Args 内部的两个结构；CB_DECLTYPE_CAST 在 C 下是空宏，
// 在 C++ 下补一个 decltype 转换，让 malloc/realloc 的返回值能赋给具体指针类型。
static void test_types_and_decltype_cast(void)
{
    printf("\n== 辅助类型与 CB_DECLTYPE_CAST ==\n");

    CB_Arg_Entry option = CB_ZERO;
    option.name = "verbose";
    option.value = NULL;
    CB_Arg_List options = CB_ZERO;
    printf("CB_Arg_Entry: name = %s, value = %s\n", option.name, option.value == NULL ? "(switch)" : option.value);
    printf("CB_Arg_List（CB_Args.options 的类型）: count = %zu\n", options.count);

    int* heap_int = NULL;
    heap_int = CB_DECLTYPE_CAST(heap_int) malloc(sizeof(int)); // C: 空宏；C++: (decltype(heap_int))
    *heap_int = 42;
    printf("CB_DECLTYPE_CAST(heap_int) malloc(sizeof(int)) -> %d\n", *heap_int);
    free(heap_int);
}

int main(void)
{
    test_da();
    test_bitset();
    test_ring();
    test_sort();
    test_case();
    test_numbers();
    test_split_join();
    test_chop_by_delim_r();
    test_utf8();
    test_path();
    test_path_extras();
    test_types_and_decltype_cast();

    return 0;
}
