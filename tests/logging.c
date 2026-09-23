#include "test_diagnostics.h"
#include "cb.h"

#include <signal.h> // SIGABRT, the panic APIs abort and are only reachable in a forked child

// The golden captures stdout only, so this file prints every observation itself. The built-in log
// handlers (cb_default_log_handler / cb_cancer_log_handler) and cb__panicf write to stderr:
// the sections below redirect stderr into a file and reprint the interesting part, which keeps
// paths, addresses and backtraces out of the golden.

static size_t handler_calls = 0;

static const char* strip_dir(const char* path)
{
    const char* base = path;
    for (const char* p = path; *p != '\0'; ++p) {
        if (*p == '/' || *p == '\\') base = p + 1;
    }
    return base;
}

// The names a handler switches on, printed so the CB_Log_Level values are visible in the golden.
static const char* level_name(CB_Log_Level level)
{
    switch (level) {
    case CB_INFO: return "CB_INFO";
    case CB_WARN: return "CB_WARN";
    case CB_ERROR: return "CB_ERROR";
    case CB_NO_LOGS: return "CB_NO_LOGS";
    }
    return "unknown";
}

// __FILE__ depends on how this test was compiled ("./tests/logging.c" from the runner,
// "tests/logging.c" from a direct cc run), so only its basename is ever printed.
static const char* display_file(const char* file)
{
    if (file == NULL) return "(none)";
    if (strcmp(file, __FILE__) == 0) return strip_dir(file);
    return file;
}

// A custom CB_Log_Handler: printing to stdout is the only way to make cb_log/cb_log_at visible to
// the golden. It deliberately ignores cb_minimal_log_level, because filtering is the built-in
// handlers' job (see "cb_minimal_log_level is read by the handlers, not by cb_log" below).
static void stdout_log_handler(CB_Log_Level level, const char* file, int line, const char* fmt, va_list args)
{
    handler_calls += 1;
    printf("  | level=%d (%s) file=%s line=%d msg=", (int)level, level_name(level), display_file(file), line);
    vprintf(fmt, args);
    printf("\n");
}

// Point stderr at a file. The next stderr_to() call closes it again, so it can then be read back.
static bool stderr_to(const char* path)
{
    fflush(stdout);
    fflush(stderr);
    if (freopen(path, "w", stderr) == NULL) return false;
    setvbuf(stderr, NULL, _IONBF, 0);
    return true;
}

// Reprint a captured stderr file. max_lines == 0 means "every line"; mask_file replaces __FILE__
// with its basename, because panic messages start with a file:line prefix.
static size_t dump_capture(const char* path, bool mask_file, size_t max_lines)
{
    char line[512];
    FILE* f = fopen(path, "r");
    size_t count = 0;
    if (f == NULL) {
        printf("  | (capture file missing)\n");
        return 0;
    }
    while (fgets(line, (int)sizeof(line), f) != NULL) {
        if (max_lines != 0 && count >= max_lines) break;
        if (mask_file) {
            const char* hit = strstr(line, __FILE__);
            if (hit != NULL) {
                printf("  | ");
                fwrite(line, 1, (size_t)(hit - line), stdout);
                printf("%s%s", strip_dir(__FILE__), hit + strlen(__FILE__));
                count += 1;
                continue;
            }
        }
        printf("  | %s", line);
        count += 1;
    }
    fclose(f);
    return count;
}

// Close the capture file, then reprint what the built-in handler wrote to stderr.
static size_t capture_dump(const char* path, bool mask_file, size_t max_lines)
{
    if (!stderr_to("logging_scratch.txt")) printf("  | (could not close the capture file)\n");
    size_t lines = dump_capture(path, mask_file, max_lines);
    printf("  -> %zu line(s) captured from stderr\n", lines);
    return lines;
}

