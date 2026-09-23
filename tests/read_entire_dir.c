#include "test_diagnostics.h"
#include "cb.h"

// ==================================================================================================
// 目录遍历家族：cb_walk_dir（宏）/ cb_walk_dir_opt（直接调用）/ cb_delete_walk_entry
// 以及手工的 cb_dir_entry_open / _next / _close
// ==================================================================================================

// 一次访问的记录。path 归一化成 '/' 分隔（Windows 下 walk 拼的是 '\\'），这样 golden 与平台无关。
typedef struct {
    size_t level;
    size_t order; // 访问序号，只用来推导 pre-order / post-order 的相对次序，不直接打印
    const char* path;
    const char* type;
} Walk_Visit;

typedef struct {
    Walk_Visit* items;
    size_t count;
    size_t capacity;
} Walk_Visit_List;

// 通过 CB_Walk_Dir_Opt.data 传给回调的累加器
typedef struct {
    Walk_Visit_List visits;
    size_t dirs;
    size_t files;
    size_t others;
    size_t skipped;
} Walk_Report;

static const char* file_type_name(CB_File_Type type)
{
    switch (type) {
    case CB_FILE_ERROR:
        return "error";
    case CB_FILE_REGULAR:
        return "file";
    case CB_FILE_DIRECTORY:
        return "dir";
    case CB_FILE_SYMLINK:
        return "link";
    case CB_FILE_OTHER:
        return "other";
    }
    return "unknown";
}

static const char* path_slash(const char* path)
{
    char* copy = cb_temp_strdup(path);
    for (char* p = copy; *p != '\0'; ++p) {
        if (*p == '\\') *p = '/';
    }
    return copy;
}

static int cmp_visit(const void* a, const void* b)
{
    return strcmp(((const Walk_Visit*)a)->path, ((const Walk_Visit*)b)->path);
}

static int cmp_cstr(const void* a, const void* b)
{
    return strcmp(*(const char* const*)a, *(const char* const*)b);
}

// CB_Walk_Func：记录条目、累加计数，并对名为 skip 的目录返回 CB_WALK_SKIP
static bool walk_record(CB_Walk_Entry entry)
{
    Walk_Report* report = (Walk_Report*)entry.data;
    CB_Walk_Action action = CB_WALK_CONT;

    Walk_Visit visit = CB_ZERO;
    visit.level = entry.level;
    visit.order = report->visits.count;
    visit.path = path_slash(entry.path);
    visit.type = file_type_name(entry.type);
    cb_da_append(&report->visits, visit);

    switch (entry.type) {
    case CB_FILE_DIRECTORY:
        report->dirs += 1;
        break;
    case CB_FILE_REGULAR:
        report->files += 1;
        break;
    case CB_FILE_SYMLINK:
    case CB_FILE_OTHER:
    case CB_FILE_ERROR:
        report->others += 1;
        break;
    }

    if (entry.type == CB_FILE_DIRECTORY && strcmp(cb_path_name(entry.path), "skip") == 0) {
        action = CB_WALK_SKIP;
        report->skipped += 1;
    }

    *entry.action = action;
    return true;
}

static size_t visit_order_of(const Walk_Report* report, const char* path)
{
    for (size_t i = 0; i < report->visits.count; ++i) {
        if (strcmp(report->visits.items[i].path, path) == 0) return report->visits.items[i].order;
    }
    return (size_t)-1;
}

static size_t visits_under(const Walk_Report* report, const char* prefix)
{
    size_t n = 0;
    size_t prefix_len = strlen(prefix);
    for (size_t i = 0; i < report->visits.count; ++i) {
        if (strncmp(report->visits.items[i].path, prefix, prefix_len) == 0) n += 1;
    }
    return n;
}

// 同一批条目按路径排序后打印，避免依赖 readdir 的返回顺序
static void print_walk(const char* title, Walk_Report* report)
{
    qsort(report->visits.items, report->visits.count, sizeof(Walk_Visit), cmp_visit);

    printf("%s：访问到 %zu 个条目（目录 %zu，普通文件 %zu，其它 %zu，SKIP %zu 次）\n", title,
           report->visits.count, report->dirs, report->files, report->others, report->skipped);
    for (size_t i = 0; i < report->visits.count; ++i) {
        printf("    level = %zu, path = %s, type = %s\n", report->visits.items[i].level,
               report->visits.items[i].path, report->visits.items[i].type);
    }
}

