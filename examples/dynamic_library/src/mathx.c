#include "core.h"

// Second translation unit on purpose: the build script collects sources recursively with
// cb_walk_dir(), so a project is never limited to one .c file.
int core_cube(int value)
{
    return value * value * value;
}
