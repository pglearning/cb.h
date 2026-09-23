#include "test_diagnostics.h"
#include "cb.h"

int main(void)
{
    CB_String_Builder sb1 = {0};

    {
        const char* content = "round-trip content\n";
        size_t content_len = strlen(content);
        bool prepared = cb_write_entire_file("read_back.txt", content, content_len);
        bool ok = prepared && cb_read_entire_file("read_back.txt", &sb1);
        printf("read back: 写回往返一致 = %d, 读到 %zu 字节\n",
               (int)(ok && sb1.count == content_len &&
                     memcmp(sb1.items, content, content_len) == 0),
               sb1.count);
        cb_sb_free(sb1);
    }

    CB_String_Builder sb = {0};
    printf("Add characters count = %d\n", cb_sb_appendf(&sb, "%s", "addtion"));
    cb_sb_appendf(&sb, "%s", "addtion");

    cb_sb_pad_align(&sb, 1);

    cb_sb_append_buf(&sb, sb.items, sb.count);

    cb_sb_append_sv(&sb, CB_SVLIT("+sv"));

    cb_sb_append_cstr(&sb, "sb.items");

    cb_sb_append(&sb, ' ');

    printf("|" CB_SV_FMT "|\n", CB_SV_ARG(cb_sb_to_sv(sb)));

    {
        // 和 nob.h 一样：追加不会自动补 '\0'，要当 C 字符串用就先 cb_sb_append_null
        cb_sb_append_null(&sb);
        size_t before_count = sb.count;
        size_t before_strlen = strlen(sb.items);
        cb_sb_append_null(&sb);
        printf("cb_sb_append_null 后: count = %zu -> %zu, strlen 不变 = %d, 内嵌 '\\0' 出现 = %d\n",
               before_count, sb.count,
               (int)(strlen(sb.items) == before_strlen),
               (int)(memchr(sb.items, '\0', sb.count) != NULL));
    }

    {
        const char* content = "0123456789";
        cb_write_entire_file("sb_read_back.txt", content, strlen(content));

        CB_String_Builder rb = CB_ZERO;
        bool ok = cb_read_entire_file("sb_read_back.txt", &rb);
        printf("read_entire_file 返回 = %d, count = %zu（读回的字节数，不含终止符）\n", (int)ok, rb.count);
        cb_sb_append_null(&rb);
        printf("append_null 后 count = %zu, strlen = %zu, 结尾是 '\\0' = %d, strstr 能找到 \"567\" = %d\n",
               rb.count, strlen(rb.items), (int)(rb.items[rb.count - 1] == '\0'),
               (int)(strstr(rb.items, "567") != NULL));

        cb_sb_free(rb);

        CB_String_Builder eb = CB_ZERO;
        cb_write_entire_file("sb_empty.txt", "", 0);
        bool eok = cb_read_entire_file("sb_empty.txt", &eb);
        printf("空文件: 返回 = %d, count = %zu, items 仍为 NULL = %d\n", (int)eok, eb.count,
               (int)(eb.items == NULL));
        cb_sb_append_null(&eb);
        printf("空文件 append_null 后可当空串 = %d\n", (int)(eb.items && eb.items[0] == '\0'));
        cb_sb_free(eb);
        cb_delete_file("sb_read_back.txt");
        cb_delete_file("sb_empty.txt");
    }

    cb_sb_free(sb);

    printf("after free: items is null = %d\n", (int)(sb.items == NULL));

    return 0;
}
