// 示例 08：utf8
//
// 覆盖：起始字节长度表、按码点计长、解码、编码、逐码点遍历、合法性校验
//
// 编译运行（在仓库根目录）：
//     cc -o /tmp/ex08 examples/08_utf8.c && /tmp/ex08

#include "../cb.h"

static void title(const char* text) { printf("\n== %s ==\n", text); }

static void demo_length(void)
{
    title("cb_bytes_for_utf8：起始字节 -> 该码点占几字节");

    printf("  0x41 ('A') 起始字节 => %u 字节\n", (unsigned)cb_bytes_for_utf8[0x41]);
    printf("  0xE4 (「中」的首字节) => %u 字节\n", (unsigned)cb_bytes_for_utf8[0xE4]);
    printf("  0xF0 (emoji 的首字节) => %u 字节\n", (unsigned)cb_bytes_for_utf8[0xF0]);
    printf("  0xFF (非法) => %u 字节\n", (unsigned)cb_bytes_for_utf8[0xFF]);
}

static void demo_utf8_len(void)
{
    title("cb_sv_utf8_len：按码点数字符数");

    CB_String_View ascii = CB_SVLIT("hello");
    CB_String_View chinese = CB_SVLIT("中文测试");
    CB_String_View emoji = CB_SVLIT("a🙂b");

    size_t overrun = 0;
    printf("  \"hello\"    字节 %2zu -> 字符 %zu\n", ascii.count, cb_sv_utf8_len(ascii, &overrun));
    printf("  \"中文测试\" 字节 %2zu -> 字符 %zu\n", chinese.count, cb_sv_utf8_len(chinese, &overrun));
    printf("  \"a🙂b\"     字节 %2zu -> 字符 %zu\n", emoji.count, cb_sv_utf8_len(emoji, &overrun));

    // bytes_overrun 用来报告"最后一个码点被截断了几字节"
    CB_String_View truncated = cb_sv_from_parts("中", 2); // 只给前 2 字节（正常是 3）
    size_t chars = cb_sv_utf8_len(truncated, &overrun);
    printf("  截断的\"中\"（只给 2 字节）-> 字符 %zu，bytes_overrun = %zu\n", chars, overrun);
}

static void demo_decode_encode(void)
{
    title("cb_utf8_decode / cb_utf8_encode");

    // 解码：给出码点与长度
    struct { const char* text; size_t bytes; } cases[] = {
        {"A", 1}, {"é", 2}, {"中", 3}, {"🙂", 4},
    };
    for (size_t i = 0; i < CB_ARRAY_LEN(cases); ++i) {
        uint32_t cp = 0;
        size_t len = 0;
        if (cb_utf8_decode(cases[i].text, cases[i].bytes, &cp, &len)) {
            printf("  \"%s\" (%zu 字节) -> U+%04X，解码消耗 %zu 字节\n", cases[i].text, cases[i].bytes, cp, len);
        } else {
            printf("  \"%s\" 解码失败\n", cases[i].text);
        }
    }

    // 编码：往返一致
    uint32_t codepoints[] = {'A', 0x00E9, 0x4E2D, 0x1F642};
    for (size_t i = 0; i < CB_ARRAY_LEN(codepoints); ++i) {
        char buf[4] = CB_ZERO;
        size_t n = cb_utf8_encode(codepoints[i], buf);
        printf("  U+%04X -> %zu 字节: %.*s\n", codepoints[i], n, (int)n, buf);
    }

    // 非法码点被拒绝
    char buf[4] = CB_ZERO;
    printf("  U+110000（超出 Unicode 范围）-> 编码返回 %zu（0 表示拒绝）\n", cb_utf8_encode(0x110000, buf));
    printf("  U+D800（代理区）-> 编码返回 %zu\n", cb_utf8_encode(0xD800, buf));
}

static void demo_iterate(void)
{
    title("cb_sv_utf8_next：逐码点遍历");

    CB_String_View text = CB_SVLIT("a中🙂b");
    printf("  \"a中🙂b\" 共 %zu 字节，逐个码点：\n", text.count);

    CB_String_View walk = text;
    uint32_t cp = 0;
    int index = 0;
    while (cb_sv_utf8_next(&walk, &cp)) {
        printf("    [%d] U+%04X（剩余 %zu 字节）\n", index++, cp, walk.count);
    }
    printf("  遍历结束后剩余 %zu 字节\n", walk.count);
}

static void demo_validate(void)
{
    title("cb_utf8_validate：合法性校验");

    struct { const char* label; const char* data; size_t size; bool expect_ok; } cases[] = {
        {"合法 ASCII", "hello", 5, true},
        {"合法中文", "中文", 6, true},
        {"非法字节 0xFF", "a\xFF""b", 3, false},
        {"截断的多字节序列", "\xE4\xB8", 2, false},
        {"过长编码 overlong", "\xC0\x80", 2, false},
        {"代理区编码", "\xED\xA0\x80", 3, false},
    };

    for (size_t i = 0; i < CB_ARRAY_LEN(cases); ++i) {
        size_t bad = 0;
        bool ok = cb_utf8_validate(cb_sv_from_parts(cases[i].data, cases[i].size), &bad);
        printf("  %-20s -> %s", cases[i].label, ok ? "合法" : "非法");
        if (!ok) printf("（首个非法字节下标 = %zu）", bad);
        printf("  %s\n", ok == cases[i].expect_ok ? "" : "  << 与预期不符");
    }
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    demo_length();
    demo_utf8_len();
    demo_decode_encode();
    demo_iterate();
    demo_validate();

    printf("\n全部 utf8 示例执行完毕\n");
    return 0;
}
