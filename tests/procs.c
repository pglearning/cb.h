#include "test_diagnostics.h"
#include "cb.h"

// 起一条必然成功退出、且不向 stdout 打印任何内容的命令：
// POSIX 用 true，Windows 用 cmd /c exit 0（与 tests/nob_parity.c 一致）。
static void append_noop_cmd(CB_Cmd* cmd)
{
#ifdef _WIN32
    cb_cmd_append(cmd, "cmd", "/c", "exit", "0");
#else
    cb_cmd_append(cmd, "true");
#endif
}

// cb.h 只提供 fd 的打开/关闭；字节读写是平台调用（POSIX 的 read/write，
// Windows 的 ReadFile/WriteFile）。Windows 分支只打印一行 skipped，
// 这样 Windows 的 golden 也是确定的。
static void test_fd_file(void)
{
    printf("\n== cb_fd_open_write / cb_fd_open_read / cb_fd_close ==\n");

    // CB_FD 在 POSIX 下是 int、Windows 下是 HANDLE：只和哨兵值比较，不打印数值本身。
    CB_FD fd = CB_INVALID_FD;
    printf("CB_FD fd = CB_INVALID_FD; fd == CB_INVALID_FD -> %d\n", (int)(fd == CB_INVALID_FD));

    fd = cb_fd_open_write("fd.txt");
    printf("cb_fd_open_write(\"fd.txt\") != CB_INVALID_FD -> %d\n", (int)(fd != CB_INVALID_FD));

#ifndef _WIN32
    long long written = (long long)write(fd, "abc", 3);
    printf("write(fd, \"abc\", 3) -> %lld\n", written);
#endif
    cb_fd_close(fd);

#ifndef _WIN32
    printf("cb_fd_close(fd) 之后 fd.txt 的字节数 = %zu\n", cb_file_size("fd.txt"));
#else
    printf("Windows：CB_FD 是 HANDLE，字节读写要用 WriteFile/ReadFile，这里只验证打开/关闭\n");
#endif

    fd = cb_fd_open_read("fd.txt");
    printf("cb_fd_open_read(\"fd.txt\") != CB_INVALID_FD -> %d\n", (int)(fd != CB_INVALID_FD));

#ifndef _WIN32
    char buf[8] = CB_ZERO;
    long long got = (long long)read(fd, buf, 3);
    printf("read(fd, buf, 3) -> %lld，读回 = |%.*s|\n", got, (int)(got > 0 ? got : 0), buf);
#endif
    cb_fd_close(fd);

    {
        // 打不开的文件返回哨兵值；错误日志走 stderr（golden 看不见），这里顺手静音。
        CB_Log_Level saved = cb_minimal_log_level;
        cb_minimal_log_level = CB_NO_LOGS;
        fd = cb_fd_open_read("no-such-file.txt");
        cb_minimal_log_level = saved;
    }
    printf("cb_fd_open_read(\"no-such-file.txt\") == CB_INVALID_FD -> %d\n", (int)(fd == CB_INVALID_FD));
}

static void test_pipe(void)
{
    printf("\n== cb_pipe_create / CB_Pipe ==\n");

    CB_Pipe pp = CB_ZERO;
    printf("cb_pipe_create(&pp) -> %d\n", (int)cb_pipe_create(&pp));
    printf("pp.read 与 pp.write 都不等于 CB_INVALID_FD -> %d\n",
           (int)(pp.read != CB_INVALID_FD && pp.write != CB_INVALID_FD));

#ifndef _WIN32
    long long written = (long long)write(pp.write, "abc", 3);
    char buf[8] = CB_ZERO;
    long long got = (long long)read(pp.read, buf, 3);
    printf("write(pp.write, \"abc\", 3) -> %lld，read(pp.read, buf, 3) -> %lld，读回 = |%.*s|\n", written,
           got, (int)(got > 0 ? got : 0), buf);
#else
    printf("Windows：管道读写要用 WriteFile/ReadFile，跳过字节往返\n");
#endif

    cb_fd_close(pp.read);
    cb_fd_close(pp.write);
    printf("cb_fd_close(pp.read) 与 cb_fd_close(pp.write) 已调用（fd 数值不打印）\n");
}