static void test_handler_plumbing(void)
{
    printf("\n== cb_set_log_handler / cb_get_log_handler / cb_log_handler ==\n");

    printf("cb_get_log_handler() == cb_default_log_handler -> %d\n",
           (int)(cb_get_log_handler() == cb_default_log_handler));
    printf("cb_log_handler == cb_get_log_handler() -> %d\n", (int)(cb_log_handler == cb_get_log_handler()));
    printf("cb_cancer_log_handler != cb_null_log_handler -> %d\n",
           (int)(cb_cancer_log_handler != cb_null_log_handler));
    printf("cb_default_log_handler != cb_null_log_handler -> %d\n",
           (int)(cb_default_log_handler != cb_null_log_handler));

    cb_set_log_handler(stdout_log_handler);
    printf("cb_set_log_handler(stdout_log_handler) -> cb_get_log_handler() == stdout_log_handler -> %d\n",
           (int)(cb_get_log_handler() == stdout_log_handler));

    // CB_Log_Handler is the handler function type; the setters store a pointer to one.
    CB_Log_Handler* custom = stdout_log_handler;
    cb_set_log_handler(custom);
    printf("CB_Log_Handler* custom = stdout_log_handler; cb_set_log_handler(custom) -> cb_get_log_handler() == "
           "custom -> %d\n",
           (int)(cb_get_log_handler() == custom));

    printf("cb_log(CB_INFO, \"info %%d\", 1):\n");
    cb_log(CB_INFO, "info %d", 1);
    printf("cb_log(CB_WARN, \"warn %%s\", \"two\"):\n");
    cb_log(CB_WARN, "warn %s", "two");
    printf("cb_log(CB_ERROR, \"error %%.2f\", 3.5):\n");
    cb_log(CB_ERROR, "error %.2f", 3.5);
    printf("cb_log(CB_NO_LOGS, \"no-logs %%d\", 4):\n");
    cb_log(CB_NO_LOGS, "no-logs %d", 4);

    // cb_log() passes file == NULL/line == 0, cb_log_at() passes the location through unchanged.
    printf("cb_log_at(CB_ERROR, \"config/module.c\", 42, \"bad value %%d\", 7):\n");
    cb_log_at(CB_ERROR, "config/module.c", 42, "bad value %d", 7);
    printf("CB_LOG_AT(CB_WARN, \"from macro %%s\", \"here\") (__FILE__/__LINE__ added by the macro):\n");
    CB_LOG_AT(CB_WARN, "from macro %s", "here");

    printf("handler calls = %zu\n", handler_calls);

    // The handler is the only thing that filters: cb_log itself forwards every level, including
    // CB_NO_LOGS. cb_minimal_log_level is read inside the built-in handlers, further down.
    printf("\n== cb_minimal_log_level is read by the handlers, not by cb_log ==\n");
    cb_minimal_log_level = CB_ERROR;
    printf("cb_minimal_log_level = CB_ERROR, a custom handler still receives everything:\n");
    cb_log(CB_INFO, "CB_INFO is delivered to a custom handler");
    cb_log(CB_NO_LOGS, "CB_NO_LOGS too");
    cb_minimal_log_level = CB_INFO;
    printf("handler calls = %zu\n", handler_calls);

    // Direct use of the exported global: cb_set_log_handler() is just an assignment to it.
    cb_log_handler = stdout_log_handler;
    printf("cb_log_handler = stdout_log_handler; cb_log(CB_WARN, \"via the global\"):\n");
    cb_log(CB_WARN, "via the global");
    printf("cb_log_handler == cb_get_log_handler() -> %d\n", (int)(cb_log_handler == cb_get_log_handler()));

    cb_set_log_handler(cb_null_log_handler);
    printf("cb_set_log_handler(cb_null_log_handler) -> cb_get_log_handler() == cb_null_log_handler -> %d\n",
           (int)(cb_get_log_handler() == cb_null_log_handler));
    printf("cb_log(CB_ERROR, \"discarded by the null handler\"):\n");
    cb_log(CB_ERROR, "discarded by the null handler");
    printf("(no \"  | \" line above: cb_null_log_handler prints nothing)\n");
}

