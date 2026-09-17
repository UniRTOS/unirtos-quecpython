#include <string.h>

#include "py/obj.h"
#include "py/objstr.h"
#include "py/runtime.h"
#include "qosa_sim.h"
#include "qosa_sys.h"
#include "qpy_compat_common.h"

static mp_obj_t qpy_sim_callback = MP_OBJ_NULL;
static qosa_sem_t qpy_sim_op_sem = QOSA_NULL;
static qosa_sim_err_e qpy_sim_op_err = QOSA_SIM_ERR_FAILURE;
static qosa_sim_get_pin_remain_retries_cnf_t qpy_sim_pin_retries;
static qosa_sim_generic_access_cnf_t qpy_sim_generic_access_result;

#define QPY_SIM_OP_TIMEOUT_MS (5000)

static void qpy_sim_op_callback(void *ctx, void *argv) {
    (void)ctx;
    qosa_sim_general_cnf_t *cnf = (qosa_sim_general_cnf_t *)argv;
    qpy_sim_op_err = cnf == NULL ? QOSA_SIM_ERR_FAILURE : (qosa_sim_err_e)cnf->err_code;
    if (qpy_sim_op_sem != QOSA_NULL) {
        qosa_sem_release(qpy_sim_op_sem);
    }
}

static void qpy_sim_pin_retries_callback(void *ctx, void *argv) {
    (void)ctx;
    qosa_sim_get_pin_remain_retries_cnf_t *cnf = (qosa_sim_get_pin_remain_retries_cnf_t *)argv;
    qpy_sim_op_err = cnf == NULL ? QOSA_SIM_ERR_FAILURE : (qosa_sim_err_e)cnf->err_code;
    if (cnf != NULL) {
        qpy_sim_pin_retries = *cnf;
    }
    if (qpy_sim_op_sem != QOSA_NULL) {
        qosa_sem_release(qpy_sim_op_sem);
    }
}
static void qpy_sim_generic_access_callback(void *ctx, void *argv) {
    (void)ctx;
    qosa_sim_generic_access_cnf_t *cnf = (qosa_sim_generic_access_cnf_t *)argv;
    qpy_sim_op_err = cnf == NULL ? QOSA_SIM_ERR_FAILURE : (qosa_sim_err_e)cnf->err_code;
    if (cnf != NULL) qpy_sim_generic_access_result = *cnf;
    if (qpy_sim_op_sem != QOSA_NULL) qosa_sem_release(qpy_sim_op_sem);
}

static int qpy_sim_prepare_op(void) {
    if (qpy_sim_op_sem == QOSA_NULL && qosa_sem_create(&qpy_sim_op_sem, 0) != QOSA_OK) {
        return -1;
    }
    qpy_sim_op_err = QOSA_SIM_ERR_FAILURE;
    return 0;
}

static mp_obj_t qpy_sim_wait_op(qosa_sim_err_e started) {
    if (started != QOSA_SIM_ERR_OK || qosa_sem_wait(qpy_sim_op_sem, QPY_SIM_OP_TIMEOUT_MS) != QOSA_OK) {
        return qpy_int_minus_one();
    }
    return MP_OBJ_NEW_SMALL_INT(qpy_sim_op_err == QOSA_SIM_ERR_OK ? 0 : -1);
}

static void qpy_sim_pin_from_arg(mp_obj_t obj, qosa_sim_pin_t *pin, const char *message) {
    mp_buffer_info_t buf;
    mp_get_buffer_raise(obj, &buf, MP_BUFFER_READ);
    if (buf.len > QOSA_SIM_PIN_LEN_MAX) {
        mp_raise_ValueError(message);
    }
    memset(pin, 0, sizeof(*pin));
    pin->length = buf.len;
    memcpy(pin->pin_data, buf.buf, buf.len);
}

static int qpy_sim_status_to_qpy(qosa_sim_status_e status) {
    switch (status) {
        case QOSA_SIM_STATUS_READY:
            return 1;
        case QOSA_SIM_STATUS_NOT_INSERTED:
        case QOSA_SIM_STATUS_UNKNOWN:
            return 0;
        case QOSA_SIM_STATUS_SIM_PIN:
            return 2;
        case QOSA_SIM_STATUS_SIM_PUK:
            return 3;
        case QOSA_SIM_STATUS_BUSY:
            return 4;
        default:
            return (int)status;
    }
}

