// File System 扩展实测：元信息 / mkdir -p / 原子写 / 符号链接 / glob / mmap
// 工作目录是 build/tests/fs.cwd（构建脚本每次运行前清空重建）
#include "test_diagnostics.h"
#include "cb.h"

#ifdef _WIN32
#define TEST_SEP "\\"
#else
#define TEST_SEP "/"
#endif

static void test_meta(void)
{
    printf("\n== 文件元信息 ==\n");

    const char* content = "hello meta";
    printf("写入 meta.txt -> %d\n", (int)cb_write_entire_file("meta.txt", content, strlen(content)));

    printf("cb_file_size(meta.txt) = %zu\n", cb_file_size("meta.txt"));

    int meta_type = cb_get_file_type("meta.txt");
    printf("cb_get_file_type(meta.txt) = %d（普通文件/不是目录/不是链接 = %d/%d/%d）\n", (int)meta_type,
           (int)(meta_type == CB_FILE_REGULAR), (int)(meta_type != CB_FILE_DIRECTORY),
           (int)(meta_type != CB_FILE_SYMLINK));

    // 时间戳本身每次运行都不同，不能进 golden；只记录它是否是个合理的时间
    printf("cb_file_mtime(meta.txt) > 1600000000 -> %d\n", (int)(cb_file_mtime("meta.txt") > 1600000000));

    printf("不存在的文件 size 返回 -1 -> %d\n", (int)(cb_file_size("no-such-file") == (size_t)-1));
    printf("不存在的文件 mtime = %lld\n", (long long)cb_file_mtime("no-such-file"));
    {
        // get_file_type 失败会打日志，静音掉免得刷屏
        CB_Log_Level saved = cb_minimal_log_level;
        cb_minimal_log_level = CB_NO_LOGS;
        printf("不存在的路径返回 CB_FILE_ERROR -> %d\n",
               (int)(cb_get_file_type("no-such-file") == CB_FILE_ERROR));
        cb_minimal_log_level = saved;
    }

    printf("cb_get_file_type(\".\") == CB_FILE_DIRECTORY -> %d\n",
           (int)(cb_get_file_type(".") == CB_FILE_DIRECTORY));
}

static void test_read_entire_file_errors(void)
{
    printf("\n== 回归：cb_read_entire_file 的错误处理 ==\n");

    CB_Log_Level saved = cb_minimal_log_level;
    cb_minimal_log_level = CB_NO_LOGS; // 预期失败，不用刷屏

    CB_String_Builder sb = CB_ZERO;
    printf("读不存在的文件 -> %d\n", (int)cb_read_entire_file("definitely-not-here.txt", &sb));

    printf("准备一个正常文件 ok.txt -> %d\n", (int)cb_write_entire_file("ok.txt", "hello", 5));
    sb.count = 0;
    bool got = cb_read_entire_file("ok.txt", &sb);
    printf("读正常文件 -> %d，count = %zu，内容 = |%.*s|\n", (int)got, sb.count, (int)sb.count,
           sb.items != NULL ? sb.items : "");

    // 读目录：fopen 成功但读取失败，必须优雅返回 false
    sb.count = 0;
    printf("读目录 -> %d\n", (int)cb_read_entire_file(".", &sb));

    // 追加语义：同一个 sb 再读一次应当是追加而不是覆盖
    sb.count = 0;
    cb_read_entire_file("ok.txt", &sb);
    cb_read_entire_file("ok.txt", &sb);
    printf("同一 sb 读两次后 count = %zu，内容 = |%.*s|\n", sb.count, (int)sb.count,
           sb.items != NULL ? sb.items : "");

    cb_minimal_log_level = saved;
    cb_sb_free(sb);
}

