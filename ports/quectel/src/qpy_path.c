#include <stdio.h>
#include <string.h>

#include "qosa_virtual_file.h"
#include "qpy_path.h"

#define QPY_PATH_MAX (256)
#define QPY_USER_ROOT "/usr"

static char qpy_virtual_cwd[QPY_PATH_MAX] = "/";

static int qpy_path_is_usr_prefix(const char *path) {
    return strcmp(path, QPY_USER_ROOT) == 0 || strncmp(path, QPY_USER_ROOT "/", 5) == 0;
}

static const char *qpy_path_resolve_virtual(const char *path, char *resolved) {
    if (path == QOSA_NULL || path[0] == '\0') {
        path = ".";
    }
    if (path[0] == '/') {
        if (qosa_vfs_realpath(path, resolved) == QOSA_NULL) {
            return QOSA_NULL;
        }
    } else if (qosa_vfs_resolve_path(qpy_virtual_cwd, path, resolved) == QOSA_NULL) {
        return QOSA_NULL;
    }
    return resolved;
}

const char *qpy_path_to_native(const char *path, char *native, size_t native_len) {
    char virtual_path[QPY_PATH_MAX];
    if (native == QOSA_NULL || native_len == 0) {
        return QOSA_NULL;
    }
    if (qpy_path_resolve_virtual(path, virtual_path) == QOSA_NULL) {
        return QOSA_NULL;
    }

    if (qpy_path_is_usr_prefix(virtual_path)) {
        snprintf(native, native_len, "%s", virtual_path);
    } else if (strcmp(virtual_path, "/") == 0) {
        snprintf(native, native_len, "%s", QPY_USER_ROOT);
    } else {
        snprintf(native, native_len, "%s%s", QPY_USER_ROOT, virtual_path);
    }
    native[native_len - 1] = '\0';
    return native;
}

const char *qpy_path_getcwd(void) {
    return qpy_virtual_cwd;
}

int qpy_path_is_virtual_root(const char *path) {
    char virtual_path[QPY_PATH_MAX];
    if (qpy_path_resolve_virtual(path, virtual_path) == QOSA_NULL) {
        return 0;
    }
    return strcmp(virtual_path, "/") == 0;
}
int qpy_path_chdir(const char *path) {
    char virtual_path[QPY_PATH_MAX];
    char native_path[QPY_PATH_MAX];
    struct qosa_vfs_stat_t st = {0};

    if (qpy_path_resolve_virtual(path, virtual_path) == QOSA_NULL) {
        return -1;
    }
    if (qpy_path_to_native(virtual_path, native_path, sizeof(native_path)) == QOSA_NULL) {
        return -1;
    }
    if (qosa_vfs_stat(native_path, &st) != 0 || !(st.st_mode & QOSA_VFS_S_IFDIR)) {
        return -1;
    }

    strncpy(qpy_virtual_cwd, virtual_path, sizeof(qpy_virtual_cwd) - 1);
    qpy_virtual_cwd[sizeof(qpy_virtual_cwd) - 1] = '\0';
    return 0;
}

void qpy_path_init(void) {
    qpy_virtual_cwd[0] = '/';
    qpy_virtual_cwd[1] = '\0';
}