static mp_obj_t qpy_sim_get_status(size_t n_args, const mp_obj_t *args) {
    qosa_uint8_t simid = qpy_get_simid(n_args, args, 0);
    qosa_sim_status_e status = QOSA_SIM_STATUS_UNKNOWN;
    if (qosa_sim_read_status(simid, &status) != QOSA_SIM_ERR_OK) {
        qosa_sim_insert_stat_e insert = qosa_sim_read_insert_stat(simid);
        return MP_OBJ_NEW_SMALL_INT(insert == QOSA_SIM_INSERT_STAT_INSERTED ? 4 : 0);
    }
    return MP_OBJ_NEW_SMALL_INT(qpy_sim_status_to_qpy(status));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_sim_get_status_obj, 0, 1, qpy_sim_get_status);

static mp_obj_t qpy_sim_get_imsi(size_t n_args, const mp_obj_t *args) {
    qosa_uint8_t simid = qpy_get_simid(n_args, args, 0);
    qosa_sim_imsi_t imsi = {0};
    if (qosa_sim_read_imsi(simid, &imsi) != QOSA_SIM_ERR_OK || imsi.imsi[0] == '\0') {
        return qpy_int_minus_one();
    }
    return mp_obj_new_str(imsi.imsi, strlen(imsi.imsi));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_sim_get_imsi_obj, 0, 1, qpy_sim_get_imsi);

static mp_obj_t qpy_sim_get_iccid(size_t n_args, const mp_obj_t *args) {
    qosa_uint8_t simid = qpy_get_simid(n_args, args, 0);
    qosa_sim_iccid_t iccid = {0};
    if (qosa_sim_read_iccid(simid, &iccid) != QOSA_SIM_ERR_OK || iccid.id[0] == '\0') {
        return qpy_int_minus_one();
    }
    return mp_obj_new_str(iccid.id, strlen(iccid.id));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_sim_get_iccid_obj, 0, 1, qpy_sim_get_iccid);

static mp_obj_t qpy_sim_get_phone_number(size_t n_args, const mp_obj_t *args) {
    qosa_uint8_t simid = qpy_get_simid(n_args, args, 0);
    qosa_sim_phonenumber_t numbers = {0};
    if (qosa_sim_get_phonenumber(simid, &numbers) != QOSA_SIM_ERR_OK || numbers.num == 0 || numbers.list[0].phone_number[0] == '\0') {
        return qpy_int_minus_one();
    }
    return mp_obj_new_str(numbers.list[0].phone_number, strlen(numbers.list[0].phone_number));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_sim_get_phone_number_obj, 0, 1, qpy_sim_get_phone_number);

static mp_obj_t qpy_sim_get_phonebook_status(void) {
    return MP_OBJ_NEW_SMALL_INT((qosa_sim_read_init_stat(0) & QOSA_SIMINI_STAT_PB_DONE) ? 1 : 0);
}
static MP_DEFINE_CONST_FUN_OBJ_0(qpy_sim_get_phonebook_status_obj, qpy_sim_get_phonebook_status);

static mp_obj_t qpy_sim_generic_access(mp_obj_t sim_id, mp_obj_t cmd) {
    int simid = mp_obj_get_int(sim_id);
    mp_buffer_info_t buf;
    mp_get_buffer_raise(cmd, &buf, MP_BUFFER_READ);
    if (buf.len <= 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid value, the length of cmd should be more than 0 bytes."));
    }
    if (simid != 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid simid."));
    }
    if (qpy_sim_prepare_op() != 0 || qosa_sim_generic_access((qosa_uint8_t)simid, buf.buf, buf.len, qpy_sim_generic_access_callback, NULL) != QOSA_SIM_ERR_OK || qosa_sem_wait(qpy_sim_op_sem, QPY_SIM_OP_TIMEOUT_MS) != QOSA_OK || qpy_sim_op_err != QOSA_SIM_ERR_OK) {
        return qpy_int_minus_one();
    }
    size_t len = qpy_sim_generic_access_result.data_len;
    if (len > sizeof(qpy_sim_generic_access_result.data)) len = sizeof(qpy_sim_generic_access_result.data);
    mp_obj_t result[2] = {
        MP_OBJ_NEW_SMALL_INT(len),
        mp_obj_new_str((const char *)qpy_sim_generic_access_result.data, len),
    };
    return mp_obj_new_tuple(2, result);
}
static MP_DEFINE_CONST_FUN_OBJ_2(qpy_sim_generic_access_obj, qpy_sim_generic_access);

