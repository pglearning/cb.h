// String_Builder 实测：追加 / 对齐 / 自引用 / 读文件往返
//
// 风格：纯 printf + golden 比对，只打实际观察到的值。
#include "test_diagnostics.h"
#include "cb.h"

int main(void)
{
    CB_String_Builder sb1 = {0};
    // 自己造文件来读：不依赖工作目录布局，同时验证"写进去 -> 读回来"的内容往返。
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

    // 追加一个 String_View
    cb_sb_append_sv(&sb, CB_SVLIT("+sv"));

    cb_sb_append_cstr(&sb, "sb.items");

    cb_sb_append(&sb, ' ');
    

    // 必须在 free 之前打印：cb_sb_free 之后 items 已经失效
    printf("|" CB_SV_FMT "|\n", CB_SV_ARG(cb_sb_to_sv(sb)));

    // ---------------------------------------------------------------- 读文件
    // 读回来的 items 必须能当合法 C 字符串用（结尾有 '\0'）
    {
        const char* content = "0123456789"; // 10 字节，正好把容量用满
        cb_write_entire_file("sb_read_back.txt", content, strlen(content));

        CB_String_Builder rb = CB_ZERO;
        bool ok = cb_read_entire_file("sb_read_back.txt", &rb);
        printf("read_entire_file 返回 = %d\n", (int)ok);
        printf("count = %zu, strlen = %zu\n", rb.count, rb.items ? strlen(rb.items) : 0);
        printf("结尾有 '\\0' = %d\n", (int)(rb.items && rb.items[rb.count] == '\0'));
        printf("strstr 能找到 \"567\" = %d\n", (int)(strstr(rb.items, "567") != NULL));
        // 空文件也要能安全地当 C 字符串用（items 至少有 1 字节）
        cb_sb_free(rb);

        CB_String_Builder eb = CB_ZERO;
        cb_write_entire_file("sb_empty.txt", "", 0);
        bool eok = cb_read_entire_file("sb_empty.txt", &eb);
        printf("空文件: 返回 = %d, count = %zu, 可当空串 = %d\n", (int)eok, eb.count,
               (int)(eb.items && eb.items[0] == '\0'));
        cb_sb_free(eb);
        cb_delete_file("sb_read_back.txt");
        cb_delete_file("sb_empty.txt");
    }

    cb_sb_free(sb);

    printf("after free: items is null = %d\n", (int)(sb.items == NULL));

    return 0;
}
