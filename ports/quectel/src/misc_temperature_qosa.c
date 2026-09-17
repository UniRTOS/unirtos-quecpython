#include "py/obj.h"
#include "py/runtime.h"

typedef int qosa_dev_error_e;
typedef int qosa_int32_t;

#define QOSA_DEV_ERRID_SUCCESS 0

extern qosa_dev_error_e qosa_dev_get_temp_value(qosa_int32_t *temp_num);

typedef struct _qpy_misc_temperature_obj_t {
    mp_obj_base_t base;
} qpy_misc_temperature_obj_t;

static mp_obj_t qpy_temperature_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 0, 0, false);
    return mp_obj_malloc(qpy_misc_temperature_obj_t, type);
}

static mp_obj_t qpy_temperature_get(mp_obj_t self_in) {
    (void)self_in;
    qosa_int32_t value = 0;
    if (qosa_dev_get_temp_value(&value) != QOSA_DEV_ERRID_SUCCESS) {
        return MP_OBJ_NEW_SMALL_INT(-1);
    }
    return mp_obj_new_int(value);
}
static MP_DEFINE_CONST_FUN_OBJ_1(qpy_temperature_get_obj, qpy_temperature_get);

static const mp_rom_map_elem_t qpy_temperature_locals_table[] = {
    { MP_ROM_QSTR(MP_QSTR_temperature), MP_ROM_PTR(&qpy_temperature_get_obj) },
};
static MP_DEFINE_CONST_DICT(qpy_temperature_locals, qpy_temperature_locals_table);

MP_DEFINE_CONST_OBJ_TYPE(
    qpy_temp_type,
    MP_QSTR_Temperature,
    MP_TYPE_FLAG_NONE,
    make_new, qpy_temperature_make_new,
    locals_dict, &qpy_temperature_locals
    );