static mp_obj_t qpy_sim_verify_pin(size_t n_args, const mp_obj_t *args) {
    qosa_sim_pin_t pin;
    qpy_sim_pin_from_arg(args[0], &pin, "invalid value, the length of pin should be no more than [8] bytes.");
    qosa_uint8_t simid = qpy_get_simid(n_args, args, 1);
    if (qpy_sim_prepare_op() != 0) return qpy_int_minus_one();
    return qpy_sim_wait_op(qosa_sim_verify_pin(simid, &pin, qpy_sim_op_callback, NULL));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_sim_verify_pin_obj, 1, 2, qpy_sim_verify_pin);

static mp_obj_t qpy_sim_change_pin(size_t n_args, const mp_obj_t *args) {
    qosa_sim_pin_t old_pin, new_pin;
    qpy_sim_pin_from_arg(args[0], &old_pin, "invalid value, the length of pin should be no more than [8] bytes.");
    qpy_sim_pin_from_arg(args[1], &new_pin, "invalid value, the length of pin should be no more than [8] bytes.");
    qosa_uint8_t simid = qpy_get_simid(n_args, args, 2);
    if (qpy_sim_prepare_op() != 0) return qpy_int_minus_one();
    return qpy_sim_wait_op(qosa_sim_change_pin(simid, QOSA_SIM_FACILITY_SC, &old_pin, &new_pin, qpy_sim_op_callback, NULL));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_sim_change_pin_obj, 2, 3, qpy_sim_change_pin);

static mp_obj_t qpy_sim_unblock_pin(size_t n_args, const mp_obj_t *args) {
    qosa_sim_pin_t puk, new_pin;
    qpy_sim_pin_from_arg(args[0], &puk, "invalid value, the length of pin or puk should be no more than [8] bytes.");
    qpy_sim_pin_from_arg(args[1], &new_pin, "invalid value, the length of pin or puk should be no more than [8] bytes.");
    qosa_uint8_t simid = qpy_get_simid(n_args, args, 2);
    if (qpy_sim_prepare_op() != 0) return qpy_int_minus_one();
    return qpy_sim_wait_op(qosa_sim_unblock_pin(simid, &puk, &new_pin, qpy_sim_op_callback, NULL));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_sim_unblock_pin_obj, 2, 3, qpy_sim_unblock_pin);

static mp_obj_t qpy_sim_set_pin_enabled(size_t n_args, const mp_obj_t *args, int enabled) {
    qosa_sim_pin_t pin;
    qpy_sim_pin_from_arg(args[0], &pin, "invalid value, the length of pin should be no more than [8] bytes.");
    qosa_uint8_t simid = qpy_get_simid(n_args, args, 1);
    if (qpy_sim_prepare_op() != 0) return qpy_int_minus_one();
    return qpy_sim_wait_op(qosa_sim_set_facility_lock(simid, QOSA_SIM_FACILITY_SC, &pin, enabled, 7, qpy_sim_op_callback, NULL));
}
static mp_obj_t qpy_sim_enable_pin(size_t n_args, const mp_obj_t *args) { return qpy_sim_set_pin_enabled(n_args, args, 1); }
static mp_obj_t qpy_sim_disable_pin(size_t n_args, const mp_obj_t *args) { return qpy_sim_set_pin_enabled(n_args, args, 0); }
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_sim_enable_pin_obj, 1, 2, qpy_sim_enable_pin);
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_sim_disable_pin_obj, 1, 2, qpy_sim_disable_pin);

static mp_obj_t qpy_sim_get_pin_rem_attempts(size_t n_args, const mp_obj_t *args) {
    int flag = 0;
    if (n_args > 0) {
        flag = mp_obj_get_int(args[0]);
        if (flag != 0 && flag != 1) {
            mp_raise_ValueError(MP_ERROR_TEXT("invalid value, flag should be in [0,1]."));
        }
    }
    qosa_uint8_t simid = qpy_get_simid(n_args, args, 1);
    if (qpy_sim_prepare_op() != 0 || qosa_sim_get_pin_remain_retries(simid, qpy_sim_pin_retries_callback, NULL) != QOSA_SIM_ERR_OK || qosa_sem_wait(qpy_sim_op_sem, QPY_SIM_OP_TIMEOUT_MS) != QOSA_OK || qpy_sim_op_err != QOSA_SIM_ERR_OK) {
        return qpy_int_minus_one();
    }
    mp_obj_t values[4] = {
        MP_OBJ_NEW_SMALL_INT(qpy_sim_pin_retries.pin1_remain),
        MP_OBJ_NEW_SMALL_INT(qpy_sim_pin_retries.puk1_remain),
        MP_OBJ_NEW_SMALL_INT(qpy_sim_pin_retries.pin2_remain),
        MP_OBJ_NEW_SMALL_INT(qpy_sim_pin_retries.puk2_remain),
    };
    return mp_obj_new_tuple(flag ? 4 : 2, values);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_sim_get_pin_rem_attempts_obj, 0, 2, qpy_sim_get_pin_rem_attempts);