static void test_mkdir_p(void)
{
    printf("\n== mkdir -p ==\n");

    printf("递归创建 a/b/c/d -> %d\n", (int)cb_mkdir_if_not_exists("a/b/c/d"));
    printf("每一层都被创建 = %d/%d/%d/%d\n",
           (int)(cb_get_file_type("a") == CB_FILE_DIRECTORY),
           (int)(cb_get_file_type("a/b") == CB_FILE_DIRECTORY),
           (int)(cb_get_file_type("a/b/c") == CB_FILE_DIRECTORY),
           (int)(cb_get_file_type("a/b/c/d") == CB_FILE_DIRECTORY));
    printf("对已存在的目录再调用仍成功 -> %d\n", (int)cb_mkdir_if_not_exists("a/b/c/d"));
    printf("在已有树上继续加深 a/b/x/y -> %d\n", (int)cb_mkdir_if_not_exists("a/b/x/y"));
    printf("新分支被创建 -> %d\n", (int)(cb_get_file_type("a/b/x/y") == CB_FILE_DIRECTORY));
}

static void test_atomic_write(void)
{
    printf("\n== 原子写 ==\n");

    printf("原子写 v1 -> %d\n", (int)cb_write_entire_file_atomic("atomic.txt", "v1", 2));
    CB_String_Builder sb = CB_ZERO;
    printf("读回 atomic.txt -> %d\n", (int)cb_read_entire_file("atomic.txt", &sb));
    printf("内容是 |%.*s|（%zu 字节）\n", (int)sb.count, sb.items != NULL ? sb.items : "", sb.count);

    // 覆盖写：内容要完全替换
    printf("原子写覆盖 version-2 -> %d\n", (int)cb_write_entire_file_atomic("atomic.txt", "version-2", 9));
    sb.count = 0;
    printf("再次读回 -> %d\n", (int)cb_read_entire_file("atomic.txt", &sb));
    printf("覆盖后内容是 |%.*s|（%zu 字节）\n", (int)sb.count, sb.items != NULL ? sb.items : "", sb.count);
    cb_sb_free(sb);

    // 不应留下临时文件
    CB_File_Paths entries = CB_ZERO;
    printf("列目录 -> %d\n", (int)cb_read_entire_dir(".", &entries));
    size_t tmp_count = 0;
    for (size_t i = 0; i < entries.count; ++i) {
        if (strstr(entries.items[i], ".tmp") != NULL) tmp_count += 1;
    }
    printf("原子写留下的 .tmp 垃圾文件个数 = %zu\n", tmp_count);
    cb_da_free(entries);
}

static void test_symlink(void)
{
    printf("\n== 符号链接 ==\n");

#ifdef _WIN32
    // Windows 下建符号链接要开发者模式或管理员权限，而且不同环境的行为还不一致：
    // CI 的 wine 里 CreateSymbolicLink 会"报成功"但链接其实不可用（lstat 看不到它、
    // readlink 读不出来），同一份 golden 于是有的机器过、有的机器挂。
    // Linux 下建链接是普通操作，完整覆盖保留在下面；Windows 下整段跳过，输出在任何
    // Windows 环境（真机 / 各种 wine 配置 / 有没有权限）下都逐字节一致。
    printf("Windows 下跳过符号链接用例（建链接需要开发者模式或管理员权限）\n");
    return;
#endif // _WIN32

    printf("创建目标文件 target.txt -> %d\n", (int)cb_write_entire_file("target.txt", "target-data", 11));
    printf("创建符号链接 link.txt -> %d\n", (int)cb_create_symlink("target.txt", "link.txt"));

    int link_type = cb_get_file_type("link.txt");
    printf("识别出符号链接 -> %d\n", (int)(link_type == CB_FILE_SYMLINK));
    printf("普通文件不是链接 -> %d\n", (int)(cb_get_file_type("target.txt") != CB_FILE_SYMLINK));
    // 注意语义：cb_get_file_type 用 lstat，不跟随链接，所以链接本身不是 regular
    printf("不跟随链接（lstat 语义）-> %d\n", (int)(link_type != CB_FILE_REGULAR));

    char* dest = cb_read_symlink("link.txt");
    printf("cb_read_symlink(link.txt) = %s\n", dest != NULL ? dest : "(NULL)");

    CB_String_Builder sb = CB_ZERO;
    printf("通过链接读文件 -> %d\n", (int)cb_read_entire_file("link.txt", &sb));
    printf("读到的内容 = |%.*s|（%zu 字节）\n", (int)sb.count, sb.items != NULL ? sb.items : "", sb.count);
    cb_sb_free(sb);

    // 预期失败探测：静音日志，否则终端上会多一行 "Could not read symlink ..."，看着像出了问题。
    {
        CB_Log_Level saved = cb_minimal_log_level;
        cb_minimal_log_level = CB_NO_LOGS;
        char* not_a_link = cb_read_symlink("target.txt");
        cb_minimal_log_level = saved;
        printf("对非链接调用 read_symlink 返回 NULL -> %d\n", (int)(not_a_link == NULL));
    }
}