static void test_default_handler(void)
{
    printf("\n== cb_default_log_handler: prefixes and cb_minimal_log_level ==\n");

    cb_set_log_handler(cb_default_log_handler);
    printf("cb_set_log_handler(cb_default_log_handler) -> cb_get_log_handler() == cb_default_log_handler -> %d\n",
           (int)(cb_get_log_handler() == cb_default_log_handler));

    cb_minimal_log_level = CB_INFO;
    if (!stderr_to("logging_capture.txt")) printf("  | (could not redirect stderr)\n");
    printf("cb_minimal_log_level = CB_INFO, then cb_log(CB_INFO/CB_WARN/CB_ERROR/CB_NO_LOGS, ...):\n");
    cb_log(CB_INFO, "info message %d", 1);
    cb_log(CB_WARN, "warn message %s", "two");
    cb_log(CB_ERROR, "error message %.1f", 3.5);
    cb_log(CB_NO_LOGS, "CB_NO_LOGS is never printed, whatever cb_minimal_log_level is");
    capture_dump("logging_capture.txt", false, 0);

    cb_minimal_log_level = CB_WARN;
    if (!stderr_to("logging_capture.txt")) printf("  | (could not redirect stderr)\n");
    printf("cb_minimal_log_level = CB_WARN, same four calls:\n");
    cb_log(CB_INFO, "info message %d", 1);
    cb_log(CB_WARN, "warn message %s", "two");
    cb_log(CB_ERROR, "error message %.1f", 3.5);
    cb_log(CB_NO_LOGS, "CB_NO_LOGS is never printed, whatever cb_minimal_log_level is");
    capture_dump("logging_capture.txt", false, 0);

    cb_minimal_log_level = CB_ERROR;
    if (!stderr_to("logging_capture.txt")) printf("  | (could not redirect stderr)\n");
    printf("cb_minimal_log_level = CB_ERROR, same four calls:\n");
    cb_log(CB_INFO, "info message %d", 1);
    cb_log(CB_WARN, "warn message %s", "two");
    cb_log(CB_ERROR, "error message %.1f", 3.5);
    cb_log(CB_NO_LOGS, "CB_NO_LOGS is never printed, whatever cb_minimal_log_level is");
    capture_dump("logging_capture.txt", false, 0);

    cb_minimal_log_level = CB_NO_LOGS;
    if (!stderr_to("logging_capture.txt")) printf("  | (could not redirect stderr)\n");
    printf("cb_minimal_log_level = CB_NO_LOGS, same four calls:\n");
    cb_log(CB_INFO, "info message %d", 1);
    cb_log(CB_WARN, "warn message %s", "two");
    cb_log(CB_ERROR, "error message %.1f", 3.5);
    cb_log(CB_NO_LOGS, "CB_NO_LOGS is never printed, whatever cb_minimal_log_level is");
    capture_dump("logging_capture.txt", false, 0);

    cb_minimal_log_level = CB_INFO;
    printf("cb_minimal_log_level = CB_NO_LOGS silences every level; restored to CB_INFO\n");

    // With a location: cb_log_at() and CB_LOG_AT() print "prefix file:line: message".
    if (!stderr_to("logging_capture.txt")) printf("  | (could not redirect stderr)\n");
    printf("cb_log_at(CB_WARN, \"config/module.c\", 42, \"bad value %%d\", 7):\n");
    cb_log_at(CB_WARN, "config/module.c", 42, "bad value %d", 7);
    capture_dump("logging_capture.txt", false, 0);

    if (!stderr_to("logging_capture.txt")) printf("  | (could not redirect stderr)\n");
    printf("CB_LOG_AT(CB_ERROR, \"from macro %%s\", \"here\"):\n");
    CB_LOG_AT(CB_ERROR, "from macro %s", "here");
    // The file part of the line is __FILE__: mask the directory to stay machine-independent.
    capture_dump("logging_capture.txt", true, 0);
}

static void test_cancer_and_null_handlers(void)
{
    printf("\n== cb_cancer_log_handler: emoji/colors only on a terminal ==\n");

    cb_set_log_handler(cb_cancer_log_handler);
    printf("cb_set_log_handler(cb_cancer_log_handler) -> cb_get_log_handler() == cb_cancer_log_handler -> %d\n",
           (int)(cb_get_log_handler() == cb_cancer_log_handler));
    cb_minimal_log_level = CB_INFO;
    if (!stderr_to("logging_capture.txt")) printf("  | (could not redirect stderr)\n");
    printf("stderr is a file, so isatty(stderr) is 0; cb_log(CB_INFO/CB_WARN/CB_ERROR/CB_NO_LOGS, ...):\n");
    cb_log(CB_INFO, "info message %d", 1);
    cb_log(CB_WARN, "warn message %s", "two");
    cb_log(CB_ERROR, "error message %.1f", 3.5);
    cb_log(CB_NO_LOGS, "CB_NO_LOGS is never printed, whatever cb_minimal_log_level is");
    capture_dump("logging_capture.txt", false, 0);
    printf("(redirected, the cancer handler produces the same plain prefixes as the default one)\n");

    printf("\n== cb_null_log_handler ==\n");
    cb_set_log_handler(cb_null_log_handler);
    printf("cb_set_log_handler(cb_null_log_handler) -> cb_get_log_handler() == cb_null_log_handler -> %d\n",
           (int)(cb_get_log_handler() == cb_null_log_handler));
    if (!stderr_to("logging_capture.txt")) printf("  | (could not redirect stderr)\n");
    printf("cb_log(CB_INFO, ...), cb_log(CB_ERROR, ...), cb_log_at(CB_ERROR, \"config.c\", 7, ...):\n");
    cb_log(CB_INFO, "info message %d", 1);
    cb_log(CB_ERROR, "error message %.1f", 3.5);
    cb_log_at(CB_ERROR, "config.c", 7, "located error message");
    capture_dump("logging_capture.txt", false, 0);

    cb_set_log_handler(cb_default_log_handler);
    cb_minimal_log_level = CB_INFO;
    printf("cb_set_log_handler(cb_default_log_handler); cb_minimal_log_level = CB_INFO\n");
}

