#include "py/obj.h"
#include "py/runtime.h"

#define QOSA_USBNET_ERR_OK 0
#define QOSA_FALSE 0
#define QOSA_TRUE 1
#define QOSA_USBNET_TYPE_ECM 1
#define QOSA_USBNET_TYPE_MBIM 2
#define QOSA_USBNET_TYPE_RNDIS 3
#define QOSA_USBNET_TYPE_NCM 5
#define QOSA_USBNET_STATUS_IDLE 0

typedef int qosa_usbnet_err_e;
typedef int qosa_usbnet_type_e;
typedef int qosa_usbnet_status_e;
typedef void (*usbnet_callback_t)(void *ctx, void *argv);

extern qosa_usbnet_err_e qosa_usbnet_set_type(qosa_uint8_t simid, qosa_usbnet_type_e type);
extern qosa_usbnet_err_e qosa_usbnet_get_type(qosa_uint8_t simid, qosa_usbnet_type_e *type);
extern qosa_usbnet_err_e qosa_usbnet_start(qosa_uint8_t simid, usbnet_callback_t cb, void *ctx);
extern qosa_usbnet_err_e qosa_usbnet_stop(qosa_uint8_t simid, usbnet_callback_t cb, void *ctx);
extern qosa_usbnet_err_e qosa_usbnet_get_status(qosa_uint8_t simid, qosa_usbnet_status_e *status);
extern qosa_usbnet_err_e qosa_usbnet_set_nat_mode(qosa_uint8_t simid, qosa_bool_t nat_mode, qosa_uint32_t cid);
extern qosa_usbnet_err_e qosa_usbnet_get_nat_mode(qosa_uint8_t simid, qosa_bool_t *nat_mode, qosa_uint32_t *cid);

static int qpy_usbnet_result(qosa_usbnet_err_e ret) {
    return ret == QOSA_USBNET_ERR_OK ? 0 : -1;
}

static qosa_usbnet_type_e qpy_usbnet_get_valid_type(mp_int_t type) {
    if (type == QOSA_USBNET_TYPE_ECM || type == QOSA_USBNET_TYPE_RNDIS || type == QOSA_USBNET_TYPE_MBIM || type == QOSA_USBNET_TYPE_NCM) {
        return (qosa_usbnet_type_e)type;
    }
    mp_raise_ValueError(MP_ERROR_TEXT("USBNET type must be Type_ECM or Type_RNDIS."));
}

static mp_obj_t qpy_usbnet_set_type(mp_obj_t type_in) {
    qosa_usbnet_type_e type = qpy_usbnet_get_valid_type(mp_obj_get_int(type_in));
    return MP_OBJ_NEW_SMALL_INT(qpy_usbnet_result(qosa_usbnet_set_type(0, type)));
}
static MP_DEFINE_CONST_FUN_OBJ_1(qpy_usbnet_set_type_obj, qpy_usbnet_set_type);

static mp_obj_t qpy_usbnet_get_type(void) {
    qosa_usbnet_type_e type = QOSA_USBNET_TYPE_ECM;
    if (qosa_usbnet_get_type(0, &type) != QOSA_USBNET_ERR_OK) {
        return MP_OBJ_NEW_SMALL_INT(-1);
    }
    return MP_OBJ_NEW_SMALL_INT((int)type);
}
static MP_DEFINE_CONST_FUN_OBJ_0(qpy_usbnet_get_type_obj, qpy_usbnet_get_type);

static mp_obj_t qpy_usbnet_get_status(void) {
    qosa_usbnet_status_e status = QOSA_USBNET_STATUS_IDLE;
    if (qosa_usbnet_get_status(0, &status) != QOSA_USBNET_ERR_OK) {
        return MP_OBJ_NEW_SMALL_INT(-1);
    }
    return MP_OBJ_NEW_SMALL_INT((int)status);
}
static MP_DEFINE_CONST_FUN_OBJ_0(qpy_usbnet_get_status_obj, qpy_usbnet_get_status);

