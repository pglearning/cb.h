// File system and paths: recursive walking, globbing, metadata, atomic writes, symlinks and mmap.
#define CB_IMPLEMENTATION
#define CB_ENABLE_ECHO
#include "cb.h"

#define OUT_FOLDER "./build/examples/07_filesystem/"

bool print_entry(CB_Walk_Entry entry)
{
    printf("walk: level %zu %s%s\n", entry.level, entry.path,
           entry.type == CB_FILE_DIRECTORY ? "/" : "");
    if (entry.type == CB_FILE_DIRECTORY && cb_sv_ends_with_cstr(cb_sv_from_cstr(entry.path), "skip")) {
        *entry.action = CB_WALK_SKIP; // do not descend into directories named "skip"
    }
    return true;
}

int main(void)
{
    cb_minimal_log_level = CB_INFO;
    if (!cb_mkdir_if_not_exists(OUT_FOLDER "tree/sub")) return 1;
    if (!cb_mkdir_if_not_exists(OUT_FOLDER "tree/skip")) return 1;
    if (!cb_write_entire_file(OUT_FOLDER "tree/a.txt", "aaa", 3)) return 1;
    if (!cb_write_entire_file(OUT_FOLDER "tree/sub/b.txt", "bbbb", 4)) return 1;
    if (!cb_write_entire_file(OUT_FOLDER "tree/skip/c.txt", "ccccc", 5)) return 1;

    cb_walk_dir(OUT_FOLDER "tree", print_entry, .post_order = false);

    // ---- path helpers work on strings only, nothing is touched on disk -----------------------
    printf("join: %s\n", cb_path_join("a/b", "../c"));
    printf("normalize: %s\n", cb_path_normalize("a//b/./c/../d"));
    printf("absolute: %s\n", cb_path_absolute("relative/file.txt"));
    printf("replace_ext: %s\n", cb_path_replace_ext("dir/file.tar.gz", ".zip"));
    printf("is_absolute(/x) = %d, is_absolute(x) = %d, sep = '%c'\n",
           cb_path_is_absolute("/x"), cb_path_is_absolute("x"), CB_PATH_SEP);

    // ---- directory listing and globbing ------------------------------------------------------
    CB_File_Paths children = CB_ZERO;
    if (!cb_read_entire_dir(OUT_FOLDER "tree", &children)) return 1;
    printf("read_entire_dir: %zu entries\n", children.count);
    free(children.items);

    CB_File_Paths matches = CB_ZERO;
    if (!cb_glob(OUT_FOLDER "tree", "*.txt", &matches)) return 1;
    printf("glob *.txt: %zu match(es), first = %s\n", matches.count, matches.count ? matches.items[0] : "-");
    free(matches.items);

    // ---- metadata, atomic write, symlink, mmap ------------------------------------------------
    printf("file_size = %zu, mtime > 0 = %d\n",
           cb_file_size(OUT_FOLDER "tree/a.txt"), (int)(cb_file_mtime(OUT_FOLDER "tree/a.txt") > 0));

    const char* payload = "written atomically";
    if (!cb_write_entire_file_atomic(OUT_FOLDER "atomic.txt", payload, strlen(payload))) return 1;
    printf("atomic write readback = %s\n", payload);

    if (cb_create_symlink("tree/a.txt", OUT_FOLDER "link.txt")) {
        char* target = cb_read_symlink(OUT_FOLDER "link.txt");
        printf("symlink target = %s\n", target != NULL ? target : "(unreadable)");
    }

    CB_Mmap map = CB_ZERO;
    if (cb_mmap_open(OUT_FOLDER "tree/sub/b.txt", &map)) {
        printf("mmap: %zu bytes = %.*s\n", map.size, (int)map.size, (const char*)map.data);
        cb_mmap_close(&map);
    }
    return 0;
}
