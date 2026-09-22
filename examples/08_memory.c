// Memory: the arena allocator, temp storage and the optional allocation tracker.
#define CB_IMPLEMENTATION
#define CB_ALLOC_TRACK
#include "cb.h"

int main(void)
{
    // ---- arena: one explicit handle, memory released as a whole ------------------------------
    CB_Arena arena = CB_ZERO;
    void* p1 = cb_arena_alloc(&arena, 8);
    void* p2 = cb_arena_alloc_aligned(&arena, 8, 64);
    printf("arena: first = %p, aligned(64) = %d\n", p1, (int)(((uintptr_t)p2 % 64) == 0));

    char* text = cb_arena_sprintf(&arena, "arena string %d", 42);
    printf("arena sprintf: %s\n", text);

    CB_Arena_Mark mark = cb_arena_save(&arena);
    void* scratch = cb_arena_alloc(&arena, 1024);
    cb_arena_rewind(&arena, mark); // everything after the mark is invalid again
    printf("arena rewind: scratch was %p, reused = %d\n", scratch, (int)(cb_arena_alloc(&arena, 1024) == scratch));

    // ---- temp storage: the implicit thread-local arena --------------------------------------
    CB_Arena_Mark temp_mark = cb_temp_save();
    const char* temp = cb_temp_sprintf("temp %s", "storage");
    printf("temp: %s\n", temp);
    cb_temp_rewind(temp_mark);

    // ---- allocation tracking (CB_ALLOC_TRACK) ------------------------------------------------
    printf("tracker: live blocks = %zu, live bytes = %zu\n", cb_alloc_live_count(), cb_alloc_live_size());

    cb_arena_free(&arena);
    printf("after arena_free: live blocks = %zu\n", cb_alloc_live_count());
    cb_alloc_report(); // lists every block that is still alive (temp storage is never freed)
    return 0;
}