static void qpy_usbnet_async_cb(void *ctx, void *argv) {
    (void)ctx;
    (void)argv;
}

static mp_obj_t qpy_usbnet_open(void) {
    return MP_OBJ_NEW_SMALL_INT(qpy_usbnet_result(qosa_usbnet_start(0, qpy_usbnet_async_cb, NULL)));
}
static MP_DEFINE_CONST_FUN_OBJ_0(qpy_usbnet_open_obj, qpy_usbnet_open);

static mp_obj_t qpy_usbnet_close(void) {
    return MP_OBJ_NEW_SMALL_INT(qpy_usbnet_result(qosa_usbnet_stop(0, qpy_usbnet_async_cb, NULL)));
}
static MP_DEFINE_CONST_FUN_OBJ_0(qpy_usbnet_close_obj, qpy_usbnet_close);

static mp_obj_t qpy_usbnet_get_nat(mp_obj_t sim_in, mp_obj_t cid_in) {
    qosa_bool_t nat = QOSA_FALSE;
    qosa_uint32_t cid = 0;
    qosa_uint8_t sim = (qosa_uint8_t)mp_obj_get_int(sim_in);
    (void)cid_in;
    if (qosa_usbnet_get_nat_mode(sim, &nat, &cid) != QOSA_USBNET_ERR_OK) {
        return MP_OBJ_NEW_SMALL_INT(-1);
    }
    return MP_OBJ_NEW_SMALL_INT(nat ? 1 : 0);
}
static MP_DEFINE_CONST_FUN_OBJ_2(qpy_usbnet_get_nat_obj, qpy_usbnet_get_nat);

static mp_obj_t qpy_usbnet_set_nat(mp_obj_t sim_in, mp_obj_t cid_in, mp_obj_t nat_in) {
    qosa_uint8_t sim = (qosa_uint8_t)mp_obj_get_int(sim_in);
    qosa_uint32_t cid = (qosa_uint32_t)mp_obj_get_int(cid_in);
    qosa_bool_t nat = mp_obj_is_true(nat_in) ? QOSA_TRUE : QOSA_FALSE;
    return MP_OBJ_NEW_SMALL_INT(qpy_usbnet_result(qosa_usbnet_set_nat_mode(sim, nat, cid)));
}
static MP_DEFINE_CONST_FUN_OBJ_3(qpy_usbnet_set_nat_obj, qpy_usbnet_set_nat);

static const mp_rom_map_elem_t qpy_usbnet_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_USBNET) },
    { MP_ROM_QSTR(MP_QSTR_set_worktype), MP_ROM_PTR(&qpy_usbnet_set_type_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_worktype), MP_ROM_PTR(&qpy_usbnet_get_type_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_status), MP_ROM_PTR(&qpy_usbnet_get_status_obj) },
    { MP_ROM_QSTR(MP_QSTR_open), MP_ROM_PTR(&qpy_usbnet_open_obj) },
    { MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&qpy_usbnet_close_obj) },
    { MP_ROM_QSTR(MP_QSTR_getNat), MP_ROM_PTR(&qpy_usbnet_get_nat_obj) },
    { MP_ROM_QSTR(MP_QSTR_setNat), MP_ROM_PTR(&qpy_usbnet_set_nat_obj) },
    { MP_ROM_QSTR(MP_QSTR_Type_ECM), MP_ROM_INT(QOSA_USBNET_TYPE_ECM) },
    { MP_ROM_QSTR(MP_QSTR_Type_RNDIS), MP_ROM_INT(QOSA_USBNET_TYPE_RNDIS) },
    { MP_ROM_QSTR(MP_QSTR_Type_MBIM), MP_ROM_INT(QOSA_USBNET_TYPE_MBIM) },
    { MP_ROM_QSTR(MP_QSTR_Type_NCM), MP_ROM_INT(QOSA_USBNET_TYPE_NCM) },
};
static MP_DEFINE_CONST_DICT(qpy_usbnet_globals, qpy_usbnet_globals_table);

const mp_obj_module_t qpy_usbnet_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&qpy_usbnet_globals,
};
