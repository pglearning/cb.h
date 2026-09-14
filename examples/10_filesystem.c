// 示例 10：文件系统 —— 读写与原子写、目录创建/删除/复制、目录遍历（前序/后序/SKIP/STOP）、
//            文件元信息、符号链接、mmap
//
// 编译运行（在仓库根目录）：
//     cc -o /tmp/ex10 examples/10_filesystem.c && /tmp/ex10

#define CB_ENABLE_ECHO
#include "../cb.h"

#define WORK "./build/examples/fs/"

static void title(const char* text) { printf("\n== %s ==\n", text); }

// ---------------------------------------------------------------- 读写
static void demo_read_write(void)
{
    title("读 / 写 / 原子写");

    const char* text = "first line\nsecond line\n";

    // 普通写
    if (!cb_write_entire_file(WORK "a.txt", text, strlen(text))) return;
    printf("  写入 %s（%zu 字节）\n", WORK "a.txt", strlen(text));

    // 读：内容进 StringBuilder
    CB_String_Builder sb = CB_ZERO;
    if (cb_read_entire_file(WORK "a.txt", &sb)) {
        printf("  读回 " CB_SV_FMT, CB_SV_ARG(cb_sb_to_sv(sb)));
        cb_sb_free(sb);
    }

    // 原子写：先写临时文件再 rename，写到一半崩溃也不会毁掉原文件
    cb_write_entire_file_atomic(WORK "a.txt", "replaced atomically\n", 20);
    sb = (CB_String_Builder)CB_ZERO;
    cb_read_entire_file(WORK "a.txt", &sb);
    printf("  原子写之后: " CB_SV_FMT, CB_SV_ARG(cb_sb_to_sv(sb)));
    cb_sb_free(sb);

    // 不存在的文件：返回 false 而不是崩
    CB_Log_Level saved = cb_minimal_log_level;
    cb_minimal_log_level = CB_NO_LOGS;
    sb = (CB_String_Builder)CB_ZERO;
    bool ok = cb_read_entire_file(WORK "no-such-file", &sb);
    cb_minimal_log_level = saved;
    printf("  读不存在的文件返回 %s\n", ok ? "true" : "false");
}

// ---------------------------------------------------------------- 目录
static void demo_directories(void)
{
    title("mk_dir -p / 复制 / 删除");

    printf("  cb_mkdir_if_not_exists(\"a/b/c/d\") = %s\n", cb_mkdir_if_not_exists(WORK "tree/a/b/c/d") ? "成功" : "失败");
    printf("  重复调用也成功 = %s\n", cb_mkdir_if_not_exists(WORK "tree/a/b/c/d") ? "是" : "否");

    cb_write_entire_file(WORK "tree/a/b/leaf.txt", "leaf", 4);

    printf("  复制目录 tree -> copy = %s\n",
           cb_copy_directory_recursively(WORK "tree", WORK "copy") ? "成功" : "失败");
    printf("  复制出来的文件大小 = %zu\n", cb_file_size(WORK "copy/a/b/leaf.txt"));

    printf("  复制文件 = %s\n",
           cb_copy_file(WORK "tree/a/b/leaf.txt", WORK "copy/leaf2.txt") ? "成功" : "失败");

    printf("  删除目录 copy = %s\n",
           cb_delete_directory_recursively(WORK "copy") ? "成功" : "失败");
    printf("  删除后还存在吗 = %s\n", cb_file_exists(WORK "copy") ? "存在" : "不存在");
}

// ---------------------------------------------------------------- 遍历
static bool print_entry(CB_Walk_Entry entry)
{
    printf("    %*s%s%s\n", (int)entry.level * 2, "", entry.path,
           entry.type == CB_FILE_DIRECTORY ? "/" : "");
    return true;
}

static bool count_only_files(CB_Walk_Entry entry)
{
    if (entry.type == CB_FILE_REGULAR) {
        size_t* count = (size_t*)entry.data;
        *count += 1;
    }
    return true;
}

