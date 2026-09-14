// 示例 05：构建 API 全量示例
//
// 把 Cmd / FD / Pipe / Proc / Procs / Chain 的每个公共函数与选项都过一遍，每个小节只做一件事。
//
// 注意：本示例用了一些 POSIX 工具（sh / cat / head / tr / sort / true），
// Windows 原生环境里没有，所以只在 Linux/macOS 上跑它。
//
// 编译运行（在仓库根目录）：
//     cc -o /tmp/ex05 examples/05_build_api.c && /tmp/ex05

#define CB_ENABLE_ECHO
#include "../cb.h"

#define WORK "./build/examples/build_api/"
#define WORK2(n) WORK n

static void title(const char* text)
{
    if (cb_color_enabled()) printf("\n\033[1m== %s ==\033[0m\n", text);
    else printf("\n== %s ==\n", text);
}

// ---------------------------------------------------------------- 1. 基本用法
static void demo_cmd_basic(void)
{
    title("1. CB_Cmd 基本用法：拼命令 + 跑命令");

    CB_Cmd cmd = CB_ZERO;

    // cb_cmd_append 是变参宏，一次可以塞任意多个参数
    cb_cmd_append(&cmd, "echo", "hello", "from", "cmd");
    printf("  参数个数 = %zu\n", cmd.count);
    printf("  cmd.items[0] = %s，cmd.items[3] = %s\n", cmd.items[0], cmd.items[3]);

    // cb_cmd_run 默认会在执行后把 cmd.count 清空，方便复用同一个 cmd
    if (!cb_cmd_run(&cmd)) cb_log(CB_ERROR, "执行失败");
    printf("  执行后 cmd.count = %zu（默认会自动重置，便于复用）\n", cmd.count);

    // .dont_reset 关掉自动重置
    cmd.count = 0;
    cb_cmd_append(&cmd, "true");
    cb_cmd_run(&cmd, .dont_reset = true);
    printf("  用 .dont_reset 跑完后 cmd.count = %zu（保留参数）\n", cmd.count);
    cb_cmd_free(cmd);
}

// ---------------------------------------------------------------- 2. 渲染成字符串
static void demo_cmd_render(void)
{
    title("2. cb_cmd_to_sb：把命令渲染成可读字符串");

    CB_Cmd cmd = CB_ZERO;
    cb_cmd_append(&cmd, "cc", "-Wall", "-o", "my app", "main.c");

    CB_String_Builder sb = CB_ZERO;
    cb_cmd_to_sb(cmd, &sb);
    printf("  渲染结果: " CB_SV_FMT "\n", CB_SV_ARG(cb_sb_to_sv(sb)));

    cb_sb_free(sb);
    cb_cmd_free(cmd);
}

// ---------------------------------------------------------------- 3. 合并与释放
static void demo_cmd_extend_free(void)
{
    title("3. cb_cmd_extend / cb_cmd_free");

    CB_Cmd compile = CB_ZERO;
    CB_Cmd link = CB_ZERO;
    cb_cmd_append(&compile, "cc", "-c", "a.c", "b.c");
    cb_cmd_append(&link, "-o", "app");

    cb_cmd_extend(&compile, &link); // 把 link 的参数接到 compile 后面
    printf("  合并后共 %zu 个参数\n", compile.count);

    CB_String_Builder sb = CB_ZERO;
    cb_cmd_to_sb(compile, &sb);
    printf("  合并结果: " CB_SV_FMT "\n", CB_SV_ARG(cb_sb_to_sv(sb)));
    cb_sb_free(sb);

    cb_cmd_free(compile);
    cb_cmd_free(link);
    printf("  free 之后 items = %s，count = %zu\n", compile.items == NULL ? "NULL" : "非空", compile.count);
}

// ---------------------------------------------------------------- 4. 重定向
static void demo_redirect(void)
{
    title("4. 重定向：.stdin_path / .stdout_path / .stderr_path");

    cb_mkdir_if_not_exists(WORK);
    const char* in_content = "line-b\nline-a\n";
    cb_write_entire_file(WORK "in.txt", in_content, strlen(in_content));

    CB_Cmd cmd = CB_ZERO;
    // stdin 来自文件、stdout 到文件、stderr 也到文件
    cb_cmd_append(&cmd, "sort");
    if (!cb_cmd_run(&cmd,
                    .stdin_path = WORK "in.txt",
                    .stdout_path = WORK "out.txt",
                    .stderr_path = WORK "err.txt")) {
        cb_log(CB_ERROR, "执行失败");
    }

    CB_String_Builder sb = CB_ZERO;
    cb_read_entire_file(WORK "out.txt", &sb);
    printf("  stdout 文件内容（已排序）: " CB_SV_FMT, CB_SV_ARG(cb_sb_to_sv(sb)));
    cb_sb_free(sb);
    printf("  stderr 文件大小 = %zu 字节（sort 没有报错）\n", cb_file_size(WORK "err.txt"));

    cb_cmd_free(cmd);
}

