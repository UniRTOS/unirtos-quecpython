#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include "py/runtime.h"
#include "py/objtuple.h"
#include "py/objstr.h"
#include "py/mperrno.h"
#include "py/mphal.h"
#include "py/builtin.h"
#include "genhdr/mpversion.h"
#include "qosa_virtual_file.h"
#include "qpy_compat_common.h"
#include "qpy_path.h"

#define QPY_PATH_MAX (256)
#define QUECPYTHON_VERSION_STRING "v1.28.0"
#define QPY_UOS_LEGACY_QPY_VERSION_STRING "V0001"
#define QPY_USR_NATIVE_ROOT "/usr"

#define QPY_UOS_UNAME_BUF_SIZE (128)
#define QPY_UOS_UNAME_MODULE_NAME_MAX (32)

extern qosa_dev_error_e qosa_dev_get_model(char *model, qosa_int32_t *len);
extern qosa_dev_error_e qosa_dev_get_firmware_version(char *firmware_version, qosa_int32_t *len);

static int qpy_os_errno(void) {
    return MP_EIO;
}

static const char *qpy_resolve_path(const char *path, char *resolved) {
    const char *native = qpy_path_to_native(path, resolved, QPY_PATH_MAX);
    if (native == QOSA_NULL) {
        mp_raise_OSError_with_filename(MP_EINVAL, path == QOSA_NULL ? "" : path);
    }
    return native;
}

static void qpy_join_path(const char *dir, const char *name, char *joined, size_t joined_len) {
    size_t dir_len = strlen(dir);
    if (dir_len == 1 && dir[0] == '/') {
        snprintf(joined, joined_len, "/%s", name);
    } else {
        snprintf(joined, joined_len, "%s/%s", dir, name);
    }
}

static int qpy_is_usr_native_root(const char *path) {
    size_t len = strlen(path);
    while (len > 1 && path[len - 1] == '/') {
        len--;
    }
    return len == strlen(QPY_USR_NATIVE_ROOT) && strncmp(path, QPY_USR_NATIVE_ROOT, len) == 0;
}

static mp_obj_t qpy_uos_usr_statvfs(void) {
    struct qosa_vfs_statvfs_t stat = {0};
    if (qosa_vfs_statvfs(QPY_USR_NATIVE_ROOT, &stat) != 0) {
        mp_raise_OSError_with_filename(qpy_os_errno(), QPY_USR_NATIVE_ROOT);
    }
    mp_obj_t items[10] = {
        mp_obj_new_int_from_uint(stat.f_bsize),
        mp_obj_new_int_from_uint(stat.f_frsize),
        mp_obj_new_int_from_uint(stat.f_blocks),
        mp_obj_new_int_from_uint(stat.f_bfree),
        mp_obj_new_int_from_uint(stat.f_bavail),
        mp_obj_new_int_from_uint(stat.f_files),
        mp_obj_new_int_from_uint(stat.f_ffree),
        mp_obj_new_int_from_uint(stat.f_favail),
        mp_obj_new_int_from_uint(stat.f_flag),
        mp_obj_new_int_from_uint(stat.f_namemax),
    };
    return mp_obj_new_tuple(10, items);
}

static mp_obj_t qpy_uos_listdir(size_t n_args, const mp_obj_t *args) {
    const char *arg_path = n_args == 0 ? "." : mp_obj_str_get_str(args[0]);
    if (qpy_path_is_virtual_root(arg_path)) {
        mp_obj_t list = mp_obj_new_list(0, NULL);
        mp_obj_list_append(list, mp_obj_new_str("usr", 3));
        return list;
    }

    char resolved[QPY_PATH_MAX];
    const char *path = qpy_resolve_path(arg_path, resolved);
    QOSA_VFS_DIR *dir = qosa_vfs_opendir(path);
    if (dir == QOSA_NULL) {
        struct qosa_vfs_stat_t st = {0};
        if (qosa_vfs_stat(path, &st) == 0 && !(st.st_mode & QOSA_VFS_S_IFDIR)) {
            return mp_obj_new_list(0, NULL);
        }
        mp_raise_OSError_with_filename(qpy_os_errno(), path);
    }

    mp_obj_t list = mp_obj_new_list(0, NULL);
    for (;;) {
        struct qosa_vfs_dirent_t *ent = qosa_vfs_readdir(dir);
        if (ent == QOSA_NULL) {
            break;
        }
        if (ent->d_name[0] == '\0' || (ent->d_name[0] == '.' && ent->d_name[1] == '\0')) {
            continue;
        }
        if (ent->d_name[0] == '.' && ent->d_name[1] == '.' && ent->d_name[2] == '\0') {
            continue;
        }
        mp_obj_list_append(list, mp_obj_new_str(ent->d_name, strlen(ent->d_name)));
    }
    qosa_vfs_closedir(dir);
    return list;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_uos_listdir_obj, 0, 1, qpy_uos_listdir);

