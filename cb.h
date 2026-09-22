////////////////////////////////////////////////////////////////////////////////////////////////////
//  NOTE:
//      - stb-style single header: define CB_IMPLEMENTATION in exactly one .c before including
//        cb.h; every other .c just includes it and sees the declarations.
//      - Layout: config macros -> platform headers -> general macros -> types, then the sections.
//        Every section holds its declarations followed by its own #ifdef CB_IMPLEMENTATION block,
//        so a section may only use what the sections above it declare.
//      - Baseline: C99 and C++11. cb.h defines no feature-test macro, exactly like nob.h: build
//        with the compiler default dialect or with -std=gnu99, and pass -D_POSIX_C_SOURCE=200112L
//        yourself when you compile with strict -std=c99 (otherwise lstat/readlink/clock_gettime/
//        nanosleep/PATH_MAX stay undeclared).
//
//  Compile switches (define before #include "cb.h"; every one has an #ifndef guard):
//      CB_ENABLE_ECHO                  print fs/cmd operations as they run
//      CB_DONT_DELETE_OLD_CB           keep the previous binary when self-rebuilding
//      CB_TRACE_CMD_RUN_FAIL_LOCATION  print the call site when cb_cmd_run() fails
//      CB_ALLOC_TRACK                  track every allocation, see cb_alloc_report()
//      CB_STRIP_PREFIX                 generate cb_-less aliases (end of this file)
//      CB_IMPLEMENTATION               emit the definitions (exactly one .c defines it)
//      CB_OOM(size)                    replace the default out-of-memory handler
//      CB_WARN_DEPRECATED              make CB_DEPRECATED warn
//      CB_PATH_MAX                     path buffer size, default PATH_MAX or 4096
//      CB_TIMER_MAX_DEPTH              timer nesting limit, default 64
//      CB_ARENA_REGION_INIT_CAPACITY   first arena block, default 64KB
//      CB_ARENA_ALIGN                  arena allocation alignment, default CB__MAX_ALIGN
//      CB_THREAD_LOCAL                 thread-local specifier for the temp storage
//      CB_DA_INIT_CAP                  dynamic array first capacity, default 256
//      CB_BITSET_WORD_BITS             bitset word size, default 64
//      CB_MAP_INIT_CAPACITY            hash map initial buckets, default 16
//      CB_WIN32_ERR_MSG_SIZE           Win32 error buffer, default 4KB
//      CB_REALLOC / CB_FREE / CB_REALLOC_RAW / CB_FREE_RAW
//      CB_TEMP_CAPACITY / CB_ASSERT / CB_PANIC_BACKTRACE
//
//  Other macros: CB_ARRAY_LEN / CB_ARRAY_GET / CB_UNUSED / CB_ZERO / CB_LINE_END / CB_CLIT /
//      CB_DECLTYPE_CAST / cb_shift / cb_swap / CB_TODO / CB_UNREACHABLE / CB_DEPRECATED / CB_LOG_AT /
//      CB_NANOS_PER_SEC.
//
//  Rules that change how you call the API:
//      - Arena and temp memory are released as a whole (cb_arena_reset / cb_arena_free),
//        never per allocation.
//      - C++: cb_return_defer() needs every declaration to appear before the first goto.
//      - cb_da_* macros accept any struct with items/count/capacity and are not type checked.
//      - C: cb_min/cb_max/cb_clamp evaluate each argument twice outside GCC/Clang; C++ is templated.
//      - Allocation failure aborts through CB_OOM; fallible APIs still return false.
////////////////////////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////////////////////////
// Sections (search the title to jump; every section starts with a row of ////):
//      Platform Header / General / Allocator / Arena + Temp Storage / Logger + Panic / Timer /
//      Dynamic Array / Bitset / Ring Buffer / Sort / StringBuilder / StringView / UTF-8 Support /
//      StringView Tools / HashMap / HashMap (uint64 keys) / File System / Path /
//      File System Extras / Math + Bits / Time + Date / Random / Runtime Environment / Hex Dump /
//      CLI Args / Process + FD / Cmd / Cmd Chain / Build Flags / C Builder / CB_STRIP_PREFIX
////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef CB_H_
#define CB_H_
#ifdef _WIN32
// Must come before any other header, otherwise MSVC/mingw warn about fopen/strcpy and friends.
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS 1
#endif /* _CRT_SECURE_NO_WARNINGS */
#endif /* _WIN32 */

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <limits.h>
#include <time.h>

////////////////////////////////////////////////////////////////////////////////////////////////////
// Platform Header
////////////////////////////////////////////////////////////////////////////////////////////////////
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
// Trim the parts of windows.h we do not use (the classic MinGW trick): keep these two guards
// only. _WINCON_ must not be defined, it removes the console API (cb_terminal_width needs
// GetConsoleScreenBufferInfo, cb.c needs SetConsoleOutputCP). _WINGDI_ must not be defined
// either: it removes wingdi.h, which the console headers depend on for LF_FACESIZE.
#define _WINUSER_
#define _IMM_
// Order matters: windows.h comes first, direct.h / io.h / shellapi.h need its macros.
#include <windows.h>
#include <direct.h>
#include <io.h>
#include <shellapi.h>
#include <winioctl.h> // FSCTL_GET_REPARSE_POINT, used to read symlink targets
#else
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#ifdef __FreeBSD__
#include <sys/sysctl.h>
#endif
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mman.h>  // mmap, for zero-copy reads of large files
#include <sys/ioctl.h> // ioctl(TIOCGWINSZ), for the terminal width
#endif

#ifdef __HAIKU__
#include <image.h>
#endif

////////////////////////////////////////////////////////////////////////////////////////////////////
// General
////////////////////////////////////////////////////////////////////////////////////////////////////
// Goes before every declaration and definition. Define CBDEF as `static inline` when cb.h is
// used from a single file and you want unused functions dropped by the compiler.
#ifndef CBDEF
#define CBDEF
#endif /* CBDEF */

#ifndef CB_ASSERT
#include <assert.h>
#define CB_ASSERT assert
#endif // !CB_ASSERT

// printf-style format-string checking at compile time.
#if defined(__GNUC__) || defined(__clang__)
#ifdef __MINGW_PRINTF_FORMAT
#define CB_PRINTF_FORMAT(STRING_INDEX, FIRST_TO_CHECK) __attribute__((format(__MINGW_PRINTF_FORMAT, STRING_INDEX, FIRST_TO_CHECK)))
#else
#define CB_PRINTF_FORMAT(STRING_INDEX, FIRST_TO_CHECK) __attribute__((format(printf, STRING_INDEX, FIRST_TO_CHECK)))
#endif // __MINGW_PRINTF_FORMAT
#else
// MSVC C mode has no equivalent attribute, so this degrades to nothing.
#define CB_PRINTF_FORMAT(STRING_INDEX, FIRST_TO_CHECK)
#endif // __GNUC__ || __clang__

// C++ default member initializers for option structs, so cb_walk_dir(root, fn, .post_order = true)
// and cb_cmd_run / cb_chain_* only need the fields you care about without triggering
// -Wmissing-field-initializers / -Wmissing-designated-field-initializers. Empty in C.
#ifdef __cplusplus
#define CB__DEFAULT(value) = value
#else
#define CB__DEFAULT(value)
#endif // __cplusplus

// CB_WARN_DEPRECATED turns the CB_DEPRECATED marker into a real compiler warning.
#ifdef CB_WARN_DEPRECATED
#ifndef CB_DEPRECATED
#if defined(__GNUC__) || defined(__clang__)
#define CB_DEPRECATED(message) __attribute__((deprecated(message)))
#elif defined(_MSC_VER)
#define CB_DEPRECATED(message) __declspec(deprecated(message))
#else
#define CB_DEPRECATED(...)
#endif
#endif /* CB_DEPRECATED */
#else
#define CB_DEPRECATED(...)
#endif /* CB_WARN_DEPRECATED */

// Zero initializer: {0} in C, {} in C++; neither triggers -Wmissing-field-initializers.
#ifdef __cplusplus
#define CB_ZERO { }
#else
#define CB_ZERO { 0 }
#endif // __cplusplus

// Line ending of the host system, for building text files.
#ifdef _WIN32
#define CB_LINE_END "\r\n"
#else
#define CB_LINE_END "\n"
#endif

// Element count of a real array, plus a bounds-checked accessor.
#define CB_ARRAY_LEN(array) (sizeof(array) / sizeof(array[0]))
#define CB_ARRAY_GET(array, index) \
    (CB_ASSERT((size_t)index < CB_ARRAY_LEN(array)), array[(size_t)index])

// Silence an unused parameter or variable warning.
#define CB_UNUSED(value) (void)(value)

// Jump to the defer: label and store the return value in result. The function needs a variable
// named result and a defer: label:
//     bool foo(void)
//     {
//         bool result = true;
//         if (failed) cb_return_defer(false);
//     defer:
//         return result;
//     }
// In C++ every declaration must appear before the first cb_return_defer(), because goto may not
// jump over an initialized declaration.
#define cb_return_defer(value) \
    do {                       \
        result = (value);      \
        goto defer;            \
    } while (0)

////////////////////////////////////////////////////////////////////////////////////////////////////
// Allocator
////////////////////////////////////////////////////////////////////////////////////////////////////
// CB_REALLOC / CB_FREE are the only two allocation exits of the whole library, so replacing them
// takes over every allocation. CB_REALLOC_RAW / CB_FREE_RAW are the bottom layer, used by the
// tracking layer itself to avoid recursion.
#include <stdlib.h>
#ifndef CB_REALLOC_RAW
#define CB_REALLOC_RAW(ptr, size) realloc((ptr), (size))
#endif // !CB_REALLOC_RAW
#ifndef CB_FREE_RAW
#define CB_FREE_RAW(ptr) free(ptr)
#endif // !CB_FREE_RAW

#ifdef CB_ALLOC_TRACK
// Tracking mode: every allocation carries an internal header with its size and call site, so
// leaks can be counted and reported. Pointers allocated with tracking on must not cross into
// code compiled with tracking off.
CBDEF void* cb__tracked_realloc(void* ptr, size_t size, const char* file, int line);
CBDEF void cb__tracked_free(void* ptr, const char* file, int line);
// Print live blocks, peak usage and the leak list (with the allocation site as file:line).
CBDEF void cb_alloc_report(void);
CBDEF size_t cb_alloc_live_count(void);
CBDEF size_t cb_alloc_live_size(void);
#define CB_REALLOC(ptr, size) cb__tracked_realloc((ptr), (size), __FILE__, __LINE__)
#define CB_FREE(ptr) cb__tracked_free((ptr), __FILE__, __LINE__)
#else
#ifndef CB_REALLOC
#define CB_REALLOC(ptr, size) CB_REALLOC_RAW((ptr), (size))
#endif // !CB_REALLOC
#ifndef CB_FREE
#define CB_FREE(ptr) CB_FREE_RAW(ptr)
#endif // !CB_FREE
#endif // CB_ALLOC_TRACK

// Every unrecoverable allocation failure goes through here. Deliberately not an assert():
// assert disappears under -DNDEBUG and OOM would become a silent crash with no explanation.
// __FILE__/__LINE__ expand at the call site, so the location stays exact through macros.
//
// To take over (log and degrade instead of dying), define CB_OOM before including cb.h after
// declaring your handler; the macro is #ifndef-guarded:
//
//     static void my_oom(size_t requested_size);
//     #define CB_OOM(size) my_oom(size)
//     #include "cb.h"
//
// The handler must not return: the caller keeps using the (NULL) pointer afterwards.
#ifndef CB_OOM
#define CB_OOM(requested_size) cb__oom((requested_size), __FILE__, __LINE__)
#endif // !CB_OOM
CBDEF void cb__oom(size_t requested_size, const char* file, int line);
// Check right after an allocation and die on failure; use it as a statement.
#define cb_alloc_check(ptr, requested_size) \
    do {                                    \
        if ((ptr) == NULL) CB_OOM(requested_size); \
    } while (0)

#ifdef __cplusplus
#define CB_DECLTYPE_CAST(T) (decltype(T))
#else
#define CB_DECLTYPE_CAST(T)
#endif // __cplusplus

#define cb_swap(T, a, b) \
    do {                 \
        T t = a;         \
        a = b;           \
        b = t;           \
    } while (0)

// Take the first argument and advance the pointer (bash-like shift).
#define cb_shift(ptr_data, count) (CB_ASSERT((count) > 0), (count)--, *(ptr_data)++)

#if defined(__cplusplus)
#define CB_CLIT(type) type
#else
#define CB_CLIT(type) (type)
#endif

////////////////////////////////////////////////////////////////////////////////////////////////////
// Arena / Temp Storage
////////////////////////////////////////////////////////////////////////////////////////////////////
// arena: an explicit scoped allocator. Create as many instances as you want; it grows as a chain
// of regions and never runs "full". temp: a thin wrapper over one thread-local arena instance for
// throwaway memory. Both share the same core; only the usage model differs (explicit handle vs
// implicit singleton). CB_TEMP_CAPACITY is the size of the first block, not a total limit: new
// blocks are appended when it fills up, and cb_temp_alloc never returns NULL (CB_OOM aborts).

#ifndef CB_ARENA_REGION_INIT_CAPACITY
#define CB_ARENA_REGION_INIT_CAPACITY (64 * 1024)
#endif // !CB_ARENA_REGION_INIT_CAPACITY

#ifndef CB_TEMP_CAPACITY
#define CB_TEMP_CAPACITY CB_ARENA_REGION_INIT_CAPACITY
#endif // !CB_TEMP_CAPACITY

// Largest alignment any standard type needs, without <stdalign.h> (C11 only): a union of the
// widest scalar types, then offsetof() on a probe struct reads its alignment. Pass CB_ARENA_ALIGN,
// or any larger power of two, as the alignment argument of cb_arena_alloc_aligned().
typedef union {
    long double ld;
    long long ll;
    void* p;
    void (*fp)(void);
} CB__Max_Align;

typedef struct {
    char c;
    CB__Max_Align m;
} CB__Max_Align_Probe;

#define CB__MAX_ALIGN ((size_t)offsetof(CB__Max_Align_Probe, m))

#ifndef CB_ARENA_ALIGN
#define CB_ARENA_ALIGN CB__MAX_ALIGN
#endif // !CB_ARENA_ALIGN

// One temp stack per thread, so threads do not trample each other. Define CB_THREAD_LOCAL
// yourself to override: empty for a single-threaded program, or your platform's specifier.
#ifndef CB_THREAD_LOCAL
#ifdef __cplusplus
#define CB_THREAD_LOCAL thread_local
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define CB_THREAD_LOCAL _Thread_local
#elif defined(_MSC_VER)
#define CB_THREAD_LOCAL __declspec(thread)
#elif defined(__GNUC__) || defined(__clang__)
#define CB_THREAD_LOCAL __thread
#else
#define CB_THREAD_LOCAL
#endif
#endif // !CB_THREAD_LOCAL

// data carries no alignment qualifier on purpose: cb_arena_alloc_aligned() rounds absolute
// addresses up, so every returned pointer is aligned no matter where data starts.
typedef struct CB_Arena_Region CB_Arena_Region;
struct CB_Arena_Region {
    CB_Arena_Region* next;
    size_t count;                  // bytes used, always a multiple of CB_ARENA_ALIGN
    size_t capacity;               // usable bytes in this region
#ifdef __cplusplus
    // C++ has no flexible array members (-Wpedantic rejects the C99 extension), so use a
    // one-element array: the data offset is unchanged, see CB__ARENA_REGION_DATA_OFFSET.
    unsigned char data[1];
#else
    unsigned char data[];
#endif
};

// Offset of the data area from the start of the region. Use offsetof(), not sizeof(): the C++
// sizeof includes tail padding, while offsetof equals the C sizeof in both languages.
#define CB__ARENA_REGION_DATA_OFFSET offsetof(CB_Arena_Region, data)

typedef struct {
    CB_Arena_Region* begin; // head of the chain
    CB_Arena_Region* end;   // current allocation region
    size_t init_capacity;   // first region capacity, 0 means CB_ARENA_REGION_INIT_CAPACITY
} CB_Arena;

// Snapshot of (region, offset inside it): rewind is O(1) and can span several regions.
typedef struct {
    CB_Arena_Region* region;
    size_t count;
} CB_Arena_Mark;

// ---- declarations ----
CBDEF void cb__oom(size_t requested_size, const char* file, int line);

CBDEF void* cb_arena_alloc(CB_Arena* a, size_t size);
// alignment must be a power of two.
CBDEF void* cb_arena_alloc_aligned(CB_Arena* a, size_t size, size_t alignment);
// The arena cannot grow in place: when new_size > old_size it allocates and copies, so the
// returned pointer may differ from old_ptr.
CBDEF void* cb_arena_realloc(CB_Arena* a, void* old_ptr, size_t old_size, size_t new_size);
CBDEF char* cb_arena_strdup(CB_Arena* a, const char* cstr);
CBDEF char* cb_arena_strndup(CB_Arena* a, const char* cstr, size_t n);
CBDEF char* cb_arena_sprintf(CB_Arena* a, const char* fmt, ...) CB_PRINTF_FORMAT(2, 3);
CBDEF char* cb_arena_vsprintf(CB_Arena* a, const char* fmt, va_list ap);
CBDEF CB_Arena_Mark cb_arena_save(CB_Arena* a);
CBDEF void cb_arena_rewind(CB_Arena* a, CB_Arena_Mark mark);
// Drop the contents but keep the regions for reuse (the usual way to reuse an arena).
CBDEF void cb_arena_reset(CB_Arena* a);
// Give every region back to the system.
CBDEF void cb_arena_free(CB_Arena* a);

CBDEF void* cb_temp_alloc(size_t size);
CBDEF char* cb_temp_strdup(const char* cstr);
CBDEF char* cb_temp_strndup(const char* cstr, size_t n);
CBDEF char* cb_temp_sprintf(const char* fmt, ...) CB_PRINTF_FORMAT(1, 2);
CBDEF char* cb_temp_vsprintf(const char* fmt, va_list ap);
// Reset the whole temp stack, keeping the allocated regions.
CBDEF void cb_temp_reset(void);
// Save a checkpoint: CB_Arena_Mark mark = cb_temp_save(); ... cb_temp_rewind(mark);
CBDEF CB_Arena_Mark cb_temp_save(void);
// Return to the checkpoint; every allocation made after it becomes invalid.
CBDEF void cb_temp_rewind(CB_Arena_Mark mark);

#ifdef CB_ALLOC_TRACK
// Tracking layer: every allocation gets a record header in front of it. The CB__Max_Align member
// raises the struct alignment, so sizeof is a multiple of it and the pointer returned to the user
// stays aligned like a plain malloc() result.
typedef struct CB__Alloc_Record {
    struct CB__Alloc_Record* prev;
    struct CB__Alloc_Record* next;
    CB__Max_Align align_guard;
    size_t size;
    const char* file;
    int line;
} CB__Alloc_Record;

extern CB__Alloc_Record* cb__alloc_head;
extern size_t cb__alloc_live_count;
extern size_t cb__alloc_live_size;
extern size_t cb__alloc_peak_size;
extern size_t cb__alloc_total_count;
#endif // CB_ALLOC_TRACK

// temp is one thread-local arena instance, defined once in this section's implementation block
extern CB_THREAD_LOCAL CB_Arena cb__temp_arena;

#ifdef CB_IMPLEMENTATION

CBDEF void cb__oom(size_t requested_size, const char* file, int line)
{
    fprintf(stderr, "%s:%d: OOM: could not allocate %zu bytes\n", file, line, requested_size);
    abort();
}
#ifdef CB_ALLOC_TRACK
CB__Alloc_Record* cb__alloc_head = NULL;
size_t cb__alloc_live_count = 0;
size_t cb__alloc_live_size = 0;
size_t cb__alloc_peak_size = 0;
size_t cb__alloc_total_count = 0;

CBDEF void* cb__tracked_realloc(void* ptr, size_t size, const char* file, int line)
{
    if (ptr == NULL) {
        if (size == 0) size = 1;
        size_t total = sizeof(CB__Alloc_Record) + size;
        CB__Alloc_Record* rec = (CB__Alloc_Record*)CB_REALLOC_RAW(NULL, total);
        cb_alloc_check(rec, total);
        rec->prev = NULL;
        rec->next = cb__alloc_head;
        if (cb__alloc_head != NULL) cb__alloc_head->prev = rec;
        cb__alloc_head = rec;
        rec->size = size;
        rec->file = file;
        rec->line = line;
        cb__alloc_live_count += 1;
        cb__alloc_live_size += size;
        cb__alloc_total_count += 1;
        if (cb__alloc_live_size > cb__alloc_peak_size) cb__alloc_peak_size = cb__alloc_live_size;
        return (char*)rec + sizeof(CB__Alloc_Record);
    }

    if (size == 0) { // realloc(p, 0) behaves like free
        cb__tracked_free(ptr, file, line);
        return NULL;
    }

    CB__Alloc_Record* rec = (CB__Alloc_Record*)(void*)((char*)ptr - sizeof(CB__Alloc_Record));
    size_t total = sizeof(CB__Alloc_Record) + size;
    CB__Alloc_Record* moved = (CB__Alloc_Record*)CB_REALLOC_RAW(rec, total);
    cb_alloc_check(moved, total);
    if (moved != rec) { // the node moved, so relink its neighbours
        if (moved->prev != NULL) moved->prev->next = moved;
        else cb__alloc_head = moved;
        if (moved->next != NULL) moved->next->prev = moved;
    }
    cb__alloc_live_size = cb__alloc_live_size - moved->size + size;
    if (cb__alloc_live_size > cb__alloc_peak_size) cb__alloc_peak_size = cb__alloc_live_size;
    moved->size = size;
    moved->file = file;
    moved->line = line;
    cb__alloc_total_count += 1;
    return (char*)moved + sizeof(CB__Alloc_Record);
}

CBDEF void cb__tracked_free(void* ptr, const char* file, int line)
{
    CB_UNUSED(file);
    CB_UNUSED(line);
    if (ptr == NULL) return;
    CB__Alloc_Record* rec = (CB__Alloc_Record*)(void*)((char*)ptr - sizeof(CB__Alloc_Record));
    if (rec->prev != NULL) rec->prev->next = rec->next;
    else cb__alloc_head = rec->next;
    if (rec->next != NULL) rec->next->prev = rec->prev;
    cb__alloc_live_count -= 1;
    cb__alloc_live_size -= rec->size;
    CB_FREE_RAW(rec);
}

CBDEF size_t cb_alloc_live_count(void) { return cb__alloc_live_count; }
CBDEF size_t cb_alloc_live_size(void) { return cb__alloc_live_size; }

CBDEF void cb_alloc_report(void)
{
    fprintf(stderr, "=== cb alloc report ===\n");
    fprintf(stderr, "live : %zu blocks, %zu bytes\n", cb__alloc_live_count, cb__alloc_live_size);
    fprintf(stderr, "peak : %zu bytes, %zu allocations total\n", cb__alloc_peak_size, cb__alloc_total_count);
    for (CB__Alloc_Record* r = cb__alloc_head; r != NULL; r = r->next) {
        fprintf(stderr, "  LEAK %zu bytes from %s:%d\n", r->size, r->file != NULL ? r->file : "?", r->line);
    }
}
#endif // CB_ALLOC_TRACK

CBDEF CB_Arena_Region* cb__arena_new_region(size_t capacity)
{
    size_t size = CB__ARENA_REGION_DATA_OFFSET + capacity;
    CB_Arena_Region* r = (CB_Arena_Region*)CB_REALLOC(NULL, size);
    cb_alloc_check(r, size);
    r->next = NULL;
    r->count = 0;
    r->capacity = capacity;
    return r;
}

CBDEF void cb__arena_free_region(CB_Arena_Region* r)
{
    CB_FREE(r);
}

CBDEF void* cb_arena_alloc_aligned(CB_Arena* a, size_t size, size_t alignment)
{
    CB_ASSERT(alignment != 0 && (alignment & (alignment - 1)) == 0 && "alignment must be a power of two");
    size_t align = alignment > CB_ARENA_ALIGN ? alignment : CB_ARENA_ALIGN;
    size_t need = size == 0 ? 1 : size;

    for (;;) {
        if (a->end == NULL) {
            size_t capacity = a->init_capacity ? a->init_capacity : CB_ARENA_REGION_INIT_CAPACITY;
            if (capacity < need + align) capacity = need + align;
            a->end = cb__arena_new_region(capacity);
            if (a->begin == NULL) a->begin = a->end;
            continue;
        }

        // Round the absolute address up to the alignment boundary. Rounding the offset alone is
        // not enough: data starts at an arbitrary offset, and a larger alignment must land on an
        // absolute boundary. alignment is a power of two, so this is a mask, not a division.
        {
            uintptr_t base = (uintptr_t)a->end->data;
            uintptr_t addr = base + (uintptr_t)a->end->count;
            uintptr_t aligned = (addr + (uintptr_t)align - 1) & ~((uintptr_t)align - 1);
            size_t offset = (size_t)(aligned - base);
            if (offset + need <= a->end->capacity) {
                void* result = a->end->data + offset;
                a->end->count = offset + need;
                return result;
            }
        }

        if (a->end->next != NULL) {
            a->end = a->end->next; // a rewound region may still fit, walk forward first
            continue;
        }

        {
            size_t capacity = a->init_capacity ? a->init_capacity : CB_ARENA_REGION_INIT_CAPACITY;
            if (capacity < need + align) capacity = need + align;
            a->end->next = cb__arena_new_region(capacity);
            a->end = a->end->next;
        }
    }
}

CBDEF void* cb_arena_alloc(CB_Arena* a, size_t size)
{
    return cb_arena_alloc_aligned(a, size, CB_ARENA_ALIGN);
}

CBDEF void* cb_arena_realloc(CB_Arena* a, void* old_ptr, size_t old_size, size_t new_size)
{
    if (new_size <= old_size) return old_ptr;
    void* new_ptr = cb_arena_alloc(a, new_size);
    if (old_ptr != NULL && old_size > 0) memcpy(new_ptr, old_ptr, old_size);
    return new_ptr;
}

CBDEF char* cb_arena_strdup(CB_Arena* a, const char* cstr)
{
    return cb_arena_strndup(a, cstr, strlen(cstr));
}

CBDEF char* cb_arena_strndup(CB_Arena* a, const char* cstr, size_t n)
{
    char* result = (char*)cb_arena_alloc(a, n + 1);
    memcpy(result, cstr, n);
    result[n] = '\0';
    return result;
}

CBDEF char* cb_arena_vsprintf(CB_Arena* a, const char* fmt, va_list ap)
{
    va_list args;
    va_copy(args, ap);
    int n = vsnprintf(NULL, 0, fmt, args);
    va_end(args);
    if (n < 0) n = 0;

    char* result = (char*)cb_arena_alloc(a, (size_t)n + 1);
    va_copy(args, ap);
    vsnprintf(result, (size_t)n + 1, fmt, args);
    va_end(args);
    return result;
}

CBDEF char* cb_arena_sprintf(CB_Arena* a, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    char* result = cb_arena_vsprintf(a, fmt, args);
    va_end(args);
    return result;
}

CBDEF CB_Arena_Mark cb_arena_save(CB_Arena* a)
{
    CB_Arena_Mark mark;
    mark.region = a->end;
    mark.count = a->end != NULL ? a->end->count : 0;
    return mark;
}

CBDEF void cb_arena_rewind(CB_Arena* a, CB_Arena_Mark mark)
{
    if (mark.region == NULL) { // the snapshot came from an arena without regions yet
        cb_arena_reset(a);
        return;
    }
    mark.region->count = mark.count;
    for (CB_Arena_Region* r = mark.region->next; r != NULL; r = r->next) {
        r->count = 0;
    }
    a->end = mark.region;
}

CBDEF void cb_arena_reset(CB_Arena* a)
{
    for (CB_Arena_Region* r = a->begin; r != NULL; r = r->next) {
        r->count = 0;
    }
    a->end = a->begin;
}

CBDEF void cb_arena_free(CB_Arena* a)
{
    CB_Arena_Region* r = a->begin;
    while (r != NULL) {
        CB_Arena_Region* next = r->next;
        cb__arena_free_region(r);
        r = next;
    }
    a->begin = NULL;
    a->end = NULL;
}
CB_THREAD_LOCAL CB_Arena cb__temp_arena = {0, 0, CB_TEMP_CAPACITY};

CBDEF void* cb_temp_alloc(size_t size) { return cb_arena_alloc(&cb__temp_arena, size); }
CBDEF char* cb_temp_strdup(const char* cstr) { return cb_arena_strdup(&cb__temp_arena, cstr); }
CBDEF char* cb_temp_strndup(const char* cstr, size_t n) { return cb_arena_strndup(&cb__temp_arena, cstr, n); }
CBDEF char* cb_temp_vsprintf(const char* fmt, va_list ap) { return cb_arena_vsprintf(&cb__temp_arena, fmt, ap); }
CBDEF void cb_temp_reset(void) { cb_arena_reset(&cb__temp_arena); }
CBDEF CB_Arena_Mark cb_temp_save(void) { return cb_arena_save(&cb__temp_arena); }
CBDEF void cb_temp_rewind(CB_Arena_Mark mark) { cb_arena_rewind(&cb__temp_arena, mark); }
CBDEF char* cb_temp_sprintf(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    char* result = cb_arena_vsprintf(&cb__temp_arena, fmt, args);
    va_end(args);
    return result;
}

