// Strings: StringBuilder, StringView, UTF-8 and the string tools (case, parsing, split/join).
#define CB_IMPLEMENTATION
#include "cb.h"

int main(void)
{
    // ---- StringBuilder is always NUL-terminated ---------------------------------------------
    CB_String_Builder sb = CB_ZERO;
    cb_sb_append_cstr(&sb, "name=");
    cb_sb_appendf(&sb, "%s", "cb.h");
    cb_sb_appendf(&sb, " count=%d", 42);
    printf("sb: %s (count = %zu)\n", sb.items, sb.count);

    // ---- StringView: cheap slices of an existing buffer --------------------------------------
    CB_String_View sv = cb_sb_to_sv(sb);
    CB_String_View key = cb_sv_chop_by_delim(&sv, ' ');
    printf("sv: key = " CB_SV_FMT ", rest = " CB_SV_FMT "\n", CB_SV_ARG(key), CB_SV_ARG(sv));

    CB_String_View line = cb_sv_from_cstr("   value = 123  ");
    CB_String_View trimmed = cb_sv_trim(line);
    printf("trim: |" CB_SV_FMT "|, starts_with(\"value\") = %d\n",
           CB_SV_ARG(trimmed), cb_sv_starts_with_cstr(trimmed, "value"));

    CB_String_View edit = cb_sv_from_cstr("prefix-body.txt");
    if (cb_sv_chop_prefix(&edit, CB_SVLIT("prefix-"))) {
        printf("chop_prefix: " CB_SV_FMT ", ends_with(\".txt\") = %d\n",
               CB_SV_ARG(edit), cb_sv_ends_with_cstr(edit, ".txt"));
    }
    CB_String_View haystack = cb_sv_from_cstr("abcabc");
    printf("find: 'b' at %d, \"ca\" at %d\n",
           cb_sv_find(&haystack, 'b'), cb_sv_find_sv(&haystack, CB_SVLIT("ca"), 0));

    // ---- string tools ------------------------------------------------------------------------
    char* upper = cb_sv_to_temp_upper(CB_SVLIT("MiXeD"));
    char* lower = cb_sv_to_temp_lower(CB_SVLIT("MiXeD"));
    printf("case: %s / %s, ignore_case eq = %d\n",
           upper, lower, cb_sv_eq_ignore_case(CB_SVLIT("ABC"), CB_SVLIT("abc")));

    int64_t number = 0;
    if (cb_sv_to_i64(CB_SVLIT("  -1234  "), &number)) printf("parse: %lld\n", (long long)number);
    uint64_t hex = 0;
    if (cb_sv_to_u64(CB_SVLIT("0xff"), &hex)) printf("parse hex: %llu\n", (unsigned long long)hex);

    CB_String_View fields = cb_sv_from_cstr("a,b,,c");
    CB_String_View field;
    printf("split:");
    while (cb_sv_split_next(&fields, ',', &field)) printf(" |" CB_SV_FMT "|", CB_SV_ARG(field));
    printf("\n");

    CB_String_View parts[] = {CB_SVLIT("one"), CB_SVLIT("two"), CB_SVLIT("three")};
    CB_String_Builder joined = CB_ZERO;
    cb_sb_append_join(&joined, parts, CB_ARRAY_LEN(parts), CB_SVLIT(" + "));
    printf("join: %s\n", joined.items);
    cb_sb_free(joined);
    cb_sb_free(sb);

    // ---- UTF-8 -------------------------------------------------------------------------------
    CB_String_View text = cb_sv_from_cstr("a\xc3\xa9\xe4\xb8\xad"); // a, e-acute, CJK
    size_t overrun = 0;
    printf("utf8: bytes = %zu, code points = %zu, valid = %d\n",
           text.count, cb_sv_utf8_len(text, &overrun), cb_utf8_validate(text, NULL));

    uint32_t codepoint = 0;
    size_t length = 0;
    if (cb_utf8_decode(text.data + 1, text.count - 1, &codepoint, &length)) {
        printf("utf8: first multi-byte code point = U+%04X in %zu bytes\n", codepoint, length);
    }
    char encoded[4];
    size_t encoded_size = cb_utf8_encode(0x4E2D, encoded);
    printf("utf8: encoded U+4E2D in %zu bytes\n", encoded_size);

    CB_String_View walk = text;
    printf("utf8: walking:");
    while (walk.count > 0) {
        if (!cb_sv_utf8_next(&walk, &codepoint)) break;
        printf(" U+%04X", codepoint);
    }
    printf("\n");
    return 0;
}