static void test_walk_dir(void)
{
    printf("\n== cb_walk_dir（宏）与 cb_walk_dir_opt（直接调用）==\n");
    printf("CB_WALK_CONT = %d, CB_WALK_SKIP = %d, CB_WALK_STOP = %d\n", (int)CB_WALK_CONT,
           (int)CB_WALK_SKIP, (int)CB_WALK_STOP);

    // pre-order：cb_walk_dir 宏，等价于 cb_walk_dir_opt(root, func, CB_CLIT(CB_Walk_Dir_Opt){...})
    Walk_Report pre = CB_ZERO;
    printf("cb_walk_dir(\"tree\", walk_record, .data = &pre) -> %d\n",
           (int)cb_walk_dir("tree", walk_record, .data = &pre));
    print_walk("pre-order", &pre);
    printf("pre-order 语义：根最先访问 = %d，父目录排在子项之前 = %d，被 SKIP 的 skip/ 子树访问条数 = %zu\n",
           (int)(visit_order_of(&pre, "tree") == 0),
           (int)(visit_order_of(&pre, "tree/sub") < visit_order_of(&pre, "tree/sub/inner.txt")),
           visits_under(&pre, "tree/skip/"));

    // post-order：同名回调 + CB_Walk_Dir_Opt 直接构造
    Walk_Report post = CB_ZERO;
    CB_Walk_Dir_Opt post_opt = CB_ZERO;
    post_opt.data = &post;
    post_opt.post_order = true;
    CB_Walk_Func post_func = walk_record;
    printf("cb_walk_dir_opt(\"tree\", walk_record, {.data = &post, .post_order = true}) -> %d\n",
           (int)cb_walk_dir_opt("tree", post_func, post_opt));
    print_walk("post-order", &post);
    printf("post-order 语义：根最后访问 = %d，父目录排在子项之后 = %d\n",
           (int)(visit_order_of(&post, "tree") + 1 == post.visits.count),
           (int)(visit_order_of(&post, "tree/sub") > visit_order_of(&post, "tree/sub/inner.txt")));
    printf("post-order 时 SKIP 已经来不及（子项在回调之前就被递归访问了）：skip/ 子树访问条数 = %zu\n",
           visits_under(&post, "tree/skip/"));

    cb_da_free(pre.visits);
    cb_da_free(post.visits);
}

// STOP 用例：stop_tree 的每一层都只有唯一子项，所以访问顺序与 readdir 返回顺序无关。
// 哨兵是一个目录（sentinel/）：返回 CB_WALK_STOP 后整棵树立刻结束，里面的 never.txt 不会被访问。
typedef struct {
    size_t visits;
    size_t never_seen;
} Stop_Report;

static bool walk_stop_on_sentinel(CB_Walk_Entry entry)
{
    Stop_Report* report = (Stop_Report*)entry.data;
    report->visits += 1;

    printf("    访问 level = %zu, path = %s, type = %s\n", entry.level, path_slash(entry.path),
           file_type_name(entry.type));

    if (entry.type == CB_FILE_REGULAR && strcmp(cb_path_name(entry.path), "never.txt") == 0) {
        report->never_seen += 1;
    }
    if (entry.type == CB_FILE_DIRECTORY && strcmp(cb_path_name(entry.path), "sentinel") == 0) {
        *entry.action = CB_WALK_STOP;
    } else {
        *entry.action = CB_WALK_CONT;
    }
    return true;
}

static bool walk_count(CB_Walk_Entry entry)
{
    size_t* count = (size_t*)entry.data;
    *count += 1;
    *entry.action = CB_WALK_CONT;
    return true;
}

static void test_walk_stop(void)
{
    printf("\n== CB_WALK_STOP：哨兵目录终止整棵树 ==\n");

    Stop_Report report = CB_ZERO;
    printf("cb_walk_dir(\"stop_tree\", walk_stop_on_sentinel, .data = &report) -> %d\n",
           (int)cb_walk_dir("stop_tree", walk_stop_on_sentinel, .data = &report));
    printf("整棵树有 4 个条目，访问在哨兵 sentinel/ 处停止：实际访问 %zu 个 = %d\n", report.visits,
           (int)(report.visits == 3));
    printf("哨兵里面的 never.txt 一次都没被访问 = %d（文件本身还在：%d）\n",
           (int)(report.never_seen == 0),
           (int)(cb_file_exists("stop_tree/only/sentinel/never.txt") == 1));

    printf("清理 stop_tree（cb_delete_directory_recursively 内部就是后序 walk）-> %d\n",
           (int)cb_delete_directory_recursively("stop_tree"));
    printf("stop_tree 已消失 = %d\n", (int)(cb_file_exists("stop_tree") == 0));
}