static mp_obj_t qpy_sim_is_insert(size_t n_args, const mp_obj_t *args) {
    qosa_uint8_t simid = qpy_get_simid(n_args, args, 0);
    return MP_OBJ_NEW_SMALL_INT(qosa_sim_read_insert_stat(simid) == QOSA_SIM_INSERT_STAT_INSERTED ? 1 : 0);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_sim_is_insert_obj, 0, 1, qpy_sim_is_insert);

static mp_obj_t qpy_sim_get_cur_simid(void) {
    return MP_OBJ_NEW_SMALL_INT(0);
}
static MP_DEFINE_CONST_FUN_OBJ_0(qpy_sim_get_cur_simid_obj, qpy_sim_get_cur_simid);

static qosa_uint8_t qpy_simdet_get_simid(size_t n_args, const mp_obj_t *args, size_t index) {
    qosa_uint8_t simid = qpy_get_simid(n_args, args, index);
    if (simid != 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid value, SIMDET only supports simid 0."));
    }
    return simid;
}

static mp_obj_t qpy_sim_set_simdet(size_t n_args, const mp_obj_t *args) {
    qosa_sim_hot_swap_cfg_t cfg = {0};
    mp_arg_check_num(n_args, 0, 2, 3, false);
    int enable = mp_obj_get_int(args[0]);
    int insert_level = mp_obj_get_int(args[1]);
    if (enable != 0 && enable != 1) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid value, detenable should be in (0,1)."));
    }
    if (insert_level != 0 && insert_level != 1) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid value, insertlevel should be in (0,1)."));
    }
    qosa_uint8_t simid = qpy_simdet_get_simid(n_args, args, 2);
    cfg.enable = enable ? QOSA_TRUE : QOSA_FALSE;
    cfg.insert_level = (qosa_sim_insert_level_e)insert_level;
    cfg.gpio = QOSA_SIM_HOT_SWAP_UNSPECIFIED_GPIO;
    return MP_OBJ_NEW_SMALL_INT(
        qosa_sim_set_sim_hot_swap(simid, &cfg) == QOSA_SIM_ERR_OK ? 0 : -1);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_sim_set_simdet_obj, 2, 3, qpy_sim_set_simdet);

static mp_obj_t qpy_sim_get_simdet(size_t n_args, const mp_obj_t *args) {
    qosa_sim_hot_swap_cfg_t cfg = {0};
    mp_arg_check_num(n_args, 0, 0, 1, false);
    qosa_uint8_t simid = qpy_simdet_get_simid(n_args, args, 0);
    if (qosa_sim_get_sim_hot_swap(simid, &cfg) != QOSA_SIM_ERR_OK) {
        return qpy_int_minus_one();
    }
    mp_obj_t values[2] = {
        MP_OBJ_NEW_SMALL_INT(cfg.enable ? 1 : 0),
        MP_OBJ_NEW_SMALL_INT(cfg.insert_level),
    };
    return mp_obj_new_tuple(2, values);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_sim_get_simdet_obj, 0, 1, qpy_sim_get_simdet);

static mp_obj_t qpy_sim_set_callback(mp_obj_t callback) {
    if (callback != mp_const_none && !mp_obj_is_callable(callback)) {
        mp_raise_TypeError(MP_ERROR_TEXT("callback must be callable"));
    }
    qpy_sim_callback = callback;
    return MP_OBJ_NEW_SMALL_INT(0);
}
static MP_DEFINE_CONST_FUN_OBJ_1(qpy_sim_set_callback_obj, qpy_sim_set_callback);

static mp_obj_t qpy_module_sim_deinit(void) {
    qpy_sim_callback = MP_OBJ_NULL;
    return MP_OBJ_NEW_SMALL_INT(0);
}
static MP_DEFINE_CONST_FUN_OBJ_0(qpy_module_sim_deinit_obj, qpy_module_sim_deinit);

