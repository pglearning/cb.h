#include "cb.h"

int main(void)
{

    // create test files
    const char* path_dir = "dir";
    const char* path_dir2 = "dir/dir2";
    if (!cb_mkdir_if_not_exists(path_dir)) {
        cb_log(CB_ERROR, "file create failed");
        return -1;
    }
    if (!cb_mkdir_if_not_exists(path_dir2)) {
        cb_log(CB_ERROR, "file create failed");
        return -1;
    }
    const char* path_file1 = "dir/file1";
    const char* path_file2 = "dir/dir2/file2";

    FILE* file1 = fopen(path_file1, "wb");
    FILE* file2 = fopen(path_file2, "wb");
    if (file1 == NULL || file2 == NULL) {
        cb_log(CB_ERROR, "file create failed");
        return -1;
    }
    fclose(file1);
    fclose(file2);

    CB_File_Paths output_paths = {0};
    cb_read_entire_dir(path_dir, &output_paths);
    if (output_paths.count <= 0) {
        cb_log(CB_ERROR, "No path has begin read");
        return -1;
    }

    for (size_t i = 0; i < output_paths.count; ++i) {
        printf("output_paths[%zu] = %s\n", i, output_paths.items[i]);
    }

    // clear test files
    cb_delete_file(path_file1);
    cb_delete_file(path_file2);
    cb_delete_file(path_dir2);
    cb_delete_file(path_dir);

    return 0;
}
