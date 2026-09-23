#include "test_diagnostics.h"
#include "cb.h"

// cb_return_defer(value) 展开成 "result = (value); goto defer;"，所以函数里必须有一个
// 名为 result 的变量和一个 defer: 标签。成功路径自然落到 defer:，失败路径跳过去。
// C++ 下 goto 不能跨过已初始化的声明，因此所有局部变量都写在第一次 cb_return_defer 之前。
static bool try_open(const char* path, CB_String_Builder* sb)
{
    bool result = true;
    if (!cb_file_exists(path)) cb_return_defer(false);
    if (!cb_read_entire_file(path, sb)) cb_return_defer(false);
defer:
    printf("    （try_open(\"%s\") 到达 defer: 标签，result = %d）\n", path, (int)result);
    return result;
}

int main(void)
{

    cb_minimal_log_level = CB_NO_LOGS;

    CB_String_Builder sb = CB_ZERO;
    CB_Mmap mm = CB_ZERO;

    printf("\n== 读文件 ==\n");
    printf("读不存在的文件 -> %d\n", (int)cb_read_entire_file("definitely-not-here.txt", &sb));
    printf("读一个目录 -> %d\n", (int)cb_read_entire_file(".", &sb));
    cb_sb_free(sb);

    printf("\n== 写文件 ==\n");
    printf("写到不存在的目录 -> %d\n", (int)cb_write_entire_file("no-such-dir/x.txt", "d", 1));
    printf("原子写到不存在的目录 -> %d\n", (int)cb_write_entire_file_atomic("no-such-dir/x.txt", "d", 1));

    printf("\n== 删除 / 复制 / 改名 ==\n");
    printf("删不存在的文件 -> %d\n", (int)cb_delete_file("no-such-file.txt"));
    printf("删不存在的目录 -> %d\n", (int)cb_delete_directory_recursively("no-such-dir"));
    printf("复制不存在的源 -> %d\n", (int)cb_copy_file("no-such-src.txt", "dst.txt"));
    printf("改名不存在的源 -> %d\n", (int)cb_rename("no-such-a.txt", "no-such-b.txt"));

    printf("\n== 目录与工作目录 ==\n");
    printf("切到不存在的目录 -> %d\n", (int)cb_set_current_dir("no-such-dir"));
    printf("mkdir_p 空串 -> %d\n", (int)cb_mkdir_if_not_exists(""));
    printf("glob 一个不存在的目录 -> %d\n", (int)cb_glob("no-such-dir", "*", NULL));
    printf("read_entire_dir 不存在的目录 -> %d\n", (int)cb_read_entire_dir("no-such-dir", NULL));

    printf("\n== 查询类 ==\n");
    printf("get_file_type(no-such-file.txt) = %d（负数即失败）\n", (int)cb_get_file_type("no-such-file.txt"));
    printf("file_exists(no-such-file.txt) = %d\n", (int)cb_file_exists("no-such-file.txt"));
    printf("file_size(no-such-file.txt) == (size_t)-1 -> %d\n",
           (int)(cb_file_size("no-such-file.txt") == (size_t)-1));
    printf("file_mtime(no-such-file.txt) = %lld\n", (long long)cb_file_mtime("no-such-file.txt"));
    printf("不存在的路径 == CB_FILE_ERROR -> %d\n",
           (int)(cb_get_file_type("no-such-file.txt") == CB_FILE_ERROR));
    printf("read_symlink(cb.h) == NULL -> %d\n", (int)(cb_read_symlink("cb.h") == NULL));
    printf("mmap_open(no-such-file.bin) -> %d\n", (int)cb_mmap_open("no-such-file.bin", &mm));
    cb_mmap_close(&mm);

    printf("\n== 参数校验类 ==\n");
    printf("数字解析 sv_to_i64(\"abc\") -> %d\n", (int)cb_sv_to_i64(CB_SVLIT("abc"), NULL));
    printf("无符号解析 sv_to_u64(\"-1\") -> %d\n", (int)cb_sv_to_u64(CB_SVLIT("-1"), NULL));
    printf("浮点解析 sv_to_f64(\"1.5x\") -> %d\n", (int)cb_sv_to_f64(CB_SVLIT("1.5x"), NULL));
    printf("utf8_validate(\"\\xFF\") -> %d\n", (int)cb_utf8_validate(cb_sv_from_parts("\xFF", 1), NULL));

    char utf8_out[4] = {0};
    printf("utf8_encode(0x110000) 返回的字节数 = %zu\n", cb_utf8_encode(0x110000, utf8_out));
    CB_Args empty_args = CB_ZERO;
    printf("空参数集里查开关 -> %d\n", (int)cb_args_has(&empty_args, "x"));

#ifdef _WIN32

    printf("\n== Windows 错误信息 ==\n");
    {
        char* msg = cb_win32_error_message(ERROR_FILE_NOT_FOUND);
        printf("cb_win32_error_message(ERROR_FILE_NOT_FOUND) 非空 -> %d\n",
               (int)(msg != NULL && msg[0] != '\0'));
        printf("未知错误码也返回字符串（非 NULL）-> %d\n",
               (int)(cb_win32_error_message(0xDEADBEEF) != NULL));
    }
#endif

    printf("\n== 成功后确实返回 true（对照组）==\n");
    printf("mkdir_p 嵌套创建 err_paths_work/a/b -> %d\n", (int)cb_mkdir_if_not_exists("err_paths_work/a/b"));
    printf("写入 err_paths_work/x.txt -> %d\n", (int)cb_write_entire_file("err_paths_work/x.txt", "hi", 2));
    printf("复制 x.txt -> y.txt -> %d\n",
           (int)cb_copy_file("err_paths_work/x.txt", "err_paths_work/y.txt"));
    printf("改名 y.txt -> z.txt -> %d\n",
           (int)cb_rename("err_paths_work/y.txt", "err_paths_work/z.txt"));
    printf("删除 z.txt -> %d\n", (int)cb_delete_file("err_paths_work/z.txt"));
    printf("递归删除 err_paths_work -> %d\n", (int)cb_delete_directory_recursively("err_paths_work"));

    printf("\n== cb_return_defer：失败与成功都经过 defer: 标签 ==\n");
    {
        CB_String_Builder defer_sb = CB_ZERO;
        printf("try_open(\"no-such-file.txt\") -> %d（文件不存在，跳到 defer: 返回 false）\n",
               (int)try_open("no-such-file.txt", &defer_sb));

        printf("写 ok.txt -> %d\n", (int)cb_write_entire_file("ok.txt", "defer-data", 10));
        bool opened = try_open("ok.txt", &defer_sb);
        printf("try_open(\"ok.txt\") -> %d，count = %zu，内容 = |%.*s|\n", (int)opened, defer_sb.count,
               (int)defer_sb.count, defer_sb.items != NULL ? defer_sb.items : "");
        printf("删除 ok.txt 收尾 -> %d\n", (int)cb_delete_file("ok.txt"));
        cb_sb_free(defer_sb);
    }

    return 0;
}