#endif // CB_IMPLEMENTATION

////////////////////////////////////////////////////////////////////////////////////////////////////
// Logger / Panic
////////////////////////////////////////////////////////////////////////////////////////////////////
// Two ways to log:
//     cb_log(level, ...)     library-internal and general use: no location, clean output
//     CB_LOG_AT(level, ...)  your own code: adds file:line automatically
// CB_LOG_AT(CB_ERROR, "bad config") -> [ERROR] mycode.c:42: bad config

typedef enum {
    CB_INFO,
    CB_WARN,
    CB_ERROR,
    CB_NO_LOGS,
} CB_Log_Level;

// Log handler. file == NULL (line == 0) means "no location"; the handler decides what to print.
typedef void(CB_Log_Handler)(CB_Log_Level level, const char* file, int line, const char* fmt, va_list args);

extern CB_Log_Level cb_minimal_log_level;

CBDEF CB_Log_Handler cb_default_log_handler;
CBDEF CB_Log_Handler cb_cancer_log_handler;
CBDEF CB_Log_Handler cb_null_log_handler;
extern CB_Log_Handler* cb_log_handler;

CBDEF void cb_log(CB_Log_Level level, const char* fmt, ...);
// Log with a location; normally use the CB_LOG_AT macro instead of writing file/line by hand.
CBDEF void cb_log_at(CB_Log_Level level, const char* file, int line, const char* fmt, ...);
#define CB_LOG_AT(level, ...) cb_log_at((level), __FILE__, __LINE__, __VA_ARGS__)

CBDEF void cb_set_log_handler(CB_Log_Handler* handler);
CBDEF CB_Log_Handler* cb_get_log_handler(void);
CBDEF void cb_default_log_handler(CB_Log_Level level, const char* file, int line, const char* fmt, va_list args);
CBDEF void cb_cancer_log_handler(CB_Log_Level level, const char* file, int line, const char* fmt, va_list args);
CBDEF void cb_null_log_handler(CB_Log_Level level, const char* file, int line, const char* fmt, va_list args);

// panic: print file:line + label + message, then abort. On glibc/Mac/FreeBSD a backtrace is
// printed as well (function names need -rdynamic, otherwise feed the addresses to addr2line);
// CB_PANIC_BACKTRACE=0 disables it. FreeBSD additionally needs -lexecinfo.
#ifndef CB_PANIC_BACKTRACE
#if (defined(__GLIBC__) || defined(__APPLE__) || defined(__FreeBSD__)) && !defined(_WIN32)
#define CB_PANIC_BACKTRACE 1
#else
#define CB_PANIC_BACKTRACE 0
#endif
#endif // !CB_PANIC_BACKTRACE

#if CB_PANIC_BACKTRACE
#include <execinfo.h>
#endif

CBDEF void cb__panicf(const char* file, int line, const char* label, const char* format, ...);
#define CB_TODO(...) cb__panicf(__FILE__, __LINE__, "TODO", __VA_ARGS__)
#define CB_UNREACHABLE(...) cb__panicf(__FILE__, __LINE__, "UNREACHABLE", __VA_ARGS__)

#ifdef CB_IMPLEMENTATION

CB_Log_Level cb_minimal_log_level = CB_INFO;
CB_Log_Handler* cb_log_handler = &cb_default_log_handler;

// ---- definitions ----
CBDEF void cb__panicf(const char* file, int line, const char* label, const char* format, ...)
{
    fprintf(stderr, "%s:%d: %s: ", file, line, label);
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fprintf(stderr, "\n");

#if CB_PANIC_BACKTRACE
    {
        void* frames[32];
        int count = backtrace(frames, (int)CB_ARRAY_LEN(frames));
        fprintf(stderr, "--- backtrace (%d frames) ---\n", count);
        backtrace_symbols_fd(frames, count, 2); // fd 2 is stderr
    }
#endif // CB_PANIC_BACKTRACE

    abort();
}

CBDEF void cb_log(CB_Log_Level level, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    cb_log_handler(level, NULL, 0, fmt, args);
    va_end(args);
}

CBDEF void cb_log_at(CB_Log_Level level, const char* file, int line, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    cb_log_handler(level, file, line, fmt, args);
    va_end(args);
}

CBDEF void cb_set_log_handler(CB_Log_Handler* handler)
{
    cb_log_handler = handler;
}

CBDEF CB_Log_Handler* cb_get_log_handler(void)
{
    return cb_log_handler;
}

CBDEF void cb_default_log_handler(CB_Log_Level level, const char* file, int line, const char* fmt, va_list args)
{
    if (level < cb_minimal_log_level) return;

    const char* prefix = NULL;
    switch (level) {
    case CB_INFO:    prefix = "[INFO] ";  break;
    case CB_WARN:    prefix = "[WARN] ";  break;
    case CB_ERROR:   prefix = "[ERROR] "; break;
    case CB_NO_LOGS: return;
    default: CB_UNREACHABLE("CB_Log_Level");
    }

    // Render the whole line into temp storage first and write it with a single fprintf, so
    // concurrent writers cannot interleave inside one message.
    CB_Arena_Mark mark = cb_temp_save();
    const char* msg = cb_temp_vsprintf(fmt, args);
    if (file != NULL) fprintf(stderr, "%s%s:%d: %s\n", prefix, file, line, msg);
    else              fprintf(stderr, "%s%s\n", prefix, msg);
    cb_temp_rewind(mark);
}

CBDEF void cb_cancer_log_handler(CB_Log_Level level, const char* file, int line, const char* fmt, va_list args)
{
    if (level < cb_minimal_log_level) return;

    // Emoji and colors only when stderr is a terminal: redirected output and recorded test
    // baselines stay plain text.
#ifdef _WIN32
    bool color = _isatty(_fileno(stderr)) != 0;
#else
    bool color = isatty(fileno(stderr)) != 0;
#endif

    const char* prefix = NULL;
    switch (level) {
    case CB_INFO:    prefix = color ? "ℹ️ \x1b[36m[INFO]\x1b[0m "  : "[INFO] ";  break;
    case CB_WARN:    prefix = color ? "⚠️ \x1b[33m[WARN]\x1b[0m "  : "[WARN] ";  break;
    case CB_ERROR:   prefix = color ? "🚨 \x1b[31m[ERROR]\x1b[0m " : "[ERROR] "; break;
    case CB_NO_LOGS: return;
    default: CB_UNREACHABLE("CB_Log_Level");
    }

    CB_Arena_Mark mark = cb_temp_save();
    const char* msg = cb_temp_vsprintf(fmt, args);
    if (file != NULL) fprintf(stderr, "%s%s:%d: %s\n", prefix, file, line, msg);
    else              fprintf(stderr, "%s%s\n", prefix, msg);
    cb_temp_rewind(mark);
}

CBDEF void cb_null_log_handler(CB_Log_Level level, const char* file, int line, const char* fmt, va_list args)
{
    CB_UNUSED(level);
    CB_UNUSED(file);
    CB_UNUSED(line);
    CB_UNUSED(fmt);
    CB_UNUSED(args);
}

#endif // CB_IMPLEMENTATION

////////////////////////////////////////////////////////////////////////////////////////////////////
// Timer
////////////////////////////////////////////////////////////////////////////////////////////////////
// Usage:
//     CB_TIMER_START("phase");  ...work...  double us = CB_TIMER_END();
//     cb_timer_end_print();   print this one measurement
//     cb_timer_end_stat();    add it to the stat entry with the same name
//     cb_timer_print_stats(); print the whole table
//
// Time source: QueryPerformanceCounter on Windows, clock_gettime(CLOCK_MONOTONIC) elsewhere.
// The stats table grows on demand, so more timer names never lose data.
// Everything goes to stderr: timing is diagnostics and would pollute stdout and recorded
// test baselines.

// Nanoseconds per second, for turning clock ticks into a duration.
#define CB_NANOS_PER_SEC 1000000000ull

// Nesting depth limit. This is not a limit on stat entries: the table grows on demand.
#ifndef CB_TIMER_MAX_DEPTH
#define CB_TIMER_MAX_DEPTH 64
#endif // !CB_TIMER_MAX_DEPTH

typedef struct {
    const char* name;
    double total;
    size_t count;
    double min;
    double max;
} CB_Timer_Stat;

typedef struct {
    const char* name;
    double start;
} CB_Timer_Frame;

typedef struct {
    CB_Timer_Stat* stats; // dynamic array (items/count/capacity)
    size_t stats_count;
    size_t stats_capacity;
    CB_Timer_Frame stack[CB_TIMER_MAX_DEPTH];
    size_t sp;
} CB_Timer;

extern CB_Timer cb_timer;

// ---- declarations ----
CBDEF double cb_get_time_ms(void);
CBDEF double cb_get_time_us(void);
// Nanoseconds from a monotonic clock; only differences between two stamps are meaningful.
CBDEF uint64_t cb_nanos_since_unspecified_epoch(void);

CBDEF void cb_timer_begin(const char* name);
// End the current measurement and return the elapsed microseconds.
CBDEF double cb_timer_end(void);
// End the current measurement and add it to the stat entry with the same name.
CBDEF double cb_timer_end_stat(void);
// End the current measurement and print this one entry to stderr.
CBDEF void cb_timer_end_print(void);
// Find or create the stat entry with this name; never returns NULL.
CBDEF CB_Timer_Stat* cb_timer_get_stat(const char* name);
// Print the table to a stream (benchmarks use stdout so it can be redirected).
CBDEF void cb_timer_fprint_stats(FILE* out);
// Print the table to stderr (the default: timing is diagnostics).
CBDEF void cb_timer_print_stats(void);
// Clear stats and the timer stack, keeping the allocated table for reuse.
CBDEF void cb_timer_reset(void);

// Only these two keep short macro names; everything else uses the function directly.
#define CB_TIMER_START(timer_name) cb_timer_begin(timer_name)
#define CB_TIMER_END() cb_timer_end()

#ifdef CB_IMPLEMENTATION

CB_Timer cb_timer = CB_ZERO;

// ---- definitions ----
#ifdef _WIN32

// Note: on Windows cb_get_time_us costs about 53ns per call, more than twice the 22ns of Linux;
// that is the price of QueryPerformanceCounter itself.
CBDEF double cb_get_time_ms(void)
{
    LARGE_INTEGER freq, count;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);
    return (double)count.QuadPart * 1000 / freq.QuadPart;
}
CBDEF double cb_get_time_us(void)
{
    LARGE_INTEGER freq, count;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);
    return (double)count.QuadPart * 1000000 / freq.QuadPart;
}
#else

// Monotonic clock: gettimeofday is wall-clock time and jumps when NTP or the user adjusts it.
CBDEF double cb_get_time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}
CBDEF double cb_get_time_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000000.0 + (double)ts.tv_nsec / 1000.0;
}
#endif // _WIN32

CBDEF void cb_timer_begin(const char* name)
{
    if (cb_timer.sp >= CB_TIMER_MAX_DEPTH) {
        cb__panicf(__FILE__, __LINE__, "CB_TIMER",
                   "timer nesting exceeded %d levels: a CB_TIMER_START has no matching CB_TIMER_END",
                   (int)CB_TIMER_MAX_DEPTH);
    }
    cb_timer.stack[cb_timer.sp].name = name;
    cb_timer.stack[cb_timer.sp].start = cb_get_time_us();
    cb_timer.sp += 1;
}

CBDEF double cb_timer_end(void)
{
    if (cb_timer.sp == 0) {
        cb__panicf(__FILE__, __LINE__, "CB_TIMER", "CB_TIMER_END without a matching CB_TIMER_START: the timer stack is empty");
    }
    cb_timer.sp -= 1;
    return cb_get_time_us() - cb_timer.stack[cb_timer.sp].start;
}

// Nanosecond monotonic timestamp, same semantics as cb_get_time_us but finer grained.
// The epoch is unspecified: only differences between two stamps are meaningful.
CBDEF uint64_t cb_nanos_since_unspecified_epoch(void)
{
#ifdef _WIN32
    LARGE_INTEGER now, freq;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    // Split into whole seconds and the remainder: multiplying the raw tick count by 1e9 would
    // overflow 64 bits after a few minutes of uptime on a 10MHz counter.
    uint64_t secs = (uint64_t)(now.QuadPart / freq.QuadPart);
    uint64_t rest = (uint64_t)(now.QuadPart % freq.QuadPart);
    return secs * CB_NANOS_PER_SEC + rest * CB_NANOS_PER_SEC / (uint64_t)freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * CB_NANOS_PER_SEC + (uint64_t)ts.tv_nsec;
#endif // _WIN32
}

CBDEF CB_Timer_Stat* cb_timer_get_stat(const char* name)
{
    for (size_t i = 0; i < cb_timer.stats_count; ++i) {
        if (strcmp(cb_timer.stats[i].name, name) == 0) return &cb_timer.stats[i];
    }

    if (cb_timer.stats_count == cb_timer.stats_capacity) {
        size_t new_capacity = cb_timer.stats_capacity == 0 ? 16 : cb_timer.stats_capacity * 2;
        size_t bytes = new_capacity * sizeof(*cb_timer.stats);
        cb_timer.stats = (CB_Timer_Stat*)CB_REALLOC(cb_timer.stats, bytes);
        cb_alloc_check(cb_timer.stats, bytes);
        cb_timer.stats_capacity = new_capacity;
    }

    CB_Timer_Stat* stat = &cb_timer.stats[cb_timer.stats_count++];
    stat->name = name;
    stat->total = 0;
    stat->count = 0;
    stat->min = 0;
    stat->max = 0;
    return stat;
}

CBDEF double cb_timer_end_stat(void)
{
    double elapsed = cb_timer_end();
    // cb_timer_end() already popped the stack, so stack[sp] is the frame that just ended.
    CB_Timer_Stat* stat = cb_timer_get_stat(cb_timer.stack[cb_timer.sp].name);
    stat->total += elapsed;
    stat->count += 1;
    if (stat->count == 1) {
        stat->min = elapsed;
        stat->max = elapsed;
    } else {
        if (elapsed < stat->min) stat->min = elapsed;
        if (elapsed > stat->max) stat->max = elapsed;
    }
    return elapsed;
}

CBDEF void cb_timer_end_print(void)
{
    double elapsed = cb_timer_end();
    fprintf(stderr, "[%s] took %.3f us\n", cb_timer.stack[cb_timer.sp].name, elapsed);
}

CBDEF void cb_timer_fprint_stats(FILE* out)
{
    if (out == NULL) return;

    // Size the name column from the actual content so long names do not skew the table.
    size_t name_width = strlen("Name");
    for (size_t i = 0; i < cb_timer.stats_count; ++i) {
        size_t len = strlen(cb_timer.stats[i].name);
        if (len > name_width) name_width = len;
    }

    fprintf(out, "\n========== Timer Statistics ==========\n");
    fprintf(out, "%-*s %10s %12s %12s %12s %12s\n",
            (int)name_width, "Name", "Count", "Avg(us)", "Min(us)", "Max(us)", "Total(us)");
    for (size_t i = 0; i < cb_timer.stats_count; ++i) {
        CB_Timer_Stat* s = &cb_timer.stats[i];
        double avg = s->count > 0 ? s->total / (double)s->count : 0.0;
        fprintf(out, "%-*s %10zu %12.3f %12.3f %12.3f %12.3f\n",
                (int)name_width, s->name, s->count, avg, s->min, s->max, s->total);
    }
    fprintf(out, "========================================\n");
}

CBDEF void cb_timer_print_stats(void)
{
    cb_timer_fprint_stats(stderr);
}

CBDEF void cb_timer_reset(void)
{
    cb_timer.stats_count = 0;
    cb_timer.sp = 0;
}

#endif // CB_IMPLEMENTATION

////////////////////////////////////////////////////////////////////////////////////////////////////
// Dynamic Array
////////////////////////////////////////////////////////////////////////////////////////////////////
typedef struct {
    const char** items;
    size_t count;
    size_t capacity;
} CB_DArray;

#ifndef CB_DA_INIT_CAP
#define CB_DA_INIT_CAP 256
#endif // !CB_DA_INIT_CAP

#define cb_da_free(da) CB_FREE((da).items)

#define cb_da_reserve(da, new_capacity)                                                                                 \
    do {                                                                                                                \
        if ((new_capacity) > (da)->capacity) {                                                                          \
            if ((da)->capacity == 0)                                                                                    \
                (da)->capacity = CB_DA_INIT_CAP;                                                                        \
            while ((new_capacity) > (da)->capacity) {                                                                   \
                (da)->capacity *= 2;                                                                                    \
            }                                                                                                           \
            (da)->items = CB_DECLTYPE_CAST((da)->items) CB_REALLOC((da)->items, (da)->capacity * sizeof(*(da)->items)); \
            cb_alloc_check((da)->items, (da)->capacity * sizeof(*(da)->items));                                        \
        }                                                                                                               \
    } while (0)

#define cb_da_append(da, data)                \
    do {                                      \
        cb_da_reserve((da), (da)->count + 1); \
        (da)->items[(da)->count++] = (data);  \
    } while (0)


#define cb_da_append_many(da, new_items, new_items_count)                                                \
    do {                                                                                                 \
        size_t cb__n = (size_t)(new_items_count);                                                        \
        /* n == 0 allows new_items to be NULL, so skip the whole append (memcpy from NULL is UB)    */   \
        if (cb__n > 0) {                                                                                 \
            const char* cb__src = (const char*)(new_items);                                              \
            /* Self-append: the source sits inside the target buffer, realloc invalidates it and the  */ \
            /* ranges overlap, so remember the offset and then use memmove                            */ \
            bool cb__self = (da)->items != NULL &&                                                       \
                            cb__src >= (const char*)(da)->items &&                                       \
                            cb__src < (const char*)(da)->items + (da)->count * sizeof(*(da)->items);     \
            size_t cb__off = cb__self ? (size_t)(cb__src - (const char*)(da)->items) : 0;                \
            cb_da_reserve((da), (da)->count + cb__n);                                                    \
            if (cb__self) cb__src = (const char*)(da)->items + cb__off;                                  \
            memmove((da)->items + (da)->count, cb__src, cb__n * sizeof(*(da)->items));                   \
            (da)->count += cb__n;                                                                        \
        }                                                                                                \
    } while (0)

#define cb_da_resize(da, new_size)     \
    do {                               \
        cb_da_reserve((da), new_size); \
        (da)->count = (new_size);      \
    } while (0)

#define cb_da_pop(da) (da)->items[(CB_ASSERT((da)->count > 0), --(da)->count)]
#define cb_da_first(da) (da)->items[(CB_ASSERT((da)->count > 0), 0)]
#define cb_da_last(da) (da)->items[(CB_ASSERT((da)->count > 0), (da)->count - 1)]
// Overwrite the removed element with the last one (order is not preserved).
#define cb_da_remove_unordered(da, i)                \
    do {                                             \
        size_t j = (i);                              \
        CB_ASSERT(j < (da)->count);                  \
        (da)->items[j] = (da)->items[--(da)->count]; \
    } while (0)

#define cb_da_foreach(Type, it, da) \
    for (Type* it = (da)->items; it < (da)->items + (da)->count; ++it)

// Drop the contents but keep the allocated memory for reuse.
#define cb_da_clear(da) ((da)->count = 0)

// Insert one element at index, shifting the tail right.
#define cb_da_insert(da, index, data)                                      \
    do {                                                                   \
        size_t cb__i = (index);                                            \
        CB_ASSERT(cb__i <= (da)->count);                                    \
        cb_da_reserve((da), (da)->count + 1);                               \
        memmove((da)->items + cb__i + 1, (da)->items + cb__i,               \
                ((da)->count - cb__i) * sizeof(*(da)->items));               \
        (da)->items[cb__i] = (data);                                        \
        (da)->count += 1;                                                   \
    } while (0)

// Remove in order, keeping the relative order of the rest. Use cb_da_remove_unordered when\n// the elements are large and their order does not matter.
#define cb_da_remove_ordered(da, index)                                    \
    do {                                                                   \
        size_t cb__i = (index);                                            \
        CB_ASSERT(cb__i < (da)->count);                                     \
        memmove((da)->items + cb__i, (da)->items + cb__i + 1,               \
                ((da)->count - cb__i - 1) * sizeof(*(da)->items));           \
        (da)->count -= 1;                                                   \
    } while (0)

// Iterate backwards; with count == 0 it starts at items, so no items-1 pointer arithmetic.
#define cb_da_foreach_rev(Type, it, da)                                    \
    for (Type* it = ((da)->count > 0 ? (da)->items + (da)->count - 1 : (da)->items); \
         (da)->count > 0 && it >= (da)->items; --it)

// Append to a fixed-capacity inline array ({ T items[N]; size_t count; }): no allocation, no
// growth, and a full array is a bug rather than something to silently drop an item over.
#define cb_fa_append(fa, item) \
    (CB_ASSERT((fa)->count < CB_ARRAY_LEN((fa)->items)), (fa)->items[(fa)->count++] = (item))

////////////////////////////////////////////////////////////////////////////////////////////////////
// Bitset
////////////////////////////////////////////////////////////////////////////////////////////////////
// Fixed-size bit set: bits is the number of bits, the storage is uint64_t words and grows on demand.

typedef struct {
    uint64_t* words;
    size_t word_count;
    size_t word_capacity;
    size_t bits;
} CB_Bitset;

#ifndef CB_BITSET_WORD_BITS
#define CB_BITSET_WORD_BITS 64
#endif // !CB_BITSET_WORD_BITS

// ---- declarations ----
// Resize to bits: new bits read as 0, bits above the new size are dropped.
CBDEF void cb_bitset_resize(CB_Bitset* bs, size_t bits);
CBDEF void cb_bitset_set(CB_Bitset* bs, size_t index);
CBDEF void cb_bitset_unset(CB_Bitset* bs, size_t index);
CBDEF void cb_bitset_toggle(CB_Bitset* bs, size_t index);
CBDEF bool cb_bitset_test(const CB_Bitset* bs, size_t index);
// Clear every bit, keeping the allocated words.
CBDEF void cb_bitset_clear_all(CB_Bitset* bs);
// Count the bits whose value equals target.
CBDEF size_t cb_bitset_count(const CB_Bitset* bs, bool target);
// Index of the first bit equal to target, or (size_t)-1 when there is none.
CBDEF size_t cb_bitset_find(const CB_Bitset* bs, bool target);
CBDEF void cb_bitset_free(CB_Bitset* bs);

#ifdef CB_IMPLEMENTATION

CBDEF void cb_bitset_resize(CB_Bitset* bs, size_t bits)
{
    size_t need_words = (bits + CB_BITSET_WORD_BITS - 1) / CB_BITSET_WORD_BITS;
    if (need_words > bs->word_capacity) {
        size_t new_capacity = bs->word_capacity == 0 ? 4 : bs->word_capacity;
        while (new_capacity < need_words) new_capacity *= 2;
        size_t bytes = new_capacity * sizeof(*bs->words);
        bs->words = (uint64_t*)CB_REALLOC(bs->words, bytes);
        cb_alloc_check(bs->words, bytes);
        bs->word_capacity = new_capacity;
    }
    // Growing: clear the words that were just added.\n    // Shrinking: clear the words that are dropped, so growing again starts clean.
    for (size_t i = bs->word_count; i < need_words; ++i) bs->words[i] = 0;
    for (size_t i = need_words; i < bs->word_count; ++i) bs->words[i] = 0;
    bs->word_count = need_words;
    bs->bits = bits;
}

CBDEF void cb_bitset_set(CB_Bitset* bs, size_t index)
{
    CB_ASSERT(index < bs->bits);
    bs->words[index / CB_BITSET_WORD_BITS] |= (uint64_t)1 << (index % CB_BITSET_WORD_BITS);
}

CBDEF void cb_bitset_unset(CB_Bitset* bs, size_t index)
{
    CB_ASSERT(index < bs->bits);
    bs->words[index / CB_BITSET_WORD_BITS] &= ~((uint64_t)1 << (index % CB_BITSET_WORD_BITS));
}

CBDEF void cb_bitset_toggle(CB_Bitset* bs, size_t index)
{
    CB_ASSERT(index < bs->bits);
    bs->words[index / CB_BITSET_WORD_BITS] ^= (uint64_t)1 << (index % CB_BITSET_WORD_BITS);
}

CBDEF bool cb_bitset_test(const CB_Bitset* bs, size_t index)
{
    CB_ASSERT(index < bs->bits);
    return (bs->words[index / CB_BITSET_WORD_BITS] & ((uint64_t)1 << (index % CB_BITSET_WORD_BITS))) != 0;
}

CBDEF void cb_bitset_clear_all(CB_Bitset* bs)
{
    if (bs->word_count > 0) memset(bs->words, 0, bs->word_count * sizeof(*bs->words));
}

CBDEF size_t cb_bitset_count(const CB_Bitset* bs, bool target)
{
    size_t n = 0;
    for (size_t i = 0; i < bs->bits; ++i) {
        if (cb_bitset_test(bs, i) == target) n += 1;
    }
    return n;
}

CBDEF size_t cb_bitset_find(const CB_Bitset* bs, bool target)
{
    for (size_t i = 0; i < bs->bits; ++i) {
        if (cb_bitset_test(bs, i) == target) return i;
    }
    return (size_t)-1;
}

CBDEF void cb_bitset_free(CB_Bitset* bs)
{
    CB_FREE(bs->words);
    bs->words = NULL;
    bs->word_count = 0;
    bs->word_capacity = 0;
    bs->bits = 0;
}

#endif // CB_IMPLEMENTATION

////////////////////////////////////////////////////////////////////////////////////////////////////
// Ring Buffer
////////////////////////////////////////////////////////////////////////////////////////////////////
// Byte ring buffer, FIFO. The capacity is fixed and never grows: a full ring accepts what fits\n// and returns the number of bytes actually written.

typedef struct {
    unsigned char* data;
    size_t capacity;
    size_t read_pos;
    size_t write_pos;
    size_t count; // bytes currently readable
} CB_Ring;

// ---- declarations ----
CBDEF bool cb_ring_init(CB_Ring* ring, size_t capacity);
CBDEF void cb_ring_free(CB_Ring* ring);
// How many bytes can still be written.
CBDEF size_t cb_ring_space(const CB_Ring* ring);
// Write and return the number of bytes actually written.
CBDEF size_t cb_ring_write(CB_Ring* ring, const void* data, size_t size);
// Read and return the number of bytes actually read.
CBDEF size_t cb_ring_read(CB_Ring* ring, void* out, size_t size);
// Copy without consuming and return the number of bytes copied.
CBDEF size_t cb_ring_peek(const CB_Ring* ring, void* out, size_t size);
// Drop all data without freeing the buffer.
CBDEF void cb_ring_clear(CB_Ring* ring);

#ifdef CB_IMPLEMENTATION

CBDEF bool cb_ring_init(CB_Ring* ring, size_t capacity)
{
    memset(ring, 0, sizeof(*ring));
    if (capacity == 0) return false;
    ring->data = (unsigned char*)CB_REALLOC(NULL, capacity);
    cb_alloc_check(ring->data, capacity);
    ring->capacity = capacity;
    return true;
}

CBDEF void cb_ring_free(CB_Ring* ring)
{
    CB_FREE(ring->data);
    memset(ring, 0, sizeof(*ring));
}

CBDEF size_t cb_ring_space(const CB_Ring* ring)
{
    return ring->capacity - ring->count;
}

CBDEF size_t cb_ring_write(CB_Ring* ring, const void* data, size_t size)
{
    const unsigned char* src = (const unsigned char*)data;
    size_t written = 0;
    while (written < size && ring->count < ring->capacity) {
        size_t chunk = ring->capacity - ring->write_pos;
        size_t room = ring->capacity - ring->count;
        if (chunk > room) chunk = room;
        if (chunk > size - written) chunk = size - written;

        memcpy(ring->data + ring->write_pos, src + written, chunk);
        ring->write_pos = (ring->write_pos + chunk) % ring->capacity;
        ring->count += chunk;
        written += chunk;
    }
    return written;
}

CBDEF size_t cb_ring_read(CB_Ring* ring, void* out, size_t size)
{
    size_t got = cb_ring_peek(ring, out, size);
    ring->read_pos = (ring->read_pos + got) % ring->capacity;
    ring->count -= got;
    return got;
}

CBDEF size_t cb_ring_peek(const CB_Ring* ring, void* out, size_t size)
{
    unsigned char* dst = (unsigned char*)out;
    size_t done = 0;
    size_t pos = ring->read_pos;
    while (done < size && done < ring->count) {
        size_t chunk = ring->capacity - pos;
        if (chunk > ring->count - done) chunk = ring->count - done;
        if (chunk > size - done) chunk = size - done;

        memcpy(dst + done, ring->data + pos, chunk);
        pos = (pos + chunk) % ring->capacity;
        done += chunk;
    }
    return done;
}

CBDEF void cb_ring_clear(CB_Ring* ring)
{
    ring->read_pos = 0;
    ring->write_pos = 0;
    ring->count = 0;
}

#endif // CB_IMPLEMENTATION