// ---------------------------------------------------------------- 5. 异步与并发
static void demo_async_parallel(void)
{
    title("5. 异步执行：.async + .max_procs + cb_procs_wait_and_reset");

    printf("  本机可用并行度 cb_nprocs() = %d\n", cb_nprocs());

    CB_Cmd cmd = CB_ZERO;
    CB_Procs procs = CB_ZERO;

    // 一次性起 4 个睡眠任务，最多同时 2 个
    for (int i = 0; i < 4; ++i) {
        cmd.count = 0;
        cb_cmd_append(&cmd, "sleep", "0.05");
        if (!cb_cmd_run(&cmd, .async = &procs, .max_procs = 2)) {
            cb_log(CB_ERROR, "启动失败");
            break;
        }
        printf("  起了第 %d 个（当前在跑的进程数 = %zu）\n", i + 1, procs.count);
    }

    // 等全部结束并清空数组（数组内存保留可复用）
    if (!cb_procs_wait_and_reset(&procs)) cb_log(CB_ERROR, "等待失败");
    printf("  全部结束，procs.count = %zu\n", procs.count);

    cb_da_free(procs);
    cb_cmd_free(cmd);
}

// ---------------------------------------------------------------- 6. 单个进程
static void demo_proc_wait(void)
{
    title("6. cb_proc_wait：等某一个进程");

    CB_Cmd cmd = CB_ZERO;
    CB_Procs procs = CB_ZERO;

    cb_cmd_append(&cmd, "true");
    if (cb_cmd_run(&cmd, .async = &procs) && procs.count == 1) {
        printf("  已异步启动，进程句柄 = %d\n", (int)procs.items[0]);
        bool ok = cb_proc_wait(procs.items[0]); // 单独等它
        printf("  cb_proc_wait 返回 %s\n", ok ? "成功" : "失败");
    }
    cb_da_free(procs);
    cb_cmd_free(cmd);
}

// ---------------------------------------------------------------- 7. 文件描述符与管道
static void demo_fd_and_pipe(void)
{
    title("7. cb_fd_open_read / cb_fd_open_write / cb_fd_close / cb_pipe_create");

    cb_mkdir_if_not_exists(WORK);

    CB_FD wfd = cb_fd_open_write(WORK "fd.txt");
    if (wfd != CB_INVALID_FD) {
#ifdef _WIN32
        DWORD written = 0;
        WriteFile(wfd, "via fd\n", 7, &written, NULL);
#else
        ssize_t written = write(wfd, "via fd\n", 7);
        CB_UNUSED(written);
#endif
        cb_fd_close(wfd);
    }
    printf("  用 fd 写出的文件大小 = %zu\n", cb_file_size(WORK "fd.txt"));

    CB_FD rfd = cb_fd_open_read(WORK "fd.txt");
    if (rfd != CB_INVALID_FD) {
        char buf[16] = CB_ZERO;
#ifdef _WIN32
        DWORD got = 0;
        ReadFile(rfd, buf, sizeof(buf) - 1, &got, NULL);
#else
        ssize_t got = read(rfd, buf, sizeof(buf) - 1);
        CB_UNUSED(got);
#endif
        printf("  用 fd 读回: %s", buf);
        cb_fd_close(rfd);
    }

    // 管道：往写端写，从读端读
    CB_Pipe pp = CB_ZERO;
    if (cb_pipe_create(&pp)) {
#ifdef _WIN32
        DWORD n = 0;
        WriteFile(pp.write, "through a pipe", 14, &n, NULL);
        char buf[32] = CB_ZERO;
        ReadFile(pp.read, buf, sizeof(buf) - 1, &n, NULL);
#else
        ssize_t n = write(pp.write, "through a pipe", 14);
        CB_UNUSED(n);
        char buf[32] = CB_ZERO;
        n = read(pp.read, buf, sizeof(buf) - 1);
        CB_UNUSED(n);
#endif
        printf("  管道里读回: %s\n", buf);
        cb_fd_close(pp.read);
        cb_fd_close(pp.write);
    }
}