static mp_obj_t qpy_uos_ilistdir(size_t n_args, const mp_obj_t *args) {
    const char *arg_path = n_args == 0 ? "." : mp_obj_str_get_str(args[0]);
    if (qpy_path_is_virtual_root(arg_path)) {
        mp_obj_t list = mp_obj_new_list(0, NULL);
        struct qosa_vfs_stat_t st = {0};
        qosa_vfs_stat("/usr", &st);
        mp_obj_t item[4] = {
            mp_obj_new_str("usr", 3),
            mp_obj_new_int(QOSA_VFS_S_IFDIR),
            MP_OBJ_NEW_SMALL_INT(0),
            MP_OBJ_NEW_SMALL_INT(0),
        };
        mp_obj_list_append(list, mp_obj_new_tuple(4, item));
        return mp_getiter(list, NULL);
    }

    char resolved[QPY_PATH_MAX];
    const char *path = qpy_resolve_path(arg_path, resolved);
    QOSA_VFS_DIR *dir = qosa_vfs_opendir(path);
    if (dir == QOSA_NULL) {
        mp_raise_OSError_with_filename(qpy_os_errno(), path);
    }

    mp_obj_t list = mp_obj_new_list(0, NULL);
    for (;;) {
        struct qosa_vfs_dirent_t *ent = qosa_vfs_readdir(dir);
        if (ent == QOSA_NULL) {
            break;
        }
        if (ent->d_name[0] == '\0' || (ent->d_name[0] == '.' && ent->d_name[1] == '\0')) {
            continue;
        }
        if (ent->d_name[0] == '.' && ent->d_name[1] == '.' && ent->d_name[2] == '\0') {
            continue;
        }

        char child[QPY_PATH_MAX];
        struct qosa_vfs_stat_t st = {0};
        qpy_join_path(path, ent->d_name, child, sizeof(child));
        qosa_vfs_stat(child, &st);

        mp_obj_t item[4] = {
            mp_obj_new_str(ent->d_name, strlen(ent->d_name)),
            mp_obj_new_int(ent->d_type == QOSA_VFS_DT_DIR ? QOSA_VFS_S_IFDIR : QOSA_VFS_S_IFREG),
            mp_obj_new_int(ent->d_ino),
            mp_obj_new_int_from_ull(ent->d_type == QOSA_VFS_DT_DIR ? 0 : st.st_size),
        };
        mp_obj_list_append(list, mp_obj_new_tuple(4, item));
    }
    qosa_vfs_closedir(dir);
    return mp_getiter(list, NULL);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_uos_ilistdir_obj, 0, 1, qpy_uos_ilistdir);

