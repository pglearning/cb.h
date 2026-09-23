#include <stdio.h>

#include "report.h"

void report_value(int value, int square, int cube)
{
    printf("app: core_square(%d) = %d, core_cube(%d) = %d\n", value, square, value, cube);
}

void report_library(const char* version)
{
    printf("app: linked against %s\n", version);
}
