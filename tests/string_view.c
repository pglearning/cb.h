// String_View 实测：chop 系列 / trim / find / 前后缀 / 空视图回归
//
// 风格：纯 printf + golden 比对，测试只把实际观察到的值打出来。
#include "cb.h"

int main(void)
{

    const char* tx = "  Hello ";
    CB_String_Builder sb = {0};
    cb_sb_appendf(&sb, "World!");

    CB_String_View sv = cb_sv_from_parts(tx, 8);

    CB_String_View sv_c = cb_sv_from_cstr("o");

    cb_sb_to_sv(sb);

    CB_String_View sv_remove = cb_sv_chop_by_func(&sv, isspace);
    printf("|" CB_SV_FMT "|\n", CB_SV_ARG(sv));
    printf("|" CB_SV_FMT "|\n", CB_SV_ARG(sv_remove));

    sv_remove = cb_sv_chop_by_delim(&sv, 'l');
    printf("|" CB_SV_FMT "|\n", CB_SV_ARG(sv));
    printf("|" CB_SV_FMT "|\n", CB_SV_ARG(sv_remove));

    sv_remove = cb_sv_chop_by_delim_r(&sv, 'l');
    printf("|" CB_SV_FMT "|\n", CB_SV_ARG(sv));
    printf("|" CB_SV_FMT "|\n", CB_SV_ARG(sv_remove));

    sv_remove = cb_sv_chop_left(&sv, 1);
    printf("|" CB_SV_FMT "|\n", CB_SV_ARG(sv));
    printf("|" CB_SV_FMT "|\n", CB_SV_ARG(sv_remove));

    sv_remove = cb_sv_chop_right(&sv, 1);
    printf("|" CB_SV_FMT "|\n", CB_SV_ARG(sv));
    printf("|" CB_SV_FMT "|\n", CB_SV_ARG(sv_remove));

    bool has_suffix;
    has_suffix = cb_sv_ends_with(sv, sv_c);
    printf("has_suffix = %d\n", (int)has_suffix);

    has_suffix = cb_sv_ends_with_cstr(sv, "i");
    printf("has_suffix = %d\n", (int)has_suffix);

    bool has_prefix;
    has_prefix = cb_sv_starts_with(sv, sv_c);
    printf("has_prefix = %d\n", (int)has_prefix);

    has_prefix = cb_sv_starts_with_cstr(sv, "i");
    printf("has_prefix = %d\n", (int)has_prefix);

    has_prefix = cb_sv_chop_prefix(&sv, sv_c);
    printf("|" CB_SV_FMT "|\n", CB_SV_ARG(sv));
    printf("has_prefix = %d\n", (int)has_prefix);

    has_suffix = cb_sv_chop_suffix(&sv, sv_c);
    printf("|" CB_SV_FMT "|\n", CB_SV_ARG(sv));
    printf("has_suffix = %d\n", (int)has_suffix);

    CB_String_View svv = cb_sv_from_cstr("   d1dd   ");
    sv = cb_sv_trim(svv);
    printf("|" CB_SV_FMT "|\n", CB_SV_ARG(svv));
    printf("|" CB_SV_FMT "|\n", CB_SV_ARG(sv));

    int index = cb_sv_find(&svv, '1');
    printf("|find index = %d|\n", index);
    printf("|" CB_SV_FMT "|\n", CB_SV_ARG(svv));
    printf("|" CB_SV_FMT "|\n", CB_SV_ARG(sv));

    // ------------------------------------------------ 空视图（data 可以是 NULL）
    CB_String_View null_a = CB_ZERO;
    CB_String_View null_b = CB_ZERO;
    printf("两个 NULL 空视图相等 = %d\n", (int)cb_sv_eq(null_a, null_b));
    printf("NULL 空视图 vs \"\" = %d\n", (int)cb_sv_eq(null_a, CB_SVLIT("")));
    printf("空视图 vs 非空 = %d\n", (int)cb_sv_eq(null_a, sv_c));
    printf("ends_with 空后缀 = %d\n", (int)cb_sv_ends_with(null_a, null_b));

    cb_sb_free(sb);
    return 0;
}