static mp_obj_t qpy_uos_remove(mp_obj_t path_in) {
    const char *arg_path = mp_obj_str_get_str(path_in);
    if (qpy_path_is_virtual_root(arg_path) || strcmp(arg_path, "/usr") == 0) {
        mp_raise_OSError_with_filename(MP_EPERM, arg_path);
    }
    char resolved[QPY_PATH_MAX];
    const char *path = qpy_resolve_path(arg_path, resolved);
    if (qpy_is_usr_native_root(path)) {
        mp_raise_OSError_with_filename(MP_EPERM, arg_path);
    }
    struct qosa_vfs_stat_t st = {0};
    int rc = -1;
    if (qosa_vfs_stat(path, &st) == 0 && (st.st_mode & QOSA_VFS_S_IFDIR)) {
        rc = qosa_vfs_rmdir_recursive(path);
    } else {
        rc = qosa_vfs_unlink(path);
    }
    if (rc != 0) {
        mp_raise_OSError_with_filename(qpy_os_errno(), path);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(qpy_uos_remove_obj, qpy_uos_remove);

static mp_obj_t qpy_uos_mkdir(mp_obj_t path_in) {
    char resolved[QPY_PATH_MAX];
    const char *path = qpy_resolve_path(mp_obj_str_get_str(path_in), resolved);
    if (qosa_vfs_mkdir(path, QOSA_VFS_S_IRWXU) != 0) {
        mp_raise_OSError_with_filename(qpy_os_errno(), path);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(qpy_uos_mkdir_obj, qpy_uos_mkdir);

static mp_obj_t qpy_uos_rmdir(mp_obj_t path_in) {
    const char *arg_path = mp_obj_str_get_str(path_in);
    if (qpy_path_is_virtual_root(arg_path) || strcmp(arg_path, "/usr") == 0) {
        mp_raise_OSError_with_filename(MP_EPERM, arg_path);
    }
    char resolved[QPY_PATH_MAX];
    const char *path = qpy_resolve_path(arg_path, resolved);
    if (qpy_is_usr_native_root(path)) {
        mp_raise_OSError_with_filename(MP_EPERM, arg_path);
    }
    if (qosa_vfs_rmdir_recursive(path) != 0) {
        mp_raise_OSError_with_filename(qpy_os_errno(), path);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(qpy_uos_rmdir_obj, qpy_uos_rmdir);

static mp_obj_t qpy_uos_rename(mp_obj_t old_in, mp_obj_t new_in) {
    char old_resolved[QPY_PATH_MAX];
    char new_resolved[QPY_PATH_MAX];
    const char *old_path = qpy_resolve_path(mp_obj_str_get_str(old_in), old_resolved);
    const char *new_path = qpy_resolve_path(mp_obj_str_get_str(new_in), new_resolved);
    if (qosa_vfs_rename(old_path, new_path) != 0) {
        mp_raise_OSError(qpy_os_errno());
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(qpy_uos_rename_obj, qpy_uos_rename);

static mp_obj_t qpy_uos_stat(mp_obj_t path_in) {
    char resolved[QPY_PATH_MAX];
    const char *path = qpy_resolve_path(mp_obj_str_get_str(path_in), resolved);
    struct qosa_vfs_stat_t st = {0};
    if (qosa_vfs_stat(path, &st) != 0) {
        mp_raise_OSError_with_filename(qpy_os_errno(), path);
    }
    mp_obj_t items[10] = {
        mp_obj_new_int_from_ull(st.st_mode),
        mp_obj_new_int_from_ull(st.st_ino),
        mp_obj_new_int_from_ull(st.st_dev),
        mp_obj_new_int_from_ull(st.st_nlink),
        mp_obj_new_int_from_ull(st.st_uid),
        mp_obj_new_int_from_ull(st.st_gid),
        mp_obj_new_int_from_ull(st.st_size),
        MP_OBJ_NEW_SMALL_INT(0),
        MP_OBJ_NEW_SMALL_INT(0),
        MP_OBJ_NEW_SMALL_INT(0),
    };
    return mp_obj_new_tuple(10, items);
}
static MP_DEFINE_CONST_FUN_OBJ_1(qpy_uos_stat_obj, qpy_uos_stat);

static mp_obj_t qpy_uos_statvfs(mp_obj_t path_in) {
    char resolved[QPY_PATH_MAX];
    const char *path = qpy_resolve_path(mp_obj_str_get_str(path_in), resolved);
    if (strncmp(path, QPY_USR_NATIVE_ROOT, strlen(QPY_USR_NATIVE_ROOT)) == 0) {
        return qpy_uos_usr_statvfs();
    }
    struct qosa_vfs_statvfs_t st = {0};
    if (qosa_vfs_statvfs(path, &st) != 0) {
        mp_raise_OSError_with_filename(qpy_os_errno(), path);
    }
    mp_obj_t items[10] = {
        mp_obj_new_int_from_uint(st.f_bsize),
        mp_obj_new_int_from_uint(st.f_frsize),
        mp_obj_new_int_from_uint(st.f_blocks),
        mp_obj_new_int_from_uint(st.f_bfree),
        mp_obj_new_int_from_uint(st.f_bavail),
        mp_obj_new_int_from_uint(st.f_files),
        mp_obj_new_int_from_uint(st.f_ffree),
        mp_obj_new_int_from_uint(st.f_favail),
        mp_obj_new_int_from_uint(st.f_flag),
        mp_obj_new_int_from_uint(st.f_namemax),
    };
    return mp_obj_new_tuple(10, items);
}
static MP_DEFINE_CONST_FUN_OBJ_1(qpy_uos_statvfs_obj, qpy_uos_statvfs);

static mp_obj_t qpy_uos_getcwd(void) {
    const char *cwd = qpy_path_getcwd();
    return mp_obj_new_str(cwd, strlen(cwd));
}
static MP_DEFINE_CONST_FUN_OBJ_0(qpy_uos_getcwd_obj, qpy_uos_getcwd);

static mp_obj_t qpy_uos_chdir(mp_obj_t path_in) {
    const char *path = mp_obj_str_get_str(path_in);
    if (qpy_path_chdir(path) != 0) {
        mp_raise_OSError_with_filename(MP_ENOENT, path);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(qpy_uos_chdir_obj, qpy_uos_chdir);

static char qpy_uos_uname2_sysname[QPY_UOS_UNAME_BUF_SIZE] = {0};
static char qpy_uos_uname2_nodename[QPY_UOS_UNAME_MODULE_NAME_MAX] = {0};
static char qpy_uos_uname2_machine[QPY_UOS_UNAME_BUF_SIZE] = {0};

static void qpy_uos_legacy_uname_fields(char *sysname, size_t sysname_len,
    char *nodename, size_t nodename_len, char *machine, size_t machine_len) {
    char model[QPY_UOS_UNAME_BUF_SIZE] = {0};
    qosa_int32_t model_len = sizeof(model) - 1;
    size_t nodename_len_actual = 0;

    // uname() also uses these buffers for its labelled tuple strings.  Clear
    // them before producing uname2's raw field values so a shorter model name
    // cannot retain a suffix from a preceding uname() call.
    if (sysname_len > 0) {
        sysname[0] = '\0';
    }
    if (nodename_len > 0) {
        nodename[0] = '\0';
    }
    if (machine_len > 0) {
        machine[0] = '\0';
    }

    if (qosa_dev_get_model(model, &model_len) == 0 && model_len > 0) {
        if (model_len >= (qosa_int32_t)sizeof(model)) {
            model_len = sizeof(model) - 1;
        }
        model[model_len] = '\0';
        while (nodename_len_actual < (size_t)model_len
            && nodename_len_actual < nodename_len - 1
            && model[nodename_len_actual] != '-'
            && model[nodename_len_actual] != '_') {
            nodename[nodename_len_actual] = model[nodename_len_actual];
            nodename_len_actual++;
        }
        nodename[nodename_len_actual] = '\0';
    }

    if (nodename_len_actual == 0) {
        strncpy(nodename, MICROPY_HW_BOARD_NAME, nodename_len - 1);
    }

    // EG800Z legacy firmware reports the product identifier without the
    // UniRTOS underscore, but with a dash before the region identifier.
    if (strcmp(MICROPY_HW_BOARD_NAME, "EG800ZCN_LA") == 0) {
        snprintf(sysname, sysname_len, "EG800Z-CNLA");
    } else {
        snprintf(sysname, sysname_len, "%s", MICROPY_HW_BOARD_NAME);
    }
    snprintf(machine, machine_len, "%s with QUECTEL", nodename);
}

static mp_obj_t qpy_uos_uname(void) {
    char sysname[QPY_UOS_UNAME_BUF_SIZE] = {0};
    char nodename[QPY_UOS_UNAME_BUF_SIZE] = {0};
    char machine[QPY_UOS_UNAME_BUF_SIZE] = {0};
    qpy_uos_legacy_uname_fields(sysname, sizeof(sysname), nodename, sizeof(nodename), machine, sizeof(machine));

    mp_obj_t items[6] = {
        mp_obj_new_str("sysname=", strlen("sysname=")),
        mp_obj_new_str("nodename=", strlen("nodename=")),
        mp_obj_new_str("release=" MICROPY_VERSION_STRING, strlen("release=" MICROPY_VERSION_STRING)),
        mp_obj_new_str("version=" MICROPY_GIT_TAG " on " MICROPY_BUILD_LEGACY_DATETIME, strlen("version=" MICROPY_GIT_TAG " on " MICROPY_BUILD_LEGACY_DATETIME)),
        mp_obj_new_str("machine=", strlen("machine=")),
        mp_obj_new_str("qpyver=" QPY_UOS_LEGACY_QPY_VERSION_STRING, strlen("qpyver=" QPY_UOS_LEGACY_QPY_VERSION_STRING)),
    };
    snprintf(qpy_uos_uname2_sysname, sizeof(qpy_uos_uname2_sysname), "sysname=%s", sysname);
    snprintf(qpy_uos_uname2_nodename, sizeof(qpy_uos_uname2_nodename), "nodename=%s", nodename);
    snprintf(qpy_uos_uname2_machine, sizeof(qpy_uos_uname2_machine), "machine=%s", machine);
    items[0] = mp_obj_new_str(qpy_uos_uname2_sysname, strlen(qpy_uos_uname2_sysname));
    items[1] = mp_obj_new_str(qpy_uos_uname2_nodename, strlen(qpy_uos_uname2_nodename));
    items[4] = mp_obj_new_str(qpy_uos_uname2_machine, strlen(qpy_uos_uname2_machine));
    return mp_obj_new_tuple(6, items);
}
static MP_DEFINE_CONST_FUN_OBJ_0(qpy_uos_uname_obj, qpy_uos_uname);

static const qstr qpy_uos_uname2_fields[] = {
    MP_QSTR_sysname,
    MP_QSTR_nodename,
    MP_QSTR_release,
    MP_QSTR_version,
    MP_QSTR_machine,
    MP_QSTR_qpyver,
};
static MP_DEFINE_STR_OBJ(qpy_uos_uname2_sysname_obj, qpy_uos_uname2_sysname);
static MP_DEFINE_STR_OBJ(qpy_uos_uname2_nodename_obj, qpy_uos_uname2_nodename);
static const MP_DEFINE_STR_OBJ(qpy_uos_uname2_release_obj, MICROPY_VERSION_STRING);
static const MP_DEFINE_STR_OBJ(qpy_uos_uname2_version_obj, MICROPY_GIT_TAG " on " MICROPY_BUILD_LEGACY_DATETIME);
static MP_DEFINE_STR_OBJ(qpy_uos_uname2_machine_obj, qpy_uos_uname2_machine);
static const MP_DEFINE_STR_OBJ(qpy_uos_uname2_qpyver_obj, QPY_UOS_LEGACY_QPY_VERSION_STRING);
static MP_DEFINE_ATTRTUPLE(
    qpy_uos_uname2_obj,
    qpy_uos_uname2_fields,
    6,
    MP_ROM_PTR(&qpy_uos_uname2_sysname_obj),
    MP_ROM_PTR(&qpy_uos_uname2_nodename_obj),
    MP_ROM_PTR(&qpy_uos_uname2_release_obj),
    MP_ROM_PTR(&qpy_uos_uname2_version_obj),
    MP_ROM_PTR(&qpy_uos_uname2_machine_obj),
    MP_ROM_PTR(&qpy_uos_uname2_qpyver_obj)
    );

static mp_obj_t qpy_uos_uname2(void) {
    qpy_uos_legacy_uname_fields(qpy_uos_uname2_sysname, sizeof(qpy_uos_uname2_sysname),
        qpy_uos_uname2_nodename, sizeof(qpy_uos_uname2_nodename),
        qpy_uos_uname2_machine, sizeof(qpy_uos_uname2_machine));
    qpy_uos_uname2_sysname_obj.len = strlen(qpy_uos_uname2_sysname);
    qpy_uos_uname2_nodename_obj.len = strlen(qpy_uos_uname2_nodename);
    qpy_uos_uname2_machine_obj.len = strlen(qpy_uos_uname2_machine);
    return MP_OBJ_FROM_PTR(&qpy_uos_uname2_obj);
}
static MP_DEFINE_CONST_FUN_OBJ_0(qpy_uos_uname2_fun_obj, qpy_uos_uname2);

static mp_obj_t qpy_uos_sdkver(void) {
    return qpy_dev_string(qosa_dev_get_firmware_version);
}
static MP_DEFINE_CONST_FUN_OBJ_0(qpy_uos_sdkver_obj, qpy_uos_sdkver);

static mp_obj_t qpy_uos_init(void) {
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(qpy_uos_init_obj, qpy_uos_init);

static mp_obj_t qpy_uos_urandom(mp_obj_t num_in) {
    mp_int_t len = mp_obj_get_int(num_in);
    if (len < 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("negative count"));
    }
    vstr_t vstr;
    vstr_init_len(&vstr, (size_t)len);
    uint32_t random = 0;
    for (mp_int_t i = 0; i < len; i++) {
        if ((i & 3) == 0) {
            random = mp_hal_get_random();
        }
        vstr.buf[i] = random & 0xff;
        random >>= 8;
    }
    mp_obj_t bytes = mp_obj_new_bytes((const byte *)vstr.buf, vstr.len);
    vstr_clear(&vstr);
    return bytes;
}
static MP_DEFINE_CONST_FUN_OBJ_1(qpy_uos_urandom_obj, qpy_uos_urandom);

static const mp_rom_map_elem_t qpy_uos_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_uos) },
    { MP_ROM_QSTR(MP_QSTR___init__), MP_ROM_PTR(&qpy_uos_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_uname), MP_ROM_PTR(&qpy_uos_uname_obj) },
    { MP_ROM_QSTR(MP_QSTR_uname2), MP_ROM_PTR(&qpy_uos_uname2_fun_obj) },
    { MP_ROM_QSTR(MP_QSTR_urandom), MP_ROM_PTR(&qpy_uos_urandom_obj) },
    { MP_ROM_QSTR(MP_QSTR_sdkver), MP_ROM_PTR(&qpy_uos_sdkver_obj) },
    { MP_ROM_QSTR(MP_QSTR_listdir), MP_ROM_PTR(&qpy_uos_listdir_obj) },
    { MP_ROM_QSTR(MP_QSTR_ilistdir), MP_ROM_PTR(&qpy_uos_ilistdir_obj) },
    { MP_ROM_QSTR(MP_QSTR_remove), MP_ROM_PTR(&qpy_uos_remove_obj) },
    { MP_ROM_QSTR(MP_QSTR_mkdir), MP_ROM_PTR(&qpy_uos_mkdir_obj) },
    { MP_ROM_QSTR(MP_QSTR_rmdir), MP_ROM_PTR(&qpy_uos_rmdir_obj) },
    { MP_ROM_QSTR(MP_QSTR_rename), MP_ROM_PTR(&qpy_uos_rename_obj) },
    { MP_ROM_QSTR(MP_QSTR_stat), MP_ROM_PTR(&qpy_uos_stat_obj) },
    { MP_ROM_QSTR(MP_QSTR_statvfs), MP_ROM_PTR(&qpy_uos_statvfs_obj) },
    { MP_ROM_QSTR(MP_QSTR_getcwd), MP_ROM_PTR(&qpy_uos_getcwd_obj) },
    { MP_ROM_QSTR(MP_QSTR_chdir), MP_ROM_PTR(&qpy_uos_chdir_obj) },
};
static MP_DEFINE_CONST_DICT(qpy_uos_globals, qpy_uos_globals_table);

const mp_obj_module_t mp_module_uos_qosa = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&qpy_uos_globals,
};

#if MICROPY_QPY_MODULE_UOS
MP_REGISTER_MODULE(MP_QSTR_os, mp_module_uos_qosa);
MP_REGISTER_MODULE(MP_QSTR_uos, mp_module_uos_qosa);
#endif

mp_import_stat_t mp_import_stat(const char *path) {
    char resolved[QPY_PATH_MAX];
    path = qpy_resolve_path(path, resolved);
    struct qosa_vfs_stat_t st = {0};
    if (qosa_vfs_stat(path, &st) != 0) {
        return MP_IMPORT_STAT_NO_EXIST;
    }
    if (st.st_mode & QOSA_VFS_S_IFDIR) {
        return MP_IMPORT_STAT_DIR;
    }
    return MP_IMPORT_STAT_FILE;
}

// Legacy QuecPython uzlib compatibility layer.
#include "moduzlib_compat.c"
