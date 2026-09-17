#include <string.h>

#include "py/objstr.h"
#include "py/runtime.h"
#include "qpy_compat_common.h"

mp_obj_t qpy_int_minus_one(void) {
    return MP_OBJ_NEW_SMALL_INT(-1);
}

mp_obj_t qpy_tuple2_int(int a, int b) {
    mp_obj_t items[2] = { MP_OBJ_NEW_SMALL_INT(a), MP_OBJ_NEW_SMALL_INT(b) };
    return mp_obj_new_tuple(2, items);
}

mp_obj_t qpy_tuple3_int(int a, int b, int c) {
    mp_obj_t items[3] = { MP_OBJ_NEW_SMALL_INT(a), MP_OBJ_NEW_SMALL_INT(b), MP_OBJ_NEW_SMALL_INT(c) };
    return mp_obj_new_tuple(3, items);
}

mp_obj_t qpy_dev_string(qosa_dev_error_e (*getter)(char *, qosa_int32_t *)) {
    char buf[QPY_STR_BUF_SIZE] = {0};
    qosa_int32_t len = sizeof(buf) - 1;
    if (getter(buf, &len) != 0 || len <= 0) {
        return qpy_int_minus_one();
    }
    if (len > (qosa_int32_t)sizeof(buf) - 1) {
        len = sizeof(buf) - 1;
    }
    buf[len] = '\0';
    return mp_obj_new_str(buf, strlen(buf));
}

qosa_uint8_t qpy_get_simid(size_t n_args, const mp_obj_t *args, size_t index) {
    qosa_uint8_t simid = 0;
    if (n_args > index) {
        mp_int_t value = mp_obj_get_int(args[index]);
        if (value != 0 && value != 1) {
            mp_raise_ValueError(MP_ERROR_TEXT("invalid value, simId should be in [0~1]."));
        }
        simid = (qosa_uint8_t)value;
    }
    return simid;
}

static mp_obj_t qpy_stub_ret_minus_one(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    (void)args;
    return qpy_int_minus_one();
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_stub_ret_minus_one_obj, 0, MP_OBJ_FUN_ARGS_MAX, qpy_stub_ret_minus_one);

static mp_obj_t qpy_stub_ret_none(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    (void)args;
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_stub_ret_none_obj, 0, MP_OBJ_FUN_ARGS_MAX, qpy_stub_ret_none);

static mp_obj_t qpy_stub_ret_zero(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    (void)args;
    return MP_OBJ_NEW_SMALL_INT(0);
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_stub_ret_zero_obj, 0, MP_OBJ_FUN_ARGS_MAX, qpy_stub_ret_zero);

static mp_obj_t qpy_stub_ret_empty_tuple(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    (void)args;
    return mp_obj_new_tuple(0, NULL);
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_stub_ret_empty_tuple_obj, 0, MP_OBJ_FUN_ARGS_MAX, qpy_stub_ret_empty_tuple);

static mp_obj_t qpy_stub_ret_empty_list(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    (void)args;
    return mp_obj_new_list(0, NULL);
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_stub_ret_empty_list_obj, 0, MP_OBJ_FUN_ARGS_MAX, qpy_stub_ret_empty_list);

static mp_obj_t qpy_stub_ret_empty_string(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    (void)args;
    return mp_obj_new_str("", 0);
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_stub_ret_empty_string_obj, 0, MP_OBJ_FUN_ARGS_MAX, qpy_stub_ret_empty_string);

mp_obj_t qpy_stub_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    (void)n_args;
    (void)n_kw;
    (void)args;
    mp_obj_base_t *self = mp_obj_malloc(mp_obj_base_t, type);
    return MP_OBJ_FROM_PTR(self);
}

static const mp_rom_map_elem_t qpy_stub_type_locals_table[] = {
    { MP_ROM_QSTR(MP_QSTR_open), MP_ROM_PTR(&qpy_stub_ret_minus_one_obj) },
    { MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&qpy_stub_ret_minus_one_obj) },
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&qpy_stub_ret_minus_one_obj) },
    { MP_ROM_QSTR(MP_QSTR_deinit), MP_ROM_PTR(&qpy_stub_ret_minus_one_obj) },
    { MP_ROM_QSTR(MP_QSTR_read), MP_ROM_PTR(&qpy_stub_ret_minus_one_obj) },
    { MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&qpy_stub_ret_minus_one_obj) },
    { MP_ROM_QSTR(MP_QSTR_start), MP_ROM_PTR(&qpy_stub_ret_minus_one_obj) },
    { MP_ROM_QSTR(MP_QSTR_stop), MP_ROM_PTR(&qpy_stub_ret_minus_one_obj) },
    { MP_ROM_QSTR(MP_QSTR_enable), MP_ROM_PTR(&qpy_stub_ret_minus_one_obj) },
    { MP_ROM_QSTR(MP_QSTR_disable), MP_ROM_PTR(&qpy_stub_ret_minus_one_obj) },
    { MP_ROM_QSTR(MP_QSTR_get), MP_ROM_PTR(&qpy_stub_ret_minus_one_obj) },
    { MP_ROM_QSTR(MP_QSTR_set), MP_ROM_PTR(&qpy_stub_ret_minus_one_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_callback), MP_ROM_PTR(&qpy_stub_ret_minus_one_obj) },
};
MP_DEFINE_CONST_DICT(qpy_stub_type_locals, qpy_stub_type_locals_table);
