#include <stdlib.h>

// The library's header by relative path, so the build script does not need an -I for it; the
// project's own include/ directory is added by the build function.
#include "../../dynamic_library/include/core.h"
#include "report.h"

int main(int argc, char** argv)
{
    // The build script passes its run_arg here: ./bin/app 7
    int value = argc > 1 ? atoi(argv[1]) : 7;
    report_value(value, core_square(value), core_cube(value));
    report_library(core_version());
    return 0;
}