static const mp_rom_map_elem_t qpy_sim_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_sim) },
    { MP_ROM_QSTR(MP_QSTR_getStatus), MP_ROM_PTR(&qpy_sim_get_status_obj) },
    { MP_ROM_QSTR(MP_QSTR_getImsi), MP_ROM_PTR(&qpy_sim_get_imsi_obj) },
    { MP_ROM_QSTR(MP_QSTR_getIccid), MP_ROM_PTR(&qpy_sim_get_iccid_obj) },
    { MP_ROM_QSTR(MP_QSTR_getPhoneNumber), MP_ROM_PTR(&qpy_sim_get_phone_number_obj) },
    { MP_ROM_QSTR(MP_QSTR_getSimlock), MP_ROM_PTR(&qpy_stub_ret_minus_one_obj) },
    { MP_ROM_QSTR(MP_QSTR_setSimlock), MP_ROM_PTR(&qpy_stub_ret_minus_one_obj) },
    { MP_ROM_QSTR(MP_QSTR_verifyPin), MP_ROM_PTR(&qpy_sim_verify_pin_obj) },
    { MP_ROM_QSTR(MP_QSTR_changePin), MP_ROM_PTR(&qpy_sim_change_pin_obj) },
    { MP_ROM_QSTR(MP_QSTR_unblockPin), MP_ROM_PTR(&qpy_sim_unblock_pin_obj) },
    { MP_ROM_QSTR(MP_QSTR_enablePin), MP_ROM_PTR(&qpy_sim_enable_pin_obj) },
    { MP_ROM_QSTR(MP_QSTR_disablePin), MP_ROM_PTR(&qpy_sim_disable_pin_obj) },
    { MP_ROM_QSTR(MP_QSTR_getPinRemAttempts), MP_ROM_PTR(&qpy_sim_get_pin_rem_attempts_obj) },
    { MP_ROM_QSTR(MP_QSTR_getPhonebookStatus), MP_ROM_PTR(&qpy_sim_get_phonebook_status_obj) },
    { MP_ROM_QSTR(MP_QSTR_readPhonebook), MP_ROM_PTR(&qpy_stub_ret_empty_tuple_obj) },
    { MP_ROM_QSTR(MP_QSTR_writePhonebook), MP_ROM_PTR(&qpy_stub_ret_minus_one_obj) },
    { MP_ROM_QSTR(MP_QSTR_genericAccess), MP_ROM_PTR(&qpy_sim_generic_access_obj) },
    { MP_ROM_QSTR(MP_QSTR_switchCard), MP_ROM_PTR(&qpy_stub_ret_minus_one_obj) },
    { MP_ROM_QSTR(MP_QSTR_setSwitchcardCallback), MP_ROM_PTR(&qpy_stub_ret_minus_one_obj) },
    { MP_ROM_QSTR(MP_QSTR_getCurSimid), MP_ROM_PTR(&qpy_sim_get_cur_simid_obj) },
    { MP_ROM_QSTR(MP_QSTR_isInsert), MP_ROM_PTR(&qpy_sim_is_insert_obj) },
    { MP_ROM_QSTR(MP_QSTR_setSimDet), MP_ROM_PTR(&qpy_sim_set_simdet_obj) },
    { MP_ROM_QSTR(MP_QSTR_getSimDet), MP_ROM_PTR(&qpy_sim_get_simdet_obj) },
    { MP_ROM_QSTR(MP_QSTR_setCallback), MP_ROM_PTR(&qpy_sim_set_callback_obj) },
    { MP_ROM_QSTR(MP_QSTR_eSimGetList), MP_ROM_PTR(&qpy_stub_ret_empty_list_obj) },
    { MP_ROM_QSTR(MP_QSTR_eSimGetEid), MP_ROM_PTR(&qpy_stub_ret_empty_string_obj) },
    { MP_ROM_QSTR(MP_QSTR_eSimSwitch), MP_ROM_PTR(&qpy_stub_ret_minus_one_obj) },
    { MP_ROM_QSTR(MP_QSTR___qpy_module_deinit__), MP_ROM_PTR(&qpy_module_sim_deinit_obj) },
};
static MP_DEFINE_CONST_DICT(qpy_sim_globals, qpy_sim_globals_table);

const mp_obj_module_t mp_module_sim = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&qpy_sim_globals,
};

#if MICROPY_QPY_MODULE_SIM
MP_REGISTER_MODULE(MP_QSTR_sim, mp_module_sim);
#endif