////////////////////////////////////////////////////////////////////////////////////////////////////
// Sort
////////////////////////////////////////////////////////////////////////////////////////////////////
// Works on any {items,count,capacity} dynamic array.
// cb_da_sort uses qsort: fast, but not stable (equal elements may be reordered).
// cb_da_sort_insertion is stable and very fast on nearly sorted arrays; best for small ones.

typedef int (*CB_Compare_Func)(const void* a, const void* b);

// ---- declarations ----
// Stable insertion sort. It takes the element size so it can move elements byte-wise.
CBDEF void cb__insertion_sort(void* items, size_t count, size_t elem_size, CB_Compare_Func cmp);

#define cb_da_sort(da, cmp) qsort((da)->items, (da)->count, sizeof(*(da)->items), (cmp))
#define cb_da_sort_insertion(da, cmp) \
    cb__insertion_sort((da)->items, (da)->count, sizeof(*(da)->items), (cmp))

#ifdef CB_IMPLEMENTATION

CBDEF void cb__insertion_sort(void* items, size_t count, size_t elem_size, CB_Compare_Func cmp)
{
    if (count < 2) return;

    CB_Arena_Mark mark = cb_temp_save();
    unsigned char* tmp = (unsigned char*)cb_temp_alloc(elem_size);
    unsigned char* base = (unsigned char*)items;

    for (size_t i = 1; i < count; ++i) {
        memcpy(tmp, base + i * elem_size, elem_size);
        size_t j = i;
        while (j > 0 && cmp(base + (j - 1) * elem_size, tmp) > 0) {
            memcpy(base + j * elem_size, base + (j - 1) * elem_size, elem_size);
            j -= 1;
        }
        memcpy(base + j * elem_size, tmp, elem_size);
    }

    cb_temp_rewind(mark);
}

#endif // CB_IMPLEMENTATION

////////////////////////////////////////////////////////////////////////////////////////////////////
// StringBuilder
////////////////////////////////////////////////////////////////////////////////////////////////////
typedef struct {
    char* items;
    size_t count;
    size_t capacity;
} CB_String_Builder;

// Core invariant: items is always a valid C string. Every append writes '\0' at items[count] and
// the capacity counts that terminator (count does not), so cb_sb_to_sv / printf("%s") / strstr stay
// safe at any moment. To put a '\0' inside the payload use cb_sb_append(&sb, '\0') explicitly:
// that byte is content, not the terminator.
CBDEF void cb__sb_terminate(CB_String_Builder* sb);

// TODO: may need to add a buffer version, more safe for memory.
CBDEF bool cb_read_entire_file(const char* path, CB_String_Builder* sb);
CBDEF int cb_sb_appendf(CB_String_Builder* sb, const char* fmt, ...) CB_PRINTF_FORMAT(2, 3);
// Pad the builder with 0 bytes up to a multiple of word_size (when building binary formats):
// content "aaaaa" aligned to 4 becomes "aaaaa000".
CBDEF void cb_sb_pad_align(CB_String_Builder* sb, size_t size);

// Append a fixed-size buffer.
#define cb_sb_append_buf(sb, buf, size)         \
    do {                                        \
        cb_da_append_many((sb), (buf), (size)); \
        cb__sb_terminate(sb);                   \
    } while (0)

// Append a StringView.
#define cb_sb_append_sv(sb, sv) cb_sb_append_buf((sb), (sv).data, (sv).count)

// Append a NUL-terminated string.
#define cb_sb_append_cstr(sb, cstr)                    \
    do {                                               \
        const char* cb__s = (cstr);                    \
        cb_sb_append_buf((sb), cb__s, strlen(cb__s));  \
    } while (0)

// Append a single character.
#define cb_sb_append(sb, ch)                 \
    do {                                     \
        char cb__ch = (char)(ch);            \
        cb_sb_append_buf((sb), &cb__ch, 1);  \
    } while (0)

// Append a NUL byte to the payload (nob.h compatibility). cb.h keeps items NUL-terminated by
// itself, so this is only needed when the NUL is part of the data, not to terminate a string.
#define cb_sb_append_null(sb) cb_sb_append((sb), '\0')

// Free the memory and zero the fields, so no dangling pointer is left behind.
#define cb_sb_free(sb)          \
    do {                        \
        CB_FREE((sb).items);    \
        (sb).items = NULL;      \
        (sb).count = 0;         \
        (sb).capacity = 0;      \
    } while (0)

#ifdef CB_IMPLEMENTATION

CBDEF bool cb_read_entire_file(const char* path, CB_String_Builder* sb)
{
    bool result = true;

    FILE* file = fopen(path, "rb");
    size_t new_count = 0;
    long long filesize = 0;

    if (file == NULL) cb_return_defer(false);

#ifdef _WIN32
    filesize = _filelengthi64(_fileno(file));
#else
    {
        struct stat statbuf;
        if (fstat(fileno(file), &statbuf) < 0) cb_return_defer(false);
        filesize = (long long)statbuf.st_size;
    }
#endif
    if (filesize < 0) cb_return_defer(false);

    new_count = sb->count + (size_t)filesize;
    if (new_count + 1 > sb->capacity) {
        sb->items = CB_DECLTYPE_CAST(sb->items) CB_REALLOC(sb->items, new_count + 1);
        cb_alloc_check(sb->items, new_count + 1);
        sb->capacity = new_count + 1;
    }

    if (fread(sb->items + sb->count, (size_t)filesize, 1, file) != 1 && filesize > 0) {
// ferror does not set errno, so the message in the defer block may be inaccurate.
        cb_return_defer(false);
    }
    sb->count = new_count;
    sb->items[sb->count] = '\0'; // the bytes read can be used as a C string right away

defer:
    if (!result) cb_log(CB_ERROR, "Could not read file %s: %s", path, strerror(errno));
    if (file != NULL) fclose(file);
    return result;
}

CBDEF int cb_sb_appendf(CB_String_Builder* sb, const char* fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    int n = vsnprintf(NULL, 0, fmt, args);
    va_end(args);

    cb_da_reserve(sb, sb->count + n + 1);
    char* dest = sb->items + sb->count;
    va_start(args, fmt);
    vsnprintf(dest, n + 1, fmt, args);
    va_end(args);

    sb->count += n;

    return n;
}

CBDEF void cb__sb_terminate(CB_String_Builder* sb)
{
    cb_da_reserve(sb, sb->count + 1);
    sb->items[sb->count] = '\0';
}

CBDEF void cb_sb_pad_align(CB_String_Builder* sb, size_t size)
{
    size_t rem = sb->count % size;
    if (rem == 0) return;
    for (size_t i = 0; i < size - rem; ++i) {
        cb_sb_append(sb, 0);
    }
}

#endif // CB_IMPLEMENTATION

////////////////////////////////////////////////////////////////////////////////////////////////////
// StringView
////////////////////////////////////////////////////////////////////////////////////////////////////
typedef struct {
    const char* data;
    size_t count;
} CB_String_View;

// Macros for printing a StringView with printf.
#ifndef CB_SV_FMT
// Compile-time StringView literal: it saves the strlen that cb_sv_from_cstr("...") would do.
// The designators must follow the declaration order of CB_String_View (data first, count second):
// C allows any order, C++ is a hard error ("designator order for field does not match...").
#define CB_SVLIT(lit) (CB_CLIT(CB_String_View){.data = (lit), .count = sizeof(lit) - 1})
// Static-initializer form, for MSVC /TC which rejects the compound literal above.
#define CB_SVLIT_STATIC(lit) {.data = (lit), .count = sizeof(lit) - 1}

#define CB_SV_FMT "%.*s"
#endif // CB_SV_FMT
#ifndef CB_SV_ARG
#define CB_SV_ARG(sv) (int)(sv).count, (sv).data
#endif // CB_SV_ARG
// USAGE:
//   CB_String_View name = ...;
//   printf("Name: "CB_SV_FMT"\n", CB_SV_ARG(name));

// Copy the StringView into temp storage as a NUL-terminated C string.
CBDEF const char* cb_sv_to_temp_cstr(CB_String_View sv);

CBDEF bool cb_sv_eq(CB_String_View a, CB_String_View b);

CBDEF CB_String_View cb_sv_from_parts(const char* data, size_t count);
CBDEF CB_String_View cb_sv_from_cstr(const char* cstr);

// View a StringBuilder as a StringView.
#define cb_sb_to_sv(sb) cb_sv_from_parts((sb).items, (sb).count)

// Chop while p(current character) is true: return the chopped prefix, move sv forward.
//     CB_String_View sv = cb_sv_from_cstr("  123abc456");
//     cb_sv_chop_by_func(&sv, isspace) -> "  ", sv = "123abc456"
//     cb_sv_chop_by_func(&sv, isdigit) -> "123", sv = "abc456"
// Move the view forward. An empty view may have data == NULL, and NULL + 0 is UB in C
// (UBSan: "applying zero offset to null pointer"), so n == 0 returns early.
#define cb__sv_advance(sv, n)  \
    do {                       \
        if ((n) > 0) {         \
            (sv)->data += (n); \
            (sv)->count -= (n);\
        }                      \
    } while (0)

CBDEF CB_String_View cb_sv_chop_by_func(CB_String_View* sv, int (*p)(int x));
// Chop up to delim, return that part and drop the delimiter itself.
//     CB_String_View sv = cb_sv_from_cstr("  1223abc456");
//     cb_sv_chop_by_delim(&sv, '2') -> "  1", sv = "23abc456"
CBDEF CB_String_View cb_sv_chop_by_delim(CB_String_View* sv, char delim);
CBDEF CB_String_View cb_sv_chop_by_delim_r(CB_String_View* sv, char delim);
CBDEF CB_String_View cb_sv_chop_left(CB_String_View* sv, size_t n);
CBDEF CB_String_View cb_sv_chop_right(CB_String_View* sv, size_t n);

// True when the view has this prefix/suffix, false otherwise.
CBDEF bool cb_sv_ends_with(CB_String_View sv, CB_String_View suffix);
CBDEF bool cb_sv_ends_with_cstr(CB_String_View sv, const char* suffix);
CBDEF bool cb_sv_starts_with(CB_String_View sv, CB_String_View prefix);
CBDEF bool cb_sv_starts_with_cstr(CB_String_View sv, const char* prefix);

// Chop the prefix if present and return true, otherwise return false.
CBDEF bool cb_sv_chop_prefix(CB_String_View* sv, CB_String_View prefix);
// Chop the suffix if present and return true, otherwise return false.
CBDEF bool cb_sv_chop_suffix(CB_String_View* sv, CB_String_View suffix);

// Return a view without leading/trailing whitespace; the original view is not modified.
CBDEF CB_String_View cb_sv_trim_left(CB_String_View sv);
CBDEF CB_String_View cb_sv_trim_right(CB_String_View sv);
CBDEF CB_String_View cb_sv_trim(CB_String_View sv);

CBDEF int cb_sv_find(CB_String_View* sv, char ch);
CBDEF int cb_sv_find_sv(CB_String_View* sv, CB_String_View target, size_t start_offset);

#ifdef CB_IMPLEMENTATION

CBDEF const char* cb_sv_to_temp_cstr(CB_String_View sv)
{
    return cb_temp_strndup(sv.data, sv.count);
}

CBDEF bool cb_sv_eq(CB_String_View a, CB_String_View b)
{
    if (a.count != b.count) {
        return false;
    } else if (a.count == 0) {
// Two empty views: data may be NULL and memcmp(NULL, NULL, 0) is UB, so handle it separately.
        return true;
    } else {
        return memcmp(a.data, b.data, a.count) == 0;
    }
}

CBDEF CB_String_View cb_sv_from_parts(const char* data, size_t count)
{
    CB_String_View sv;
    sv.count = count;
    sv.data = data;
    return sv;
}

CBDEF CB_String_View cb_sv_from_cstr(const char* cstr)
{
    return cb_sv_from_parts(cstr, strlen(cstr));
}

CBDEF CB_String_View cb_sv_chop_by_func(CB_String_View* sv, int (*p)(int x))
{
    size_t i = 0;
    while (i < sv->count && p(sv->data[i])) {
        i += 1;
    }

    CB_String_View result = cb_sv_from_parts(sv->data, i);
    cb__sv_advance(sv, i);

    return result;
}

CBDEF CB_String_View cb_sv_chop_by_delim(CB_String_View* sv, char delim)
{
    size_t i = 0;
    while (i < sv->count && sv->data[i] != delim) {
        i += 1;
    }

    CB_String_View result = cb_sv_from_parts(sv->data, i);

    cb__sv_advance(sv, i < sv->count ? i + 1 : i);

    return result;
}

// Find the LAST delim from the right: return the part before it and leave sv with the part after
// it; without a delimiter the whole view is returned and sv becomes empty.
CBDEF CB_String_View cb_sv_chop_by_delim_r(CB_String_View* sv, char delim)
{
    size_t i = sv->count;
    while (i > 0 && sv->data[i - 1] != delim) i -= 1;

    if (i == 0) {
// No delimiter: return everything and empty the original view.
        CB_String_View whole = cb_sv_from_parts(sv->data, sv->count);
        cb__sv_advance(sv, sv->count);
        return whole;
    }

    size_t delim_index = i - 1; // i is "delimiter index + 1"
    CB_String_View result = cb_sv_from_parts(sv->data, delim_index);
    cb__sv_advance(sv, delim_index + 1);
    return result;
}

CBDEF CB_String_View cb_sv_chop_left(CB_String_View* sv, size_t n)
{
    if (n > sv->count) {
        n = sv->count;
    }

    CB_String_View result = cb_sv_from_parts(sv->data, n);
    cb__sv_advance(sv, n);

    return result;
}

CBDEF CB_String_View cb_sv_chop_right(CB_String_View* sv, size_t n)
{
    if (n > sv->count) {
        n = sv->count;
    }

// With n == 0 do not compute sv->data + sv->count - 0 (an empty view may have data == NULL).
    CB_String_View result = cb_sv_from_parts(n > 0 ? sv->data + sv->count - n : sv->data, n);
    sv->count -= n;

    return result;
}

CBDEF bool cb_sv_ends_with(CB_String_View sv, CB_String_View suffix)
{
    if (suffix.count > sv.count) {
        return false;
    }
// An empty suffix matches every view; handling it separately also avoids pointer arithmetic on
// NULL (sv.data may be NULL when sv.count == 0, and NULL + 0 is UB by the standard).
    if (suffix.count == 0) {
        return true;
    }

    CB_String_View sv_tail = {
        .data = sv.data + sv.count - suffix.count,
        .count = suffix.count,
    };
    return cb_sv_eq(sv_tail, suffix);
}

CBDEF bool cb_sv_ends_with_cstr(CB_String_View sv, const char* suffix)
{
    return cb_sv_ends_with(sv, cb_sv_from_cstr(suffix));
}

CBDEF bool cb_sv_starts_with(CB_String_View sv, CB_String_View prefix)
{
    if (prefix.count > sv.count) {
        return false;
    }

    CB_String_View actual_prefix = cb_sv_from_parts(sv.data, prefix.count);
    return cb_sv_eq(prefix, actual_prefix);
}

CBDEF bool cb_sv_starts_with_cstr(CB_String_View sv, const char* prefix)
{
    return cb_sv_starts_with(sv, cb_sv_from_cstr(prefix));
}

CBDEF bool cb_sv_chop_prefix(CB_String_View* sv, CB_String_View prefix)
{
    if (cb_sv_starts_with(*sv, prefix)) {
        cb_sv_chop_left(sv, prefix.count);
        return true;
    }
    return false;
}

CBDEF bool cb_sv_chop_suffix(CB_String_View* sv, CB_String_View suffix)
{
    if (cb_sv_ends_with(*sv, suffix)) {
        cb_sv_chop_right(sv, suffix.count);
        return true;
    }
    return false;
}

CBDEF CB_String_View cb_sv_trim_left(CB_String_View sv)
{
    size_t i = 0;
    while (i < sv.count && isspace(sv.data[i])) {
        i += 1;
    }

    return cb_sv_from_parts(i > 0 ? sv.data + i : sv.data, sv.count - i);
}

CBDEF CB_String_View cb_sv_trim_right(CB_String_View sv)
{
    size_t i = 0;
    while (i < sv.count && isspace(sv.data[sv.count - 1 - i])) {
        i += 1;
    }

    return cb_sv_from_parts(sv.data, sv.count - i);
}

CBDEF CB_String_View cb_sv_trim(CB_String_View sv)
{
    return cb_sv_trim_right(cb_sv_trim_left(sv));
}

// TODO: add find reverse version?
CBDEF int cb_sv_find(CB_String_View* sv, char ch)
{
    for (size_t i = 0; i < sv->count; i++) {
        if (sv->data[i] == ch) return i;
    }
    return -1;
}

CBDEF int cb_sv_find_sv(CB_String_View* sv, CB_String_View target, size_t start_offset)
{
    if (sv->count <= 0 || target.count <= 0) return -1;
    if (start_offset >= sv->count - target.count) return -1;
    if (target.count > sv->count - start_offset) return -1;

    size_t limit = sv->count - target.count + 1;
    for (size_t i = start_offset; i < limit; i++) {
        if (memcmp(sv->data + i, target.data, target.count) == 0) return i;
    }
    return -1;
}

#endif // CB_IMPLEMENTATION

////////////////////////////////////////////////////////////////////////////////////////////////////
// UTF-8 Support
////////////////////////////////////////////////////////////////////////////////////////////////////
extern const uint8_t cb_bytes_for_utf8[];

CBDEF size_t cb_sv_utf8_len(CB_String_View sv, size_t* bytes_overrun);

#ifdef CB_IMPLEMENTATION

const uint8_t cb_bytes_for_utf8[] = {
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2, 2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3, 4,4,4,4,4,4,4,4,5,5,5,5,6,6,6,6,
};

CBDEF size_t cb_sv_utf8_len(CB_String_View sv, size_t* bytes_overrun)
{
    size_t i = 0;
    size_t n = 0;
    while (true) {
        if (i >= sv.count) {
            if (bytes_overrun) *bytes_overrun = i - sv.count;
            return n;
        }
        i += cb_bytes_for_utf8[(uint8_t)sv.data[i]];
        n += 1;
    }
    CB_UNREACHABLE("cb_sv_utf8_len");
    return 0;
}

#endif // CB_IMPLEMENTATION

////////////////////////////////////////////////////////////////////////////////////////////////////
// StringView Tools
////////////////////////////////////////////////////////////////////////////////////////////////////
// Case conversion; the result lives in temp storage.
CBDEF char* cb_sv_to_temp_upper(CB_String_View sv);
CBDEF char* cb_sv_to_temp_lower(CB_String_View sv);
CBDEF bool cb_sv_eq_ignore_case(CB_String_View a, CB_String_View b);

// Number parsing is strict and allows surrounding whitespace: on success it writes *out and returns
// true, on failure it returns false and leaves *out untouched. 0x/0X hex, 0b/0B binary and 0o/0O
// octal prefixes are accepted; integers take no decimal point and no exponent.
CBDEF bool cb_sv_to_i64(CB_String_View sv, int64_t* out);
CBDEF bool cb_sv_to_u64(CB_String_View sv, uint64_t* out);
CBDEF bool cb_sv_to_f64(CB_String_View sv, double* out);

// Take the next delim-separated field; sv is consumed in place.
//     while (cb_sv_split_next(&sv, ',', &part)) { ... }
CBDEF bool cb_sv_split_next(CB_String_View* sv, char delim, CB_String_View* out);
// Append parts joined by sep to sb (no trailing separator).
CBDEF void cb_sb_append_join(CB_String_Builder* sb, const CB_String_View* parts, size_t count, CB_String_View sep);

// UTF-8: decode one code point.
CBDEF bool cb_utf8_decode(const char* data, size_t size, uint32_t* out_codepoint, size_t* out_length);
// Encode one code point into out (at least 4 bytes) and return the length; 0 when invalid.
CBDEF size_t cb_utf8_encode(uint32_t codepoint, char out[4]);
// Take one code point from the head of sv and move sv forward.
CBDEF bool cb_sv_utf8_next(CB_String_View* sv, uint32_t* out_codepoint);
// Validate the view as UTF-8; on failure out_bad_offset receives the offending index.
CBDEF bool cb_utf8_validate(CB_String_View sv, size_t* out_bad_offset);

#ifdef CB_IMPLEMENTATION

CBDEF char* cb_sv_to_temp_upper(CB_String_View sv)
{
    char* result = cb_temp_strndup(sv.data, sv.count);
    for (size_t i = 0; i < sv.count; ++i) result[i] = (char)toupper((unsigned char)result[i]);
    return result;
}

CBDEF char* cb_sv_to_temp_lower(CB_String_View sv)
{
    char* result = cb_temp_strndup(sv.data, sv.count);
    for (size_t i = 0; i < sv.count; ++i) result[i] = (char)tolower((unsigned char)result[i]);
    return result;
}

CBDEF bool cb_sv_eq_ignore_case(CB_String_View a, CB_String_View b)
{
    if (a.count != b.count) return false;
    for (size_t i = 0; i < a.count; ++i) {
        if (tolower((unsigned char)a.data[i]) != tolower((unsigned char)b.data[i])) return false;
    }
    return true;
}

// Internal: trim, then parse the optional sign and the base prefix.
CBDEF bool cb__sv_number_parts(CB_String_View sv, CB_String_View* out_digits, int* out_base, bool* out_negative)
{
    sv = cb_sv_trim(sv);
    if (sv.count == 0) return false;

    bool negative = false;
    if (sv.data[0] == '+' || sv.data[0] == '-') {
        negative = sv.data[0] == '-';
        sv.data += 1;
        sv.count -= 1;
    }
    if (sv.count == 0) return false;

    int base = 10;
    if (sv.count >= 2 && sv.data[0] == '0') {
        char c = sv.data[1];
        if (c == 'x' || c == 'X') {
            base = 16;
        } else if (c == 'b' || c == 'B') {
            base = 2;
        } else if (c == 'o' || c == 'O') {
            base = 8;
        }
        if (base != 10) {
            sv.data += 2;
            sv.count -= 2;
            if (sv.count == 0) return false;
        }
    }

    *out_digits = sv;
    *out_base = base;
    *out_negative = negative;
    return true;
}

CBDEF bool cb__sv_digit_value(char c, int base, int* out)
{
    int v;
    if (c >= '0' && c <= '9') {
        v = c - '0';
    } else if (c >= 'a' && c <= 'z') {
        v = c - 'a' + 10;
    } else if (c >= 'A' && c <= 'Z') {
        v = c - 'A' + 10;
    } else {
        return false;
    }
    if (v >= base) return false;
    *out = v;
    return true;
}

CBDEF bool cb_sv_to_u64(CB_String_View sv, uint64_t* out)
{
    CB_String_View digits = CB_ZERO;
    int base = 10;
    bool negative = false;
    if (!cb__sv_number_parts(sv, &digits, &base, &negative)) return false;
    if (negative) return false; // unsigned does not accept a minus sign

    uint64_t value = 0;
    for (size_t i = 0; i < digits.count; ++i) {
        int d;
        if (digits.data[i] == '_') continue; // allow 1_000_000
        if (!cb__sv_digit_value(digits.data[i], base, &d)) return false;
        if (value > (UINT64_MAX - (uint64_t)d) / (uint64_t)base) return false; // overflow
        value = value * (uint64_t)base + (uint64_t)d;
    }
    *out = value;
    return true;
}

CBDEF bool cb_sv_to_i64(CB_String_View sv, int64_t* out)
{
    CB_String_View digits = CB_ZERO;
    int base = 10;
    bool negative = false;
    if (!cb__sv_number_parts(sv, &digits, &base, &negative)) return false;

    uint64_t limit = negative ? (uint64_t)INT64_MAX + 1u : (uint64_t)INT64_MAX;
    uint64_t magnitude = 0;
    for (size_t i = 0; i < digits.count; ++i) {
        int d;
        if (digits.data[i] == '_') continue;
        if (!cb__sv_digit_value(digits.data[i], base, &d)) return false;
        if (magnitude > (limit - (uint64_t)d) / (uint64_t)base) return false; // overflow
        magnitude = magnitude * (uint64_t)base + (uint64_t)d;
    }

    if (negative && magnitude == (uint64_t)INT64_MAX + 1u) {
        *out = INT64_MIN;
    } else if (negative) {
        *out = -(int64_t)magnitude;
    } else {
        *out = (int64_t)magnitude;
    }
    return true;
}

CBDEF bool cb_sv_to_f64(CB_String_View sv, double* out)
{
    sv = cb_sv_trim(sv);
    if (sv.count == 0) return false;

// Floating-point syntax is messy, so hand it to strtod on a NUL-terminated temp copy.
    char* cstr = cb_temp_strndup(sv.data, sv.count);
    errno = 0;
    char* end = NULL;
    double value = strtod(cstr, &end);
    if (end == cstr) return false;            // nothing was parsed
    if (end != cstr + sv.count) return false; // trailing garbage means it is not a pure number
    if (errno == ERANGE) return false;        // overflow or underflow
    *out = value;
    return true;
}

CBDEF bool cb_sv_split_next(CB_String_View* sv, char delim, CB_String_View* out)
{
    if (sv->count == 0) return false;
    *out = cb_sv_chop_by_delim(sv, delim);
    return true;
}

CBDEF void cb_sb_append_join(CB_String_Builder* sb, const CB_String_View* parts, size_t count, CB_String_View sep)
{
    for (size_t i = 0; i < count; ++i) {
        if (i > 0) cb_sb_append_sv(sb, sep);
        cb_sb_append_sv(sb, parts[i]);
    }
}

CBDEF bool cb_utf8_decode(const char* data, size_t size, uint32_t* out_codepoint, size_t* out_length)
{
    if (size == 0) return false;

    const unsigned char* bytes = (const unsigned char*)data;
    unsigned char first = bytes[0];

// Continuation bytes 0x80-0xBF must be rejected as leading bytes: the table maps them to 1,
// and it cannot be changed to 0 (cb_sv_utf8_len would loop forever on i += 0). Without this
// check a stray continuation byte would pass as the single-byte character U+0080.
    if (first >= 0x80 && first <= 0xBF) return false;

    size_t length = cb_bytes_for_utf8[first];
    if (length == 0 || length > size || length > 4) return false;

    uint32_t codepoint = 0;
    if (length == 1) {
        codepoint = first;
    } else {
        static const uint32_t first_mask[] = {0, 0x7F, 0x1F, 0x0F, 0x07};
        static const uint32_t min_value[] = {0, 0, 0x80, 0x800, 0x10000};

        codepoint = first & first_mask[length];
        for (size_t i = 1; i < length; ++i) {
            if ((bytes[i] & 0xC0) != 0x80) return false; // continuation bytes must be 10xxxxxx
            codepoint = (codepoint << 6) | (uint32_t)(bytes[i] & 0x3F);
        }
        if (codepoint < min_value[length]) return false;              // overlong encoding
        if (codepoint > 0x10FFFF) return false;                       // outside the Unicode range
        if (codepoint >= 0xD800 && codepoint <= 0xDFFF) return false; // surrogate range
    }

    if (out_codepoint) *out_codepoint = codepoint;
    if (out_length) *out_length = length;
    return true;
}

