// 示例 09：路径与 glob
//
// 覆盖：join / normalize / absolute / replace_ext / 取路径各部分 / glob 匹配与列目录
//
// 注意：路径输出的分隔符是平台原生的（Windows 是 \，其余是 /），但输入两种都认。
//
// 编译运行（在仓库根目录）：
//     cc -o /tmp/ex09 examples/09_paths.c && /tmp/ex09

#include "../cb.h"

static void title(const char* text) { printf("\n== %s ==\n", text); }

static void demo_join_normalize(void)
{
    title("cb_path_join / cb_path_normalize（纯字符串处理，不访问文件系统）");

    printf("  平台分隔符 = '%c'\n", CB_PATH_SEP);
    printf("  join(\"src\", \"main.c\")      = %s\n", cb_path_join("src", "main.c"));
    printf("  join(\"src/\", \"main.c\")     = %s\n", cb_path_join("src/", "main.c"));
    printf("  join(\"src\", \"/abs/path\")   = %s   ← b 是绝对路径时以 b 为准\n", cb_path_join("src", "/abs/path"));
    printf("  join(\"\", \"x\")              = %s\n", cb_path_join("", "x"));
    printf("  join(\"src\", \"\")            = %s\n\n", cb_path_join("src", ""));

    printf("  normalize(\"a/./b/../c\")     = %s\n", cb_path_normalize("a/./b/../c"));
    printf("  normalize(\"a//b///c\")       = %s   ← 折叠重复分隔符\n", cb_path_normalize("a//b///c"));
    printf("  normalize(\"../a\")           = %s   ← 开头的 .. 保留\n", cb_path_normalize("../a"));
    printf("  normalize(\"/..\")            = %s   ← 根目录上的 .. 丢弃\n", cb_path_normalize("/.."));
    printf("  normalize(\"\")               = %s\n", cb_path_normalize(""));
    printf("  normalize(\"a/\")             = %s   ← 去掉末尾分隔符\n", cb_path_normalize("a/"));
}

static void demo_absolute(void)
{
    title("cb_path_absolute / cb_path_is_absolute / cb_get_current_dir_temp / cb_set_current_dir");

    printf("  当前工作目录 = %s\n", cb_get_current_dir_temp());
    printf("  cb_path_absolute(\"x\")        = %s\n", cb_path_absolute("x"));
    printf("  cb_path_absolute(\"/a/../b\")   = %s   ← 已经是绝对的，只做 normalize\n", cb_path_absolute("/a/../b"));
    printf("  cb_path_is_absolute(\"/x\")     = %s\n", cb_path_is_absolute("/x") ? "真" : "假");
    printf("  cb_path_is_absolute(\"x\")      = %s\n", cb_path_is_absolute("x") ? "真" : "假");
    printf("  cb_path_is_absolute(\"\")       = %s\n", cb_path_is_absolute("") ? "真" : "假");
}

static void demo_parts(void)
{
    title("cb_path_replace_ext / cb_path_name / cb_temp_dir_name / cb_temp_file_name / cb_temp_file_ext");

    printf("  replace_ext(\"a/b.c\", \"h\")   = %s\n", cb_path_replace_ext("a/b.c", "h"));
    printf("  replace_ext(\"a/b.c\", \".h\")  = %s   ← 带不带点都行\n", cb_path_replace_ext("a/b.c", ".h"));
    printf("  replace_ext(\"a/b\", \"h\")     = %s   ← 原本没有扩展名\n", cb_path_replace_ext("a/b", "h"));
    printf("  replace_ext(\"a/b.c\", NULL)   = %s   ← 去掉扩展名\n", cb_path_replace_ext("a/b.c", NULL));
    printf("  replace_ext(\"a.b/c\", \"h\")   = %s   ← 目录名里的点不算\n\n", cb_path_replace_ext("a.b/c", "h"));

    printf("  cb_path_name(\"/path/to/file.c\") = %s\n", cb_path_name("/path/to/file.c"));
    printf("  cb_path_name(\"no-slash\")        = %s\n\n", cb_path_name("no-slash"));

    printf("  cb_temp_dir_name(\"/p/to/f.c\")   = %s\n", cb_temp_dir_name("/p/to/f.c"));
    printf("  cb_temp_file_name(\"/p/to/f.c\")  = %s\n", cb_temp_file_name("/p/to/f.c"));
    printf("  cb_temp_file_ext(\"/p/to/f.c\")   = %s\n", cb_temp_file_ext("/p/to/f.c"));
}

static void demo_glob_match(void)
{
    title("cb_glob_match：纯匹配（不访问文件系统）");

    struct { const char* pattern; const char* text; } cases[] = {
        {"*.c", "main.c"}, {"*.c", "main.h"}, {"*", "anything"}, {"*", ""},
        {"a*c", "abbbc"}, {"a*c", "ac"}, {"?.c", "a.c"}, {"?.c", "ab.c"},
        {"[abc]x", "bx"}, {"[abc]x", "dx"}, {"[a-z]*", "test"}, {"[a-z]*", "Test"},
        {"[!abc]x", "dx"}, {"src/*/*.c", "src/a/b.c"},
    };
    for (size_t i = 0; i < CB_ARRAY_LEN(cases); ++i) {
        printf("  %-12s 匹配 %-12s -> %s\n", cases[i].pattern, cases[i].text,
               cb_glob_match(cases[i].pattern, cases[i].text) ? "是" : "否");
    }
}

static void demo_glob(void)
{
    title("cb_glob：列目录并筛选（只匹配单层，不递归）");

    // 用 examples/ 目录本身来演示
    CB_File_Paths hits = CB_ZERO;
    if (!cb_glob("examples", "*.c", &hits)) {
        printf("  列目录失败\n");
        return;
    }
    printf("  examples/*.c 匹配到 %zu 个：\n", hits.count);
    for (size_t i = 0; i < hits.count; ++i) printf("    %s\n", hits.items[i]);
    cb_da_free(hits);

    hits = (CB_File_Paths)CB_ZERO;
    cb_glob("docs", "*.md", &hits);
    printf("  docs/*.md 匹配到 %zu 个\n", hits.count);
    cb_da_free(hits);
}

static void demo_is_sep(void)
{
    printf("-- cb_path_is_sep：判断单个字符是不是路径分隔符 --\n");
#ifdef _WIN32
    printf("  本平台接受 '/' 和 '\\\\'：");
#else
    printf("  本平台只接受 '/'：");
#endif
    const char* probes = "/\\abc";
    for (const char* p = probes; *p; ++p) printf("  '%c'->%d", *p, cb_path_is_sep(*p) ? 1 : 0);
    printf("\n");
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    demo_join_normalize();
    demo_absolute();
    demo_parts();
    demo_glob_match();
    demo_glob();
    demo_is_sep();

    printf("\n全部路径示例执行完毕\n");
    return 0;
}