static void test_proc_wait(void)
{
    printf("\n== cb_proc_wait / cb__proc_wait_async / CB_INVALID_PROC ==\n");

    CB_Proc invalid = CB_INVALID_PROC;
    printf("CB_Proc invalid = CB_INVALID_PROC; invalid == CB_INVALID_PROC -> %d\n",
           (int)(invalid == CB_INVALID_PROC));
    printf("cb_proc_wait(CB_INVALID_PROC) -> %d（无效进程直接返回 false）\n",
           (int)cb_proc_wait(CB_INVALID_PROC));
    printf("cb__proc_wait_async(CB_INVALID_PROC, 0) -> %d（无效进程直接返回 0）\n",
           cb__proc_wait_async(CB_INVALID_PROC, 0));

    CB_Cmd cmd = CB_ZERO;
    append_noop_cmd(&cmd);

    // 直接拿原始 CB_Proc：cb__cmd_start_process 不等待，cb_proc_wait 才等待。
    CB_Proc proc = cb__cmd_start_process(cmd, NULL, NULL, NULL);
    printf("cb__cmd_start_process(cmd, NULL, NULL, NULL) != CB_INVALID_PROC -> %d\n",
           (int)(proc != CB_INVALID_PROC));

    // cb__proc_wait_async 是轮询：子进程还没退出时返回 0，退出且退出码为 0 时返回 1。
    // 轮询几次取决于调度，所以只打印最终有没有拿到 1。
    int ret = 0;
    for (int i = 0; i < 1000000 && ret == 0; ++i) ret = cb__proc_wait_async(proc, 0);
    printf("轮询 cb__proc_wait_async(proc, 0) 直到返回 1 -> %d\n", (int)(ret == 1));

    CB_Proc proc2 = cb__cmd_start_process(cmd, NULL, NULL, NULL);
    printf("cb__cmd_start_process 起第二个进程 != CB_INVALID_PROC -> %d\n", (int)(proc2 != CB_INVALID_PROC));
    printf("cb_proc_wait(proc2) 阻塞到进程退出并返回退出码 0 -> %d\n", (int)cb_proc_wait(proc2));

    cb_cmd_free(cmd);
}

static void test_procs_throttle(void)
{
    printf("\n== cb_cmd_run(.async, .max_procs) / cb_procs_wait / CB_Procs ==\n");

    CB_Procs procs = CB_ZERO;
    CB_Cmd cmd = CB_ZERO;

    // max_procs = 1：run 之前会先调 cb__proc_wait_async 回收已结束的进程，
    // 所以第 2、3 条命令要等前一条退出才会启动，数组里始终只有 1 个进程。
    for (int i = 1; i <= 3; ++i) {
        append_noop_cmd(&cmd);
        bool ok = cb_cmd_run(&cmd, .async = &procs, .max_procs = 1);
        printf("cb_cmd_run(.async = &procs, .max_procs = 1) #%d -> %d，procs.count = %zu\n", i, (int)ok,
               procs.count);
    }

    printf("cb_procs_wait(procs) -> %d（收尾剩下的 1 个）\n", (int)cb_procs_wait(procs));
    printf("cb_procs_wait 之后 procs.count = %zu（只等待，不重置数组）\n", procs.count);
    cb_da_free(procs);

    // max_procs = 4：3 条命令都没到上限，于是全部并行启动，count 累积到 3。
    CB_Procs many = CB_ZERO;
    for (int i = 1; i <= 3; ++i) {
        append_noop_cmd(&cmd);
        bool ok = cb_cmd_run(&cmd, .async = &many, .max_procs = 4);
        printf("cb_cmd_run(.async = &many, .max_procs = 4) #%d -> %d，many.count = %zu\n", i, (int)ok,
               many.count);
    }
    printf("cb_procs_wait(many) -> %d\n", (int)cb_procs_wait(many));
    cb_da_free(many);

    // cb_nprocs() 是机器核数（不是进程池计数），各机器不同，所以只断言下界。
    printf("cb_nprocs()（可并行度，按 CPU 核数）>= 1 -> %d\n", (int)(cb_nprocs() >= 1));

    cb_cmd_free(cmd);
}

int main(void)
{
    test_fd_file();
    test_pipe();
    test_proc_wait();
    test_procs_throttle();
    return 0;
}