CBDEF size_t cb_utf8_encode(uint32_t codepoint, char out[4])
{
    if (codepoint > 0x10FFFF) return 0;
    if (codepoint >= 0xD800 && codepoint <= 0xDFFF) return 0;

    if (codepoint < 0x80) {
        out[0] = (char)codepoint;
        return 1;
    }
    if (codepoint < 0x800) {
        out[0] = (char)(0xC0 | (codepoint >> 6));
        out[1] = (char)(0x80 | (codepoint & 0x3F));
        return 2;
    }
    if (codepoint < 0x10000) {
        out[0] = (char)(0xE0 | (codepoint >> 12));
        out[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        out[2] = (char)(0x80 | (codepoint & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (codepoint >> 18));
    out[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
    out[3] = (char)(0x80 | (codepoint & 0x3F));
    return 4;
}

CBDEF bool cb_sv_utf8_next(CB_String_View* sv, uint32_t* out_codepoint)
{
    size_t length = 0;
    if (!cb_utf8_decode(sv->data, sv->count, out_codepoint, &length)) return false;
    sv->data += length;
    sv->count -= length;
    return true;
}

CBDEF bool cb_utf8_validate(CB_String_View sv, size_t* out_bad_offset)
{
    size_t offset = 0;
    while (offset < sv.count) {
        size_t length = 0;
        if (!cb_utf8_decode(sv.data + offset, sv.count - offset, NULL, &length)) {
            if (out_bad_offset) *out_bad_offset = offset;
            return false;
        }
        offset += length;
    }
    return true;
}

#endif // CB_IMPLEMENTATION

////////////////////////////////////////////////////////////////////////////////////////////////////
// HashMap
////////////////////////////////////////////////////////////////////////////////////////////////////
// Open addressing with linear probing. Keys are copied into the map, so the caller does not have
//
// Storage backends:
//   - CB_REALLOC / CB_FREE by default
//   - after cb_map_init_arena(&m, &arena, n) every allocation comes from the arena.
//     The arena cannot free single blocks, so slot arrays abandoned by a rehash live until
//
// Usage:
//   CB_Map m = CB_ZERO;
//   cb_map_put_cstr(&m, "answer", (void*)(intptr_t)42);
//   void* v = NULL;
//   if (cb_map_get_cstr(&m, "answer", &v)) { ... }
//   cb_map_foreach(&m, it) { printf("%.*s\n", (int)it.key.count, it.key.data); }
//   cb_map_free(&m);

#ifndef CB_MAP_INIT_CAPACITY
#define CB_MAP_INIT_CAPACITY 16
#endif // !CB_MAP_INIT_CAPACITY

#define CB_MAP_EMPTY 0
#define CB_MAP_USED 1
#define CB_MAP_TOMBSTONE 2

typedef struct {
    CB_String_View key; // key.data points at the copy owned by the map
    void* value;
    uint64_t hash; // cached hash, so a rehash does not recompute it
    uint8_t state;
} CB_Map_Slot;

typedef struct {
    CB_Map_Slot* slots;
    size_t capacity; // slot count, always a power of two; 0 means nothing allocated yet
    size_t count;    // live entries
    size_t tombstones;
    CB_Arena* arena; // when non-NULL every allocation comes from the arena
} CB_Map;

typedef struct {
    CB_Map* map;
    size_t index;
    CB_String_View key; // current entry, filled in by cb_map_next
    void* value;
} CB_Map_Iter;

// ---- hashing ----
CBDEF uint64_t cb_hash_bytes(const void* data, size_t size);
CBDEF uint64_t cb_hash_u64(uint64_t value);

// ---- declarations ----
CBDEF void cb_map_init_capacity(CB_Map* map, size_t capacity);
// Arena backend: every allocation comes from the arena, which must outlive the map.
CBDEF void cb_map_init_arena(CB_Map* map, CB_Arena* arena, size_t capacity);
// Insert or overwrite (the key is copied). A NULL value is allowed.
CBDEF void cb_map_put(CB_Map* map, CB_String_View key, void* value);
CBDEF void cb_map_put_cstr(CB_Map* map, const char* key, void* value);
// A miss returns false and leaves *out untouched; out may be NULL to only test presence.
CBDEF bool cb_map_get(const CB_Map* map, CB_String_View key, void** out);
CBDEF bool cb_map_get_cstr(const CB_Map* map, const char* key, void** out);
CBDEF bool cb_map_has(const CB_Map* map, CB_String_View key);
CBDEF bool cb_map_del(CB_Map* map, CB_String_View key);
CBDEF size_t cb_map_count(const CB_Map* map);
// Drop the entries but keep the allocated capacity.
CBDEF void cb_map_clear(CB_Map* map);
CBDEF void cb_map_free(CB_Map* map);
CBDEF CB_Map_Iter cb_map_iter(CB_Map* map);
CBDEF bool cb_map_next(CB_Map_Iter* it);
#define cb_map_foreach(map, it) \
    for (CB_Map_Iter it = cb_map_iter(map); cb_map_next(&it);)

#ifdef CB_IMPLEMENTATION

CBDEF uint64_t cb_hash_bytes(const void* data, size_t size)
{
// FNV-1a 64: good on short keys and trivial to implement.
    const unsigned char* p = (const unsigned char*)data;
    uint64_t hash = 1469598103934665603ull;
    for (size_t i = 0; i < size; ++i) {
        hash ^= (uint64_t)p[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

CBDEF uint64_t cb_hash_u64(uint64_t value)
{
// splitmix64 finalizer: spreads sequential integers so they do not cluster.
    value += 0x9E3779B97F4A7C15ull;
    value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ull;
    value = (value ^ (value >> 27)) * 0x94D049BB133111EBull;
    return value ^ (value >> 31);
}

CBDEF void* cb__map_alloc(CB_Map* map, size_t size)
{
    if (map->arena != NULL) return cb_arena_alloc(map->arena, size);
    void* p = CB_REALLOC(NULL, size);
    cb_alloc_check(p, size);
    return p;
}

CBDEF void cb__map_dealloc(CB_Map* map, void* ptr)
{
    if (map->arena != NULL) return; // the arena cannot free single blocks
    CB_FREE(ptr);
}

CBDEF CB_Map_Slot* cb__map_find_slot(const CB_Map* map, CB_String_View key, uint64_t hash, bool for_insert)
{
    if (map->capacity == 0) return NULL;

    size_t mask = map->capacity - 1;
    size_t i = (size_t)hash & mask;
    CB_Map_Slot* first_tombstone = NULL;

    for (size_t probe = 0; probe < map->capacity; ++probe) {
        CB_Map_Slot* slot = &map->slots[i];
        if (slot->state == CB_MAP_EMPTY) {
            if (for_insert) return first_tombstone != NULL ? first_tombstone : slot;
            return NULL;
        }
        if (slot->state == CB_MAP_TOMBSTONE) {
            if (first_tombstone == NULL) first_tombstone = slot;
        } else if (slot->hash == hash && cb_sv_eq(slot->key, key)) {
            return slot;
        }
        i = (i + 1) & mask;
    }
    return for_insert ? first_tombstone : NULL;
}

// Rehash everything into a new capacity in one pass (a power of two larger than the current one).
CBDEF void cb__map_resize_to(CB_Map* map, size_t new_capacity)
{
    CB_Map_Slot* new_slots = (CB_Map_Slot*)cb__map_alloc(map, new_capacity * sizeof(CB_Map_Slot));
    memset(new_slots, 0, new_capacity * sizeof(CB_Map_Slot));

    CB_Map_Slot* old_slots = map->slots;
    size_t old_capacity = map->capacity;

    map->slots = new_slots;
    map->capacity = new_capacity;
    map->count = 0;
    map->tombstones = 0;

    for (size_t i = 0; i < old_capacity; ++i) {
        if (old_slots[i].state != CB_MAP_USED) continue;
// Key memory is not copied, only the pointer moves.
        CB_Map_Slot* dst = cb__map_find_slot(map, old_slots[i].key, old_slots[i].hash, true);
        *dst = old_slots[i];
        map->count += 1;
    }

    if (map->arena == NULL) CB_FREE(old_slots);
}

// Grow in one step: repeated small grows would leave a trail of abandoned slot arrays.
CBDEF void cb__map_grow(CB_Map* map)
{
    cb__map_resize_to(map, map->capacity == 0 ? CB_MAP_INIT_CAPACITY : map->capacity * 2);
}

// Preallocate. The map must be zero-initialized first (CB_ZERO).
CBDEF void cb_map_init_capacity(CB_Map* map, size_t capacity)
{
    CB_ASSERT(map->slots == NULL && map->capacity == 0 && "the map must be zero-initialized and initialized only once");
    if (capacity == 0) return;

// Round up to a power of two and keep the load factor at or below 3/4.
    size_t wanted = capacity * 4 / 3 + 1;
    size_t power = CB_MAP_INIT_CAPACITY;
    while (power < wanted) power *= 2;
    cb__map_resize_to(map, power);
}

// Arena backend: the arena must outlive the map; slots and keys are allocated from it.
CBDEF void cb_map_init_arena(CB_Map* map, CB_Arena* arena, size_t capacity)
{
    map->arena = arena; // set first, otherwise the initial slots would go through malloc
    cb_map_init_capacity(map, capacity);
}

CBDEF void cb_map_put(CB_Map* map, CB_String_View key, void* value)
{
    if (map->capacity == 0) cb__map_grow(map);
// Grow once the load factor (tombstones included) reaches 3/4, which keeps probe chains short.
    if ((map->count + map->tombstones + 1) * 4 >= map->capacity * 3) cb__map_grow(map);

    uint64_t hash = cb_hash_bytes(key.data, key.count);
    CB_Map_Slot* slot = cb__map_find_slot(map, key, hash, true);
    CB_ASSERT(slot != NULL);

    if (slot->state == CB_MAP_USED) {
        slot->value = value; // key already present: overwrite the value, do not copy the key again
        return;
    }
    if (slot->state == CB_MAP_TOMBSTONE) map->tombstones -= 1;

    char* key_copy = (char*)cb__map_alloc(map, key.count + 1);
    memcpy(key_copy, key.data, key.count);
    key_copy[key.count] = '\0';

    slot->key = cb_sv_from_parts(key_copy, key.count);
    slot->value = value;
    slot->hash = hash;
    slot->state = CB_MAP_USED;
    map->count += 1;
}

CBDEF void cb_map_put_cstr(CB_Map* map, const char* key, void* value)
{
    cb_map_put(map, cb_sv_from_cstr(key), value);
}

CBDEF bool cb_map_get(const CB_Map* map, CB_String_View key, void** out)
{
    if (map->capacity == 0) return false;
    uint64_t hash = cb_hash_bytes(key.data, key.count);
    CB_Map_Slot* slot = cb__map_find_slot(map, key, hash, false);
    if (slot == NULL) return false;
    if (out != NULL) *out = slot->value;
    return true;
}

CBDEF bool cb_map_get_cstr(const CB_Map* map, const char* key, void** out)
{
    return cb_map_get(map, cb_sv_from_cstr(key), out);
}

CBDEF bool cb_map_has(const CB_Map* map, CB_String_View key)
{
    return cb_map_get(map, key, NULL);
}

CBDEF size_t cb_map_count(const CB_Map* map)
{
    return map->count;
}

CBDEF bool cb_map_del(CB_Map* map, CB_String_View key)
{
    if (map->capacity == 0) return false;
    uint64_t hash = cb_hash_bytes(key.data, key.count);
    CB_Map_Slot* slot = cb__map_find_slot(map, key, hash, false);
    if (slot == NULL) return false;

    cb__map_dealloc(map, (void*)slot->key.data);
    slot->key = cb_sv_from_parts(NULL, 0);
    slot->value = NULL;
    slot->hash = 0;
    slot->state = CB_MAP_TOMBSTONE;
    map->count -= 1;
    map->tombstones += 1;
    return true;
}

CBDEF void cb_map_clear(CB_Map* map)
{
    for (size_t i = 0; i < map->capacity; ++i) {
        if (map->slots[i].state == CB_MAP_USED) {
            cb__map_dealloc(map, (void*)map->slots[i].key.data);
        }
    }
    if (map->capacity > 0) memset(map->slots, 0, map->capacity * sizeof(CB_Map_Slot));
    map->count = 0;
    map->tombstones = 0;
}

CBDEF void cb_map_free(CB_Map* map)
{
    cb_map_clear(map);
    if (map->arena == NULL) CB_FREE(map->slots);
    map->slots = NULL;
    map->capacity = 0;
    map->count = 0;
    map->tombstones = 0;
    map->arena = NULL; // detach the arena so the map variable can be reused
}

CBDEF CB_Map_Iter cb_map_iter(CB_Map* map)
{
    CB_Map_Iter it = CB_ZERO;
    it.map = map;
    it.index = 0;
    return it;
}

CBDEF bool cb_map_next(CB_Map_Iter* it)
{
    while (it->index < it->map->capacity) {
        CB_Map_Slot* slot = &it->map->slots[it->index++];
        if (slot->state == CB_MAP_USED) {
            it->key = slot->key;
            it->value = slot->value;
            return true;
        }
    }
    return false;
}

#endif // CB_IMPLEMENTATION

////////////////////////////////////////////////////////////////////////////////////////////////////
// HashMap (uint64 keys)
////////////////////////////////////////////////////////////////////////////////////////////////////
// Same open-addressing implementation as CB_Map, with integer keys and a cheaper hash/comparison.
// Keys need no separate allocation, so this version is leaner than the string-key one.

typedef struct {
    uint64_t key;
    void* value;
    uint64_t hash;
    uint8_t state;
} CB_Map_U64_Slot;

typedef struct {
    CB_Map_U64_Slot* slots;
    size_t capacity;
    size_t count;
    size_t tombstones;
    CB_Arena* arena;
} CB_Map_U64;

typedef struct {
    CB_Map_U64* map;
    size_t index;
    uint64_t key;
    void* value;
} CB_Map_U64_Iter;

// ---- declarations ----
CBDEF void cb_map_u64_init_capacity(CB_Map_U64* map, size_t capacity);
CBDEF void cb_map_u64_init_arena(CB_Map_U64* map, CB_Arena* arena, size_t capacity);
CBDEF void cb_map_u64_put(CB_Map_U64* map, uint64_t key, void* value);
CBDEF bool cb_map_u64_get(const CB_Map_U64* map, uint64_t key, void** out);
CBDEF bool cb_map_u64_has(const CB_Map_U64* map, uint64_t key);
CBDEF bool cb_map_u64_del(CB_Map_U64* map, uint64_t key);
CBDEF size_t cb_map_u64_count(const CB_Map_U64* map);
CBDEF void cb_map_u64_clear(CB_Map_U64* map);
CBDEF void cb_map_u64_free(CB_Map_U64* map);
CBDEF CB_Map_U64_Iter cb_map_u64_iter(CB_Map_U64* map);
CBDEF bool cb_map_u64_next(CB_Map_U64_Iter* it);
#define cb_map_u64_foreach(map, it) \
    for (CB_Map_U64_Iter it = cb_map_u64_iter(map); cb_map_u64_next(&it);)

#ifdef CB_IMPLEMENTATION

CBDEF void* cb__map_u64_alloc(CB_Map_U64* map, size_t size)
{
    if (map->arena != NULL) return cb_arena_alloc(map->arena, size);
    void* p = CB_REALLOC(NULL, size);
    cb_alloc_check(p, size);
    return p;
}

CBDEF CB_Map_U64_Slot* cb__map_u64_find_slot(const CB_Map_U64* map, uint64_t key, uint64_t hash, bool for_insert)
{
    if (map->capacity == 0) return NULL;

    size_t mask = map->capacity - 1;
    size_t i = (size_t)hash & mask;
    CB_Map_U64_Slot* first_tombstone = NULL;

    for (size_t probe = 0; probe < map->capacity; ++probe) {
        CB_Map_U64_Slot* slot = &map->slots[i];
        if (slot->state == CB_MAP_EMPTY) {
            if (for_insert) return first_tombstone != NULL ? first_tombstone : slot;
            return NULL;
        }
        if (slot->state == CB_MAP_TOMBSTONE) {
            if (first_tombstone == NULL) first_tombstone = slot;
        } else if (slot->hash == hash && slot->key == key) {
            return slot;
        }
        i = (i + 1) & mask;
    }
    return for_insert ? first_tombstone : NULL;
}

CBDEF void cb__map_u64_resize_to(CB_Map_U64* map, size_t new_capacity)
{
    CB_Map_U64_Slot* new_slots = (CB_Map_U64_Slot*)cb__map_u64_alloc(map, new_capacity * sizeof(CB_Map_U64_Slot));
    memset(new_slots, 0, new_capacity * sizeof(CB_Map_U64_Slot));

    CB_Map_U64_Slot* old_slots = map->slots;
    size_t old_capacity = map->capacity;

    map->slots = new_slots;
    map->capacity = new_capacity;
    map->count = 0;
    map->tombstones = 0;

    for (size_t i = 0; i < old_capacity; ++i) {
        if (old_slots[i].state != CB_MAP_USED) continue;
        CB_Map_U64_Slot* dst = cb__map_u64_find_slot(map, old_slots[i].key, old_slots[i].hash, true);
        *dst = old_slots[i];
        map->count += 1;
    }

    if (map->arena == NULL) CB_FREE(old_slots);
}

CBDEF void cb__map_u64_grow(CB_Map_U64* map)
{
    cb__map_u64_resize_to(map, map->capacity == 0 ? CB_MAP_INIT_CAPACITY : map->capacity * 2);
}

CBDEF void cb_map_u64_init_capacity(CB_Map_U64* map, size_t capacity)
{
    CB_ASSERT(map->slots == NULL && map->capacity == 0 && "the map must be zero-initialized and initialized only once");
    if (capacity == 0) return;

    size_t wanted = capacity * 4 / 3 + 1;
    size_t power = CB_MAP_INIT_CAPACITY;
    while (power < wanted) power *= 2;
    cb__map_u64_resize_to(map, power);
}

CBDEF void cb_map_u64_init_arena(CB_Map_U64* map, CB_Arena* arena, size_t capacity)
{
    map->arena = arena; // set first, otherwise the initial slots would go through malloc
    cb_map_u64_init_capacity(map, capacity);
}

CBDEF void cb_map_u64_put(CB_Map_U64* map, uint64_t key, void* value)
{
    if (map->capacity == 0) cb__map_u64_grow(map);
    if ((map->count + map->tombstones + 1) * 4 >= map->capacity * 3) cb__map_u64_grow(map);

    uint64_t hash = cb_hash_u64(key);
    CB_Map_U64_Slot* slot = cb__map_u64_find_slot(map, key, hash, true);
    CB_ASSERT(slot != NULL);

    if (slot->state == CB_MAP_USED) {
        slot->value = value;
        return;
    }
    if (slot->state == CB_MAP_TOMBSTONE) map->tombstones -= 1;

    slot->key = key;
    slot->value = value;
    slot->hash = hash;
    slot->state = CB_MAP_USED;
    map->count += 1;
}

CBDEF bool cb_map_u64_get(const CB_Map_U64* map, uint64_t key, void** out)
{
    if (map->capacity == 0) return false;
    uint64_t hash = cb_hash_u64(key);
    CB_Map_U64_Slot* slot = cb__map_u64_find_slot(map, key, hash, false);
    if (slot == NULL) return false;
    if (out != NULL) *out = slot->value;
    return true;
}

CBDEF bool cb_map_u64_has(const CB_Map_U64* map, uint64_t key)
{
    return cb_map_u64_get(map, key, NULL);
}

CBDEF size_t cb_map_u64_count(const CB_Map_U64* map)
{
    return map->count;
}

CBDEF bool cb_map_u64_del(CB_Map_U64* map, uint64_t key)
{
    if (map->capacity == 0) return false;
    uint64_t hash = cb_hash_u64(key);
    CB_Map_U64_Slot* slot = cb__map_u64_find_slot(map, key, hash, false);
    if (slot == NULL) return false;

    slot->key = 0;
    slot->value = NULL;
    slot->hash = 0;
    slot->state = CB_MAP_TOMBSTONE;
    map->count -= 1;
    map->tombstones += 1;
    return true;
}

CBDEF void cb_map_u64_clear(CB_Map_U64* map)
{
    if (map->capacity > 0) memset(map->slots, 0, map->capacity * sizeof(CB_Map_U64_Slot));
    map->count = 0;
    map->tombstones = 0;
}

CBDEF void cb_map_u64_free(CB_Map_U64* map)
{
    if (map->arena == NULL) CB_FREE(map->slots);
    map->slots = NULL;
    map->capacity = 0;
    map->count = 0;
    map->tombstones = 0;
    map->arena = NULL;
}

CBDEF CB_Map_U64_Iter cb_map_u64_iter(CB_Map_U64* map)
{
    CB_Map_U64_Iter it = CB_ZERO;
    it.map = map;
    it.index = 0;
    return it;
}

CBDEF bool cb_map_u64_next(CB_Map_U64_Iter* it)
{
    while (it->index < it->map->capacity) {
        CB_Map_U64_Slot* slot = &it->map->slots[it->index++];
        if (slot->state == CB_MAP_USED) {
            it->key = slot->key;
            it->value = slot->value;
            return true;
        }
    }
    return false;
}

#endif // CB_IMPLEMENTATION

////////////////////////////////////////////////////////////////////////////////////////////////////
// File System
////////////////////////////////////////////////////////////////////////////////////////////////////
// PATH_MAX is optional in POSIX, so provide a fallback before anyone uses it.
#ifndef CB_PATH_MAX
#ifdef PATH_MAX
#define CB_PATH_MAX PATH_MAX
#else
#define CB_PATH_MAX 4096
#endif /* PATH_MAX */
#endif /* !CB_PATH_MAX */
#ifdef _WIN32
// Based on https://stackoverflow.com/a/75644008 (.NET uses a 4096 * sizeof(WCHAR) stack buffer).
#ifndef CB_WIN32_ERR_MSG_SIZE
#define CB_WIN32_ERR_MSG_SIZE (4 * 1024)
#endif // CB_WIN32_ERR_MSG_SIZE

CBDEF char* cb_win32_error_message(DWORD err);
#endif // _WIN32

CBDEF const char* cb_path_name(const char* path);
CBDEF bool cb_rename(const char* old_path, const char* new_path);
CBDEF int cb_file_exists(const char* file_path);
CBDEF const char* cb_get_current_dir_temp(void);
CBDEF bool cb_set_current_dir(const char* path);

CBDEF char* cb_temp_dir_name(const char* path);
CBDEF char* cb_temp_file_name(const char* path);
CBDEF char* cb_temp_file_ext(const char* path);
CBDEF char* cb_temp_running_executable_path(void);

// File types are split only five ways: error / regular file / directory / symlink / other.
// Finer kinds (FIFO, socket, device, junction) are not implemented.

typedef enum {
    CB_FILE_ERROR = -1,
    CB_FILE_REGULAR = 0,
    CB_FILE_DIRECTORY,
    CB_FILE_SYMLINK,
    CB_FILE_OTHER,
} CB_File_Type;

typedef enum {
    CB_WALK_CONT,
    CB_WALK_SKIP,
    CB_WALK_STOP,
} CB_Walk_Action;

typedef struct {
    const char* path;
    CB_File_Type type;
    size_t level;
    void* data;
    CB_Walk_Action* action;
} CB_Walk_Entry;

typedef bool (*CB_Walk_Func)(CB_Walk_Entry entry);

// Only the fields you care about are needed: cb_walk_dir(root, fn, .post_order = true) / (.data = &x);
// the rest fall back to the defaults from CB__DEFAULT (see the General section).
// The struct must be tagged: in C++ an "anonymous struct with default member initializers"
// is reported by -Wnon-c-typedef-for-linkage.
typedef struct CB_Walk_Dir_Opt {
    void* data CB__DEFAULT(nullptr);
    bool post_order CB__DEFAULT(false);
} CB_Walk_Dir_Opt;

CBDEF bool cb_delete_walk_entry(CB_Walk_Entry entry);
CBDEF bool cb__walk_dir_opt_impl(CB_String_Builder* file_path, CB_Walk_Func func, size_t level, bool* stop, CB_Walk_Dir_Opt opt);
CBDEF bool cb_walk_dir_opt(const char* root, CB_Walk_Func func, CB_Walk_Dir_Opt opt);
#define cb_walk_dir(root, func, ...) cb_walk_dir_opt((root), (func), CB_CLIT(CB_Walk_Dir_Opt){__VA_ARGS__})

typedef struct {
    char* name;
    bool error;

    struct {
#ifdef _WIN32
        WIN32_FIND_DATA win32_data;
        HANDLE win32_hFind;
        bool win32_init;
#else
        DIR* posix_dir;
        struct dirent* posix_ent;
#endif // _WIN32
    } cb__private;
} CB_Dir_Entry;

// Open a directory for iteration. Returns false on failure (cb_log prints the error).
CBDEF bool cb_dir_entry_open(const char* dir_path, CB_Dir_Entry* dir);
// Fetch the next entry. false means the end or an error (dir->error is set in that case).
CBDEF bool cb_dir_entry_next(CB_Dir_Entry* dir);
CBDEF void cb_dir_entry_close(CB_Dir_Entry dir);

typedef struct {
    const char** items;
    size_t count;
    size_t capacity;
} CB_File_Paths;


// Recursive mkdir (mkdir -p): intermediate levels are created, an existing directory is fine.
CBDEF bool cb_mkdir_if_not_exists(const char* path);
CBDEF bool cb_copy_file(const char* src_path, const char* dst_path);
CBDEF bool cb_copy_directory_recursively(const char* src_path, const char* dst_path);
CBDEF bool cb_delete_directory_recursively(const char* dir_path);
// List a directory's entry names (without "." and ".."); the memory is temp storage.
CBDEF bool cb_read_entire_dir(const char* parent, CB_File_Paths* children);
CBDEF bool cb_write_entire_file(const char* path, const void* data, size_t size);
CBDEF CB_File_Type cb_get_file_type(const char* path);
CBDEF bool cb_delete_file(const char* path);

#ifdef CB_IMPLEMENTATION

#ifdef _WIN32

CBDEF char* cb_win32_error_message(DWORD err)
{
    static char win32ErrMsg[CB_WIN32_ERR_MSG_SIZE] = CB_ZERO;
    DWORD errMsgSize = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, err, LANG_USER_DEFAULT, win32ErrMsg,
                                      CB_WIN32_ERR_MSG_SIZE, NULL);

    if (errMsgSize == 0) {
        if (GetLastError() != ERROR_MR_MID_NOT_FOUND) {
            if (sprintf(win32ErrMsg, "Could not get error message for 0x%lX", err) > 0) {
                return (char*)&win32ErrMsg;
            } else {
                return NULL;
            }
        } else {
            if (sprintf(win32ErrMsg, "Invalid Windows Error code (0x%lX)", err) > 0) {
                return (char*)&win32ErrMsg;
            } else {
                return NULL;
            }
        }
    }

    while (errMsgSize > 1 && isspace(win32ErrMsg[errMsgSize - 1])) {
        win32ErrMsg[--errMsgSize] = '\0';
    }

    return win32ErrMsg;
}
#endif // _WIN32

CBDEF bool cb_delete_walk_entry(CB_Walk_Entry entry)
{
    return cb_delete_file(entry.path);
}

CBDEF bool cb__walk_dir_opt_impl(CB_String_Builder* file_path, CB_Walk_Func func, size_t level, bool* stop, CB_Walk_Dir_Opt opt)
{
    CB_ASSERT(file_path->count > 0 && "file_path was probably not properly NULL-terminated");
    bool result = true;

    CB_Dir_Entry dir = CB_ZERO;
    size_t saved_file_path_count = file_path->count;
    CB_Walk_Action action = CB_WALK_CONT;

    CB_File_Type file_type = cb_get_file_type(file_path->items);
    if (file_type < 0) cb_return_defer(false);

    // Pre-order: handle this entry, then recurse into the children
    if (!opt.post_order) {
        if (!func(CB_CLIT(CB_Walk_Entry){
                .path = file_path->items,
                .type = file_type,
                .level = level,
                .data = opt.data,
                .action = &action,
            })) cb_return_defer(false);

        switch (action) {
        case CB_WALK_CONT:
            break;
        case CB_WALK_STOP:
            *stop = true;
            cb_return_defer(true); // STOP ends the walk too: return right away, do not fall through to SKIP
        case CB_WALK_SKIP:
            cb_return_defer(true);
        default:
            CB_UNREACHABLE("CB_Walk_Action");
        }
    }

    if (file_type == CB_FILE_DIRECTORY) {
        if (!cb_dir_entry_open(file_path->items, &dir)) cb_return_defer(false);
        while (true) {
            // next entry
            if (!cb_dir_entry_next(&dir)) {
                if (!dir.error) break;
                cb_return_defer(false);
            }

            // skip . and ..
            if (strcmp(dir.name, ".") == 0) continue;
            if (strcmp(dir.name, "..") == 0) continue;

            // Rebuild the child path from the parent length. Not saved - 1: the StringBuilder count does
            // not include the terminator (items[count] is the '\0').
            file_path->count = saved_file_path_count;
#ifdef _WIN32
            cb_sb_appendf(file_path, "\\%s", dir.name);
#else
            cb_sb_appendf(file_path, "/%s", dir.name);
#endif // _WIN32

            // recurse into the subdirectory
            if (!cb__walk_dir_opt_impl(file_path, func, level + 1, stop, opt)) cb_return_defer(false);
            if (*stop) cb_return_defer(true);
        }
        // recursion overwrote items[saved] with '/', put the terminator back
        file_path->count = saved_file_path_count;
        cb__sb_terminate(file_path);
    }

    // Post-order: recurse into the children first and handle this entry last
    if (opt.post_order) {
        if (!func(CB_CLIT(CB_Walk_Entry){
                .path = file_path->items,
                .type = file_type,
                .level = level,
                .data = opt.data,
                .action = &action,
            })) cb_return_defer(false);

        switch (action) {
        case CB_WALK_CONT:
            break;
        case CB_WALK_STOP:
            *stop = true;
            cb_return_defer(true); // STOP ends the walk too: return right away, do not fall through to SKIP
        case CB_WALK_SKIP:
            cb_return_defer(true);
        default:
            CB_UNREACHABLE("CB_Walk_Action");
        }
    }

defer:
    // restore file_path
    file_path->count = saved_file_path_count;
    cb_da_last(file_path) = '\0';
    cb_dir_entry_close(dir);
    return result;
}

CBDEF bool cb_walk_dir_opt(const char* root, CB_Walk_Func func, CB_Walk_Dir_Opt opt)
{
    CB_String_Builder file_path = CB_ZERO;

    cb_sb_appendf(&file_path, "%s", root);

    bool stop = false;
    bool ok = cb__walk_dir_opt_impl(&file_path, func, 0, &stop, opt);
    CB_FREE(file_path.items);
    return ok;
}

CBDEF const char* cb_path_name(const char* path)
{
#ifdef _WIN32
    const char* p1 = strrchr(path, '/');
    const char* p2 = strrchr(path, '\\');
    const char* p = (p1 > p2) ? p1 : p2; // NULL is ignored if the other search is successful
    return p ? p + 1 : path;
#else
    const char* p = strrchr(path, '/');
    return p ? p + 1 : path;
#endif // _WIN32
}

CBDEF bool cb_rename(const char* old_path, const char* new_path)
{
#ifdef CB_ENABLE_ECHO
    cb_log(CB_INFO, "Renaming %s -> %s", old_path, new_path);
#endif // CB_ENABLE_ECHO
#ifdef _WIN32
    if (!MoveFileEx(old_path, new_path, MOVEFILE_REPLACE_EXISTING)) {
        cb_log(CB_ERROR, "Could not rename %s to %s: %s", old_path, new_path, cb_win32_error_message(GetLastError()));
        return false;
    }
#else
    if (rename(old_path, new_path) < 0) {
        cb_log(CB_ERROR, "Could not rename %s to %s: %s", old_path, new_path, strerror(errno));
        return false;
    }
#endif // _WIN32
    return true;
}

// Returns 0 when the path does not exist.
CBDEF int cb_file_exists(const char* file_path)
{
#ifdef _WIN32
    return GetFileAttributesA(file_path) != INVALID_FILE_ATTRIBUTES;
#else
    return access(file_path, F_OK) == 0;
#endif // _WIN32
}

CBDEF const char* cb_get_current_dir_temp(void)
{
#ifdef _WIN32
    DWORD nBufferLength = GetCurrentDirectory(0, NULL);
    if (nBufferLength == 0) {
        cb_log(CB_ERROR, "Could not get current directory: %s", cb_win32_error_message(GetLastError()));
        return NULL;
    }

    char* buffer = (char*)cb_temp_alloc(nBufferLength);
    if (GetCurrentDirectory(nBufferLength, buffer) == 0) {
        cb_log(CB_ERROR, "Could not get current directory: %s", cb_win32_error_message(GetLastError()));
        return NULL;
    }

    return buffer;
#else
    // PATH_MAX is optional in POSIX, so use the fallback value and retry with a bigger buffer if needed.
    char* buffer = (char*)cb_temp_alloc(CB_PATH_MAX);
    while (getcwd(buffer, CB_PATH_MAX) == NULL) {
        if (errno != ERANGE) {
            cb_log(CB_ERROR, "Could not get current directory: %s", strerror(errno));
            return NULL;
        }
        // The path is longer than the buffer: retry with a larger one (the old block stays in temp storage)
        buffer = (char*)cb_temp_alloc(CB_PATH_MAX * 2);
        if (getcwd(buffer, CB_PATH_MAX * 2) != NULL) break;
        cb_log(CB_ERROR, "Could not get current directory: %s", strerror(errno));
        return NULL;
    }

    return buffer;
#endif // _WIN32
}

CBDEF bool cb_set_current_dir(const char* path)
{
#ifdef _WIN32
    if (!SetCurrentDirectoryA(path)) {
        cb_log(CB_ERROR, "Could not set current directory to %s: %s", path, cb_win32_error_message(GetLastError()));
        return false;
    }
    return true;
#else
    if (chdir(path) < 0) {
        cb_log(CB_ERROR, "Could not set current directory to %s: %s", path, strerror(errno));
        return false;
    }
    return true;
#endif // _WIN32
}

CBDEF char* cb_temp_dir_name(const char* path)
{
#ifdef _WIN32
    if (!path) path = "";
    char* drive = (char*)cb_temp_alloc(_MAX_DRIVE);
    char* dir = (char*)cb_temp_alloc(_MAX_DIR);
    // https://learn.microsoft.com/en-us/previous-versions/visualstudio/visual-studio-2010/8e46eyt7(v=vs.100)
    errno_t ret = _splitpath_s(path, drive, _MAX_DRIVE, dir, _MAX_DIR, NULL, 0, NULL, 0);
    CB_ASSERT(ret == 0);
    return cb_temp_sprintf("%s%s", drive, dir);
#else
    // Taken from musl's dirname: libc vendors do not agree on whether dirname(3) modifies its argument.
    if (!path || !*path) return cb_temp_strdup(".");
    size_t i = strlen(path) - 1;
    for (; path[i] == '/'; i--)
        if (!i) return cb_temp_strdup("/");
    for (; path[i] != '/'; i--)
        if (!i) return cb_temp_strdup(".");
    for (; path[i] == '/'; i--)
        if (!i) return cb_temp_strdup("/");
    return cb_temp_strndup(path, i + 1);
#endif // _WIN32
}

CBDEF char* cb_temp_file_name(const char* path)
{
#ifdef _WIN32
    if (!path) path = ""; // Treating NULL as empty.
    char* fname = (char*)cb_temp_alloc(_MAX_FNAME);
    char* ext = (char*)cb_temp_alloc(_MAX_EXT);
    // https://learn.microsoft.com/en-us/previous-versions/visualstudio/visual-studio-2010/8e46eyt7(v=vs.100)
    errno_t ret = _splitpath_s(path, NULL, 0, NULL, 0, fname, _MAX_FNAME, ext, _MAX_EXT);
    CB_ASSERT(ret == 0);
    return cb_temp_sprintf("%s%s", fname, ext);
#else
    // Taken from musl's basename: libc vendors do not agree on whether basename(3) modifies its argument.
    if (!path || !*path) return cb_temp_strdup(".");
    char* s = cb_temp_strdup(path);
    size_t i = strlen(s) - 1;
    for (; i && s[i] == '/'; i--)
        s[i] = 0;
    for (; i && s[i - 1] != '/'; i--)
        ;
    return s + i;
#endif // _WIN32
}

CBDEF char* cb_temp_file_ext(const char* path)
{
#ifdef _WIN32
    if (!path) path = ""; // Treating NULL as empty.
    char* ext = (char*)cb_temp_alloc(_MAX_EXT);
    // https://learn.microsoft.com/en-us/previous-versions/visualstudio/visual-studio-2010/8e46eyt7(v=vs.100)
    errno_t ret = _splitpath_s(path, NULL, 0, NULL, 0, NULL, 0, ext, _MAX_EXT);
    CB_ASSERT(ret == 0);
    return ext;
#else
    return strrchr(cb_temp_file_name(path), '.');
#endif // _WIN32
}

CBDEF char* cb_temp_running_executable_path(void)
{
#if defined(__linux__)
    char buf[4096];
    int length = readlink("/proc/self/exe", buf, CB_ARRAY_LEN(buf));
    if (length < 0) return cb_temp_strdup("");
    return cb_temp_strndup(buf, length);
#elif defined(_WIN32)
    char buf[MAX_PATH];
    int length = GetModuleFileNameA(NULL, buf, MAX_PATH);
    return cb_temp_strndup(buf, length);
#elif defined(__APPLE__)
    char buf[4096];
    uint32_t size = CB_ARRAY_LEN(buf);
    if (_NSGetExecutablePath(buf, &size) != 0) return cb_temp_strdup("");
    int length = strlen(buf);
    return cb_temp_strndup(buf, length);
#elif defined(__FreeBSD__)
    char buf[4096];
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1};
    size_t length = sizeof(buf);
    if (sysctl(mib, 4, buf, &length, NULL, 0) < 0) return cb_temp_strdup("");
    return cb_temp_strndup(buf, length);
#elif defined(__HAIKU__)
    int cookie = 0;
    image_info info;
    while (get_next_image_info(B_CURRENT_TEAM, &cookie, &info) == B_OK)
        if (info.type == B_APP_IMAGE)
            break;
    return cb_temp_strndup(info.name, strlen(info.name));
#else
    fprintf(stderr, "%s:%d: TODO: cb_temp_running_executable_path is not implemented for this platform\n", __FILE__, __LINE__);
    return cb_temp_strdup("");
#endif
}

CBDEF bool cb_dir_entry_open(const char* dir_path, CB_Dir_Entry* dir)
{
    memset(dir, 0, sizeof(*dir));
#ifdef _WIN32
    CB_Arena_Mark temp_mark = cb_temp_save();
    char* buffer = cb_temp_sprintf("%s\\*", dir_path);
    dir->cb__private.win32_hFind = FindFirstFile(buffer, &dir->cb__private.win32_data);
    cb_temp_rewind(temp_mark);

    if (dir->cb__private.win32_hFind == INVALID_HANDLE_VALUE) {
        cb_log(CB_ERROR, "Could not open directory %s: %s", dir_path, cb_win32_error_message(GetLastError()));
        dir->error = true;
        return false;
    }
#else
    dir->cb__private.posix_dir = opendir(dir_path);
    if (dir->cb__private.posix_dir == NULL) {
        cb_log(CB_ERROR, "Could not open directory %s: %s", dir_path, strerror(errno));
        dir->error = true;
        return false;
    }
#endif // _WIN32
    return true;
}

CBDEF bool cb_dir_entry_next(CB_Dir_Entry* dir)
{
#ifdef _WIN32
    if (!dir->cb__private.win32_init) {
        dir->cb__private.win32_init = true;
        dir->name = dir->cb__private.win32_data.cFileName;
        return true;
    }

    if (!FindNextFile(dir->cb__private.win32_hFind, &dir->cb__private.win32_data)) {
        if (GetLastError() == ERROR_NO_MORE_FILES) return false;
        cb_log(CB_ERROR, "Could not read next directory entry: %s", cb_win32_error_message(GetLastError()));
        dir->error = true;
        return false;
    }
    dir->name = dir->cb__private.win32_data.cFileName;
#else
    errno = 0;
    dir->cb__private.posix_ent = readdir(dir->cb__private.posix_dir);
    if (dir->cb__private.posix_ent == NULL) {
        if (errno == 0) return false;
        cb_log(CB_ERROR, "Could not read next directory entry: %s", strerror(errno));
        dir->error = true;
        return false;
    }
    dir->name = dir->cb__private.posix_ent->d_name;
#endif // _WIN32
    return true;
}

CBDEF void cb_dir_entry_close(CB_Dir_Entry dir)
{
#ifdef _WIN32
    FindClose(dir.cb__private.win32_hFind);
#else
    if (dir.cb__private.posix_dir) closedir(dir.cb__private.posix_dir);
#endif // _WIN32
}

// copy_file_range() needs Linux 4.5+
CBDEF bool cb_copy_file(const char* src_path, const char* dst_path)
{
#ifdef CB_ENABLE_ECHO
    cb_log(CB_INFO, "Copying '%s' -> '%s'", src_path, dst_path);
#endif // CB_ENABLE_ECHO

#ifdef _WIN32
    if (!CopyFile(src_path, dst_path, FALSE)) {
        cb_log(CB_ERROR, "Could not copy file: '%s'", cb_win32_error_message(GetLastError()));
        return false;
    }
    return true;
#else
    int src_fd = -1;
    int dst_fd = -1;
    size_t buf_size = 32 * 1024;
    char* buf = (char*)CB_REALLOC(NULL, buf_size);
    cb_alloc_check(buf, buf_size);
    bool result = true;

    src_fd = open(src_path, O_RDONLY);
    if (src_fd < 0) {
        cb_log(CB_ERROR, "Could not open file '%s': %s", src_path, strerror(errno));
        cb_return_defer(false);
    }
    struct stat src_stat;
    if (fstat(src_fd, &src_stat) < 0) {
        cb_log(CB_ERROR, "Could not get mode of file '%s': %s", src_path, strerror(errno));
        cb_return_defer(false);
    }

    dst_fd = open(dst_path, O_CREAT | O_TRUNC | O_WRONLY, src_stat.st_mode);
    if (dst_fd < 0) {
        cb_log(CB_ERROR, "Could not create file '%s': %s", dst_path, strerror(errno));
        cb_return_defer(false);
    }

    while (true) {
        ssize_t n = read(src_fd, buf, buf_size);
        if (n == 0) break;
        if (n < 0) {
            cb_log(CB_ERROR, "Could not read from file '%s': %s", src_path, strerror(errno));
            cb_return_defer(false);
        }
        char* buf2 = buf;
        while (n > 0) {
            ssize_t m = write(dst_fd, buf2, n);
            if (m < 0) {
                cb_log(CB_ERROR, "Could not write to file '%s': %s", dst_path, strerror(errno));
                cb_return_defer(false);
            }
            n -= m;
            buf2 += m;
        }
    }

defer:
    CB_FREE(buf);
    close(src_fd);
    close(dst_fd);
    return result;
#endif
}

CBDEF bool cb_copy_directory_recursively(const char* src_path, const char* dst_path)
{
    bool result = true;
    CB_File_Paths children = CB_ZERO;
    CB_String_Builder src_sb = CB_ZERO;
    CB_String_Builder dst_sb = CB_ZERO;
    CB_Arena_Mark temp_checkpoint = cb_temp_save();

    CB_File_Type type = cb_get_file_type(src_path);
    if (type < 0) return false;

    switch (type) {
    case CB_FILE_DIRECTORY: {
        if (!cb_mkdir_if_not_exists(dst_path)) cb_return_defer(false);
        if (!cb_read_entire_dir(src_path, &children)) cb_return_defer(false);

        for (size_t i = 0; i < children.count; ++i) {
            if (strcmp(children.items[i], ".") == 0) continue;
            if (strcmp(children.items[i], "..") == 0) continue;

            src_sb.count = 0;
            cb_sb_append_cstr(&src_sb, src_path);
            cb_sb_append_cstr(&src_sb, "/");
            cb_sb_append_cstr(&src_sb, children.items[i]);

            dst_sb.count = 0;
            cb_sb_append_cstr(&dst_sb, dst_path);
            cb_sb_append_cstr(&dst_sb, "/");
            cb_sb_append_cstr(&dst_sb, children.items[i]);

            if (!cb_copy_directory_recursively(src_sb.items, dst_sb.items)) {
                cb_return_defer(false);
            }
        }
    } break;

    case CB_FILE_REGULAR: {
        if (!cb_copy_file(src_path, dst_path)) cb_return_defer(false);
    } break;

    case CB_FILE_SYMLINK: {
        cb_log(CB_WARN, "TODO: cb_copy_directory_recursively not support symlinks yet");
    } break;

    case CB_FILE_ERROR:
    case CB_FILE_OTHER: {
        cb_log(CB_ERROR, "Unsupported type of file %s", src_path);
        cb_return_defer(false);
    } break;

    default:
        CB_UNREACHABLE("cb_copy_directory_recursively");
    }

defer:
    cb_temp_rewind(temp_checkpoint);
    cb_da_free(src_sb);
    cb_da_free(dst_sb);
    cb_da_free(children);
    return result;
}

CBDEF bool cb_delete_directory_recursively(const char* dir_path)
{
    // No designated initializers here: partial designators trigger -Wmissing-designated-field-initializers in C++
    CB_Walk_Dir_Opt opt = CB_ZERO;
    opt.post_order = true;
    return cb_walk_dir_opt(dir_path, cb_delete_walk_entry, opt);
}

// For parent = "~/dir/" the children are "dir2", "file1", "file2" ("." and ".." are skipped).
CBDEF bool cb_read_entire_dir(const char* parent, CB_File_Paths* children)
{
    if (strlen(parent) == 0) {
        cb_log(CB_ERROR, "Cannot read empty path");
        return false;
    }
    bool result = true;
    CB_Dir_Entry dir = CB_ZERO;
    if (!cb_dir_entry_open(parent, &dir)) cb_return_defer(false);
    while (cb_dir_entry_next(&dir)) {
        // Skip "." and "..": they are noise and a naive "recurse over the listing" loop would never end.
        if (strcmp(dir.name, ".") == 0 || strcmp(dir.name, "..") == 0) continue;
        cb_da_append(children, cb_temp_strdup(dir.name));
    }
    if (dir.error) cb_return_defer(false);

defer:
    cb_dir_entry_close(dir);
    return result;
}

CBDEF bool cb_write_entire_file(const char* path, const void* data, size_t size)
{
    bool result = true;

    const char* buf = NULL;
    FILE* f = fopen(path, "wb");
    if (f == NULL) {
        cb_log(CB_ERROR, "Could not open file %s for writing: %s\n", path, strerror(errno));
        cb_return_defer(false);
    }

    buf = (const char*)data;
    while (size > 0) {
        size_t n = fwrite(buf, 1, size, f);
        if (ferror(f)) {
            cb_log(CB_ERROR, "Could not write into file %s: %s\n", path, strerror(errno));
            cb_return_defer(false);
        }
        size -= n;
        buf += n;
    }

defer:
    if (f) fclose(f);
    return result;
}

CBDEF CB_File_Type cb_get_file_type(const char* path)
{
#ifdef _WIN32
    DWORD attr = GetFileAttributesA(path);
    if (attr == INVALID_FILE_ATTRIBUTES) {
        cb_log(CB_ERROR, "Could not get file attrbutes of %s: %s", path, cb_win32_error_message(GetLastError()));
        return CB_FILE_ERROR;
    }

    // Order matters: a symlink (including directory links and junctions) has both DIRECTORY and
    // REPARSE_POINT set; checking REPARSE_POINT first matches POSIX lstat (the link, not the target).
    if (attr & FILE_ATTRIBUTE_REPARSE_POINT) return CB_FILE_SYMLINK;
    if (attr & FILE_ATTRIBUTE_DIRECTORY) return CB_FILE_DIRECTORY;
    return CB_FILE_REGULAR;
#else
    struct stat statbuf;
    if (lstat(path, &statbuf) < 0) {
        cb_log(CB_ERROR, "Could not get stat of %s: %s", path, strerror(errno));
        return CB_FILE_ERROR;
    }

    if (S_ISREG(statbuf.st_mode)) return CB_FILE_REGULAR;
    if (S_ISDIR(statbuf.st_mode)) return CB_FILE_DIRECTORY;
    if (S_ISLNK(statbuf.st_mode)) return CB_FILE_SYMLINK;
    return CB_FILE_OTHER;
#endif // _WIN32
}

CBDEF bool cb_delete_file(const char* path)
{
#ifdef CB_ENABLE_ECHO
    cb_log(CB_INFO, "deleting %s", path);
#endif // !CB_ENABLE_ECHO

#ifdef _WIN32
    CB_File_Type type = cb_get_file_type(path);
    switch (type) {
    case CB_FILE_ERROR:
        // Could not get the type (for example the path is gone): behave like remove() failing on POSIX
        return false;
    case CB_FILE_DIRECTORY:
        if (!RemoveDirectoryA(path)) {
            cb_log(CB_ERROR, "Could not delete directory %s: %s", path, cb_win32_error_message(GetLastError()));
            return false;
        }
        break;
    case CB_FILE_REGULAR:
    case CB_FILE_SYMLINK:
    case CB_FILE_OTHER:
        if (!DeleteFileA(path)) {
            cb_log(CB_ERROR, "Could not delete file %s: %s", path, cb_win32_error_message(GetLastError()));
            return false;
        }
        break;
    default:
        CB_UNREACHABLE("CB_File_Type");
    }
    return true;
#else
    if (remove(path) < 0) {
        cb_log(CB_ERROR, "Could not delete file %s: %s", path, strerror(errno));
        return false;
    }
    return true;
#endif // _WIN32
}

#endif // CB_IMPLEMENTATION

////////////////////////////////////////////////////////////////////////////////////////////////////
// Path
////////////////////////////////////////////////////////////////////////////////////////////////////
// Results are allocated in temp storage. These functions only manipulate strings and never
// touch the file system (cb_path_absolute is the exception: it reads the current directory).

// ---- declarations ----
// Absolute path? POSIX: starts with '/'; Windows: starts with '/' or '\\', or looks like C:/
CBDEF bool cb_path_is_absolute(const char* path);
// Path separator predicate ('\\' on Windows, '/' elsewhere).
CBDEF bool cb_path_is_sep(char c);
// Join two path parts, handling separators (no doubled separator is produced).
// When b is absolute, a is ignored and b is returned (same convention as os.path.join).
CBDEF char* cb_path_join(const char* a, const char* b);
// Collapse doubled separators and resolve "." and "..". No symlinks, no file system access.
// Relative paths stay relative; a ".." at the root is dropped ("/.." -> "/").
CBDEF char* cb_path_normalize(const char* path);
// Turn a relative path into an absolute one (current directory + normalize).
CBDEF char* cb_path_absolute(const char* path);
// Replace the extension. new_ext may or may not start with a dot; NULL or "" removes it.
// Only the part after the last dot of the last path component is touched.
CBDEF char* cb_path_replace_ext(const char* path, const char* new_ext);

#ifdef _WIN32
#define CB_PATH_SEP '\\'
#else
#define CB_PATH_SEP '/'
#endif // _WIN32

#ifdef CB_IMPLEMENTATION

CBDEF bool cb_path_is_sep(char c)
{
#ifdef _WIN32
    return c == '/' || c == '\\';
#else
    return c == '/';
#endif // _WIN32
}

CBDEF bool cb_path_is_absolute(const char* path)
{
    if (path == NULL || path[0] == '\0') return false;
    if (cb_path_is_sep(path[0])) return true;
#ifdef _WIN32
    // Windows drive path: letter + ':' + separator
    if (isalpha((unsigned char)path[0]) && path[1] == ':' && cb_path_is_sep(path[2])) return true;
#endif // _WIN32
    return false;
}

CBDEF char* cb_path_join(const char* a, const char* b)
{
    if (a == NULL || a[0] == '\0') return cb_temp_strdup(b == NULL ? "" : b);
    if (b == NULL || b[0] == '\0') return cb_temp_strdup(a);
    if (cb_path_is_absolute(b)) return cb_temp_strdup(b);

    // b is definitely relative here. Whatever separator a ends with, the output uses the native one.
    size_t a_len = strlen(a);
    if (cb_path_is_sep(a[a_len - 1])) {
        return cb_temp_sprintf("%.*s%c%s", (int)(a_len - 1), a, CB_PATH_SEP, b);
    }
    return cb_temp_sprintf("%s%c%s", a, CB_PATH_SEP, b);
}

// Split on any path separator: on Windows both '/' and '\\' separate, so CB_PATH_SEP alone is not enough.
CBDEF CB_String_View cb__sv_chop_by_path_sep(CB_String_View* sv)
{
    size_t i = 0;
    while (i < sv->count && !cb_path_is_sep(sv->data[i])) i += 1;

    CB_String_View result = cb_sv_from_parts(sv->data, i);
    cb__sv_advance(sv, i < sv->count ? i + 1 : i);
    return result;
}

CBDEF char* cb_path_normalize(const char* path)
{
    if (path == NULL || path[0] == '\0') return cb_temp_strdup(".");

    CB_String_Builder sb = CB_ZERO;
    bool absolute = cb_path_is_sep(path[0]);
    if (absolute) cb_sb_append(&sb, CB_PATH_SEP);

    // Resolve with a component stack instead of doing complicated backtracking on the string
    CB_File_Paths parts = CB_ZERO;

    CB_String_View rest = cb_sv_from_cstr(path);
    while (rest.count > 0) {
        CB_String_View part = cb__sv_chop_by_path_sep(&rest);
        if (part.count == 0) continue;                      // doubled separator
        if (cb_sv_eq(part, cb_sv_from_cstr("."))) continue; // current directory

        if (cb_sv_eq(part, cb_sv_from_cstr(".."))) {
            if (parts.count > 0) {
                parts.count -= 1; // can go back up
            } else if (!absolute) {
                // a leading ".." of a relative path must be kept
                cb_da_append(&parts, cb_temp_strndup(part.data, part.count));
            }
            continue;
        }
        cb_da_append(&parts, cb_temp_strndup(part.data, part.count));
    }

    for (size_t i = 0; i < parts.count; ++i) {
        if (i > 0) cb_sb_append(&sb, CB_PATH_SEP);
        cb_sb_append_cstr(&sb, parts.items[i]);
    }
    // Empty path: the root for absolute input, "." for relative input
    if (sb.count == 0) {
        cb_sb_append(&sb, absolute ? CB_PATH_SEP : '.');
    }

    char* result = cb_temp_strdup(sb.items);
    cb_sb_free(sb);
    cb_da_free(parts);
    return result;
}

CBDEF char* cb_path_absolute(const char* path)
{
    if (path == NULL || path[0] == '\0') path = ".";
    if (cb_path_is_absolute(path)) return cb_path_normalize(path);

    const char* cwd = cb_get_current_dir_temp();
    if (cwd == NULL) return cb_path_normalize(path);
    return cb_path_normalize(cb_path_join(cwd, path));
}

CBDEF char* cb_path_replace_ext(const char* path, const char* new_ext)
{
    if (path == NULL) return cb_temp_strdup("");

    // Only look for the last dot inside the last path component
    size_t last_sep = 0;
    for (size_t i = 0; path[i] != '\0'; ++i) {
        if (cb_path_is_sep(path[i])) last_sep = i + 1;
    }

    size_t dot = (size_t)-1;
    for (size_t i = last_sep; path[i] != '\0'; ++i) {
        if (path[i] == '.') dot = i;
    }

    size_t base_len = (dot == (size_t)-1) ? strlen(path) : dot;

    if (new_ext == NULL || new_ext[0] == '\0') return cb_temp_strndup(path, base_len);

    const char* ext = new_ext[0] == '.' ? new_ext + 1 : new_ext;
    return cb_temp_sprintf("%.*s.%s", (int)base_len, path, ext);
}

#endif // CB_IMPLEMENTATION

////////////////////////////////////////////////////////////////////////////////////////////////////
// File System Extras
////////////////////////////////////////////////////////////////////////////////////////////////////
#ifdef _WIN32
#define CB_PROCESS_ID() ((unsigned long)GetCurrentProcessId())
#else
#define CB_PROCESS_ID() ((unsigned long)getpid())
#endif // _WIN32

// ---- declarations ----
// File size in bytes; (size_t)-1 on failure. A directory's size is platform dependent.
CBDEF size_t cb_file_size(const char* path);
// Last modification time (Unix seconds); -1 on failure.
CBDEF int64_t cb_file_mtime(const char* path);

// Atomic write: write a temp file next to the target, then rename over it.
// rename is atomic within one file system, so the target is never seen half written.
CBDEF bool cb_write_entire_file_atomic(const char* path, const void* data, size_t size);

// Create a symlink. On Windows this needs developer mode or administrator rights.
CBDEF bool cb_create_symlink(const char* target, const char* link_path);
// Read a symlink target (allocated in temp storage); NULL on failure.
CBDEF char* cb_read_symlink(const char* path);

// Wildcard match: * any run, ? one character, [abc] / [a-z] / [!abc] character classes.
CBDEF bool cb_glob_match(const char* pattern, const char* text);
// List the entries of dir matching pattern (one level only, no recursion).
// out receives full "dir/name" paths; the memory is temp storage.
CBDEF bool cb_glob(const char* dir, const char* pattern, CB_File_Paths* out);

// Read-only memory mapping, for zero-copy reads of large files.
typedef struct {
    void* data;
    size_t size;
#ifdef _WIN32
    HANDLE file_handle;
    HANDLE mapping_handle;
#else
    int fd;
#endif // _WIN32
} CB_Mmap;

CBDEF bool cb_mmap_open(const char* path, CB_Mmap* out);
CBDEF void cb_mmap_close(CB_Mmap* mmap);

#ifdef CB_IMPLEMENTATION

CBDEF size_t cb_file_size(const char* path)
{
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA info;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &info)) return (size_t)-1;
    return ((size_t)info.nFileSizeHigh << 32) | (size_t)info.nFileSizeLow;
#else
    struct stat statbuf;
    if (stat(path, &statbuf) < 0) return (size_t)-1;
    return (size_t)statbuf.st_size;
#endif // _WIN32
}

CBDEF int64_t cb_file_mtime(const char* path)
{
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA info;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &info)) return -1;
    ULARGE_INTEGER t;
    t.LowPart = info.ftLastWriteTime.dwLowDateTime;
    t.HighPart = info.ftLastWriteTime.dwHighDateTime;
    // FILETIME counts 100ns units since 1601-01-01: shift to the Unix epoch first, then to seconds
    return (int64_t)((t.QuadPart - 116444736000000000ull) / 10000000ull);
#else
    struct stat statbuf;
    if (stat(path, &statbuf) < 0) return -1;
    return (int64_t)statbuf.st_mtime;
#endif // _WIN32
}

// This implementation lives here while the declaration is in the File System section: it needs
// cb_path_is_sep, and each section of this file is "declarations then definitions".
CBDEF bool cb_mkdir_if_not_exists(const char* path)
{
    if (path == NULL || path[0] == '\0') return false;

    // One pass: every time a separator shows up, create the prefix collected so far (no recursion).
    bool result = true;
    CB_String_Builder partial = CB_ZERO;

    for (size_t i = 0;; ++i) {
        char c = path[i];
        if (c == '\0' || cb_path_is_sep(c)) {
            if (partial.count > 0) {
                // partial.items is already a valid C string (StringBuilder invariant), use it directly

                // A drive prefix like "C:" cannot be mkdir'd (it is the "current directory of the drive")
                bool is_drive = partial.count == 2 && partial.items[1] == ':';
                if (!is_drive) {
#ifdef _WIN32
                    int rc = _mkdir(partial.items);
#else
                    int rc = mkdir(partial.items, 0755);
#endif
                    if (rc < 0 && errno != EEXIST) {
                        cb_log(CB_ERROR, "Could not create directory '%s': %s", partial.items,
                               strerror(errno));
                        cb_return_defer(false);
                    }
#ifdef CB_ENABLE_ECHO
                    if (rc == 0) cb_log(CB_INFO, "Created directory '%s'", partial.items);
#endif
                }

            }
            if (c == '\0') break;
        }
        cb_sb_append(&partial, c);
    }

defer:
    cb_sb_free(partial);
    return result;
}

CBDEF bool cb_write_entire_file_atomic(const char* path, const void* data, size_t size)
{
    static size_t counter = 0;
    // The temp file sits next to the target so rename cannot cross a file system
    char* tmp_path = cb_temp_sprintf("%s.tmp%lu_%zu", path, CB_PROCESS_ID(), counter++);

    if (!cb_write_entire_file(tmp_path, data, size)) return false;

    if (!cb_rename(tmp_path, path)) {
        cb_delete_file(tmp_path); // do not leave garbage behind
        return false;
    }
    return true;
}

CBDEF bool cb_create_symlink(const char* target, const char* link_path)
{
#ifdef _WIN32
    DWORD flags = SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
    if (cb_get_file_type(target) == CB_FILE_DIRECTORY) flags |= SYMBOLIC_LINK_FLAG_DIRECTORY;
    if (!CreateSymbolicLinkA(link_path, target, flags)) {
        cb_log(CB_ERROR, "Could not create symlink %s -> %s: %s", link_path, target, cb_win32_error_message(GetLastError()));
        return false;
    }
    return true;
#else
    if (symlink(target, link_path) < 0) {
        cb_log(CB_ERROR, "Could not create symlink %s -> %s: %s", link_path, target, strerror(errno));
        return false;
    }
    return true;
#endif // _WIN32
}

CBDEF char* cb_read_symlink(const char* path)
{
#ifdef _WIN32
    // A Windows symlink is a reparse point; reading its target needs DeviceIoControl(FSCTL_GET_REPARSE_POINT).
    // The compact struct below is local so ddk/ntifs.h does not have to be included.
    typedef struct {
        ULONG tag;
        USHORT data_length;
        USHORT reserved;
        USHORT substitute_offset;
        USHORT substitute_length;
        USHORT print_offset;
        USHORT print_length;
        // Note: a symlink has an extra ULONG flags field that a mount point does not, so the start of
        // PathBuffer depends on the tag (see path_buffer_offset below).
    } CB__Reparse_Header;

    HANDLE handle = CreateFileA(path, 0,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                NULL, OPEN_EXISTING,
                                FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (handle == INVALID_HANDLE_VALUE) {
        cb_log(CB_ERROR, "Could not open %s: %s", path, cb_win32_error_message(GetLastError()));
        return NULL;
    }

    enum { CB__REPARSE_BUFFER_SIZE = 16384 }; // MAXIMUM_REPARSE_DATA_BUFFER_SIZE
    unsigned char buffer[CB__REPARSE_BUFFER_SIZE];
    DWORD bytes_returned = 0;
    if (!DeviceIoControl(handle, FSCTL_GET_REPARSE_POINT, NULL, 0,
                         buffer, (DWORD)sizeof(buffer), &bytes_returned, NULL)) {
        cb_log(CB_ERROR, "Could not read reparse point of %s: %s", path, cb_win32_error_message(GetLastError()));
        CloseHandle(handle);
        return NULL;
    }
    CloseHandle(handle);

    CB__Reparse_Header* header = (CB__Reparse_Header*)buffer;
    size_t path_buffer_offset;
    if (header->tag == IO_REPARSE_TAG_SYMLINK) {
        path_buffer_offset = 20; // one ULONG flags more than a mount point
    } else if (header->tag == IO_REPARSE_TAG_MOUNT_POINT) {
        path_buffer_offset = 16;
    } else {
        cb_log(CB_ERROR, "%s is not a symlink (reparse tag = 0x%lX)", path, (unsigned long)header->tag);
        return NULL;
    }

    if (path_buffer_offset + header->substitute_offset + header->substitute_length > bytes_returned) {
        cb_log(CB_ERROR, "incomplete reparse point data: %s", path);
        return NULL;
    }

    const WCHAR* wide = (const WCHAR*)(buffer + path_buffer_offset + header->substitute_offset);
    int wide_len = (int)(header->substitute_length / sizeof(WCHAR));

    // Targets usually carry the "\??\" prefix (NT object path); strip it to get a normal path
    if (wide_len >= 4 && wide[0] == L'\\' && wide[1] == L'?' && wide[2] == L'?' && wide[3] == L'\\') {
        wide += 4;
        wide_len -= 4;
    }

    int utf8_len = WideCharToMultiByte(CP_UTF8, 0, wide, wide_len, NULL, 0, NULL, NULL);
    if (utf8_len <= 0) {
        cb_log(CB_ERROR, "Could not convert symlink target of %s to UTF-8", path);
        return NULL;
    }
    char* result = (char*)cb_temp_alloc((size_t)utf8_len + 1);
    WideCharToMultiByte(CP_UTF8, 0, wide, wide_len, result, utf8_len, NULL, NULL);
    result[utf8_len] = '\0';
    return result;
#else
    size_t capacity = 256;
    for (;;) {
        char* buffer = (char*)cb_temp_alloc(capacity);
        ssize_t n = readlink(path, buffer, capacity);
        if (n < 0) {
            cb_log(CB_ERROR, "Could not read symlink %s: %s", path, strerror(errno));
            return NULL;
        }
        if ((size_t)n < capacity) { // readlink does not NUL-terminate, do it here
            buffer[n] = '\0';
            return buffer;
        }
        capacity *= 2; // truncated, try a bigger buffer
    }
#endif // _WIN32
}

CBDEF bool cb_glob_match(const char* pattern, const char* text)
{
    // Iterative '*' handling with backtracking instead of recursion (deep paths would blow the stack)
    const char* p = pattern;
    const char* t = text;
    const char* star_p = NULL;
    const char* star_t = NULL;

    while (*t != '\0') {
        if (*p == '*') {
            star_p = p++;
            star_t = t;
        } else if (*p == '?') {
            p += 1;
            t += 1;
        } else if (*p == '[') {
            const char* cls = p + 1;
            bool negate = false;
            if (*cls == '!' || *cls == '^') {
                negate = true;
                cls += 1;
            }
            bool matched = false;
            bool first = true;
            while (*cls != '\0' && (*cls != ']' || first)) {
                first = false;
                if (cls[1] == '-' && cls[2] != '\0' && cls[2] != ']') {
                    if ((unsigned char)*t >= (unsigned char)cls[0] && (unsigned char)*t <= (unsigned char)cls[2]) matched = true;
                    cls += 3;
                } else {
                    if (*cls == *t) matched = true;
                    cls += 1;
                }
            }
            if (*cls != ']') return false; // unterminated character class: treat it as no match

            if (matched == negate) { // no match: backtrack to the previous '*'
                if (star_p == NULL) return false;
                p = star_p + 1;
                t = ++star_t;
                continue;
            }
            p = cls + 1;
            t += 1;
        } else {
            if (*p != *t) {
                if (star_p == NULL) return false;
                p = star_p + 1;
                t = ++star_t;
                continue;
            }
            p += 1;
            t += 1;
        }
    }

    while (*p == '*') p += 1;
    return *p == '\0';
}

CBDEF bool cb_glob(const char* dir, const char* pattern, CB_File_Paths* out)
{
    bool result = true;
    CB_File_Paths children = CB_ZERO;

    if (!cb_read_entire_dir(dir, &children)) cb_return_defer(false);

    for (size_t i = 0; i < children.count; ++i) {
        const char* name = children.items[i];
        if (!cb_glob_match(pattern, name)) continue;
        cb_da_append(out, cb_path_join(dir, name));
    }

defer:
    cb_da_free(children);
    return result;
}

CBDEF bool cb_mmap_open(const char* path, CB_Mmap* out)
{
    memset(out, 0, sizeof(*out));
#ifndef _WIN32
    out->fd = -1;
#endif

#ifdef _WIN32
    HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        cb_log(CB_ERROR, "Could not open file %s: %s", path, cb_win32_error_message(GetLastError()));
        return false;
    }

    LARGE_INTEGER file_size;
    if (!GetFileSizeEx(file, &file_size)) {
        cb_log(CB_ERROR, "Could not get size of %s: %s", path, cb_win32_error_message(GetLastError()));
        CloseHandle(file);
        return false;
    }

    out->file_handle = file;
    if (file_size.QuadPart == 0) return true; // an empty file cannot be mapped, hand back an empty view

    HANDLE mapping = CreateFileMappingA(file, NULL, PAGE_READONLY, 0, 0, NULL);
    if (mapping == NULL) {
        cb_log(CB_ERROR, "Could not create mapping for %s: %s", path, cb_win32_error_message(GetLastError()));
        CloseHandle(file);
        out->file_handle = NULL;
        return false;
    }

    void* data = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    if (data == NULL) {
        cb_log(CB_ERROR, "Could not map view of %s: %s", path, cb_win32_error_message(GetLastError()));
        CloseHandle(mapping);
        CloseHandle(file);
        out->file_handle = NULL;
        return false;
    }

    out->mapping_handle = mapping;
    out->data = data;
    out->size = (size_t)file_size.QuadPart;
    return true;
#else
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        cb_log(CB_ERROR, "Could not open file %s: %s", path, strerror(errno));
        return false;
    }

    struct stat statbuf;
    if (fstat(fd, &statbuf) < 0) {
        cb_log(CB_ERROR, "Could not get size of %s: %s", path, strerror(errno));
        close(fd);
        return false;
    }

    out->fd = fd;
    if (statbuf.st_size == 0) return true; // mmap of length 0 fails with EINVAL, hand back an empty view

    void* data = mmap(NULL, (size_t)statbuf.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (data == MAP_FAILED) {
        cb_log(CB_ERROR, "Could not mmap %s: %s", path, strerror(errno));
        close(fd);
        out->fd = -1;
        return false;
    }

    out->data = data;
    out->size = (size_t)statbuf.st_size;
    return true;
#endif // _WIN32
}

CBDEF void cb_mmap_close(CB_Mmap* mmap)
{
#ifdef _WIN32
    if (mmap->data != NULL) UnmapViewOfFile(mmap->data);
    if (mmap->mapping_handle != NULL) CloseHandle(mmap->mapping_handle);
    if (mmap->file_handle != NULL) CloseHandle(mmap->file_handle);
#else
    if (mmap->data != NULL && mmap->size > 0) munmap(mmap->data, mmap->size);
    if (mmap->fd >= 0) close(mmap->fd);
#endif // _WIN32
    memset(mmap, 0, sizeof(*mmap));
#ifndef _WIN32
    mmap->fd = -1;
#endif
}

#endif // CB_IMPLEMENTATION

////////////////////////////////////////////////////////////////////////////////////////////////////
// Math & Bits
////////////////////////////////////////////////////////////////////////////////////////////////////
// cb_min / cb_max / cb_clamp. On GCC/Clang every argument is evaluated exactly once and keeps its
// own type; on other compilers the plain macro fallback evaluates an argument twice, so do not pass
// something like i++ to it. C++ uses templates and has no such restriction.

#ifdef __cplusplus

template <typename T>
struct cb__remove_ref {
    typedef T type;
};
template <typename T>
struct cb__remove_ref<T&> {
    typedef T type;
};

// References are stripped for the C++ version: `a < b ? a : b` yields an lvalue, so decltype
// would give T& and the function would return a dangling reference to a local copy.
#define CB__MINMAX_RET(expr) typename cb__remove_ref<decltype(expr)>::type

template <typename T, typename U>
constexpr auto cb_min(T a, U b) -> CB__MINMAX_RET(a < b ? a : b)
{
    return a < b ? a : b;
}

template <typename T, typename U>
constexpr auto cb_max(T a, U b) -> CB__MINMAX_RET(a > b ? a : b)
{
    return a > b ? a : b;
}

template <typename T, typename U, typename V>
constexpr auto cb_clamp(T x, U lo, V hi) -> CB__MINMAX_RET(cb_min(cb_max(x, lo), hi))
{
    return cb_min(cb_max(x, lo), hi);
}

#else // !__cplusplus

#if defined(__GNUC__) || defined(__clang__)
// Single evaluation: each argument is read exactly once and keeps its own type, so
// cb_min(2, 1.9) is 1.9 rather than a truncated 1.
#define cb_min(a, b) \
    __extension__({ __typeof__(a) cb__a = (a); __typeof__(b) cb__b = (b); cb__a < cb__b ? cb__a : cb__b; })
#define cb_max(a, b) \
    __extension__({ __typeof__(a) cb__a = (a); __typeof__(b) cb__b = (b); cb__a > cb__b ? cb__a : cb__b; })
#define cb_clamp(x, lo, hi) \
    __extension__({ __typeof__(x) cb__x = (x); __typeof__(lo) cb__lo = (lo); __typeof__(hi) cb__hi = (hi); \
                    cb__x < cb__lo ? cb__lo : (cb__x > cb__hi ? cb__hi : cb__x); })
#else
// Portable fallback: each argument is evaluated twice, so avoid side effects in the arguments.
#define cb_min(a, b) ((a) < (b) ? (a) : (b))
#define cb_max(a, b) ((a) > (b) ? (a) : (b))
#define cb_clamp(x, lo, hi) ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))
#endif


#endif // __cplusplus

// Round value up/down to a multiple of alignment (a power of two):
//     cb_align_up(13, 8) -> 16        cb_align_down(13, 8) -> 8
#define cb_align_up(value, alignment) (((value) + (alignment) - 1) & ~((alignment) - 1))
#define cb_align_down(value, alignment) ((value) & ~((alignment) - 1))

// ---- declarations ----
CBDEF bool cb_is_pow2(uint64_t value);
// Round up to the next power of two: 0 and 1 give 1, overflow gives 0.
//     cb_next_pow2(100) -> 128
CBDEF uint64_t cb_next_pow2(uint64_t value);
// Number of set bits: cb_popcount64(0xFF00) -> 8.
CBDEF int cb_popcount64(uint64_t value);
// Trailing/leading zero count: cb_ctz64(8) -> 3, cb_clz64(1) -> 63; both give 64 for 0.
CBDEF int cb_ctz64(uint64_t value);
CBDEF int cb_clz64(uint64_t value);
CBDEF uint64_t cb_rotl64(uint64_t value, int amount);
CBDEF uint64_t cb_rotr64(uint64_t value, int amount);
// Host byte order: cb_is_little_endian() -> true on x86 and ARM.
CBDEF bool cb_is_little_endian(void);
CBDEF uint32_t cb_bswap32(uint32_t value);
CBDEF uint64_t cb_bswap64(uint64_t value);

#ifdef CB_IMPLEMENTATION

CBDEF bool cb_is_pow2(uint64_t value)
{
    return value != 0 && (value & (value - 1)) == 0;
}

CBDEF uint64_t cb_next_pow2(uint64_t value)
{
    if (value <= 1) return 1;
    if (value > (uint64_t)1 << 63) return 0; // overflow
    value -= 1;
    value |= value >> 1;
    value |= value >> 2;
    value |= value >> 4;
    value |= value >> 8;
    value |= value >> 16;
    value |= value >> 32;
    return value + 1;
}

CBDEF int cb_popcount64(uint64_t value)
{
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_popcountll(value);
#else
    int count = 0;
    while (value != 0) {
        value &= value - 1;
        count += 1;
    }
    return count;
#endif
}

CBDEF int cb_ctz64(uint64_t value)
{
    if (value == 0) return 64;
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_ctzll(value);
#else
    int count = 0;
    while ((value & 1) == 0) {
        value >>= 1;
        count += 1;
    }
    return count;
#endif
}

CBDEF int cb_clz64(uint64_t value)
{
    if (value == 0) return 64;
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_clzll(value);
#else
    int count = 0;
    while ((value & ((uint64_t)1 << 63)) == 0) {
        value <<= 1;
        count += 1;
    }
    return count;
#endif
}

CBDEF uint64_t cb_rotl64(uint64_t value, int amount)
{
    amount &= 63;
    if (amount == 0) return value;
    return (value << amount) | (value >> (64 - amount));
}

CBDEF uint64_t cb_rotr64(uint64_t value, int amount)
{
    amount &= 63;
    if (amount == 0) return value;
    return (value >> amount) | (value << (64 - amount));
}

CBDEF bool cb_is_little_endian(void)
{
    const uint16_t probe = 1;
    return *(const uint8_t*)&probe == 1;
}

CBDEF uint32_t cb_bswap32(uint32_t value)
{
    return ((value & 0x000000FFu) << 24) | ((value & 0x0000FF00u) << 8) |
           ((value & 0x00FF0000u) >> 8) | ((value & 0xFF000000u) >> 24);
}

CBDEF uint64_t cb_bswap64(uint64_t value)
{
    return ((uint64_t)cb_bswap32((uint32_t)(value & 0xFFFFFFFFu)) << 32) |
           (uint64_t)cb_bswap32((uint32_t)(value >> 32));
}

#endif // CB_IMPLEMENTATION

////////////////////////////////////////////////////////////////////////////////////////////////////
// Time & Date
////////////////////////////////////////////////////////////////////////////////////////////////////
// Current UTC time in Unix seconds: cb_time_now() -> 1758400000.
CBDEF int64_t cb_time_now(void);
// Unix seconds to an ISO 8601 UTC string in temp storage:
//     cb_time_to_iso8601(1758400000) -> "2025-09-21T18:13:20Z"
CBDEF char* cb_time_to_iso8601(int64_t unix_seconds);
// Human-readable duration in temp storage: 0.42 -> "420ms", 12.5 -> "12.5s",
// 185 -> "3m5s", 7325 -> "2h2m".
CBDEF char* cb_duration_to_str(double seconds);

#ifdef CB_IMPLEMENTATION

CBDEF int64_t cb_time_now(void)
{
    return (int64_t)time(NULL);
}

CBDEF char* cb_time_to_iso8601(int64_t unix_seconds)
{
    time_t t = (time_t)unix_seconds;
    struct tm tm_utc;
#ifdef _WIN32
    if (gmtime_s(&tm_utc, &t) != 0) return NULL;
#else
    if (gmtime_r(&t, &tm_utc) == NULL) return NULL;
#endif // _WIN32

    return cb_temp_sprintf("%04d-%02d-%02dT%02d:%02d:%02dZ",
                           tm_utc.tm_year + 1900, tm_utc.tm_mon + 1, tm_utc.tm_mday,
                           tm_utc.tm_hour, tm_utc.tm_min, tm_utc.tm_sec);
}

CBDEF char* cb_duration_to_str(double seconds)
{
    if (seconds < 0) seconds = 0;
    if (seconds < 1.0) return cb_temp_sprintf("%.0fms", seconds * 1000.0);

    if (seconds < 60.0) {
// whole seconds drop the decimal point: 12 -> "12s"
        double rounded = (double)(int64_t)(seconds * 10.0 + 0.5) / 10.0;
        if (rounded == (double)(int64_t)rounded) return cb_temp_sprintf("%llds", (long long)(int64_t)rounded);
        return cb_temp_sprintf("%.1fs", rounded);
    }

    int64_t total = (int64_t)(seconds + 0.5);
    int64_t hours = total / 3600;
    int64_t minutes = (total % 3600) / 60;
    int64_t secs = total % 60;

    if (hours > 0) return cb_temp_sprintf("%lldh%lldm", (long long)hours, (long long)minutes);
    return cb_temp_sprintf("%lldm%llds", (long long)minutes, (long long)secs);
}

#endif // CB_IMPLEMENTATION

////////////////////////////////////////////////////////////////////////////////////////////////////
// Random
////////////////////////////////////////////////////////////////////////////////////////////////////
// xoshiro256** state. Better than rand() and identical on every platform, so a seed reproduces
// a run exactly. Fine for simulation, sampling and shuffling, NOT for cryptography.
//     CB_Rng rng; cb_rng_seed(&rng, 42); uint64_t x = cb_rng_next(&rng);

typedef struct {
    uint64_t state[4];
} CB_Rng;

// ---- declarations ----
CBDEF void cb_rng_seed(CB_Rng* rng, uint64_t seed);
CBDEF uint64_t cb_rng_next(CB_Rng* rng);
// Uniform double in [0,1): cb_rng_double(&rng) -> 0.379...
CBDEF double cb_rng_double(CB_Rng* rng);
// Uniform integer in [0,bound): cb_rng_range(&rng, 10) -> 0..9; bound 0 returns 0.
CBDEF uint64_t cb_rng_range(CB_Rng* rng, uint64_t bound);
// Fisher-Yates shuffle in place: cb_rng_shuffle(&rng, xs, count, sizeof(xs[0]));
CBDEF void cb_rng_shuffle(CB_Rng* rng, void* items, size_t count, size_t elem_size);

#ifdef CB_IMPLEMENTATION

CBDEF uint64_t cb__rng_splitmix64(uint64_t* state)
{
    uint64_t z = (*state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

CBDEF void cb_rng_seed(CB_Rng* rng, uint64_t seed)
{
// splitmix64 spreads one seed into four state words; even seed 0 gives a non-zero state.
    uint64_t state = seed;
    for (int i = 0; i < 4; ++i) rng->state[i] = cb__rng_splitmix64(&state);
}

CBDEF uint64_t cb_rng_next(CB_Rng* rng)
{
    uint64_t* s = rng->state;
    uint64_t result = cb_rotl64(s[1] * 5, 7) * 9;

    uint64_t t = s[1] << 17;
    s[2] ^= s[0];
    s[3] ^= s[1];
    s[1] ^= s[2];
    s[0] ^= s[3];
    s[2] ^= t;
    s[3] = cb_rotl64(s[3], 45);

    return result;
}

CBDEF double cb_rng_double(CB_Rng* rng)
{
// The high 53 bits give a uniform double in [0,1).
    return (double)(cb_rng_next(rng) >> 11) * (1.0 / 9007199254740992.0);
}

CBDEF uint64_t cb_rng_range(CB_Rng* rng, uint64_t bound)
{
    if (bound == 0) return 0;
// Rejection sampling: a plain modulo would skew the distribution.
    uint64_t limit = UINT64_MAX - (UINT64_MAX % bound) - 1;
    uint64_t value;
    do {
        value = cb_rng_next(rng);
    } while (value > limit);
    return value % bound;
}

CBDEF void cb_rng_shuffle(CB_Rng* rng, void* items, size_t count, size_t elem_size)
{
    if (count < 2 || elem_size == 0) return;

    CB_Arena_Mark mark = cb_temp_save();
    unsigned char* tmp = (unsigned char*)cb_temp_alloc(elem_size);
    unsigned char* base = (unsigned char*)items;

    for (size_t i = count - 1; i > 0; --i) {
        size_t j = (size_t)cb_rng_range(rng, (uint64_t)i + 1);
        if (i == j) continue;
        memcpy(tmp, base + i * elem_size, elem_size);
        memcpy(base + i * elem_size, base + j * elem_size, elem_size);
        memcpy(base + j * elem_size, tmp, elem_size);
    }

    cb_temp_rewind(mark);
}

#endif // CB_IMPLEMENTATION

////////////////////////////////////////////////////////////////////////////////////////////////////
// Runtime Environment
////////////////////////////////////////////////////////////////////////////////////////////////////
// Environment variable copied into temp storage, NULL when unset (like getenv):
//     const char* home = cb_env_get("HOME");
CBDEF char* cb_env_get(const char* name);
CBDEF bool cb_env_set(const char* name, const char* value);
// Is stdout a terminal? cb_stdout_is_tty() -> false when the output is redirected.
CBDEF bool cb_stdout_is_tty(void);
// Terminal width in columns, 80 when it cannot be determined.
CBDEF int cb_terminal_width(void);
// Are ANSI colors wanted? true when stdout is a tty, NO_COLOR is unset and colors were not
// disabled explicitly.
CBDEF bool cb_color_enabled(void);

#ifdef CB_IMPLEMENTATION

CBDEF char* cb_env_get(const char* name)
{
    const char* value = getenv(name);
    if (value == NULL) return NULL;
    return cb_temp_strdup(value);
}

CBDEF bool cb_env_set(const char* name, const char* value)
{
#ifdef _WIN32
    if (_putenv_s(name, value) != 0) {
        cb_log(CB_ERROR, "Could not set env %s", name);
        return false;
    }
    return true;
#else
    if (setenv(name, value, 1) != 0) {
        cb_log(CB_ERROR, "Could not set env %s: %s", name, strerror(errno));
        return false;
    }
    return true;
#endif // _WIN32
}

CBDEF bool cb_stdout_is_tty(void)
{
#ifdef _WIN32
    return _isatty(_fileno(stdout)) != 0;
#else
    return isatty(STDOUT_FILENO) != 0;
#endif // _WIN32
}

CBDEF int cb_terminal_width(void)
{
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info)) {
        int width = (int)(info.srWindow.Right - info.srWindow.Left + 1);
        if (width > 0) return width;
    }
#else
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) return (int)ws.ws_col;
#endif // _WIN32
    return 80;
}

