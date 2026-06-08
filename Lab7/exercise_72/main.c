#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_PATH      4096
#define MAX_COMPONENT 256

const char* curr_working_dir = "/path/to/current/directory";

static int append_char(char* dst, int* len, char c) {
    if (*len >= MAX_PATH - 1) {
        return -1;
    }

    dst[*len] = c;
    (*len)++;
    dst[*len] = '\0';
    return 0;
}

static int append_str(char* dst, int* len, const char* src) {
    while (*src != '\0') {
        if (append_char(dst, len, *src) < 0) {
            return -1;
        }
        src++;
    }

    return 0;
}

static void remove_last_component(char* resolved_path, int* len) {
    // 如果已經是 "/"，就不能再往上
    if (*len <= 1) {
        resolved_path[0] = '/';
        resolved_path[1] = '\0';
        *len = 1;
        return;
    }

    // 如果最後是 '/'，先跳過
    if (resolved_path[*len - 1] == '/') {
        (*len)--;
    }

    // 往前刪到上一個 '/'
    while (*len > 1 && resolved_path[*len - 1] != '/') {
        (*len)--;
    }

    resolved_path[*len] = '\0';
}

static int append_component(char* resolved_path, int* len,
                            const char* comp, int comp_len) {
    if (comp_len == 0) {
        return 0;
    }

    // "." 直接忽略
    if (comp_len == 1 && comp[0] == '.') {
        return 0;
    }

    // ".." 回上一層
    if (comp_len == 2 && comp[0] == '.' && comp[1] == '.') {
        remove_last_component(resolved_path, len);
        return 0;
    }

    // 如果目前不是根目錄，而且最後不是 '/'，補一個 '/'
    if (*len > 1 && resolved_path[*len - 1] != '/') {
        if (append_char(resolved_path, len, '/') < 0) {
            return -1;
        }
    }

    // 加上 component
    for (int i = 0; i < comp_len; i++) {
        if (append_char(resolved_path, len, comp[i]) < 0) {
            return -1;
        }
    }

    return 0;
}
/**
 * Resolve a relative or absolute filepath
 */
char* my_realpath(const char* path, char* resolved_path) {
    // TODO: Implement this function
    int len = 0;

    if (path == 0 || resolved_path == 0) {
        return 0;
    }

    resolved_path[0] = '\0';

    /*
     * Absolute path:
     *   "/a/b" 從 root 開始
     *
     * Relative path:
     *   "a/b" 先從 curr_working_dir 開始
     */
    if (path[0] == '/') {
        resolved_path[0] = '/';
        resolved_path[1] = '\0';
        len = 1;
    } else {
        if (append_str(resolved_path, &len, curr_working_dir) < 0) {
            return 0;
        }

        if (len == 0) {
            resolved_path[0] = '/';
            resolved_path[1] = '\0';
            len = 1;
        }
    }

    int i = 0;

    while (path[i] != '\0') {
        // 跳過連續的 '/'
        while (path[i] == '/') {
            i++;
        }

        int start = i;

        // 找到這個 component 的結尾
        while (path[i] != '\0' && path[i] != '/') {
            i++;
        }

        int comp_len = i - start;

        if (append_component(resolved_path, &len, &path[start], comp_len) < 0) {
            return 0;
        }
    }

    // 如果最後多一個 '/'，且不是根目錄，就拿掉
    if (len > 1 && resolved_path[len - 1] == '/') {
        resolved_path[len - 1] = '\0';
        len--;
    }

    return resolved_path;
}

int main() {
    char resolved[MAX_PATH];
    const char* test_paths[] = {
        ".",
        "..",
        "./test",
        "../parent",
        "dir1/dir2/../../dir3",
        "/absolute/path",
        "relative/./path",
        NULL,
    };
    for (int i = 0; test_paths[i] != NULL; i++) {
        printf("[%d] \"%s\"", i, test_paths[i]);
        if (my_realpath(test_paths[i], resolved))
            printf(" --> \"%s\"\n", resolved);
    }
    return 0;
}
