// The tracked allocator is opt-in and changes every allocation in this translation unit, so this
// is the only test that defines CB_ALLOC_TRACK; it has to come before test_diagnostics.h, which
// defines CB_IMPLEMENTATION.
#define CB_ALLOC_TRACK

// A custom CB_OOM replaces the default cb__oom(file, line) target; cb.h documents this override
// form for taking over the OOM path. The declaration needs size_t, hence <stddef.h> here.
#include <stddef.h>
static void test_oom_handler(size_t requested_size);
#define CB_OOM(size) test_oom_handler(size)

#include "test_diagnostics.h"
#include "cb.h"

#include <signal.h> // SIGABRT, the OOM hooks abort and are only reachable in a forked child

// Everything below prints to stdout: the golden captures stdout only. cb_alloc_report() writes to
// stderr, so that section redirects stderr into a file and reprints it.

// The replacement OOM handler must not return: the caller keeps using the NULL pointer afterwards.
// abort() does not flush stdio, so stdout is flushed first to keep the message in the golden.
static void test_oom_handler(size_t requested_size)
{
    printf("  | custom CB_OOM handler: requested %zu bytes, aborting\n", requested_size);
    fflush(stdout);
    abort();
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

static size_t dump_capture(const char* path)
{
    char line[512];
    FILE* f = fopen(path, "r");
    size_t count = 0;
    if (f == NULL) {
        printf("  | (capture file missing)\n");
        return 0;
    }
    while (fgets(line, (int)sizeof(line), f) != NULL) {
        printf("  | %s", line);
        count += 1;
    }
    fclose(f);
    return count;
}

static void test_tracked_functions(void)
{
    printf("\n== cb__tracked_realloc / cb__tracked_free: what CB_REALLOC/CB_FREE expand to ==\n");

    void* block = cb__tracked_realloc(NULL, 64, "alloc_demo.c", 11);
    printf("cb__tracked_realloc(NULL, 64, \"alloc_demo.c\", 11) -> non-NULL = %d\n", (int)(block != NULL));
    printf("cb_alloc_live_count() = %zu, cb_alloc_live_size() = %zu\n", cb_alloc_live_count(), cb_alloc_live_size());
    printf("cb__alloc_live_count = %zu, cb__alloc_live_size = %zu, cb__alloc_peak_size = %zu, cb__alloc_total_count = %zu\n",
           cb__alloc_live_count, cb__alloc_live_size, cb__alloc_peak_size, cb__alloc_total_count);

    // The user pointer sits exactly one CB__Alloc_Record header behind cb__alloc_head.
    CB__Alloc_Record* head = cb__alloc_head;
    printf("cb__alloc_head points at a CB__Alloc_Record: ->size = %zu, ->file = %s, ->line = %d, ->prev == NULL = %d\n",
           head->size, head->file, head->line, (int)(head->prev == NULL));
    printf("the returned pointer is the header plus sizeof(CB__Alloc_Record) -> %d\n",
           (int)((char*)block - (char*)head == (ptrdiff_t)sizeof(CB__Alloc_Record)));

    // cb__tracked_free() marks file/line as unused: the free site is not recorded.
    cb__tracked_free(block, "alloc_demo.c", 12);
    printf("cb__tracked_free(block, \"alloc_demo.c\", 12) -> cb_alloc_live_count() = %zu, cb_alloc_live_size() = %zu\n",
           cb_alloc_live_count(), cb_alloc_live_size());
    printf("peak stays %zu; cb__alloc_head == NULL -> %d\n", cb__alloc_peak_size, (int)(cb__alloc_head == NULL));
}

static void test_realloc_free_macros(void)
{
    printf("\n== CB_REALLOC / CB_FREE (the __FILE__/__LINE__ form) ==\n");

    void* p = CB_REALLOC(NULL, 100);
    printf("CB_REALLOC(NULL, 100) -> non-NULL = %d, live_count = %zu, live_size = %zu\n", (int)(p != NULL),
           cb_alloc_live_count(), cb_alloc_live_size());

    p = CB_REALLOC(p, 300);
    printf("CB_REALLOC(p, 300) -> non-NULL = %d, live_count = %zu, live_size = %zu (grown in place or moved)\n",
           (int)(p != NULL), cb_alloc_live_count(), cb_alloc_live_size());
    printf("peak = %zu, cb__alloc_head->size = %zu (the record tracks the new size)\n", cb__alloc_peak_size,
           cb__alloc_head->size);

    CB_FREE(p);
    printf("CB_FREE(p) -> live_count = %zu, live_size = %zu, peak stays %zu, total_count = %zu\n",
           cb_alloc_live_count(), cb_alloc_live_size(), cb__alloc_peak_size, cb__alloc_total_count);

    // realloc(p, 0) behaves like free; realloc(NULL, 0) still has to return a usable pointer.
    void* zero = CB_REALLOC(NULL, 0);
    printf("CB_REALLOC(NULL, 0) -> non-NULL = %d, live_size = %zu (a 0-byte request is stored as 1)\n",
           (int)(zero != NULL), cb_alloc_live_size());
    void* freed = CB_REALLOC(zero, 0);
    printf("CB_REALLOC(zero, 0) -> returned NULL = %d and freed the block: live_count = %zu, live_size = %zu\n",
           (int)(freed == NULL), cb_alloc_live_count(), cb_alloc_live_size());
}

static void test_raw_layer(void)
{
    printf("\n== CB_REALLOC_RAW / CB_FREE_RAW: the untracked bottom layer ==\n");

    size_t count_before = cb_alloc_live_count();
    size_t size_before = cb_alloc_live_size();
    size_t total_before = cb__alloc_total_count;
    void* raw = CB_REALLOC_RAW(NULL, 32);
    printf("CB_REALLOC_RAW(NULL, 32) -> non-NULL = %d\n", (int)(raw != NULL));
    printf("live_count = %zu (unchanged = %d), live_size = %zu (unchanged = %d), total_count = %zu (unchanged = %d)\n",
           cb_alloc_live_count(), (int)(cb_alloc_live_count() == count_before), cb_alloc_live_size(),
           (int)(cb_alloc_live_size() == size_before), cb__alloc_total_count,
           (int)(cb__alloc_total_count == total_before));
    CB_FREE_RAW(raw);
    printf("CB_FREE_RAW(raw) -> live_count = %zu, live_size = %zu\n", cb_alloc_live_count(), cb_alloc_live_size());
}

static void test_alloc_report(void)
{
    printf("\n== cb_alloc_report (stderr captured) ==\n");

    // One live block, so the report has a LEAK line with the recorded allocation site.
    void* leak = cb__tracked_realloc(NULL, 48, "alloc_demo.c", 77);
    printf("cb__tracked_realloc(NULL, 48, \"alloc_demo.c\", 77) -> non-NULL = %d, live_count = %zu\n",
           (int)(leak != NULL), cb_alloc_live_count());

    if (!stderr_to("alloc_report.txt")) printf("  | (could not redirect stderr)\n");
    cb_alloc_report();
    if (!stderr_to("alloc_scratch.txt")) printf("  | (could not close the report file)\n");
    printf("cb_alloc_report() wrote to stderr:\n");
    printf("  -> %zu line(s)\n", dump_capture("alloc_report.txt"));

    cb__tracked_free(leak, "alloc_demo.c", 78);
    printf("cb__tracked_free(leak, \"alloc_demo.c\", 78) -> live_count = %zu, live_size = %zu\n",
           cb_alloc_live_count(), cb_alloc_live_size());
}

static void test_containers(void)
{
    printf("\n== containers allocate through the tracked layer ==\n");

    CB_DArray da = CB_ZERO;
    size_t count_before = cb_alloc_live_count();
    size_t size_before = cb_alloc_live_size();
    size_t peak_before = cb__alloc_peak_size;
    cb_da_append(&da, "seven");
    printf("cb_da_append(&da, \"seven\") -> count = %zu, capacity = %zu, items[0] = %s\n", da.count, da.capacity,
           da.items[0]);
    printf("live_count %zu -> %zu (the first reserve allocates the whole capacity at once)\n", count_before,
           cb_alloc_live_count());
    printf("live_size delta == capacity * sizeof(*da.items) -> %d\n",
           (int)(cb_alloc_live_size() - size_before == da.capacity * sizeof(*da.items)));
    printf("peak before the containers = %zu, raised by the container allocation -> %d\n", peak_before,
           (int)(cb__alloc_peak_size > peak_before));
    cb_da_free(da);
    printf("cb_da_free(da) -> live_count = %zu, live_size = %zu\n", cb_alloc_live_count(), cb_alloc_live_size());

    CB_Map m = CB_ZERO;
    int value = 7;
    count_before = cb_alloc_live_count();
    size_before = cb_alloc_live_size();
    cb_map_put_cstr(&m, "key", &value);
    printf("cb_map_put_cstr(&m, \"key\", &value) -> count = %zu, capacity = %zu\n", m.count, m.capacity);
    printf("live_count %zu -> %zu (one block for the slots, one for the copied key)\n", count_before,
           cb_alloc_live_count());
    printf("live_size delta == capacity * sizeof(m.slots[0]) + strlen(\"key\") + 1 -> %d\n",
           (int)(cb_alloc_live_size() - size_before == m.capacity * sizeof(m.slots[0]) + strlen("key") + 1));
    cb_map_free(&m);
    printf("cb_map_free(&m) -> live_count = %zu, live_size = %zu\n", cb_alloc_live_count(), cb_alloc_live_size());
}

#ifndef _WIN32

typedef enum {
    OOM_CUSTOM_HANDLER, // cb_alloc_check(NULL, n) -> CB_OOM(n) -> test_oom_handler
    OOM_DEFAULT_HOOK,   // cb__oom() called directly, the default CB_OOM target
} Oom_Kind;

// Both OOM paths abort, so they run in a forked child and the parent reports the exit status.
static int oom_child(Oom_Kind kind, const char* capture_path)
{
    fflush(stdout);
    fflush(stderr);
    pid_t pid = fork();
    if (pid == 0) {
        if (freopen(capture_path, "w", stderr) == NULL) _exit(90);
        setvbuf(stderr, NULL, _IONBF, 0);
        switch (kind) {
        case OOM_CUSTOM_HANDLER:
            cb_alloc_check(NULL, 64); // a real allocation failure is not reproducible on demand
            break;
        case OOM_DEFAULT_HOOK:
            cb__oom(1024 * 1024 * 1024, "alloc_demo.c", 9);
            break;
        }
        _exit(91); // not reached: both hooks abort
    }
    if (pid < 0) return -1;
    {
        int status = 0;
        if (waitpid(pid, &status, 0) < 0) return -1;
        return status;
    }
}

#endif // !_WIN32

static void test_oom_paths(void)
{
    printf("\n== cb_alloc_check, a custom CB_OOM and cb__oom ==\n");

    void* checked = cb__tracked_realloc(NULL, 16, "alloc_demo.c", 91);
    cb_alloc_check(checked, 16); // non-NULL: the check is a no-op
    printf("cb_alloc_check(non-NULL, 16) -> returned normally, live_count = %zu, live_size = %zu\n",
           cb_alloc_live_count(), cb_alloc_live_size());
    cb__tracked_free(checked, "alloc_demo.c", 92);
    printf("cb__tracked_free(checked, \"alloc_demo.c\", 92) -> live_count = %zu, live_size = %zu\n",
           cb_alloc_live_count(), cb_alloc_live_size());

#ifndef _WIN32
    printf("cb_alloc_check(NULL, 64) -> CB_OOM(64) -> test_oom_handler, in a forked child:\n");
    int status = oom_child(OOM_CUSTOM_HANDLER, "alloc_oom_custom.txt");
    if (status < 0) {
        printf("  fork failed, no child status\n");
    } else {
        printf("  child died by signal = %d, signal == SIGABRT = %d\n", WIFSIGNALED(status) ? 1 : 0,
               (WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT) ? 1 : 0);
    }

    printf("cb__oom(1 GiB, \"alloc_demo.c\", 9), the default CB_OOM target, in a forked child:\n");
    status = oom_child(OOM_DEFAULT_HOOK, "alloc_oom_default.txt");
    if (status < 0) {
        printf("  fork failed, no child status\n");
    } else {
        printf("  -> %zu line(s) captured from the child's stderr\n", dump_capture("alloc_oom_default.txt"));
        printf("  child died by signal = %d, signal == SIGABRT = %d\n", WIFSIGNALED(status) ? 1 : 0,
               (WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT) ? 1 : 0);
    }
#else
    CB_UNUSED(test_oom_handler); // declared as CB_OOM: unused when there is no fork()
    printf("cb_alloc_check(NULL, 64) -> CB_OOM(64) -> test_oom_handler: skipped on this platform (no fork())\n");
    printf("cb__oom(1 GiB, \"alloc_demo.c\", 9), the default CB_OOM target: skipped on this platform (no fork())\n");
#endif // !_WIN32
}

int main(void)
{
    printf("== CB_ALLOC_TRACK: every allocation goes through the tracking layer ==\n");
    printf("before anything: cb_alloc_live_count() = %zu, cb_alloc_live_size() = %zu, peak = %zu, total_count = %zu\n",
           cb_alloc_live_count(), cb_alloc_live_size(), cb__alloc_peak_size, cb__alloc_total_count);
    printf("cb__alloc_head == NULL -> %d\n", (int)(cb__alloc_head == NULL));

    test_tracked_functions();
    test_realloc_free_macros();
    test_raw_layer();
    test_alloc_report();
    test_containers();
    test_oom_paths();

    printf("\n== final state: every block released ==\n");
    printf("cb_alloc_live_count() = %zu, cb_alloc_live_size() = %zu, total_count = %zu\n", cb_alloc_live_count(),
           cb_alloc_live_size(), cb__alloc_total_count);
    printf("cb__alloc_head == NULL -> %d\n", (int)(cb__alloc_head == NULL));

    return 0;
}
