#ifndef QPY_COMPAT_COMMON_H
#define QPY_COMPAT_COMMON_H

#include <stddef.h>

#include "py/obj.h"
#include "qosa_def.h"

#define QPY_STR_BUF_SIZE (128)

typedef int qosa_dev_error_e;

mp_obj_t qpy_int_minus_one(void);
mp_obj_t qpy_tuple2_int(int a, int b);
mp_obj_t qpy_tuple3_int(int a, int b, int c);
mp_obj_t qpy_dev_string(qosa_dev_error_e (*getter)(char *, qosa_int32_t *));
qosa_uint8_t qpy_get_simid(size_t n_args, const mp_obj_t *args, size_t index);
mp_obj_t qpy_stub_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args);

extern const mp_obj_fun_builtin_var_t qpy_stub_ret_minus_one_obj;
extern const mp_obj_fun_builtin_var_t qpy_stub_ret_none_obj;
extern const mp_obj_fun_builtin_var_t qpy_stub_ret_zero_obj;
extern const mp_obj_fun_builtin_var_t qpy_stub_ret_empty_tuple_obj;
extern const mp_obj_fun_builtin_var_t qpy_stub_ret_empty_list_obj;
extern const mp_obj_fun_builtin_var_t qpy_stub_ret_empty_string_obj;
extern const mp_obj_dict_t qpy_stub_type_locals;

#endif