CBDEF bool cb_color_enabled(void)
{
    if (getenv("NO_COLOR") != NULL) return false; // https://no-color.org/
    return cb_stdout_is_tty();
}

#endif // CB_IMPLEMENTATION

////////////////////////////////////////////////////////////////////////////////////////////////////
// Hex Dump
////////////////////////////////////////////////////////////////////////////////////////////////////
CBDEF void cb_dump_hex(const void* data, size_t size);

#ifdef CB_IMPLEMENTATION

// Hex dump, 16 bytes per line, written to stderr (offset + hex + ASCII). Diagnostics go to
// stderr so stdout stays clean:
//     cb_dump_hex(bytes, sizeof(bytes));
//     00000000  00 01 41 42 7f 80 ff                           |..AB...|
CBDEF void cb_dump_hex(const void* data, size_t size)
{
    const unsigned char* bytes = (const unsigned char*)data;
    for (size_t offset = 0; offset < size; offset += 16) {
        fprintf(stderr, "%08zx  ", offset);
        for (size_t i = 0; i < 16; ++i) {
            if (offset + i < size) fprintf(stderr, "%02x ", bytes[offset + i]);
            else fprintf(stderr, "   ");
            if (i == 7) fprintf(stderr, " ");
        }
        fprintf(stderr, " |");
        for (size_t i = 0; i < 16 && offset + i < size; ++i) {
            unsigned char c = bytes[offset + i];
            fprintf(stderr, "%c", (c >= 32 && c < 127) ? c : '.');
        }
        fprintf(stderr, "|\n");
    }
}

