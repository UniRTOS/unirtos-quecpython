#ifndef QPY_PATH_H
#define QPY_PATH_H

#include <stddef.h>

void qpy_path_init(void);
const char *qpy_path_getcwd(void);
const char *qpy_path_to_native(const char *path, char *native, size_t native_len);
int qpy_path_is_virtual_root(const char *path);
int qpy_path_chdir(const char *path);

#endif