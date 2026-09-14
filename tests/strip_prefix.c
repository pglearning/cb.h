// CB_STRIP_PREFIX 实测：定义它之后，公共接口可以不带 cb_/CB_ 前缀使用。
// 同时是一道编译期断言：有 11 个名字刻意不生成别名（去掉前缀会和标准库 / POSIX /
// 系统库 / C++ 撞车，判定依据见 cb.h 末尾别名区的注释），谁把它们加进别名区，编译就直接失败。
#define CB_STRIP_PREFIX
#include "test_diagnostics.h"
#include "cb.h"

// 版本号防漂移：CB_VERSION_STRING 由这三个数字拼出，改了数字就必须同步这里，
// 否则编译直接失败（顺便提醒：升级版本时记得看一遍 docs 与 README 里有没有提到版本）。
#if CB_VERSION_MAJOR != 1 || CB_VERSION_MINOR != 1 || CB_VERSION_PATCH != 0
#error "CB_VERSION_* 变了：请同步这条断言"
#endif

// 下面这些名字在标准库 / 系统头里已经存在，全部保留 cb_ 前缀、别名区不处理它们
#ifdef log
#error "log 不该有别名：会和 libm 的 log() 冲突"
#endif
#ifdef rename
#error "rename 不该有别名：会和 stdio.h 的 rename() 冲突"
#endif
// min / max 只在非 Windows 上断言：<windows.h> 自己就会定义这两个宏
#if !defined(_WIN32)
#ifdef min
#error "min 不该有别名：会和 Windows 的 min 宏 / C++ std::min 冲突"
#endif
#ifdef max
#error "max 不该有别名：会和 Windows 的 max 宏 / C++ std::max 冲突"
#endif
#endif
#ifdef clamp
#error "clamp 不该有别名：会和 C++ std::clamp 冲突"
#endif
#ifdef glob
#error "glob 不该有别名：会和 POSIX glob.h 的 glob() 冲突"
#endif
#ifdef rotl64
#error "rotl64 不该有别名：会和 glibc 的 rotl64 冲突"
#endif
#ifdef rotr64
#error "rotr64 不该有别名：会和 glibc 的 rotr64 冲突"
#endif
#ifdef swap
#error "swap 不该有别名：会和 C++ std::swap 冲突"
#endif
// ERROR 只在非 Windows 上断言：<wingdi.h> 自己会定义 `#define ERROR 0`
#if !defined(_WIN32)
#ifdef ERROR
#error "ERROR 不该有别名：会和 mingw <wingdi.h> 的 ERROR 冲突"
#endif
#endif

