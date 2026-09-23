#include <stdio.h>
#include "test_diagnostics.h"
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

    printf("\n== cb_sv_utf8_len ==\n");

    // Same walk, one codepoint at a time: the table value the loop above already prints, next to
    // the number of characters cb_sv_utf8_len() still sees in the remainder.
    CB_String_View rest = cb_sv_from_cstr("Привет, Мир! 你好");
    while (rest.count > 0) {
        size_t n = cb_bytes_for_utf8[(uint8_t)*rest.data];
        CB_String_View c = cb_sv_chop_left(&rest, n);
        printf(CB_SV_FMT" => 表值 %zu, 剩余 cb_sv_utf8_len = %zu\n",
               CB_SV_ARG(c), n, cb_sv_utf8_len(rest, NULL));
    }

    // And the whole view in one call, side by side with the count of the hand-rolled loop.
    CB_String_View whole = cb_sv_from_cstr("Привет, Мир! 你好");
    size_t overrun = (size_t)-1;
    size_t len = cb_sv_utf8_len(whole, &overrun);
    printf("cb_sv_utf8_len(完整串) = %zu, 手写循环 count = %zu, byte 数 = %zu, bytes_overrun = %zu\n",
           len, count, whole.count, overrun);

    // A 3-byte sequence cut after its first byte: still one character, and the walk reports the
    // 2 bytes it wanted to read past the end of the view.
    CB_String_View cut = cb_sv_from_parts("你好", 1);
    size_t cut_overrun = 0;
    size_t cut_len = cb_sv_utf8_len(cut, &cut_overrun);
    printf("cb_sv_utf8_len(截断的 3 字节序列, byte 数 = %zu) = %zu, bytes_overrun = %zu\n",
           cut.count, cut_len, cut_overrun);

    // bytes_overrun may be NULL: the length is still returned and no out-parameter is written.
    printf("cb_sv_utf8_len(截断串, NULL) = %zu\n", cb_sv_utf8_len(cut, NULL));

    // An empty view terminates the walk immediately, with nothing overrun.
    CB_String_View empty = cb_sv_from_parts("", 0);
    size_t empty_overrun = (size_t)-1;
    size_t empty_len = cb_sv_utf8_len(empty, &empty_overrun);
    printf("cb_sv_utf8_len(空 view) = %zu, bytes_overrun = %zu\n", empty_len, empty_overrun);

    return 0;
}