#ifndef _WIN32

typedef enum {
    PANIC_EXPLICIT,    // cb__panicf() with an explicit file/line
    PANIC_TODO,        // CB_TODO() adds __FILE__/__LINE__
    PANIC_UNREACHABLE, // CB_UNREACHABLE() adds __FILE__/__LINE__
} Panic_Kind;

// Every panic API aborts, so it runs in a forked child and the parent reports the exit status.
static int panic_child(Panic_Kind kind, const char* capture_path)
{
    fflush(stdout);
    fflush(stderr);
    pid_t pid = fork();
    if (pid == 0) {
        if (freopen(capture_path, "w", stderr) == NULL) _exit(90);
        setvbuf(stderr, NULL, _IONBF, 0);
        switch (kind) {
        case PANIC_EXPLICIT:
            cb__panicf("panic_demo.c", 77, "TODO", "explicit panic %d", 3);
            break;
        case PANIC_TODO:
            CB_TODO("not implemented yet: %s", "feature");
            break;
        case PANIC_UNREACHABLE:
            CB_UNREACHABLE("this branch must never run");
            break;
        }
        _exit(91); // not reached: all three abort
    }
    if (pid < 0) return -1;
    {
        int status = 0;
        if (waitpid(pid, &status, 0) < 0) return -1;
        return status;
    }
}

static void report_panic(const char* call, Panic_Kind kind, const char* capture_path)
{
    printf("%s:\n", call);
    int status = panic_child(kind, capture_path);
    if (status < 0) {
        printf("  fork failed, no child status\n");
        return;
    }
    // Only the first line of the child's stderr is stable: everything after it is a backtrace.
    dump_capture(capture_path, true, 1);
    printf("  child died by signal = %d, signal == SIGABRT = %d\n", WIFSIGNALED(status) ? 1 : 0,
           (WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT) ? 1 : 0);
#if CB_PANIC_BACKTRACE
    printf("  (the child's stderr continues with a backtrace: not part of the golden)\n");
#endif
}

static void test_panic_apis(void)
{
    printf("\n== cb__panicf / CB_TODO / CB_UNREACHABLE (each in a forked child) ==\n");
    report_panic("cb__panicf(\"panic_demo.c\", 77, \"TODO\", \"explicit panic %d\", 3)", PANIC_EXPLICIT,
                 "logging_panic_explicit.txt");
    report_panic("CB_TODO(\"not implemented yet: %s\", \"feature\")", PANIC_TODO, "logging_panic_todo.txt");
    report_panic("CB_UNREACHABLE(\"this branch must never run\")", PANIC_UNREACHABLE,
                 "logging_panic_unreachable.txt");
}

#else // _WIN32

static void test_panic_apis(void)
{
    printf("\n== cb__panicf / CB_TODO / CB_UNREACHABLE ==\n");
    printf("cb__panicf(\"panic_demo.c\", 77, \"TODO\", \"explicit panic %%d\", 3) -> skipped on this platform "
           "(no fork())\n");
    printf("CB_TODO(\"not implemented yet: %%s\", \"feature\") -> skipped on this platform (no fork())\n");
    printf("CB_UNREACHABLE(\"this branch must never run\") -> skipped on this platform (no fork())\n");
}

#endif // _WIN32

int main(void)
{
    test_handler_plumbing();
    test_default_handler();
    test_cancer_and_null_handlers();
    test_panic_apis();

    return 0;
}
