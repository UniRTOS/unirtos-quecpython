#include "py/obj.h"
#include "py/objstr.h"
#include "py/runtime.h"
#include "qpy_compat_common.h"

static mp_obj_t qpy_atcmd_send_sync(size_t n_args, const mp_obj_t *args) {
    mp_arg_check_num(n_args, 0, 1, 5, false);
    (void)mp_obj_str_get_str(args[0]);

    // First compatibility pass: keep old scripts from failing on argument shape.
    // The real A03 AT bridge will replace this -1 fallback once the sync API is confirmed.
    if (n_args > 1 && !mp_obj_is_integer(args[1])) {
        mp_buffer_info_t bufinfo;
        mp_get_buffer_raise(args[1], &bufinfo, MP_BUFFER_WRITE);
    }
    if (n_args > 2 && !mp_obj_is_integer(args[2])) {
        (void)mp_obj_str_get_str(args[2]);
    }
    if (n_args > 3 && mp_obj_get_int(args[3]) <= 0) {
        return qpy_int_minus_one();
    }
    if (n_args > 4) {
        (void)qpy_get_simid(n_args, args, 4);
    }
    return qpy_int_minus_one();
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_atcmd_send_sync_obj, 1, 5, qpy_atcmd_send_sync);

static const mp_rom_map_elem_t qpy_atcmd_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_atcmd) },
    { MP_ROM_QSTR(MP_QSTR_sendSync), MP_ROM_PTR(&qpy_atcmd_send_sync_obj) },
};
static MP_DEFINE_CONST_DICT(qpy_atcmd_globals, qpy_atcmd_globals_table);

const mp_obj_module_t mp_module_atcmd = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&qpy_atcmd_globals,
};

#if MICROPY_QPY_MODULE_ATCMD
MP_REGISTER_MODULE(MP_QSTR_atcmd, mp_module_atcmd);
#endif