// ---------------------------------------------------------------- 8. 命令链
static void demo_chain(void)
{
    title("8. cb_chain_*：把多条命令用管道串起来");

    CB_Chain chain = CB_ZERO;
    CB_Cmd cmd = CB_ZERO;

    if (!cb_chain_begin(&chain)) return;

    cb_cmd_append(&cmd, "echo", "chain-through-pipe");
    cb_chain_cmd(&chain, &cmd);

    cb_cmd_append(&cmd, "tr", "a-z", "A-Z");
    cb_chain_cmd(&chain, &cmd);

    cb_cmd_append(&cmd, "head", "-c", "11");
    cb_chain_cmd(&chain, &cmd);

    if (!cb_chain_end(&chain, .stdout_path = WORK "chain_out.txt")) {
        cb_log(CB_ERROR, "命令链失败");
    }
    printf("  等价于 echo | tr | head > 文件\n");

    CB_String_Builder sb = CB_ZERO;
    cb_read_entire_file(WORK "chain_out.txt", &sb);
    printf("  结果: " CB_SV_FMT "\n", CB_SV_ARG(cb_sb_to_sv(sb)));
    cb_sb_free(sb);

    // 带 stdin 起点、以及把某一段的 stderr 并进 stdout
    if (cb_chain_begin(&chain, .stdin_path = WORK "in.txt")) {
        cb_cmd_append(&cmd, "cat");
        cb_chain_cmd(&chain, &cmd, .err2out = true);
        cb_cmd_append(&cmd, "sort");
        cb_chain_cmd(&chain, &cmd);
        cb_chain_end(&chain, .stdout_path = WORK "chain2.txt");

        sb = (CB_String_Builder)CB_ZERO;
        cb_read_entire_file(WORK "chain2.txt", &sb);
        printf("  带 stdin 起点的结果: " CB_SV_FMT, CB_SV_ARG(cb_sb_to_sv(sb)));
        cb_sb_free(sb);
    }

    cb_da_free(chain.cmd);
    cb_cmd_free(cmd);
}

// ---------------------------------------------------------------- 8.5 Chain 的 _opt 版本
static void demo_chain_opt(void)
{
    title("8.5 cb_chain_*_opt：需要显式构造选项时用它们");

    // cb_chain_begin/cmd/end 都是宏，展开后就是这三个 _opt 函数
    CB_Chain chain = CB_ZERO;
    CB_Cmd cmd = CB_ZERO;

    CB_Chain_Begin_Opt begin_opt = CB_ZERO;
    begin_opt.stdin_path = NULL; // 也可以给一个输入文件

    CB_Chain_Cmd_Opt cmd_opt = CB_ZERO;
    cmd_opt.err2out = false;
    cmd_opt.dont_reset = false;

    CB_Chain_End_Opt end_opt = CB_ZERO;
    end_opt.stdout_path = WORK "chain_opt.txt";
    end_opt.stderr_path = NULL;
    end_opt.async = NULL;
    end_opt.max_procs = 0;

    if (cb_chain_begin_opt(&chain, begin_opt)) {
        cb_cmd_append(&cmd, "echo", "via-opt");
        cb_chain_cmd_opt(&chain, &cmd, cmd_opt);
        cb_cmd_append(&cmd, "tr", "a-z", "A-Z");
        cb_chain_cmd_opt(&chain, &cmd, cmd_opt);
        cb_chain_end_opt(&chain, end_opt);

        CB_String_Builder sb = CB_ZERO;
        cb_read_entire_file(WORK "chain_opt.txt", &sb);
        printf("  结果: " CB_SV_FMT "\n", CB_SV_ARG(cb_sb_to_sv(sb)));
        cb_sb_free(sb);
    }
    cb_da_free(chain.cmd);
    cb_cmd_free(cmd);
}

// ---------------------------------------------------------------- 8.6 cb_procs_wait
static void demo_procs_wait(void)
{
    title("8.6 cb_procs_wait：等待全部进程（不清空数组）");

    CB_Cmd cmd = CB_ZERO;
    CB_Procs procs = CB_ZERO;

    for (int i = 0; i < 2; ++i) {
        cmd.count = 0;
        cb_cmd_append(&cmd, "true");
        cb_cmd_run(&cmd, .async = &procs);
    }
    printf("  起了 %zu 个进程\n", procs.count);

    bool ok = cb_procs_wait(procs); // 只等，不清空
    printf("  cb_procs_wait = %s，数组仍有 %zu 个句柄（需自己清）\n",
           ok ? "成功" : "失败", procs.count);

    procs.count = 0; // 或者用 cb_procs_wait_and_reset(&procs) 一步到位
    cb_da_free(procs);
    cb_cmd_free(cmd);
}