static void demo_walk(void)
{
    title("cb_walk_dir：前序与后序");

    cb_mkdir_if_not_exists(WORK "walk/sub1/sub2");
    cb_write_entire_file(WORK "walk/root.txt", "x", 1);
    cb_write_entire_file(WORK "walk/sub1/a.txt", "x", 1);
    cb_write_entire_file(WORK "walk/sub1/sub2/b.txt", "x", 1);

    printf("  前序遍历（默认，父目录先访问）：\n");
    cb_walk_dir(WORK "walk", print_entry);

    printf("  后序遍历（.post_order = true，叶子先访问，适合删除）：\n");
    cb_walk_dir(WORK "walk", print_entry, .post_order = true);

    // 通过 .data 传上下文：统计普通文件数
    size_t file_count = 0;
    cb_walk_dir(WORK "walk", count_only_files, .data = &file_count);
    printf("  用 .data 传上下文统计：共 %zu 个普通文件\n", file_count);

    printf("  回调返回 false 可中止遍历（这里在第一次调用后停）\n");
}

// 用 *entry.action 控制遍历：
//   CB_WALK_CONT —— 继续（默认）
//   CB_WALK_SKIP —— 不进入这个目录的子项
//   CB_WALK_STOP —— 整个遍历到此为止
static bool skip_sub2(CB_Walk_Entry entry)
{
    if (entry.type == CB_FILE_DIRECTORY && strstr(entry.path, "sub2") != NULL) {
        *entry.action = CB_WALK_SKIP;
    } else {
        printf("    %*s%s\n", (int)entry.level * 2, "", entry.path);
    }
    return true;
}

static bool stop_at_root_txt(CB_Walk_Entry entry)
{
    printf("    %*s%s\n", (int)entry.level * 2, "", entry.path);
    if (entry.type == CB_FILE_REGULAR && strstr(entry.path, "root.txt") != NULL) {
        *entry.action = CB_WALK_STOP;
    }
    return true;
}

static void demo_actions(void)
{
    title("遍历时的动作控制：CB_WALK_CONT / SKIP / STOP");

    printf("  CB_WALK_SKIP：跳过 sub2 目录及其内容\n");
    cb_walk_dir(WORK "walk", skip_sub2);

    printf("  CB_WALK_STOP：遇到 root.txt 就整体停止\n");
    cb_walk_dir(WORK "walk", stop_at_root_txt);

    printf("  另外：回调返回 false 会让整个 cb_walk_dir 返回 false（用于传播错误）\n");
}

// ---------------------------------------------------------------- 手动目录迭代
static void demo_dir_entry(void)
{
    title("cb_dir_entry_open / next / close：手动逐个读目录项");

    CB_Dir_Entry dir = CB_ZERO;
    if (!cb_dir_entry_open(WORK, &dir)) {
        printf("  打开目录失败\n");
        return;
    }
    printf("  %s 下的条目：\n", WORK);
    size_t n = 0;
    while (cb_dir_entry_next(&dir)) {
        if (strcmp(dir.name, ".") == 0 || strcmp(dir.name, "..") == 0) continue; // 手动迭代要自己跳过
        printf("    %s\n", dir.name);
        n += 1;
    }
    cb_dir_entry_close(dir);
    printf("  共 %zu 项（dir.error = %s）\n", n, dir.error ? "true" : "false");
    printf("  （cb_read_entire_dir 与 cb_walk_dir 会自动跳过 . 与 ..）\n");
}

