#include <stdio.h>
#include "cb.h"

int main()
{
    CB_String_View sv = cb_sv_from_cstr("Привет, Мир! 你好");
    size_t count = 0;
    while (sv.count > 0) {
        size_t n = cb_bytes_for_utf8[(uint8_t)*sv.data];
        CB_String_View c = cb_sv_chop_left(&sv, n);
        printf(CB_SV_FMT" => %zu\n", CB_SV_ARG(c), n);
        count += 1;
    }
    printf("count = %zu\n", count);
    return 0;
}