static void test_glob_match(void)
{
    printf("\n== glob 匹配（纯函数）==\n");

    printf("cb_glob_match(\"*.c\", \"main.c\") = %d\n", (int)cb_glob_match("*.c", "main.c"));
    printf("cb_glob_match(\"*.c\", \"main.h\") = %d\n", (int)cb_glob_match("*.c", "main.h"));
    printf("cb_glob_match(\"*\", \"anything\") = %d\n", (int)cb_glob_match("*", "anything"));
    printf("cb_glob_match(\"*\", \"\") = %d\n", (int)cb_glob_match("*", ""));
    printf("cb_glob_match(\"a*c\", \"abbbc\") = %d\n", (int)cb_glob_match("a*c", "abbbc"));
    printf("cb_glob_match(\"a*c\", \"ac\") = %d（* 可为空）\n", (int)cb_glob_match("a*c", "ac"));
    printf("cb_glob_match(\"a*c\", \"ab\") = %d\n", (int)cb_glob_match("a*c", "ab"));

    printf("cb_glob_match(\"?.c\", \"a.c\") = %d\n", (int)cb_glob_match("?.c", "a.c"));
    printf("cb_glob_match(\"?.c\", \"ab.c\") = %d（? 只匹配一个字符）\n", (int)cb_glob_match("?.c", "ab.c"));

    printf("cb_glob_match(\"[abc]x\", \"bx\") = %d\n", (int)cb_glob_match("[abc]x", "bx"));
    printf("cb_glob_match(\"[abc]x\", \"dx\") = %d\n", (int)cb_glob_match("[abc]x", "dx"));
    printf("cb_glob_match(\"[a-z]1\", \"q1\") = %d\n", (int)cb_glob_match("[a-z]1", "q1"));
    printf("cb_glob_match(\"[a-z]1\", \"Q1\") = %d\n", (int)cb_glob_match("[a-z]1", "Q1"));
    printf("cb_glob_match(\"[!abc]x\", \"dx\") = %d\n", (int)cb_glob_match("[!abc]x", "dx"));
    printf("cb_glob_match(\"[!abc]x\", \"ax\") = %d\n", (int)cb_glob_match("[!abc]x", "ax"));

    printf("cb_glob_match(\"src/*/*.c\", \"src/a/b.c\") = %d\n", (int)cb_glob_match("src/*/*.c", "src/a/b.c"));
    printf("cb_glob_match(\"a*b*c\", \"aXXbYYc\") = %d\n", (int)cb_glob_match("a*b*c", "aXXbYYc"));
    printf("cb_glob_match(\"a*b*c\", \"aXXbYY\") = %d\n", (int)cb_glob_match("a*b*c", "aXXbYY"));
    printf("cb_glob_match(\"\", \"\") = %d\n", (int)cb_glob_match("", ""));
    printf("cb_glob_match(\"\", \"x\") = %d\n", (int)cb_glob_match("", "x"));
}