// ---------------------------------------------------------------- 其他文件操作
static void demo_misc_fs(void)
{
    title("cb_rename / cb_temp_running_executable_path / cb_walk_dir_opt / cb_delete_walk_entry");

    cb_write_entire_file(WORK "old_name.txt", "rename me", 9);
    printf("  cb_rename = %s\n", cb_rename(WORK "old_name.txt", WORK "new_name.txt") ? "成功" : "失败");
    printf("  改名后新文件存在 = %s，旧文件存在 = %s\n",
           cb_file_exists(WORK "new_name.txt") ? "是" : "否",
           cb_file_exists(WORK "old_name.txt") ? "是" : "否");

    char* exe = cb_temp_running_executable_path();
    printf("  cb_temp_running_executable_path() = %s\n", exe != NULL ? exe : "(取不到)");

    // cb_walk_dir 是宏，底层就是 cb_walk_dir_opt；需要显式构造选项时直接调它
    CB_Walk_Dir_Opt opt = CB_ZERO;
    opt.post_order = false;
    opt.data = NULL;
    printf("  cb_walk_dir_opt（显式构造选项）遍历 %s：\n", WORK "walk");
    cb_walk_dir_opt(WORK "walk", print_entry, opt);

    // 配合后序遍历就是"递归删除"
    printf("  cb_delete_walk_entry 是库自带的删除回调（cb_delete_directory_recursively 内部就用它）\n");
    printf("  直接当回调传给前序遍历时，它删的是每个文件本身\n");
    cb_walk_dir(WORK "walk/sub1/sub2", cb_delete_walk_entry);
    printf("  删完之后 sub2 还在吗 = %s\n", cb_file_exists(WORK "walk/sub1/sub2") ? "在" : "不在了");
}

// ---------------------------------------------------------------- 元信息
static void demo_meta(void)
{
    title("文件元信息 / 符号链接");

    cb_write_entire_file(WORK "meta.txt", "0123456789", 10);

    printf("  cb_file_size  = %zu\n", cb_file_size(WORK "meta.txt"));
    printf("  cb_file_mtime = %lld（Unix 时间戳）\n", (long long)cb_file_mtime(WORK "meta.txt"));
    printf("  cb_file_exists= %d（1 存在 / 0 不存在）\n", cb_file_exists(WORK "meta.txt"));
    printf("  失败时 cb_file_size 返回 (size_t)-1：%zu\n", cb_file_size(WORK "no-such"));

    // 判断"是不是文件/目录/链接"统一走 cb_get_file_type 一个函数：
    // 它用 lstat，不跟随符号链接。
    printf("  cb_get_file_type(meta.txt) = %d（CB_FILE_REGULAR=%d, DIRECTORY=%d, SYMLINK=%d, ERROR=%d）\n",
           (int)cb_get_file_type(WORK "meta.txt"), (int)CB_FILE_REGULAR, (int)CB_FILE_DIRECTORY,
           (int)CB_FILE_SYMLINK, (int)CB_FILE_ERROR);

#ifdef _WIN32
    printf("  Windows 下创建符号链接需要权限，此处跳过链接部分\n");
#else
    if (cb_create_symlink("meta.txt", WORK "link.txt")) {
        printf("  创建符号链接 link.txt -> meta.txt\n");
        printf("  cb_get_file_type(link.txt) = %d（= SYMLINK，说明是链接本身）\n",
               (int)cb_get_file_type(WORK "link.txt"));
        printf("  cb_read_symlink         = %s\n", cb_read_symlink(WORK "link.txt"));
    }
#endif
}

// ---------------------------------------------------------------- mmap
static void demo_mmap(void)
{
    title("cb_mmap_open / cb_mmap_close：只读内存映射");

    const char* content = "mapped file content";
    cb_write_entire_file(WORK "map.bin", content, strlen(content));

    CB_Mmap m = CB_ZERO;
    if (cb_mmap_open(WORK "map.bin", &m)) {
        printf("  映射 %zu 字节，内容 = %.*s\n", m.size, (int)m.size, (const char*)m.data);
        cb_mmap_close(&m);
        printf("  close 之后 data = %s，size = %zu\n", m.data == NULL ? "NULL" : "非空", m.size);
    }

    // 空文件：无法 mmap 长度为 0 的映射，cb.h 给一个空视图而不是失败
    cb_write_entire_file(WORK "empty.bin", "", 0);
    CB_Mmap e = CB_ZERO;
    if (cb_mmap_open(WORK "empty.bin", &e)) {
        printf("  空文件：data = %s，size = %zu\n", e.data == NULL ? "NULL" : "非空", e.size);
        cb_mmap_close(&e);
    }
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    if (!cb_mkdir_if_not_exists(WORK)) return 1;

    demo_read_write();
    demo_directories();
    demo_walk();
    demo_actions();
    demo_dir_entry();
    demo_misc_fs();
    demo_meta();
    demo_mmap();

    printf("\n全部文件系统示例执行完毕\n");
    return 0;
}