#endif // CB_IMPLEMENTATION

////////////////////////////////////////////////////////////////////////////////////////////////////
// CLI Args
////////////////////////////////////////////////////////////////////////////////////////////////////
// Minimal argument parser. Accepted forms:
//     --key=value   option with a value
//     --key         switch (value is NULL)
//     -k            switch, same as --k
//     -k=value      option with a value
//     --            everything after it is a positional argument
//     anything else is a positional argument
// "--key value" (space separated) is not supported: write --key=value.
//     CB_Args args; cb_args_parse(&args, argc, argv);
//     if (cb_args_has(&args, "verbose")) { ... }
//     const char* out = cb_args_get(&args, "out", "a.bin");

typedef struct {
    const char* name;  // name without the leading - / --
    const char* value; // NULL for a switch
} CB_Arg_Entry;

typedef struct {
    CB_Arg_Entry* items;
    size_t count;
    size_t capacity;
} CB_Arg_List;

typedef struct {
    CB_Arg_List options;
    CB_File_Paths positionals;
} CB_Args;

// ---- declarations ----
// Pass main's argc/argv; argv[0] is treated as the program name and skipped.
CBDEF void cb_args_parse(CB_Args* args, int argc, char** argv);
// Was this switch/option given?
CBDEF bool cb_args_has(const CB_Args* args, const char* name);
// Value of an option, or fallback when it is absent (the last occurrence wins).
CBDEF const char* cb_args_get(const CB_Args* args, const char* name, const char* fallback);
// cb_args_get_first() returns the first occurrence instead of the last one.
CBDEF const char* cb_args_get_first(const CB_Args* args, const char* name);
CBDEF size_t cb_args_positional_count(const CB_Args* args);
CBDEF const char* cb_args_positional(const CB_Args* args, size_t index);
CBDEF void cb_args_free(CB_Args* args);

#ifdef CB_IMPLEMENTATION

CBDEF void cb_args_parse(CB_Args* args, int argc, char** argv)
{
    bool only_positionals = false;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];

        if (only_positionals) {
            cb_da_append(&args->positionals, cb_temp_strdup(arg));
            continue;
        }
        if (strcmp(arg, "--") == 0) {
            only_positionals = true;
            continue;
        }
        if (arg[0] != '-' || arg[1] == '\0') { // a lone "-" is also a positional argument
            cb_da_append(&args->positionals, cb_temp_strdup(arg));
            continue;
        }

        const char* name = arg + 1;
        if (name[0] == '-') name += 1; // --key -> key
        if (name[0] == '\0') {         // an empty name, as in a bare "--"
            cb_da_append(&args->positionals, cb_temp_strdup(arg));
            continue;
        }

        CB_Arg_Entry entry = CB_ZERO;
        const char* eq = strchr(name, '=');
        if (eq != NULL) {
            entry.name = cb_temp_strndup(name, (size_t)(eq - name));
            entry.value = cb_temp_strdup(eq + 1);
        } else {
            entry.name = cb_temp_strdup(name);
            entry.value = NULL;
        }
        cb_da_append(&args->options, entry);
    }
}

CBDEF bool cb_args_has(const CB_Args* args, const char* name)
{
    for (size_t i = 0; i < args->options.count; ++i) {
        if (strcmp(args->options.items[i].name, name) == 0) return true;
    }
    return false;
}

CBDEF const char* cb_args_get_first(const CB_Args* args, const char* name)
{
    for (size_t i = 0; i < args->options.count; ++i) {
        if (strcmp(args->options.items[i].name, name) == 0) return args->options.items[i].value;
    }
    return NULL;
}