static void test_glob(void)
{
    printf("\n== glob 列目录 ==\n");

    printf("建 g/sub -> %d\n", (int)cb_mkdir_if_not_exists("g/sub"));
    printf("写 g/one.c -> %d\n", (int)cb_write_entire_file("g/one.c", "1", 1));
    printf("写 g/two.c -> %d\n", (int)cb_write_entire_file("g/two.c", "2", 1));
    printf("写 g/three.h -> %d\n", (int)cb_write_entire_file("g/three.h", "3", 1));
    printf("写 g/sub/deep.c -> %d\n", (int)cb_write_entire_file("g/sub/deep.c", "4", 1));

    CB_File_Paths hits = CB_ZERO;
    printf("glob g/*.c -> %d\n", (int)cb_glob("g", "*.c", &hits));
    printf("匹配到 %zu 个 .c（不递归进 sub）\n", hits.count);

    bool found_one = false, found_two = false;
    for (size_t i = 0; i < hits.count; ++i) {
        if (strcmp(hits.items[i], "g" TEST_SEP "one.c") == 0) found_one = true;
        if (strcmp(hits.items[i], "g" TEST_SEP "two.c") == 0) found_two = true;
    }
    printf("路径是 dir/名字 形式（找到 one.c/two.c）= %d/%d\n", (int)found_one, (int)found_two);
    cb_da_free(hits);

    hits = (CB_File_Paths)CB_ZERO;
    bool glob_h = cb_glob("g", "*.h", &hits);
    printf("glob g/*.h -> %d，匹配 %zu 个\n", (int)glob_h, hits.count);
    cb_da_free(hits);

    hits = (CB_File_Paths)CB_ZERO;
    bool glob_all = cb_glob("g", "*", &hits);
    printf("glob g/* -> %d，匹配 %zu 个（全部 4 项）\n", (int)glob_all, hits.count);
    cb_da_free(hits);

    hits = (CB_File_Paths)CB_ZERO;
    bool glob_none = cb_glob("g", "nomatch*", &hits);
    printf("glob g/nomatch* -> %d，匹配 %zu 个（无匹配时返回空列表）\n", (int)glob_none, hits.count);
    cb_da_free(hits);
}

static void test_mmap(void)
{
    printf("\n== mmap ==\n");

    const char* content = "mapped file content 0123456789";
    size_t len = strlen(content);
    printf("写 map.txt -> %d\n", (int)cb_write_entire_file("map.txt", content, len));

    CB_Mmap m = CB_ZERO;
    printf("mmap 打开 map.txt -> %d\n", (int)cb_mmap_open("map.txt", &m));
    printf("映射长度 = %zu\n", m.size);
    printf("映射内容与文件一致 = %d\n", (int)(m.data != NULL && memcmp(m.data, content, len) == 0));
    cb_mmap_close(&m);
    printf("close 清空句柄 = %d\n", (int)(m.data == NULL && m.size == 0));

    // 空文件：长度 0 无法 mmap，应返回一个空视图而不是失败
    printf("写空文件 empty.txt -> %d\n", (int)cb_write_entire_file("empty.txt", "", 0));
    CB_Mmap e = CB_ZERO;
    printf("空文件 mmap 成功 -> %d\n", (int)cb_mmap_open("empty.txt", &e));
    printf("空文件给的是空视图 = %d\n", (int)(e.data == NULL && e.size == 0));
    cb_mmap_close(&e);

    // 不存在的文件应失败且不崩
    CB_Mmap missing = CB_ZERO;
    CB_Log_Level saved = cb_minimal_log_level;
    cb_minimal_log_level = CB_NO_LOGS; // 预期报错，静音
    bool opened = cb_mmap_open("no-such-file.bin", &missing);
    cb_minimal_log_level = saved;
    printf("打开不存在的文件 -> %d\n", (int)opened);
    cb_mmap_close(&missing); // 失败之后 close 也必须安全，且句柄可再次使用

    printf("同一个句柄可以重新打开 -> %d\n", (int)cb_mmap_open("map.txt", &missing));
    printf("重新打开的映射长度 = %zu\n", missing.size);
    cb_mmap_close(&missing);
}

int main(void)
{
    test_meta();
    test_read_entire_file_errors();
    test_mkdir_p();
    test_atomic_write();
    test_symlink();
    test_glob_match();
    test_glob();
    test_mmap();

    return 0;
}