static void test_delete_walk_entry(void)
{
    printf("\n== cb_delete_walk_entry：作为 CB_Walk_Func 使用 ==\n");

    size_t entries = 0;
    CB_Walk_Dir_Opt count_opt = CB_ZERO;
    count_opt.data = &entries;
    bool counted = cb_walk_dir_opt("del_tree", walk_count, count_opt);
    printf("先数一遍 del_tree（cb_walk_dir_opt + 计数回调）-> %d，条目数 = %zu\n", (int)counted, entries);

    CB_Walk_Func delete_func = cb_delete_walk_entry;
    CB_Walk_Dir_Opt delete_opt = CB_ZERO;
    delete_opt.post_order = true; // 必须后序：先删文件，再删已经空了的目录
    printf("cb_walk_dir_opt(\"del_tree\", cb_delete_walk_entry, .post_order = true) -> %d\n",
           (int)cb_walk_dir_opt("del_tree", delete_func, delete_opt));

    printf("整棵树都被删除 = %d/%d/%d/%d\n", (int)(cb_file_exists("del_tree") == 0),
           (int)(cb_file_exists("del_tree/one.txt") == 0), (int)(cb_file_exists("del_tree/nested") == 0),
           (int)(cb_file_exists("del_tree/nested/two.txt") == 0));
}

static void test_dir_entry_manual(void)
{
    printf("\n== cb_dir_entry_open / _next / _close：手工遍历 ==\n");

    CB_Dir_Entry dir = CB_ZERO;
    printf("cb_dir_entry_open(\"tree\") -> %d\n", (int)cb_dir_entry_open("tree", &dir));

    CB_DArray names = CB_ZERO;
    size_t total = 0;
    while (cb_dir_entry_next(&dir)) {
        total += 1;
        if (strcmp(dir.name, ".") == 0) continue;
        if (strcmp(dir.name, "..") == 0) continue;
        cb_da_append(&names, cb_temp_strdup(dir.name));
    }
    printf("cb_dir_entry_next 读到 %zu 个名字，过滤 . 与 .. 后剩 %zu 个（这个 API 本身不过滤）\n", total,
           names.count);
    printf("正常读完时 dir.error = %d\n", (int)dir.error);
    cb_dir_entry_close(dir);

    qsort(names.items, names.count, sizeof(const char*), cmp_cstr);
    for (size_t i = 0; i < names.count; ++i) {
        printf("    条目[%zu] = %s\n", i, names.items[i]);
    }
    cb_da_free(names);

    CB_Dir_Entry missing = CB_ZERO;
    bool missing_opened = cb_dir_entry_open("no-such-dir", &missing);
    printf("cb_dir_entry_open(\"no-such-dir\") -> %d，dir.error = %d\n", (int)missing_opened,
           (int)missing.error);
    cb_dir_entry_close(missing);
}

// 根带尾斜杠时不应出现重复分隔符：walk_dir 只拼一个分隔符（与 cb_path_join 的规则一致），
// 所以 "tree/" 这种写法得到的是 "tree/root.txt"，而不是 "tree//root.txt"。
static bool has_double_sep(const char* path)
{
    for (size_t i = 1; path[i] != '\0'; ++i) {
        if (path[i] == path[i - 1] && (path[i] == '/' || path[i] == '\\')) return true;
    }
    return false;
}

static size_t count_double_sep(const Walk_Report* report)
{
    size_t n = 0;
    for (size_t i = 0; i < report->visits.count; ++i) {
        if (has_double_sep(report->visits.items[i].path)) n += 1;
    }
    return n;
}