// ---------------------------------------------------------------- 8.7 cb_cc_* 编译命令抽象
static void demo_cc_helpers(void)
{
    title("8.7 cb_cc / cb_cc_flags / cb_cc_output / cb_cc_inputs：编译器抽象");

    printf("  这四个宏把「用哪个编译器、加哪些警告、怎么指定输出/输入」抽象出来，\n");
    printf("  会按当前平台与编译器自动选参数（MSVC 用 /W4，其余用 -Wall 等）。\n\n");

    CB_Cmd cmd = CB_ZERO;
    cb_cc(&cmd);                                     // 编译器
    cb_cc_flags(&cmd);                               // 警告等标志
    cb_cc_output(&cmd, WORK "cc_helper_demo");       // 输出
    cb_cc_inputs(&cmd, "examples/01_hello.c");       // 输入

    CB_String_Builder sb = CB_ZERO;
    cb_cmd_to_sb(cmd, &sb);
    printf("  在本平台上展开成: " CB_SV_FMT "\n", CB_SV_ARG(cb_sb_to_sv(sb)));
    cb_sb_free(sb);

    printf("  真正执行它（顺便验证四个宏拼出来的命令确实能编译）：\n");
    bool ok = cb_cmd_run(&cmd);
    printf("  编译结果 = %s\n", ok ? "成功" : "失败");
    cb_cmd_free(cmd);
}

// ---------------------------------------------------------------- 9. 增量判断
static void demo_needs_rebuild(void)
{
    title("9. cb_needs_rebuild：增量构建的基础");

    cb_mkdir_if_not_exists(WORK);
    const char* src_content = "int main(void){return 0;}\n";
    cb_write_entire_file(WORK "src.c", src_content, strlen(src_content));

    const char* one[] = {WORK "src.c"};

    // 目标不存在 -> 必须重建
    printf("  目标不存在时 cb_needs_rebuild = %d（1 表示需要重建）\n",
           cb_needs_rebuild(WORK "no-such-bin", one, CB_ARRAY_LEN(one)));

    // 源文件不存在 -> 报错（这里预期会失败，临时把日志静音，免得刷屏）
    {
        CB_Log_Level saved = cb_minimal_log_level;
        cb_minimal_log_level = CB_NO_LOGS;
        const char* missing[] = {WORK "no-such-src.c"};
        int r = cb_needs_rebuild(WORK "src.c", missing, CB_ARRAY_LEN(missing));
        cb_minimal_log_level = saved;
        printf("  源不存在时   cb_needs_rebuild = %d（-1 表示出错）\n", r);
    }

    // 真的编译一次，然后比较时间戳
    CB_Cmd cmd = CB_ZERO;
    cb_cmd_append(&cmd, "cc", "-o", WORK "bin", WORK "src.c");
    if (cb_cmd_run(&cmd)) {
        printf("  编译后 cb_needs_rebuild = %d（0 表示已是最新）\n",
               cb_needs_rebuild(WORK "bin", one, CB_ARRAY_LEN(one)));

        // 多个依赖：依赖有几个就往数组里放几个
        const char* many[] = {WORK "src.c", WORK "src.c"};
        printf("  两个依赖（同一个文件算两次，仍然是最新）cb_needs_rebuild = %d\n",
               cb_needs_rebuild(WORK "bin", many, CB_ARRAY_LEN(many)));
    }
    cb_cmd_free(cmd);
}

// ---------------------------------------------------------------- 10. 自重建宏
static void demo_self_rebuild(void)
{
    title("10. CB_SELF_REBUILD / CB_SELF_REBUILD_PLUS");

    // CB_SELF_REBUILD 展开成一组"用哪个编译器、怎么编译"的参数
    CB_Cmd cmd = CB_ZERO;
    cb_cmd_append(&cmd, CB_SELF_REBUILD("out_bin", "examples/05_build_api.c"));

    CB_String_Builder sb = CB_ZERO;
    cb_cmd_to_sb(cmd, &sb);
    printf("  CB_SELF_REBUILD 展开为: " CB_SV_FMT "\n", CB_SV_ARG(cb_sb_to_sv(sb)));
    cb_sb_free(sb);
    cb_cmd_free(cmd);

    printf("  CB_SELF_REBUILD_PLUS(argc, argv, ...) 会：\n");
    printf("    1) 比较可执行文件与列出的源文件的修改时间\n");
    printf("    2) 需要时重新编译自己并重新执行（路径相对当前工作目录）\n");
    printf("    3) 本示例的 main 开头就调用了它，只是当前总是最新的，所以没触发\n");
}

int main(void)
{
    // 不缓冲 stdout：这样 printf 与走 stderr 的日志在终端里的顺序才自然
    setvbuf(stdout, NULL, _IONBF, 0);
    cb_set_log_handler(cb_default_log_handler);

    demo_cmd_basic();
    demo_cmd_render();
    demo_cmd_extend_free();
    demo_redirect();
    demo_async_parallel();
    demo_proc_wait();
    demo_fd_and_pipe();
    demo_chain();
    demo_chain_opt();
    demo_procs_wait();
    demo_cc_helpers();
    demo_needs_rebuild();
    demo_self_rebuild();

    printf("\n全部构建 API 示例执行完毕\n");
    return 0;
}