CBDEF const char* cb_args_get(const CB_Args* args, const char* name, const char* fallback)
{
    // the last occurrence wins
    const char* found = NULL;
    for (size_t i = 0; i < args->options.count; ++i) {
        if (strcmp(args->options.items[i].name, name) == 0) found = args->options.items[i].value;
    }
    return found != NULL ? found : fallback;
}

CBDEF size_t cb_args_positional_count(const CB_Args* args)
{
    return args->positionals.count;
}

CBDEF const char* cb_args_positional(const CB_Args* args, size_t index)
{
    if (index >= args->positionals.count) return NULL;
    return args->positionals.items[index];
}

CBDEF void cb_args_free(CB_Args* args)
{
    cb_da_free(args->options);
    cb_da_free(args->positionals);
    args->options.items = NULL;
    args->options.count = 0;
    args->options.capacity = 0;
    args->positionals.items = NULL;
    args->positionals.count = 0;
    args->positionals.capacity = 0;
}

#endif // CB_IMPLEMENTATION

////////////////////////////////////////////////////////////////////////////////////////////////////
// Process & FD
////////////////////////////////////////////////////////////////////////////////////////////////////
#ifdef _WIN32
typedef HANDLE CB_Proc;
#define CB_INVALID_PROC INVALID_HANDLE_VALUE
typedef HANDLE CB_FD;
#define CB_INVALID_FD INVALID_HANDLE_VALUE
#else
typedef int CB_Proc;
#define CB_INVALID_PROC (-1)
typedef int CB_FD;
#define CB_INVALID_FD (-1)
#endif // _WIN32

CBDEF CB_FD cb_fd_open_read(const char* path);
CBDEF CB_FD cb_fd_open_write(const char* path);
CBDEF void cb_fd_close(CB_FD fd);

typedef struct {
    CB_FD read;
    CB_FD write;
} CB_Pipe;

CBDEF bool cb_pipe_create(CB_Pipe* pp);

typedef struct {
    CB_Proc* items;
    size_t count;
    size_t capacity;
} CB_Procs;

CBDEF int cb__proc_wait_async(CB_Proc proc, int ms);
// Wait for one child: cb_proc_wait(proc) -> true when it exited with status 0.
CBDEF bool cb_proc_wait(CB_Proc proc);
// Wait for every process of the array: cb_procs_wait(procs).
CBDEF bool cb_procs_wait(CB_Procs procs);
// Wait for all of them and reset the array for reuse: cb_procs_wait_and_reset(&procs).
CBDEF bool cb_procs_wait_and_reset(CB_Procs* procs);

#ifdef CB_IMPLEMENTATION

CBDEF CB_FD cb_fd_open_read(const char* path)
{
#ifdef _WIN32
    // https://docs.microsoft.com/en-us/windows/win32/fileio/opening-a-file-for-reading-or-writing
    SECURITY_ATTRIBUTES saAttr = CB_ZERO;
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;

    CB_FD result = CreateFile(
        path,
        GENERIC_READ,
        0,
        &saAttr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_READONLY,
        NULL);

    if (result == INVALID_HANDLE_VALUE) {
        cb_log(CB_ERROR, "Could not open file %s: %s", path, cb_win32_error_message(GetLastError()));
        return CB_INVALID_FD;
    }

    return result;
#else
    CB_FD result = open(path, O_RDONLY);
    if (result < 0) {
        cb_log(CB_ERROR, "Could not open file %s: %s", path, strerror(errno));
        return CB_INVALID_FD;
    }
    return result;
#endif // _WIN32
}

CBDEF CB_FD cb_fd_open_write(const char* path)
{
#ifdef _WIN32
    SECURITY_ATTRIBUTES saAttr = CB_ZERO;
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;

    CB_FD result = CreateFile(
        path,                  // name of the write
        GENERIC_WRITE,         // open for writing
        0,                     // do not share
        &saAttr,               // default security
        CREATE_ALWAYS,         // create always
        FILE_ATTRIBUTE_NORMAL, // normal file
        NULL);                 // no attr. template

    if (result == INVALID_HANDLE_VALUE) {
        cb_log(CB_ERROR, "Could not open file %s: %s", path, cb_win32_error_message(GetLastError()));
        return CB_INVALID_FD;
    }

    return result;
#else
    CB_FD result = open(path,
            O_WRONLY | O_CREAT | O_TRUNC,
            S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (result < 0) {
        cb_log(CB_ERROR, "Could not open file %s: %s", path, strerror(errno));
        return CB_INVALID_FD;
    }
    return result;
#endif // _WIN32
}

CBDEF void cb_fd_close(CB_FD fd)
{
#ifdef _WIN32
    CloseHandle(fd);
#else
    close(fd);
#endif // _WIN32
}

CBDEF bool cb_pipe_create(CB_Pipe* pp)
{
#ifdef _WIN32
    // https://docs.microsoft.com/en-us/windows/win32/ProcThread/creating-a-child-process-with-redirected-input-and-output
    SECURITY_ATTRIBUTES saAttr = CB_ZERO;
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;

    if (!CreatePipe(&pp->read, &pp->write, &saAttr, 0)) {
        cb_log(CB_ERROR, "Could not create pipe: %s", cb_win32_error_message(GetLastError()));
        return false;
    }

    return true;
#else
    int pipefd[2];
    if (pipe(pipefd) < 0) {
        cb_log(CB_ERROR, "Could not create pipe: %s\n", strerror(errno));
        return false;
    }

    pp->read = pipefd[0];
    pp->write = pipefd[1];

    return true;
#endif // _WIN32
}

CBDEF int cb__proc_wait_async(CB_Proc proc, int ms)
{
    if (proc == CB_INVALID_PROC) return 0;

#ifdef _WIN32
    DWORD result = WaitForSingleObject(proc, ms);
    if (result == WAIT_TIMEOUT) return 0;

    if (result == WAIT_FAILED) {
        cb_log(CB_ERROR, "Could not wait on child process: %s", cb_win32_error_message(GetLastError()));
        return -1;
    }

    DWORD exit_status;
    if (!GetExitCodeProcess(proc, &exit_status)) {
        cb_log(CB_ERROR, "Could not get process exit code: %s", cb_win32_error_message(GetLastError()));
        return -1;
    }

    if (exit_status != 0) {
        cb_log(CB_ERROR, "Command exited with exit code %lu", exit_status);
        return -1;
    }

    CloseHandle(proc);
    return 1;
#else
    long ns = ms * 1000 * 1000;
    struct timespec duration = {
        .tv_sec = ns / (1000 * 1000 * 1000),
        .tv_nsec = ns % (1000 * 1000 * 1000),
    };

    int wstatus = 0;
    pid_t pid = waitpid(proc, &wstatus, WNOHANG);
    if (pid < 0) {
        cb_log(CB_ERROR, "Could not wait on command (pid %d): %s", proc, strerror(errno));
        return -1;
    }

    if (pid == 0) {
        nanosleep(&duration, NULL);
        return 0;
    }

    if (WIFEXITED(wstatus)) {
        int exit_status = WEXITSTATUS(wstatus);
        if (exit_status != 0) {
            cb_log(CB_ERROR, "Command exited with exit code %d", exit_status);
            return -1;
        }

        return 1;
    }

    if (WIFSIGNALED(wstatus)) {
        cb_log(CB_ERROR, "Command process was terminated by signl %d", WTERMSIG(wstatus));
        return -1;
    }

    nanosleep(&duration, NULL);
    return 0;
#endif // _WIN32
}

CBDEF bool cb_proc_wait(CB_Proc proc)
{
    if (proc == CB_INVALID_PROC) return false;

#ifdef _WIN32
    DWORD result = WaitForSingleObject(proc, INFINITE);
    if (result == WAIT_FAILED) {
        cb_log(CB_ERROR, "Could not wait on child process: %s", cb_win32_error_message(GetLastError()));
        return false;
    }

    DWORD exit_status;
    if (!GetExitCodeProcess(proc, &exit_status)) {
        cb_log(CB_ERROR, "Could not get process exit code: %s", cb_win32_error_message(GetLastError()));
        return false;
    }

    if (exit_status != 0) {
        cb_log(CB_ERROR, "Command exited with exit code %lu", exit_status);
        return false;
    }

    CloseHandle(proc);

    return true;
#else
    while (true) {
        int wstatus = 0;
        if (waitpid(proc, &wstatus, 0) < 0) {
            cb_log(CB_ERROR, "Could not wait on command (pid %d): %s", proc, strerror(errno));
            return false;
        }

        if (WIFEXITED(wstatus)) {
            int exit_status = WEXITSTATUS(wstatus);
            if (exit_status != 0) {
                cb_log(CB_ERROR, "Command exited with exit code %d", exit_status);
                return false;
            }
            break;
        }

        if (WIFSIGNALED(wstatus)) {
            cb_log(CB_ERROR, "Command process was terminated by signal %d", WTERMSIG(wstatus));
            return false;
        }
    }

    return true;
#endif // _WIN32
}

CBDEF bool cb_procs_wait(CB_Procs procs)
{
    bool success = true;
    for (size_t i = 0; i < procs.count; ++i) {
        success = cb_proc_wait(procs.items[i]) && success;
    }
    return success;
}

CBDEF bool cb_procs_wait_and_reset(CB_Procs* procs)
{
    bool success = cb_procs_wait(*procs);
    procs->count = 0;
    return success;
}

#endif // CB_IMPLEMENTATION

////////////////////////////////////////////////////////////////////////////////////////////////////
// Cmd
////////////////////////////////////////////////////////////////////////////////////////////////////
typedef struct {
    const char** items;
    size_t count;
    size_t capacity;
} CB_Cmd;

// Options of cb_cmd_run_opt(); the cb_cmd_run(cmd, ...) macro fills them by name:
//     cb_cmd_run(&cmd, .stdout_path = "out.txt", .async = &procs, .max_procs = 4);
typedef struct CB_Cmd_Opt {
    // run asynchronously, appending the CB_Proc to this array
    CB_Procs* async CB__DEFAULT(nullptr);
    // concurrency limit for .async; 0 means cb_nprocs()
    size_t max_procs CB__DEFAULT(0);
    // keep cmd.count after the run, so the same command can be run again
    bool dont_reset CB__DEFAULT(false);
    // redirect stdin from this file
    const char* stdin_path CB__DEFAULT(nullptr);
    // redirect stdout to this file
    const char* stdout_path CB__DEFAULT(nullptr);
    // redirect stderr to this file
    const char* stderr_path CB__DEFAULT(nullptr);
} CB_Cmd_Opt;

CBDEF void cb__cmd_append(CB_Cmd* cmd, size_t n, const char** args);
#ifdef __cplusplus
template <typename... Args>
#define cb_cmd_append(cmd, ...) cb__cpp_cmd_append_wrapper(cmd, __VA_ARGS__)
CBDEF void cb__cpp_cmd_append_wrapper(CB_Cmd* cmd, Args... strs)
{
    const char* args[] = {strs...};
    cb__cmd_append(cmd, sizeof(args) / sizeof(args[0]), args);
}
#else
#define cb_cmd_append(cmd, ...) \
    cb__cmd_append(cmd, sizeof((const char*[]){__VA_ARGS__}) / sizeof(const char*), (const char*[]){__VA_ARGS__})
#endif // __cplusplus

// Append every argument of another command: cb_cmd_extend(&cmd, &other);
#define cb_cmd_extend(cmd, other_cmd) \
    cb_da_append_many((cmd), (other_cmd)->items, (other_cmd)->count)
// Free the argv storage and zero the handle: cb_cmd_free(cmd);
#define cb_cmd_free(cmd) \
    do {                     \
        cb_da_free(cmd);     \
        (cmd).items = NULL;  \
        (cmd).count = 0;     \
        (cmd).capacity = 0;  \
    } while (0)

CBDEF void cb_cmd_to_sb(CB_Cmd cmd, CB_String_Builder* sb);
CBDEF int cb_nprocs(void);
CBDEF CB_Proc cb__cmd_start_process(CB_Cmd cmd, CB_FD* fdin, CB_FD* fdout, CB_FD* fderr);
CBDEF bool cb_cmd_run_opt(CB_Cmd* cmd, CB_Cmd_Opt opt);
CBDEF bool cb__cmd_run_opt_with_location(CB_Cmd* cmd, const char* file, int line, CB_Cmd_Opt opt);
#define cb_cmd_run(cmd, ...) cb__cmd_run_opt_with_location((cmd), __FILE__, __LINE__, CB_CLIT(CB_Cmd_Opt){__VA_ARGS__})

typedef struct {
    CB_FD fdin;
    CB_Cmd cmd;
    bool err2out;
} CB_Pipes;

#ifdef CB_IMPLEMENTATION

CBDEF void cb__cmd_append(CB_Cmd* cmd, size_t n, const char** args)
{
    for (size_t i = 0; i < n; ++i) {
        cb_da_append(cmd, args[i]);
    }
}

CBDEF void cb_cmd_to_sb(CB_Cmd cmd, CB_String_Builder* sb)
{
    for (size_t i = 0; i < cmd.count; ++i) {
        const char* arg = cmd.items[i];
        if (arg == NULL) break;
        if (i > 0) cb_sb_append_cstr(sb, " ");
        if (!strchr(arg, ' ')) {
            cb_sb_append_cstr(sb, arg);
        } else {
            cb_sb_append(sb, '\'');
            cb_sb_append_cstr(sb, arg);
            cb_sb_append(sb, '\'');
        }
    }
}

#ifdef _WIN32
// Render a command line the way Windows parses it:
// https://learn.microsoft.com/en-gb/archive/blogs/twistylittlepassagesallalike/everyone-quotes-command-line-arguments-the-wrong-way
CBDEF void cb__win32_cmd_quote(CB_Cmd cmd, CB_String_Builder* quoted)
{
    for (size_t i = 0; i < cmd.count; ++i) {
        const char* arg = cmd.items[i];
        if (arg == NULL) break;
        size_t len = strlen(arg);
        if (i > 0) cb_sb_append(quoted, ' ');
        if (len != 0 && NULL == strpbrk(arg, " \t\n\v\"")) {
            // no quoting needed
            cb_sb_append_buf(quoted, arg, len);
        } else {
            // needs escaping: double quotes inside the argument, plus the backslashes before one
            size_t backslashes = 0;
            cb_sb_append(quoted, '\"');
            for (size_t j = 0; j < len; ++j) {
                char x = arg[j];
                if (x == '\\') {
                    backslashes += 1;
                } else {
                    if (x == '\"') {
                        // escape the accumulated backslashes and this double quote
                        for (size_t k = 0; k < 1 + backslashes; ++k) {
                            cb_sb_append(quoted, '\\');
                        }
                    }
                    backslashes = 0;
                }
                cb_sb_append(quoted, x);
            }
            // trailing backslashes need escaping too
            for (size_t k = 0; k < backslashes; ++k) {
                cb_sb_append(quoted, '\\');
            }
            cb_sb_append(quoted, '\"');
        }
    }
}
#endif // _WIN32

CBDEF int cb_nprocs(void)
{
#ifdef _WIN32
    SYSTEM_INFO siSysInfo;
    GetSystemInfo(&siSysInfo);
    return siSysInfo.dwNumberOfProcessors;
#else
    return sysconf(_SC_NPROCESSORS_ONLN);
#endif // _WIN32
}

CBDEF CB_Proc cb__cmd_start_process(CB_Cmd cmd, CB_FD* fdin, CB_FD* fdout, CB_FD* fderr)
{
    if (cmd.count < 1) {
        cb_log(CB_ERROR, "Could not run empty command");
        return CB_INVALID_PROC;
    }

#ifdef CB_ENABLE_ECHO
    CB_String_Builder sb = CB_ZERO;
    cb_cmd_to_sb(cmd, &sb);
    cb_log(CB_INFO, "CMD: %s", sb.items);
    cb_sb_free(sb);
    memset(&sb, 0, sizeof(sb));
#endif // !CB_ENABLE_ECHO

#ifdef _WIN32
    // https://docs.microsoft.com/en-us/windows/win32/procthread/creating-a-child-process-with-redirected-input-and-output
    STARTUPINFO siStartInfo;
    ZeroMemory(&siStartInfo, sizeof(siStartInfo));
    siStartInfo.cb = sizeof(STARTUPINFO);
    // NOTE: passing NULL for a std handle should be fine (see GetStdHandle attach/detach behavior)
    // TODO: check for errors in GetStdHandle
    siStartInfo.hStdError = fderr ? *fderr : GetStdHandle(STD_ERROR_HANDLE);
    siStartInfo.hStdOutput = fdout ? *fdout : GetStdHandle(STD_OUTPUT_HANDLE);
    siStartInfo.hStdInput = fdin ? *fdin : GetStdHandle(STD_INPUT_HANDLE);
    siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

    PROCESS_INFORMATION piProcInfo;
    ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));

    CB_String_Builder quoted = CB_ZERO;
    cb__win32_cmd_quote(cmd, &quoted);
    BOOL bSuccess = CreateProcessA(NULL, quoted.items, NULL, NULL, TRUE, 0, NULL, NULL, &siStartInfo, &piProcInfo);
    cb_sb_free(quoted);

    if (!bSuccess) {
        cb_log(CB_ERROR, "Could not create child process for %s: %s", cmd.items[0], cb_win32_error_message(GetLastError()));
        return CB_INVALID_PROC;
    }

    CloseHandle(piProcInfo.hThread);

    return piProcInfo.hProcess;
#else
    pid_t cpid = fork();
    if (cpid < 0) {
        cb_log(CB_ERROR, "Could not fork child process: %s", strerror(errno));
        return CB_INVALID_PROC;
    }

    if (cpid == 0) {
        if (fdin) {
            if (dup2(*fdin, STDIN_FILENO) < 0) {
                cb_log(CB_ERROR, "Could not setup stdin for child process: %s", strerror(errno));
                exit(1);
            }
        }
        if (fdout) {
            if (dup2(*fdout, STDOUT_FILENO) < 0) {
                cb_log(CB_ERROR, "Could not setup stdout for child process: %s", strerror(errno));
                exit(1);
            }
        }
        if (fderr) {
            if (dup2(*fderr, STDERR_FILENO) < 0) {
                cb_log(CB_ERROR, "Could not setup stderr for child process: %s", strerror(errno));
                exit(1);
            }
        }

        // NOTE: this small leak lives in the child process only and is a one-off
        CB_Cmd cmd_null = CB_ZERO;
        cb_da_append_many(&cmd_null, cmd.items, cmd.count);
        cb_da_append(&cmd_null, (const char*)NULL);

        if (execvp(cmd.items[0], (char* const*)cmd_null.items) < 0) {
            cb_log(CB_ERROR, "Could not exec child process for %s: %s", cmd.items[0], strerror(errno));
            exit(1);
        }
        CB_UNREACHABLE("cb__cmd_start_process");
    }

    return cpid;
#endif // _WIN32
}

CBDEF bool cb_cmd_run_opt(CB_Cmd* cmd, CB_Cmd_Opt opt)
{
    bool result = true;
    CB_FD fdin = CB_INVALID_FD;
    CB_FD fdout = CB_INVALID_FD;
    CB_FD fderr = CB_INVALID_FD;
    CB_FD* opt_fdin = NULL;
    CB_FD* opt_fdout = NULL;
    CB_FD* opt_fderr = NULL;
    CB_Proc proc = CB_INVALID_PROC;

    size_t max_procs = opt.max_procs > 0 ? opt.max_procs : (size_t)cb_nprocs() + 1;

    if (opt.async && max_procs > 0) {
        while (opt.async->count >= max_procs) {
            for (size_t i = 0; i < opt.async->count; ++i) {
                int ret = cb__proc_wait_async(opt.async->items[i], i);
                if (ret < 0) cb_return_defer(false);
                if (ret) {
                    cb_da_remove_unordered(opt.async, i);
                    break;
                }
            }
        }
    }

    if (opt.stdin_path) {
        fdin = cb_fd_open_read(opt.stdin_path);
        if (fdin == CB_INVALID_FD) cb_return_defer(false);
        opt_fdin = &fdin;
    }
    if (opt.stdout_path) {
        fdout = cb_fd_open_write(opt.stdout_path);
        if (fdout == CB_INVALID_FD) cb_return_defer(false);
        opt_fdout = &fdout;
    }
    if (opt.stderr_path) {
        fderr = cb_fd_open_write(opt.stderr_path);
        if (fderr == CB_INVALID_FD) cb_return_defer(false);
        opt_fderr = &fderr;
    }
    proc = cb__cmd_start_process(*cmd, opt_fdin, opt_fdout, opt_fderr);

    if (opt.async) {
        if (proc == CB_INVALID_PROC) cb_return_defer(false);
        cb_da_append(opt.async, proc);
    } else {
        if (!cb_proc_wait(proc)) cb_return_defer(false);
    }

defer:
    if (opt_fdin) cb_fd_close(*opt_fdin);
    if (opt_fdout) cb_fd_close(*opt_fdout);
    if (opt_fderr) cb_fd_close(*opt_fderr);
    if (!opt.dont_reset) cmd->count = 0;
    return result;
}

CBDEF bool cb__cmd_run_opt_with_location(CB_Cmd* cmd, const char* file, int line, CB_Cmd_Opt opt)
{
    bool ok = cb_cmd_run_opt(cmd, opt);
#ifdef CB_TRACE_CMD_RUN_FAIL_LOCATION
    if (!ok) {
        cb_log(CB_ERROR, "%s:%d: ERROR: cmd_run failed", file, line);
    }
#else
    (void)(file);
    (void)(line);
#endif // CB_TRACE_CMD_RUN_FAIL_LOCATION
    return ok;
}

#endif // CB_IMPLEMENTATION

////////////////////////////////////////////////////////////////////////////////////////////////////
// Cmd Chain
////////////////////////////////////////////////////////////////////////////////////////////////////
// Pipeline of commands; the code below is equivalent to "foo | bar | baz > out.txt":
//   CB_Chain chain = CB_ZERO;  CB_Cmd cmd = CB_ZERO;
//   if (!cb_chain_begin(&chain)) return 1;
//   cb_cmd_append(&cmd, "foo"); if (!cb_chain_cmd(&chain, &cmd)) return 1;
//   cb_cmd_append(&cmd, "bar"); if (!cb_chain_cmd(&chain, &cmd)) return 1;
//   cb_cmd_append(&cmd, "baz"); if (!cb_chain_cmd(&chain, &cmd)) return 1;
//   if (!cb_chain_end(&chain, .stdout_path = "out.txt")) return 1;
//
// The only allocation inside CB_Chain is .cmd; free it with cb_da_free(chain.cmd) when reusing.

typedef struct {
    // output end of the previous command, used as the input of the next one
    CB_FD fdin;
    // command accumulated by the last cb_chain_cmd()
    CB_Cmd cmd;
    // .err2out of the last cb_chain_cmd()
    bool err2out;
} CB_Chain;

typedef struct CB_Chain_Begin_Opt {
    const char* stdin_path CB__DEFAULT(nullptr);
} CB_Chain_Begin_Opt;

typedef struct CB_Chain_Cmd_Opt {
    bool err2out CB__DEFAULT(false);
    bool dont_reset CB__DEFAULT(false);
} CB_Chain_Cmd_Opt;

typedef struct CB_Chain_End_Opt {
    CB_Procs* async CB__DEFAULT(nullptr);
    size_t max_procs CB__DEFAULT(0);
    const char* stdout_path CB__DEFAULT(nullptr);
    const char* stderr_path CB__DEFAULT(nullptr);
} CB_Chain_End_Opt;

// Remembers the fds to close at the end (a chain needs at most 3; 5 slots is plenty).
typedef struct {
    CB_FD items[5];
    size_t count;
} CB_Fd_List;

// ---- declarations ----
CBDEF bool cb_chain_begin_opt(CB_Chain* chain, CB_Chain_Begin_Opt opt);
CBDEF bool cb_chain_cmd_opt(CB_Chain* chain, CB_Cmd* cmd, CB_Chain_Cmd_Opt opt);
CBDEF bool cb_chain_end_opt(CB_Chain* chain, CB_Chain_End_Opt opt);

#define cb_chain_begin(chain, ...) cb_chain_begin_opt((chain), CB_CLIT(CB_Chain_Begin_Opt){__VA_ARGS__})
#define cb_chain_cmd(chain, cmd, ...) cb_chain_cmd_opt((chain), (cmd), CB_CLIT(CB_Chain_Cmd_Opt){__VA_ARGS__})
#define cb_chain_end(chain, ...) cb_chain_end_opt((chain), CB_CLIT(CB_Chain_End_Opt){__VA_ARGS__})

#ifdef CB_IMPLEMENTATION

CBDEF void cb__fd_list_push(CB_Fd_List* list, CB_FD fd)
{
    cb_fa_append(list, fd); // the list has room for every stage; a full list would leak an fd
}

CBDEF void cb__fd_list_close_all(CB_Fd_List* list)
{
    for (size_t i = 0; i < list->count; ++i) cb_fd_close(list->items[i]);
    list->count = 0;
}

CBDEF CB_FD cb__fd_stdout(void)
{
#ifdef _WIN32
    return GetStdHandle(STD_OUTPUT_HANDLE);
#else
    return STDOUT_FILENO;
#endif // _WIN32
}

CBDEF bool cb_chain_begin_opt(CB_Chain* chain, CB_Chain_Begin_Opt opt)
{
    chain->cmd.count = 0;
    chain->err2out = false;
    chain->fdin = CB_INVALID_FD;
    if (opt.stdin_path != NULL) {
        chain->fdin = cb_fd_open_read(opt.stdin_path);
        if (chain->fdin == CB_INVALID_FD) return false;
    }
    return true;
}

CBDEF bool cb_chain_cmd_opt(CB_Chain* chain, CB_Cmd* cmd, CB_Chain_Cmd_Opt opt)
{
    bool result = true;
    CB_Pipe pp = CB_ZERO;
    CB_Fd_List fds = CB_ZERO;

    CB_ASSERT(cmd->count > 0);

    if (chain->cmd.count != 0) { // not the first stage: run the previous one with its output piped
        CB_FD* pfdin = NULL;
        if (chain->fdin != CB_INVALID_FD) {
            cb__fd_list_push(&fds, chain->fdin);
            pfdin = &chain->fdin;
        }
        if (!cb_pipe_create(&pp)) cb_return_defer(false);
        cb__fd_list_push(&fds, pp.write);

        CB_FD* pfdout = &pp.write;
        CB_FD* pfderr = chain->err2out ? pfdout : NULL;

        CB_Proc proc = cb__cmd_start_process(chain->cmd, pfdin, pfdout, pfderr);
        chain->cmd.count = 0;
        if (proc == CB_INVALID_PROC) {
            cb__fd_list_push(&fds, pp.read);
            cb_return_defer(false);
        }
        chain->fdin = pp.read;
    }

    cb_da_append_many(&chain->cmd, cmd->items, cmd->count);
    chain->err2out = opt.err2out;

defer:
    cb__fd_list_close_all(&fds);
    if (!opt.dont_reset) cmd->count = 0;
    return result;
}

CBDEF bool cb_chain_end_opt(CB_Chain* chain, CB_Chain_End_Opt opt)
{
    bool result = true;
    CB_FD* pfdin = NULL;
    CB_Fd_List fds = CB_ZERO;

    if (chain->fdin != CB_INVALID_FD) {
        cb__fd_list_push(&fds, chain->fdin);
        pfdin = &chain->fdin;
    }

    if (chain->cmd.count != 0) { // the chain is not empty
        size_t max_procs = opt.max_procs > 0 ? opt.max_procs : (size_t)cb_nprocs() + 1;

        if (opt.async != NULL && max_procs > 0) {
            // wait until a concurrency slot frees up
            while (opt.async->count >= max_procs) {
                for (size_t i = 0; i < opt.async->count; ++i) {
                    int ret = cb__proc_wait_async(opt.async->items[i], 1);
                    if (ret < 0) cb_return_defer(false);
                    if (ret) {
                        cb_da_remove_unordered(opt.async, i);
                        break;
                    }
                }
            }
        }

        CB_FD fdout = cb__fd_stdout();
        if (opt.stdout_path != NULL) {
            fdout = cb_fd_open_write(opt.stdout_path);
            if (fdout == CB_INVALID_FD) cb_return_defer(false);
            cb__fd_list_push(&fds, fdout);
        }

        CB_FD fderr = CB_INVALID_FD;
        CB_FD* pfderr = NULL;
        if (chain->err2out) pfderr = &fdout;
        if (opt.stderr_path != NULL) {
            if (pfderr == NULL) {
                fderr = cb_fd_open_write(opt.stderr_path);
                if (fderr == CB_INVALID_FD) cb_return_defer(false);
                cb__fd_list_push(&fds, fderr);
                pfderr = &fderr;
            } else {
                // err2out was set on the last command, so its stderr already went to stdout:
                // create the stderr file empty, which keeps the semantics consistent
                CB_ASSERT(chain->err2out);
                if (!cb_write_entire_file(opt.stderr_path, NULL, 0)) cb_return_defer(false);
            }
        }

        CB_Proc proc = cb__cmd_start_process(chain->cmd, pfdin, &fdout, pfderr);
        chain->cmd.count = 0;

        if (opt.async != NULL) {
            if (proc == CB_INVALID_PROC) cb_return_defer(false);
            cb_da_append(opt.async, proc);
        } else {
            if (!cb_proc_wait(proc)) cb_return_defer(false);
        }
    }

defer:
    cb__fd_list_close_all(&fds);
    return result;
}

