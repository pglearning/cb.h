// 示例 03：多项目构建
//
// 一个构建脚本管理多个子项目，并处理它们之间的依赖关系。
// 主体是做法 A：在代码里显式列出项目表（清晰、可控、依赖顺序一目了然）；
// 后半段是做法 B：用 cb_walk_dir 遍历目录自动发现子项目。
//
// 示例在运行时生成这棵项目树，libmath 被 app 依赖：
//     build/examples/multi/libmath/src/   静态库
//     build/examples/multi/app/src/       可执行程序，链接 libmath
//     build/examples/multi/tool/src/      另一个独立可执行程序
//     build/examples/multi/build/         所有产物
//
// 编译运行（在仓库根目录）：
//     cc -o /tmp/ex03 examples/03_multi_project.c && /tmp/ex03
//     /tmp/ex03 clean

#define CB_ENABLE_ECHO
#include "../cb.h"

#define ROOT "./build/examples/multi/"
#define OUT ROOT "build/"

// ---------------------------------------------------------------- 项目表（做法 A）
typedef struct {
    const char* name;
    const char* dir;              // 相对 ROOT 的项目目录
    const char* const* sources;   // 源文件名（不含目录前缀）
    size_t source_count;
    bool is_static_lib;           // true: 产出 .a；false: 产出可执行文件
    const char* const* deps;      // 依赖的静态库名（不含 lib 前缀与 .a 后缀）
    size_t dep_count;
} Project;

static const char* math_sources[] = {"math.c"};
static const char* app_sources[] = {"main.c"};
static const char* tool_sources[] = {"tool.c"};

static const char* app_deps[] = {"math"};

// 顺序就是构建顺序：被依赖的排在前面
static const Project projects[] = {
    {"math", "libmath/", math_sources, CB_ARRAY_LEN(math_sources), true, NULL, 0},
    {"app", "app/", app_sources, CB_ARRAY_LEN(app_sources), false, app_deps, CB_ARRAY_LEN(app_deps)},
    {"tool", "tool/", tool_sources, CB_ARRAY_LEN(tool_sources), false, NULL, 0},
};
#define project_count CB_ARRAY_LEN(projects)

// 依赖是按"项目名"引用的，这里查回它的目录
static const Project* find_project(const char* name)
{
    for (size_t i = 0; i < project_count; ++i) {
        if (strcmp(projects[i].name, name) == 0) return &projects[i];
    }
    return NULL;
}

// ---------------------------------------------------------------- 生成演示项目
static bool generate_demo_tree(void)
{
    bool result = true;
    CB_String_Builder sb = CB_ZERO;

    struct {
        const char* path;
        const char* content;
    } files[] = {
        {ROOT "libmath/src/math.h",
         "#ifndef MATH_H\n#define MATH_H\nint math_square(int x);\n#endif\n"},
        {ROOT "libmath/src/math.c",
         "#include \"math.h\"\nint math_square(int x) { return x * x; }\n"},
        {ROOT "app/src/main.c",
         "#include <stdio.h>\n#include \"math.h\"\n"
         "int main(void) { printf(\"7 squared = %d\\n\", math_square(7)); return 0; }\n"},
        {ROOT "tool/src/tool.c",
         "#include <stdio.h>\nint main(void) { printf(\"this is the tool\\n\"); return 0; }\n"},
    };

    for (size_t i = 0; i < CB_ARRAY_LEN(files); ++i) {
        const char* dir = cb_path_normalize(cb_path_join(files[i].path, ".."));
        if (!cb_mkdir_if_not_exists(dir)) cb_return_defer(false);
        cb_sb_append_cstr(&sb, files[i].content);
        if (!cb_write_entire_file(files[i].path, sb.items, sb.count)) cb_return_defer(false);
        sb.count = 0;
    }

defer:
    cb_sb_free(sb);
    return result;
}