static void test_walk_trailing_slash(void)
{
    printf("\n== 根带尾斜杠：不产生重复分隔符 ==\n");

    Walk_Report trailing = CB_ZERO;
    printf("cb_walk_dir(\"tree/\", walk_record, .data = &trailing) -> %d\n",
           (int)cb_walk_dir("tree/", walk_record, .data = &trailing));
    print_walk("root = \"tree/\"", &trailing);
    printf("双分隔符条目数 = %zu（期望 0）\n", count_double_sep(&trailing));

    Walk_Report plain = CB_ZERO;
    printf("cb_walk_dir(\"tree\", walk_record, .data = &plain) -> %d\n",
           (int)cb_walk_dir("tree", walk_record, .data = &plain));
    printf("两种根写法访问到的条目数：带斜杠 %zu，不带斜杠 %zu，相同 = %d\n", trailing.visits.count,
           plain.visits.count, (int)(trailing.visits.count == plain.visits.count));

    cb_da_free(trailing.visits);
    cb_da_free(plain.visits);
}

int main(void)
{

    const char* path_dir = "dir";
    const char* path_dir2 = "dir/dir2";
    if (!cb_mkdir_if_not_exists(path_dir)) {
        cb_log(CB_ERROR, "file create failed");
        return -1;
    }
    if (!cb_mkdir_if_not_exists(path_dir2)) {
        cb_log(CB_ERROR, "file create failed");
        return -1;
    }
    const char* path_file1 = "dir/file1";
    const char* path_file2 = "dir/dir2/file2";

    FILE* file1 = fopen(path_file1, "wb");
    FILE* file2 = fopen(path_file2, "wb");
    if (file1 == NULL || file2 == NULL) {
        if (file1 != NULL) fclose(file1);
        if (file2 != NULL) fclose(file2);
        cb_log(CB_ERROR, "file create failed");
        return -1;
    }
    fclose(file1);
    fclose(file2);

    CB_File_Paths output_paths = {0};
    cb_read_entire_dir(path_dir, &output_paths);
    if (output_paths.count <= 0) {
        cb_log(CB_ERROR, "No path has begin read");
        return -1;
    }

    for (size_t i = 0; i < output_paths.count; ++i) {
        printf("output_paths[%zu] = %s\n", i, output_paths.items[i]);
    }

    cb_delete_file(path_file1);
    cb_delete_file(path_file2);
    cb_delete_file(path_dir2);
    cb_delete_file(path_dir);
    free(output_paths.items);

    // ---- 遍历用例的夹具树 ----
    // tree/{root.txt, sub/inner.txt, skip/secret.txt}：skip/ 用来演示 CB_WALK_SKIP
    if (!cb_mkdir_if_not_exists("tree/sub") || !cb_mkdir_if_not_exists("tree/skip")) {
        cb_log(CB_ERROR, "file create failed");
        return -1;
    }
    if (!cb_write_entire_file("tree/root.txt", "root", 4) ||
        !cb_write_entire_file("tree/sub/inner.txt", "inner", 5) ||
        !cb_write_entire_file("tree/skip/secret.txt", "secret", 6)) {
        cb_log(CB_ERROR, "file create failed");
        return -1;
    }

    // stop_tree/only/sentinel/never.txt：每层只有一个子项，所以访问顺序确定
    if (!cb_mkdir_if_not_exists("stop_tree/only/sentinel")) {
        cb_log(CB_ERROR, "file create failed");
        return -1;
    }
    if (!cb_write_entire_file("stop_tree/only/sentinel/never.txt", "never", 5)) {
        cb_log(CB_ERROR, "file create failed");
        return -1;
    }

    // del_tree/{one.txt, nested/two.txt}：交给 cb_delete_walk_entry 整棵删掉
    if (!cb_mkdir_if_not_exists("del_tree/nested")) {
        cb_log(CB_ERROR, "file create failed");
        return -1;
    }
    if (!cb_write_entire_file("del_tree/one.txt", "one", 3) ||
        !cb_write_entire_file("del_tree/nested/two.txt", "two", 3)) {
        cb_log(CB_ERROR, "file create failed");
        return -1;
    }

    test_walk_dir();
    test_walk_trailing_slash();
    test_walk_stop();
    test_delete_walk_entry();
    test_dir_entry_manual();

    bool tree_deleted = cb_delete_directory_recursively("tree");
    printf("\n清理 tree -> %d，tree 已消失 = %d\n", (int)tree_deleted, (int)(cb_file_exists("tree") == 0));

    return 0;
}