#endif // CB_IMPLEMENTATION

////////////////////////////////////////////////////////////////////////////////////////////////////
// Build Flags
////////////////////////////////////////////////////////////////////////////////////////////////////
// Defaults for the compiler abstraction used by build scripts. Every macro has its own #ifndef
// guard, so a project can override any of them before including cb.h:
//     #define cb_cc_flags(cmd) cb_cmd_append(cmd, "-Wall", "-Wextra", "-O2")
//     #include "cb.h"
//
// The platform split follows what actually compiles on each system:
//   macOS    neither -std=c99 nor -D_POSIX_C_SOURCE is passed: both hide symbols we need.
//   FreeBSD  -D_POSIX_C_SOURCE hides required symbols, -std=c99 is fine.
//   Linux    -std=c99 needs -D_POSIX_C_SOURCE=200112L, otherwise lstat/readlink/clock_gettime/
//            nanosleep/PATH_MAX stay undeclared.
//   MSVC     C and C++ modes take different switches (/TC vs /TP, /std:c++20) and no -I. form.
#ifndef cb_cc
#if defined(_WIN32) && defined(_MSC_VER)
#define cb_cc(cmd) cb_cmd_append(cmd, "cl.exe")
#elif defined(_WIN32) && defined(__clang__)
#define cb_cc(cmd) cb_cmd_append(cmd, "clang")
#elif defined(_WIN32) && defined(__TINYC__)
#define cb_cc(cmd) cb_cmd_append(cmd, "tcc")
#elif defined(__cplusplus)
#define cb_cc(cmd) cb_cmd_append(cmd, "cc", "-x", "c++")
#else
#define cb_cc(cmd) cb_cmd_append(cmd, "cc")
#endif
#endif /* cb_cc */

#ifndef cb_cc_flags
#if defined(__cplusplus)
#if defined(_MSC_VER)
#define cb_cc_flags(cmd) cb_cmd_append(cmd, "/std:c++20", "/TP", "/W4", "/nologo", "/D_CRT_SECURE_NO_WARNINGS", "-I.")
#else
#define cb_cc_flags(cmd) cb_cmd_append(cmd, "-Wall", "-Wextra", "-Wno-missing-field-initializers", "-Wswitch-enum", "-ggdb", "-I.")
#endif
#else // !__cplusplus
#if defined(_MSC_VER)
#define cb_cc_flags(cmd) cb_cmd_append(cmd, "/TC", "/W4", "/nologo", "/D_CRT_SECURE_NO_WARNINGS", "-I.")
#elif defined(__APPLE__) || defined(__MACH__)
#define cb_cc_flags(cmd) cb_cmd_append(cmd, "-Wall", "-Wextra", "-Wswitch-enum", "-I.")
#elif defined(__FreeBSD__)
#define cb_cc_flags(cmd) cb_cmd_append(cmd, "-Wall", "-Wextra", "-Wswitch-enum", "-std=c99", "-ggdb", "-I.")
#else
#define cb_cc_flags(cmd) cb_cmd_append(cmd, "-Wall", "-Wextra", "-Wswitch-enum", "-std=c99", "-D_POSIX_C_SOURCE=200112L", "-ggdb", "-I.")
#endif
#endif // __cplusplus
#endif /* cb_cc_flags */

#ifndef cb_cc_output
#if defined(_MSC_VER) && !defined(__clang__)
#define cb_cc_output(cmd, output_path) cb_cmd_append(cmd, cb_temp_sprintf("/Fe:%s", (output_path)), cb_temp_sprintf("/Fo:%s", (output_path)))
#else
#define cb_cc_output(cmd, output_path) cb_cmd_append(cmd, "-o", (output_path))
#endif
#endif /* cb_cc_output */

#ifndef cb_cc_inputs
#define cb_cc_inputs(cmd, ...) cb_cmd_append(cmd, __VA_ARGS__)
#endif /* cb_cc_inputs */

////////////////////////////////////////////////////////////////////////////////////////////////////
// C Builder
////////////////////////////////////////////////////////////////////////////////////////////////////
#ifndef CB_REBUILD_URSELF
#if defined(_WIN32)
#if defined(__clang__)
#if defined(__cplusplus)
#define CB_REBUILD_URSELF(binary_path, source_path) "clang", "-x", "c++", "-o", binary_path, source_path
#else
#define CB_REBUILD_URSELF(binary_path, source_path) "clang", "-x", "c", "-o", binary_path, source_path
#endif
#elif defined(__GNUC__)
#if defined(__cplusplus)
#define CB_REBUILD_URSELF(binary_path, source_path) "gcc", "-x", "c++", "-o", binary_path, source_path
#else
#define CB_REBUILD_URSELF(binary_path, source_path) "gcc", "-x", "c", "-o", binary_path, source_path
#endif
#elif defined(_MSC_VER)
#define CB_REBUILD_URSELF(binary_path, source_path) "cl.exe", cb_temp_sprintf("/Fe:%s", (binary_path)), source_path
#elif defined(__TINYC__)
#define CB_REBUILD_URSELF(binary_path, source_path) "tcc", "-o", binary_path, source_path
#endif
#else
#if defined(__cplusplus)
#define CB_REBUILD_URSELF(binary_path, source_path) "cc", "-x", "c++", "-o", binary_path, source_path
#else
#define CB_REBUILD_URSELF(binary_path, source_path) "cc", "-x", "c", "-o", binary_path, source_path
#endif
#endif
#endif

// Compiler command line used to rebuild this build script; redefine it to bootstrap differently.
CBDEF int cb_needs_rebuild(const char* binary_path, const char** source_paths, size_t source_paths_count);
CBDEF void cb__self_rebuild(int argc, char** argv, const char* source_path, ...);
// Call once at the top of main(): rebuild this program and re-run it when the program itself or any
// listed source file is newer than the binary. C99 needs at least one listed source path:
//     CB_SELF_REBUILD(argc, argv, "cb.h");
#define CB_SELF_REBUILD(argc, argv, ...) cb__self_rebuild(argc, argv, __FILE__, __VA_ARGS__, NULL)

#ifdef CB_IMPLEMENTATION

CBDEF int cb_needs_rebuild(const char* binary_path, const char** source_paths, size_t source_paths_count)
{
#ifdef _WIN32
    BOOL bSuccess;

    HANDLE output_path_fd = CreateFile(binary_path, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_READONLY, NULL);
    if (output_path_fd == INVALID_HANDLE_VALUE) {
        // NOTE: a missing output always means a rebuild
        if (GetLastError() == ERROR_FILE_NOT_FOUND) return 1;
        cb_log(CB_ERROR, "Could not open file %s: %s", binary_path, cb_win32_error_message(GetLastError()));
        return -1;
    }
    FILETIME output_path_time;
    bSuccess = GetFileTime(output_path_fd, NULL, NULL, &output_path_time);
    CloseHandle(output_path_fd);
    if (!bSuccess) {
        cb_log(CB_ERROR, "Could not get time of %s: %s", binary_path, cb_win32_error_message(GetLastError()));
        return -1;
    }

    for (size_t i = 0; i < source_paths_count; ++i) {
        const char* input_path = source_paths[i];
        HANDLE input_path_fd = CreateFile(input_path, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_READONLY, NULL);
        if (input_path_fd == INVALID_HANDLE_VALUE) {
            // NOTE: a missing source file is an error, the build depends on it
            cb_log(CB_ERROR, "Could not open file %s: %s", input_path, cb_win32_error_message(GetLastError()));
            return -1;
        }
        FILETIME input_path_time;
        bSuccess = GetFileTime(input_path_fd, NULL, NULL, &input_path_time);
        CloseHandle(input_path_fd);
        if (!bSuccess) {
            cb_log(CB_ERROR, "Could not get time of %s: %s", input_path, cb_win32_error_message(GetLastError()));
            return -1;
        }

        // NOTE: one source newer than the output is enough to force a rebuild
        if (CompareFileTime(&input_path_time, &output_path_time) == 1) return 1;
    }

    return 0;
#else
    struct stat statbuf = CB_ZERO;

    if (stat(binary_path, &statbuf) < 0) {
        // NOTE: a missing output must be rebuilt
        if (errno == ENOENT) return 1;
        cb_log(CB_ERROR, "Could not stat %s: %s", binary_path, strerror(errno));
        return -1;
    }
    time_t binary_path_time = statbuf.st_mtime;

    for (size_t i = 0; i < source_paths_count; ++i) {
        const char* input_path = source_paths[i];
        if (stat(input_path, &statbuf) < 0) {
            // NOTE: a missing input is an error (the build depends on it)
            cb_log(CB_ERROR, "Could not stat %s: %s", input_path, strerror(errno));
            return -1;
        }
        time_t source_path_time = statbuf.st_mtime;
        // NOTE: one source newer than the output forces a rebuild
        if (source_path_time > binary_path_time) return 1;
    }

    return 0;
#endif // _WIN32
}

// Idea taken from nob.h, which took it from https://github.com/zhiayang/nabs
CBDEF void cb__self_rebuild(int argc, char** argv, const char* source_path, ...)
{
    const char* binary_path = cb_shift(argv, argc);
#ifdef _WIN32
    // On Windows the executable is usually invoked without the extension (./cb, not ./cb.exe),
    // so add it back when renaming.
    if (!cb_sv_ends_with_cstr(cb_sv_from_cstr(binary_path), ".exe")) {
        binary_path = cb_temp_sprintf("%s.exe", binary_path);
    }
#endif

    CB_File_Paths source_paths = CB_ZERO;
    cb_da_append(&source_paths, source_path);
    va_list args;
    va_start(args, source_path);
    while (1) {
        const char* path = va_arg(args, const char*);
        if (path == NULL) break;
        cb_da_append(&source_paths, path);
    }
    va_end(args);

    int should_rebuild = cb_needs_rebuild(binary_path, source_paths.items, source_paths.count);
    if (should_rebuild < 0) exit(1); // error, return -1
    if (!should_rebuild) {           // dont needed to rebuild
        CB_FREE(source_paths.items);
        return;
    }

    CB_Cmd cmd = CB_ZERO;
    const char* old_binary_path = cb_temp_sprintf("%s.old", binary_path);

    if (!cb_rename(binary_path, old_binary_path)) exit(1);
    cb_cmd_append(&cmd, CB_REBUILD_URSELF(binary_path, source_path));
    CB_Cmd_Opt opt = CB_ZERO;
    if (!cb_cmd_run_opt(&cmd, opt)) {
        cb_rename(old_binary_path, binary_path);
        exit(1);
    }

#ifndef CB_DONT_DELETE_OLD_CB
    cb_delete_file(old_binary_path);
#endif // CB_EXPERIMENTAL_DELETE_OLD

    cb_cmd_append(&cmd, binary_path);
    cb_da_append_many(&cmd, argv, argc);
    if (!cb_cmd_run_opt(&cmd, opt)) exit(1);
    exit(0);
}

#endif // CB_IMPLEMENTATION

#endif /* CB_H_ */

// >>> CB_STRIP_PREFIX
////////////////////////////////////////////////////////////////////////////////////////////////////
// CB_STRIP_PREFIX
////////////////////////////////////////////////////////////////////////////////////////////////////
// Off by default. Define CB_STRIP_PREFIX before including cb.h to write temp_sprintf /
// String_View instead of cb_temp_sprintf / CB_String_View.
//
// This region sits at the very end on purpose: the definitions above use the prefixed names, so
// aliases placed earlier would interfere with them. It has its own guard, so including cb.h from
// several .c files expands it once per file.
//
// Names deliberately NOT aliased (dropping the prefix would collide):
//   ERROR     mingw <wingdi.h> defines ERROR
//   PATH_MAX  limits.h defines it (CB_PATH_MAX is the fallback for exactly that)
//   clamp     C++ std::clamp
//   glob      POSIX glob()
//   log       libm log()
//   max/min   Windows macros, C++ std::max / std::min
//   rename    stdio.h rename()
//   rotl64    C23 <stdbit.h>, glibc
//   rotr64    C23 <stdbit.h>, glibc
//   swap      C++ std::swap
#ifndef CB_STRIP_PREFIX_GUARD_
#define CB_STRIP_PREFIX_GUARD_
#  ifdef CB_STRIP_PREFIX
// ---- General ----
#    define ASSERT CB_ASSERT
#    define PRINTF_FORMAT CB_PRINTF_FORMAT
// ---- Allocator ----
#    define CLIT CB_CLIT
#    define DECLTYPE_CAST CB_DECLTYPE_CAST
#    define FREE CB_FREE
#    define FREE_RAW CB_FREE_RAW
#    define OOM CB_OOM
#    define REALLOC CB_REALLOC
#    define REALLOC_RAW CB_REALLOC_RAW
#    define alloc_check cb_alloc_check
#    define alloc_live_count cb_alloc_live_count
#    define alloc_live_size cb_alloc_live_size
#    define alloc_report cb_alloc_report
#    define shift cb_shift
// ---- Logger / Panic ----
#    define ARRAY_GET CB_ARRAY_GET
#    define ARRAY_LEN CB_ARRAY_LEN
#    define DEPRECATED CB_DEPRECATED
#    define INFO CB_INFO
#    define LOG_AT CB_LOG_AT
#    define Log_Level CB_Log_Level
#    define NO_LOGS CB_NO_LOGS
#    define PANIC_BACKTRACE CB_PANIC_BACKTRACE
#    define TODO CB_TODO
#    define UNREACHABLE CB_UNREACHABLE
#    define UNUSED CB_UNUSED
#    define WARN CB_WARN
#    define ZERO CB_ZERO
#    define cancer_log_handler cb_cancer_log_handler
#    define default_log_handler cb_default_log_handler
#    define get_log_handler cb_get_log_handler
#    define log_at cb_log_at
#    define log_handler cb_log_handler
#    define minimal_log_level cb_minimal_log_level
#    define null_log_handler cb_null_log_handler
#    define return_defer cb_return_defer
#    define set_log_handler cb_set_log_handler
// ---- Timer ----
#    define TIMER_END CB_TIMER_END
#    define TIMER_MAX_DEPTH CB_TIMER_MAX_DEPTH
#    define TIMER_START CB_TIMER_START
#    define Timer CB_Timer
#    define Timer_Frame CB_Timer_Frame
#    define Timer_Stat CB_Timer_Stat
#    define get_time_ms cb_get_time_ms
#    define get_time_us cb_get_time_us
#    define nanos_since_unspecified_epoch cb_nanos_since_unspecified_epoch
#    define timer_begin cb_timer_begin
#    define timer_end cb_timer_end
#    define timer_end_print cb_timer_end_print
#    define timer_end_stat cb_timer_end_stat
#    define timer_fprint_stats cb_timer_fprint_stats
#    define timer_get_stat cb_timer_get_stat
#    define timer_print_stats cb_timer_print_stats
#    define timer_reset cb_timer_reset
// ---- Arena / Temp Storage ----
#    define ARENA_ALIGN CB_ARENA_ALIGN
#    define ARENA_REGION_INIT_CAPACITY CB_ARENA_REGION_INIT_CAPACITY
#    define Arena CB_Arena
#    define Arena_Mark CB_Arena_Mark
#    define Arena_Region CB_Arena_Region
#    define TEMP_CAPACITY CB_TEMP_CAPACITY
#    define THREAD_LOCAL CB_THREAD_LOCAL
#    define arena_alloc cb_arena_alloc
#    define arena_alloc_aligned cb_arena_alloc_aligned
#    define arena_free cb_arena_free
#    define arena_realloc cb_arena_realloc
#    define arena_reset cb_arena_reset
#    define arena_rewind cb_arena_rewind
#    define arena_save cb_arena_save
#    define arena_sprintf cb_arena_sprintf
#    define arena_strdup cb_arena_strdup
#    define arena_strndup cb_arena_strndup
#    define arena_vsprintf cb_arena_vsprintf
#    define temp_alloc cb_temp_alloc
#    define temp_reset cb_temp_reset
#    define temp_rewind cb_temp_rewind
#    define temp_save cb_temp_save
#    define temp_sprintf cb_temp_sprintf
#    define temp_strdup cb_temp_strdup
#    define temp_strndup cb_temp_strndup
#    define temp_vsprintf cb_temp_vsprintf
// ---- Dynamic Array ----
#    define DA_INIT_CAP CB_DA_INIT_CAP
#    define DArray CB_DArray
#    define da_append cb_da_append
#    define da_append_many cb_da_append_many
#    define da_clear cb_da_clear
#    define da_first cb_da_first
#    define da_foreach cb_da_foreach
#    define da_foreach_rev cb_da_foreach_rev
#    define da_free cb_da_free
#    define da_insert cb_da_insert
#    define da_last cb_da_last
#    define da_pop cb_da_pop
#    define da_remove_ordered cb_da_remove_ordered
#    define da_remove_unordered cb_da_remove_unordered
#    define da_reserve cb_da_reserve
#    define da_resize cb_da_resize
#    define fa_append cb_fa_append
// ---- Bitset ----
#    define BITSET_WORD_BITS CB_BITSET_WORD_BITS
#    define Bitset CB_Bitset
#    define bitset_clear_all cb_bitset_clear_all
#    define bitset_count cb_bitset_count
#    define bitset_find cb_bitset_find
#    define bitset_free cb_bitset_free
#    define bitset_resize cb_bitset_resize
#    define bitset_set cb_bitset_set
#    define bitset_test cb_bitset_test
#    define bitset_toggle cb_bitset_toggle
#    define bitset_unset cb_bitset_unset
// ---- Ring Buffer ----
#    define Ring CB_Ring
#    define ring_clear cb_ring_clear
#    define ring_free cb_ring_free
#    define ring_init cb_ring_init
#    define ring_peek cb_ring_peek
#    define ring_read cb_ring_read
#    define ring_space cb_ring_space
#    define ring_write cb_ring_write
// ---- Sort ----
#    define Compare_Func CB_Compare_Func
#    define da_sort cb_da_sort
#    define da_sort_insertion cb_da_sort_insertion
// ---- StringBuilder ----
#    define String_Builder CB_String_Builder
#    define read_entire_file cb_read_entire_file
#    define sb_append cb_sb_append
#    define sb_append_buf cb_sb_append_buf
#    define sb_append_cstr cb_sb_append_cstr
#    define sb_append_null cb_sb_append_null
#    define sb_append_sv cb_sb_append_sv
#    define sb_appendf cb_sb_appendf
#    define sb_free cb_sb_free
#    define sb_pad_align cb_sb_pad_align
// ---- StringView ----
#    define SVLIT CB_SVLIT
#    define SVLIT_STATIC CB_SVLIT_STATIC
#    define SV_ARG CB_SV_ARG
#    define SV_FMT CB_SV_FMT
#    define String_View CB_String_View
#    define sb_to_sv cb_sb_to_sv
#    define sv_chop_by_delim cb_sv_chop_by_delim
#    define sv_chop_by_delim_r cb_sv_chop_by_delim_r
#    define sv_chop_by_func cb_sv_chop_by_func
#    define sv_chop_left cb_sv_chop_left
#    define sv_chop_prefix cb_sv_chop_prefix
#    define sv_chop_right cb_sv_chop_right
#    define sv_chop_suffix cb_sv_chop_suffix
#    define sv_ends_with cb_sv_ends_with
#    define sv_ends_with_cstr cb_sv_ends_with_cstr
#    define sv_eq cb_sv_eq
#    define sv_find cb_sv_find
#    define sv_find_sv cb_sv_find_sv
#    define sv_from_cstr cb_sv_from_cstr
#    define sv_from_parts cb_sv_from_parts
#    define sv_starts_with cb_sv_starts_with
#    define sv_starts_with_cstr cb_sv_starts_with_cstr
#    define sv_to_temp_cstr cb_sv_to_temp_cstr
#    define sv_trim cb_sv_trim
#    define sv_trim_left cb_sv_trim_left
#    define sv_trim_right cb_sv_trim_right
// ---- UTF-8 Support ----
#    define sv_utf8_len cb_sv_utf8_len
#    define bytes_for_utf8 cb_bytes_for_utf8
// ---- StringView Tools ----
#    define sb_append_join cb_sb_append_join
#    define sv_eq_ignore_case cb_sv_eq_ignore_case
#    define sv_split_next cb_sv_split_next
#    define sv_to_f64 cb_sv_to_f64
#    define sv_to_i64 cb_sv_to_i64
#    define sv_to_temp_lower cb_sv_to_temp_lower
#    define sv_to_temp_upper cb_sv_to_temp_upper
#    define sv_to_u64 cb_sv_to_u64
#    define sv_utf8_next cb_sv_utf8_next
#    define utf8_decode cb_utf8_decode
#    define utf8_encode cb_utf8_encode
#    define utf8_validate cb_utf8_validate
// ---- HashMap ----
#    define MAP_EMPTY CB_MAP_EMPTY
#    define MAP_INIT_CAPACITY CB_MAP_INIT_CAPACITY
#    define MAP_TOMBSTONE CB_MAP_TOMBSTONE
#    define MAP_USED CB_MAP_USED
#    define Map CB_Map
#    define Map_Iter CB_Map_Iter
#    define Map_Slot CB_Map_Slot
#    define hash_bytes cb_hash_bytes
#    define hash_u64 cb_hash_u64
#    define map_clear cb_map_clear
#    define map_count cb_map_count
#    define map_del cb_map_del
#    define map_foreach cb_map_foreach
#    define map_free cb_map_free
#    define map_get cb_map_get
#    define map_get_cstr cb_map_get_cstr
#    define map_has cb_map_has
#    define map_init_arena cb_map_init_arena
#    define map_init_capacity cb_map_init_capacity
#    define map_iter cb_map_iter
#    define map_next cb_map_next
#    define map_put cb_map_put
#    define map_put_cstr cb_map_put_cstr
// ---- HashMap (uint64 keys) ----
#    define Map_U64 CB_Map_U64
#    define Map_U64_Iter CB_Map_U64_Iter
#    define Map_U64_Slot CB_Map_U64_Slot
#    define map_u64_clear cb_map_u64_clear
#    define map_u64_count cb_map_u64_count
#    define map_u64_del cb_map_u64_del
#    define map_u64_foreach cb_map_u64_foreach
#    define map_u64_free cb_map_u64_free
#    define map_u64_get cb_map_u64_get
#    define map_u64_has cb_map_u64_has
#    define map_u64_init_arena cb_map_u64_init_arena
#    define map_u64_init_capacity cb_map_u64_init_capacity
#    define map_u64_iter cb_map_u64_iter
#    define map_u64_next cb_map_u64_next
#    define map_u64_put cb_map_u64_put
// ---- File System ----
#    define Dir_Entry CB_Dir_Entry
#    define FILE_DIRECTORY CB_FILE_DIRECTORY
#    define FILE_ERROR CB_FILE_ERROR
#    define FILE_OTHER CB_FILE_OTHER
#    define FILE_REGULAR CB_FILE_REGULAR
#    define FILE_SYMLINK CB_FILE_SYMLINK
#    define File_Paths CB_File_Paths
#    define File_Type CB_File_Type
#    define WALK_CONT CB_WALK_CONT
#    define WALK_SKIP CB_WALK_SKIP
#    define WALK_STOP CB_WALK_STOP
#    define WIN32_ERR_MSG_SIZE CB_WIN32_ERR_MSG_SIZE
#    define Walk_Action CB_Walk_Action
#    define Walk_Dir_Opt CB_Walk_Dir_Opt
#    define Walk_Entry CB_Walk_Entry
#    define Walk_Func CB_Walk_Func
#    define copy_directory_recursively cb_copy_directory_recursively
#    define copy_file cb_copy_file
#    define delete_directory_recursively cb_delete_directory_recursively
#    define delete_file cb_delete_file
#    define delete_walk_entry cb_delete_walk_entry
#    define dir_entry_close cb_dir_entry_close
#    define dir_entry_next cb_dir_entry_next
#    define dir_entry_open cb_dir_entry_open
#    define file_exists cb_file_exists
#    define get_current_dir_temp cb_get_current_dir_temp
#    define get_file_type cb_get_file_type
#    define mkdir_if_not_exists cb_mkdir_if_not_exists
#    define path_name cb_path_name
#    define read_entire_dir cb_read_entire_dir
#    define set_current_dir cb_set_current_dir
#    define temp_dir_name cb_temp_dir_name
#    define temp_file_ext cb_temp_file_ext
#    define temp_file_name cb_temp_file_name
#    define temp_running_executable_path cb_temp_running_executable_path
#    define walk_dir cb_walk_dir
#    define walk_dir_opt cb_walk_dir_opt
#    define win32_error_message cb_win32_error_message
#    define write_entire_file cb_write_entire_file
// ---- Path ----
#    define PATH_SEP CB_PATH_SEP
#    define path_absolute cb_path_absolute
#    define path_is_absolute cb_path_is_absolute
#    define path_is_sep cb_path_is_sep
#    define path_join cb_path_join
#    define path_normalize cb_path_normalize
#    define path_replace_ext cb_path_replace_ext
// ---- File System Extras ----
#    define Mmap CB_Mmap
#    define PROCESS_ID CB_PROCESS_ID
#    define create_symlink cb_create_symlink
#    define file_mtime cb_file_mtime
#    define file_size cb_file_size
#    define glob_match cb_glob_match
#    define mmap_close cb_mmap_close
#    define mmap_open cb_mmap_open
#    define read_symlink cb_read_symlink
#    define write_entire_file_atomic cb_write_entire_file_atomic
// ---- Math & Bits ----
#    define align_down cb_align_down
#    define align_up cb_align_up
#    define bswap32 cb_bswap32
#    define bswap64 cb_bswap64
#    define clz64 cb_clz64
#    define ctz64 cb_ctz64
#    define is_little_endian cb_is_little_endian
#    define is_pow2 cb_is_pow2
#    define next_pow2 cb_next_pow2
#    define popcount64 cb_popcount64
// ---- Time & Date ----
#    define duration_to_str cb_duration_to_str
#    define time_now cb_time_now
#    define time_to_iso8601 cb_time_to_iso8601
// ---- Random ----
#    define Rng CB_Rng
#    define rng_double cb_rng_double
#    define rng_next cb_rng_next
#    define rng_range cb_rng_range
#    define rng_seed cb_rng_seed
#    define rng_shuffle cb_rng_shuffle
// ---- Runtime Environment ----
#    define color_enabled cb_color_enabled
#    define env_get cb_env_get
#    define env_set cb_env_set
#    define stdout_is_tty cb_stdout_is_tty
#    define terminal_width cb_terminal_width
// ---- Hex Dump ----
#    define dump_hex cb_dump_hex
// ---- CLI Args ----
#    define Arg_Entry CB_Arg_Entry
#    define Arg_List CB_Arg_List
#    define Args CB_Args
#    define args_free cb_args_free
#    define args_get cb_args_get
#    define args_get_first cb_args_get_first
#    define args_has cb_args_has
#    define args_parse cb_args_parse
#    define args_positional cb_args_positional
#    define args_positional_count cb_args_positional_count
// ---- Process & FD ----
#    define INVALID_FD CB_INVALID_FD
#    define INVALID_PROC CB_INVALID_PROC
#    define Pipe CB_Pipe
#    define Procs CB_Procs
#    define fd_close cb_fd_close
#    define fd_open_read cb_fd_open_read
#    define fd_open_write cb_fd_open_write
#    define pipe_create cb_pipe_create
#    define proc_wait cb_proc_wait
#    define procs_wait cb_procs_wait
#    define procs_wait_and_reset cb_procs_wait_and_reset
// ---- Cmd ----
#    define Cmd CB_Cmd
#    define Cmd_Opt CB_Cmd_Opt
#    define Pipes CB_Pipes
#    define cmd_append cb_cmd_append
#    define cmd_extend cb_cmd_extend
#    define cmd_free cb_cmd_free
#    define cmd_run cb_cmd_run
#    define cmd_run_opt cb_cmd_run_opt
#    define cmd_to_sb cb_cmd_to_sb
#    define nprocs cb_nprocs
// ---- Cmd Chain ----
#    define Chain CB_Chain
#    define Chain_Begin_Opt CB_Chain_Begin_Opt
#    define Chain_Cmd_Opt CB_Chain_Cmd_Opt
#    define Chain_End_Opt CB_Chain_End_Opt
#    define Fd_List CB_Fd_List
#    define chain_begin cb_chain_begin
#    define chain_begin_opt cb_chain_begin_opt
#    define chain_cmd cb_chain_cmd
#    define chain_cmd_opt cb_chain_cmd_opt
#    define chain_end cb_chain_end
#    define chain_end_opt cb_chain_end_opt
// ---- C Builder ----
#    define REBUILD_URSELF CB_REBUILD_URSELF
#    define SELF_REBUILD CB_SELF_REBUILD
#    define cc cb_cc
#    define cc_flags cb_cc_flags
#    define cc_inputs cb_cc_inputs
#    define cc_output cb_cc_output
#    define needs_rebuild cb_needs_rebuild
#  endif // CB_STRIP_PREFIX
#endif // CB_STRIP_PREFIX_GUARD_
// <<< end of CB_STRIP_PREFIX