// ---------------------------------------------------------------- 构建一个项目
static bool build_project(const Project* p)
{
    bool result = true;
    CB_Cmd cmd = CB_ZERO;
    CB_Procs procs = CB_ZERO;
    CB_File_Paths objects = CB_ZERO;
    const char* proj_dir = cb_temp_sprintf("%s%s", ROOT, p->dir);

    if (!cb_mkdir_if_not_exists(OUT)) cb_return_defer(false);

    // 1) 并行编译这个项目的所有源文件
    for (size_t i = 0; i < p->source_count; ++i) {
        const char* src = cb_temp_sprintf("%ssrc/%s", proj_dir, p->sources[i]);
        // 目标名带上项目名，避免不同项目同名源文件互相覆盖
        const char* obj = cb_temp_sprintf("%s%s_%s.o", OUT, p->name, p->sources[i]);
        cb_da_append(&objects, (const char*)obj);

        const char* deps[] = {src};
        int needs = cb_needs_rebuild(obj, deps, CB_ARRAY_LEN(deps));
        if (needs < 0) cb_return_defer(false);
        if (needs == 0) continue;

        cmd.count = 0;
        cb_cmd_append(&cmd, "cc", "-Wall", "-Wextra", "-c", "-o", obj, src,
                      cb_temp_sprintf("-I%ssrc", proj_dir));
        // 依赖了别的库，就把那些库的头文件目录也带上
        for (size_t d = 0; d < p->dep_count; ++d) {
            const Project* dep = find_project(p->deps[d]);
            CB_ASSERT(dep != NULL && "依赖的项目名写错了");
            cb_cmd_append(&cmd, cb_temp_sprintf("-I%s%s/src", ROOT, dep->dir));
        }
        if (!cb_cmd_run(&cmd, .async = &procs, .max_procs = (size_t)cb_nprocs())) {
            cb_return_defer(false);
        }
    }
    if (!cb_procs_wait_and_reset(&procs)) cb_return_defer(false);

    // 2) 链接或打包
    if (p->is_static_lib) {
        const char* lib = cb_temp_sprintf("%slib%s.a", OUT, p->name);
        int needs = cb_needs_rebuild(lib, objects.items, objects.count);
        if (needs < 0) cb_return_defer(false);
        if (needs != 0) {
            cmd.count = 0;
            cb_cmd_append(&cmd, "ar", "rcs", lib);
            // CB_Cmd 本身就是 DA 兼容结构体，追加一个数组用 cb_da_append_many
            cb_da_append_many(&cmd, objects.items, objects.count);
            if (!cb_cmd_run(&cmd)) cb_return_defer(false);
        }
        cb_log(CB_INFO, "[%s] 静态库 -> %s", p->name, lib);
    } else {
        const char* exe = cb_temp_sprintf("%s%s", OUT, p->name);
        cmd.count = 0;
        cb_cmd_append(&cmd, "cc", "-o", exe);
        cb_da_append_many(&cmd, objects.items, objects.count);
        // 链接依赖的静态库，注意顺序：被依赖的放后面
        for (size_t d = 0; d < p->dep_count; ++d) {
            cb_cmd_append(&cmd, cb_temp_sprintf("%slib%s.a", OUT, p->deps[d]));
        }
        if (!cb_cmd_run(&cmd)) cb_return_defer(false);
        cb_log(CB_INFO, "[%s] 可执行 -> %s", p->name, exe);
    }

defer:
    cb_cmd_free(cmd);
    cb_da_free(procs);
    cb_da_free(objects);
    return result;
}

// ---------------------------------------------------------------- 做法 B：遍历发现
// 适合项目很多、且每个子目录都自带构建脚本的场合：遍历一级子目录，逐个进去跑它的构建脚本。
static bool visit_project_dirs(CB_Walk_Entry entry)
{
    if (entry.type != CB_FILE_DIRECTORY) return true;
    if (entry.level != 1) return true; // 只看第一层子目录

    cb_log(CB_INFO, "发现子项目目录: %s", entry.path);

    // 真实场景里通常在这里 cb_set_current_dir(entry.path) 再跑 ./build。
    // 这里只演示"发现"，所以让 walk 跳过这个目录的子项。
    *entry.action = CB_WALK_SKIP;
    return true;
}

int main(int argc, char** argv)
{
    CB_SELF_REBUILD_PLUS(argc, argv, "cb.h");
    cb_set_log_handler(cb_default_log_handler);

    const char* program = cb_shift(argv, argc);
    const char* command = argc > 0 ? cb_shift(argv, argc) : "build";

    if (strcmp(command, "build") == 0) {
        if (!generate_demo_tree()) return 1;

        // 做法 A：按表逐个构建（顺序即依赖顺序）
        for (size_t i = 0; i < project_count; ++i) {
            cb_log(CB_INFO, "=== 构建项目 %s ===", projects[i].name);
            if (!build_project(&projects[i])) return 1;
        }

        // 做法 B：演示用 walk_dir 发现子项目目录
        cb_log(CB_INFO, "（做法 B）遍历项目根目录发现子项目：");
        bool ok = cb_walk_dir(cb_path_normalize(ROOT), visit_project_dirs);
        if (!ok) return 1;

        // 跑一下产物
        cb_log(CB_INFO, "（产物）");
        CB_Cmd run = CB_ZERO;
        cb_cmd_append(&run, OUT "app");
        if (!cb_cmd_run(&run)) { cb_cmd_free(run); return 1; }
        run.count = 0;
        cb_cmd_append(&run, OUT "tool");
        ok = cb_cmd_run(&run);
        cb_cmd_free(run);
        return ok ? 0 : 1;
    }

    if (strcmp(command, "clean") == 0) {
        if (!cb_file_exists(ROOT)) {
            cb_log(CB_INFO, "没有需要清理的东西");
            return 0;
        }
        return cb_delete_directory_recursively(ROOT) ? 0 : 1;
    }

    printf("用法: %s [build|clean]\n", program);
    return 0;
}