// 风格：纯 printf + golden 比对；上面的 #error 是编译期断言，不进运行时输出。
int main(void)
{
    // ---------------------------------------------------------- 类型与宏
    printf("\n== 类型 / 宏 / 常量 ==\n");
    {
        String_View sv = SVLIT("a/b/c.txt");
        printf("String_View + SVLIT 可用（类型名去掉了 CB_ 前缀）: count = %zu\n", sv.count);

        String_Builder sb = ZERO;
        sb_append_cstr(&sb, "hello");
        sb_append(&sb, ' ');
        sb_append_cstr(&sb, "world");
        // sb 的 count 不含结尾的 '\0'（items[count] 就是那个 '\0'），所以是 11
        printf("String_Builder 可用: count = %zu, items = |%s|\n", sb.count, sb.items);
        sb_free(sb);

        printf("ARRAY_LEN(\"abc\") = %zu\n", (size_t)ARRAY_LEN("abc"));
        // ERROR 已被系统头占用（mingw 的 wingdi.h），所以没有别名，只能写带前缀的 CB_ERROR
        printf("日志级别常量可用（ERROR 因系统头占用而保留前缀）: INFO = %d, WARN = %d, "
               "CB_ERROR = %d, NO_LOGS = %d\n",
               (int)INFO, (int)WARN, (int)CB_ERROR, (int)NO_LOGS);
    }

    // ---------------------------------------------------------- 字符串
    printf("\n== 字符串 / 路径 ==\n");
    {
        String_View sv = SVLIT("a/b/c.txt");
        String_View head = sv_chop_by_delim(&sv, '/');
        printf("sv_chop_by_delim / sv_eq 可用: head = |" SV_FMT "|, sv = |" SV_FMT "|, "
               "sv_eq(head, \"a\") = %d, sv_eq(sv, \"b/c.txt\") = %d\n",
               SV_ARG(head), SV_ARG(sv), (int)sv_eq(head, SVLIT("a")),
               (int)sv_eq(sv, SVLIT("b/c.txt")));

        // path_join 产出的是本机原生分隔符，所以期望值按平台写
        char* joined = path_join("dir", "file.txt");
#ifdef _WIN32
        printf("path_join 可用（Windows 用反斜杠）: %s\n", joined);
#else
        printf("path_join 可用: %s\n", joined);
#endif

        char* ext = path_replace_ext("a/b/c.txt", "md");
        printf("path_replace_ext 可用: %s\n", ext);
    }

    // ---------------------------------------------------------- 容器
    printf("\n== 容器 ==\n");
    {
        // CB_DArray 的元素类型就是 const char*（见 cb.h），所以这里塞字符串
        DArray da = ZERO;
        da_append(&da, "one");
        da_append(&da, "two");
        da_append(&da, "three");
        printf("DArray / da_append 可用: count = %zu, items[0] = %s, items[2] = %s\n",
               da.count, da.items[0], da.items[2]);
        da_free(da);

        Map m = ZERO;
        map_init_capacity(&m, 8);
        map_put_cstr(&m, "k", (void*)(intptr_t)7);
        void* v = NULL;
        // 先取返回值再打印：同一 printf 里既写 v 又读 v 是未定序的
        int cb__found = (int)map_get_cstr(&m, "k", &v);
        printf("Map / map_put_cstr / map_get_cstr 可用: get(\"k\") = %d, v = %d\n",
               cb__found, (int)(intptr_t)v);
        map_free(&m);
    }

    // ---------------------------------------------------------- 内存
    printf("\n== 内存：temp / arena ==\n");
    {
        char* t = temp_sprintf("%s-%d", "temp", 42);
        printf("temp_sprintf 可用: %s\n", t);

        Arena_Mark mark = temp_save();
        temp_strdup("abc");
        temp_rewind(mark);
        printf("temp_save / temp_rewind 可用: mark.count = %zu\n", mark.count);

        Arena a = ZERO;
        int* xs = (int*)arena_alloc(&a, sizeof(int) * 4);
        xs[3] = 9;
        printf("Arena / arena_alloc 可用: xs[3] = %d\n", xs[3]);
        arena_free(&a);
    }

    // ---------------------------------------------------------- 文件系统
    printf("\n== 文件系统 ==\n");
    {
        printf("mkdir_if_not_exists 递归建目录 = %d\n",
               (int)mkdir_if_not_exists("strip_work/a/b"));
        const char* content = "payload";
        printf("write_entire_file 可用 = %d\n",
               (int)write_entire_file("strip_work/a/b/f.txt", content, strlen(content)));

        String_Builder rb = ZERO;
        int read_ok = (int)read_entire_file("strip_work/a/b/f.txt", &rb);
        printf("read_entire_file 可用 = %d, 读回内容 = |%s|, 与写入一致 = %d\n", read_ok,
               read_ok ? rb.items : "", (int)(read_ok && strcmp(rb.items, content) == 0));
        sb_free(rb);

        printf("get_file_type 可用 = %d, FILE_REGULAR = %d\n",
               (int)get_file_type("strip_work/a/b/f.txt"), (int)FILE_REGULAR);
        printf("delete_directory_recursively 可用 = %d\n",
               (int)delete_directory_recursively("strip_work"));
    }

    // ---------------------------------------------------------- 命令
    printf("\n== Cmd / 构建 API ==\n");
    {
        Cmd cmd = ZERO;
        cmd_append(&cmd, "echo", "strip-prefix-ok");
        printf("Cmd / cmd_append 可用: count = %zu, items[0] = %s, items[1] = %s\n",
               cmd.count, cmd.items[0], cmd.items[1]);
        cmd_free(cmd);

        const char* deps[] = {"cb.h"};
        // 目标不存在 -> 需要重建，结果是 1（不打印耗时，golden 才稳定）
        printf("needs_rebuild 可用（目标不存在 -> 需要重建） = %d\n",
               needs_rebuild("no-such-bin", deps, ARRAY_LEN(deps)));
    }

    // ------------------------------------------------ 带前缀的名字仍然可用
    printf("\n== 带前缀的名字没有被破坏 ==\n");
    {
        CB_String_View sv = CB_SVLIT("both");
        printf("cb_ 前缀的名字照样能用（别名是增量，不是替换）: cb_sv_eq(sv, sv) = %d\n",
               (int)cb_sv_eq(sv, sv));
        printf("cb_min 仍是带前缀的名字（没有别名 min）: cb_min(3, 5) = %d\n", cb_min(3, 5));
    }

    return 0;
}
