#include <stdio.h>
#include <string.h>

#include "py/obj.h"
#include "py/objstr.h"
#include "py/runtime.h"
#include "qpy_compat_common.h"

#define QOSA_DEV_SUCCESS (0)

extern qosa_dev_error_e qosa_dev_get_cust_firmware_version(char *version, qosa_int32_t *len);
extern qosa_dev_error_e qosa_dev_get_sim_imei(qosa_int32_t sim_id, char *imei, qosa_int32_t *len);
extern qosa_dev_error_e qosa_dev_get_sn(char *SN, qosa_int32_t *len);
extern qosa_dev_error_e qosa_dev_get_product_id(char *product_id, qosa_int32_t *len);

static mp_obj_t qpy_modem_get_dev_fw_version(void) {
    return qpy_dev_string(qosa_dev_get_cust_firmware_version);
}
static MP_DEFINE_CONST_FUN_OBJ_0(qpy_modem_get_dev_fw_version_obj, qpy_modem_get_dev_fw_version);

static mp_obj_t qpy_modem_get_dev_imei(size_t n_args, const mp_obj_t *args) {
    qosa_uint8_t simid = qpy_get_simid(n_args, args, 0);
    char buf[QPY_STR_BUF_SIZE] = {0};
    qosa_int32_t len = sizeof(buf) - 1;
    if (qosa_dev_get_sim_imei(simid, buf, &len) != QOSA_DEV_SUCCESS || len <= 0) {
        return qpy_int_minus_one();
    }
    if (len > (qosa_int32_t)sizeof(buf) - 1) {
        len = sizeof(buf) - 1;
    }
    buf[len] = '\0';
    return mp_obj_new_str(buf, strlen(buf));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_modem_get_dev_imei_obj, 0, 1, qpy_modem_get_dev_imei);

static mp_obj_t qpy_modem_get_dev_model(void) {
    return mp_obj_new_str("EG800Z", 6);
}
static MP_DEFINE_CONST_FUN_OBJ_0(qpy_modem_get_dev_model_obj, qpy_modem_get_dev_model);

static mp_obj_t qpy_modem_get_dev_sn(size_t n_args, const mp_obj_t *args) {
    if (n_args > 0) {
        (void)qpy_get_simid(n_args, args, 0);
    }
    return qpy_dev_string(qosa_dev_get_sn);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_modem_get_dev_sn_obj, 0, 1, qpy_modem_get_dev_sn);

static mp_obj_t qpy_modem_get_dev_product_id(void) {
    return qpy_dev_string(qosa_dev_get_product_id);
}
static MP_DEFINE_CONST_FUN_OBJ_0(qpy_modem_get_dev_product_id_obj, qpy_modem_get_dev_product_id);

static const mp_rom_map_elem_t qpy_modem_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_modem) },
    { MP_ROM_QSTR(MP_QSTR_getDevFwVersion), MP_ROM_PTR(&qpy_modem_get_dev_fw_version_obj) },
    { MP_ROM_QSTR(MP_QSTR_getDevImei), MP_ROM_PTR(&qpy_modem_get_dev_imei_obj) },
    { MP_ROM_QSTR(MP_QSTR_getDevModel), MP_ROM_PTR(&qpy_modem_get_dev_model_obj) },
    { MP_ROM_QSTR(MP_QSTR_getDevSN), MP_ROM_PTR(&qpy_modem_get_dev_sn_obj) },
    { MP_ROM_QSTR(MP_QSTR_getDevProductId), MP_ROM_PTR(&qpy_modem_get_dev_product_id_obj) },
};
static MP_DEFINE_CONST_DICT(qpy_modem_globals, qpy_modem_globals_table);

const mp_obj_module_t mp_module_modem = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&qpy_modem_globals,
};

#if MICROPY_QPY_MODULE_MODEM
MP_REGISTER_MODULE(MP_QSTR_modem, mp_module_modem);
#endif
