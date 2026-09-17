#include <string.h>

#include "py/obj.h"
#include "py/objstr.h"
#include "py/runtime.h"
#include "shared/runtime/pyexec.h"

static mp_obj_t qpy_example_exec(mp_obj_t path_in) {
    size_t len = 0;
    const char *path = mp_obj_str_get_data(path_in, &len);

    char fname[128] = {0};
    if (len == 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("file path is empty"));
    }
    if (path[0] != '/') {
        if (len + 1 >= sizeof(fname)) {
            mp_raise_ValueError(MP_ERROR_TEXT("file path is too long"));
        }
        fname[0] = '/';
        memcpy(fname + 1, path, len);
    } else {
        if (len >= sizeof(fname)) {
            mp_raise_ValueError(MP_ERROR_TEXT("file path is too long"));
        }
        memcpy(fname, path, len);
    }

    int ret = pyexec_file_if_exists(fname);
    // pyexec_file_if_exists returns 1 on normal exit OR when the file is missing,
    // 0 on unhandled exception. Surface failures as a runtime error.
    if (ret == 0) {
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("execute file failed"));
    }

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(qpy_example_exec_obj, qpy_example_exec);

static const mp_rom_map_elem_t qpy_example_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_example) },
    { MP_ROM_QSTR(MP_QSTR_exec), MP_ROM_PTR(&qpy_example_exec_obj) },
};
static MP_DEFINE_CONST_DICT(qpy_example_globals, qpy_example_globals_table);

const mp_obj_module_t mp_module_example = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&qpy_example_globals,
};

#if MICROPY_QPY_MODULE_EXAMPLE
MP_REGISTER_MODULE(MP_QSTR_example, mp_module_example);
#endif
