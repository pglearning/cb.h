// The compile-time switches, and the small macros that come with every cb.h build.
//
// Switches are read before including cb.h. CB_IMPLEMENTATION is the only mandatory one: exactly one
// .c file must define it and cb.c plays that role for the library's own tests.
#define CB_IMPLEMENTATION
#define CB_ENABLE_ECHO

// Behaviour switches (all optional):
//   CB_DONT_DELETE_OLD_CB            keep <binary>.old when CB_SELF_REBUILD() swaps the binary
//   CB_TRACE_CMD_RUN_FAIL_LOCATION   print the call site when cb_cmd_run() fails
//   CB_ALLOC_TRACK                   track allocations, see cb_alloc_report()
//   CB_STRIP_PREFIX                  also provide names without the cb_/CB_ prefix (see cb.h end)
//   CB_OOM(size)                     replace the out-of-memory handler
//   CB_WARN_DEPRECATED               let CB_DEPRECATED emit compiler warnings
// Tuning switches (all optional):
//   CB_PATH_MAX, CB_TIMER_MAX_DEPTH, CB_ARENA_REGION_INIT_CAPACITY, CB_ARENA_ALIGN,
//   CB_THREAD_LOCAL, CB_DA_INIT_CAP, CB_BITSET_WORD_BITS, CB_MAP_INIT_CAPACITY,
//   CB_WIN32_ERR_MSG_SIZE, CB_TEMP_CAPACITY, CB_ASSERT, CB_PANIC_BACKTRACE
#include "cb.h"

int main(void)
{
    printf("line ending of this system: %s", CB_LINE_END);
    printf("CB_NANOS_PER_SEC = %llu\n", (unsigned long long)CB_NANOS_PER_SEC);
    printf("CB_PATH_MAX = %d, CB_DA_INIT_CAP = %d, CB_MAP_INIT_CAPACITY = %d\n",
           (int)CB_PATH_MAX, (int)CB_DA_INIT_CAP, (int)CB_MAP_INIT_CAPACITY);

    // Small macros available in every build.
    int numbers[] = {10, 20, 30};
    printf("CB_ARRAY_LEN = %zu, CB_ARRAY_GET(1) = %d\n", CB_ARRAY_LEN(numbers), CB_ARRAY_GET(numbers, 1));

    int zeroed[4] = CB_ZERO; // {0} in C, {} in C++
    printf("CB_ZERO: %d %d %d %d\n", zeroed[0], zeroed[1], zeroed[2], zeroed[3]);

    int a = 1, b = 2;
    cb_swap(int, a, b);
    printf("cb_swap: a = %d, b = %d\n", a, b);

    const char* args[] = {"first", "second"};
    const char** cursor = args; // cb_shift advances a pointer variable, not an array
    size_t arg_count = CB_ARRAY_LEN(args);
    const char* head = cb_shift(cursor, arg_count);
    printf("cb_shift: first = %s, remaining = %zu\n", head, arg_count);

    CB_UNUSED(a); // silences -Wunused-parameter / -Wunused-variable
    return 0;
}
