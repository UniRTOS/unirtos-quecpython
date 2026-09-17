#include "py/obj.h"
#include "py/runtime.h"
#include "qosa_usb.h"

extern void mp_hal_stdio_wake(void);

typedef struct _qpy_misc_usb_obj_t {
    mp_obj_base_t base;
} qpy_misc_usb_obj_t;

static qpy_misc_usb_obj_t *qpy_usb_singleton;
static mp_obj_t qpy_usb_callback = mp_const_none;
static volatile int qpy_usb_pending;
static volatile int qpy_usb_pending_state;

static qosa_uint32_t qpy_usb_vbus_callback(qosa_usb_vbus_state_e state, void *ctx) {
    (void)ctx;
    qpy_usb_pending_state = (int)state;
    qpy_usb_pending = 1;
    mp_hal_stdio_wake();
    return 0;
}

void qpy_misc_usb_poll_pending(void) {
    if (!qpy_usb_pending || qpy_usb_callback == mp_const_none) {
        return;
    }
    int state = qpy_usb_pending_state;
    qpy_usb_pending = 0;
    mp_sched_schedule(qpy_usb_callback, MP_OBJ_NEW_SMALL_INT(state));
}

static mp_obj_t qpy_usb_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 0, 0, false);
    if (qpy_usb_singleton == NULL) {
        qpy_usb_singleton = mp_obj_malloc_with_finaliser(qpy_misc_usb_obj_t, type);
        qosa_usb_init();
    }
    return MP_OBJ_FROM_PTR(qpy_usb_singleton);
}

static mp_obj_t qpy_usb_get_status(mp_obj_t self_in) {
    (void)self_in;
    qosa_usb_state_e state = qosa_usb_get_connect_state();
    return MP_OBJ_NEW_SMALL_INT((int)state);
}
static MP_DEFINE_CONST_FUN_OBJ_1(qpy_usb_get_status_obj, qpy_usb_get_status);

static mp_obj_t qpy_usb_set_callback(mp_obj_t self_in, mp_obj_t callback) {
    (void)self_in;
    if (callback != mp_const_none && !mp_obj_is_callable(callback)) {
        mp_raise_TypeError(MP_ERROR_TEXT("callback must be callable"));
    }
    qpy_usb_callback = callback;
    if (qosa_usb_init() != QOSA_USB_SUCCESS) {
        return MP_OBJ_NEW_SMALL_INT(-1);
    }
    return MP_OBJ_NEW_SMALL_INT(qosa_usb_bind_vbus_cb(qpy_usb_vbus_callback) == QOSA_USB_SUCCESS ? 0 : -1);
}
static MP_DEFINE_CONST_FUN_OBJ_2(qpy_usb_set_callback_obj, qpy_usb_set_callback);

static mp_obj_t qpy_usb_disable(size_t n_args, const mp_obj_t *args) {
    (void)args;
    if (n_args == 2) {
        return MP_OBJ_NEW_SMALL_INT(-1);
    }
    return MP_OBJ_NEW_SMALL_INT((int)qosa_usb_get_connect_state());
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_usb_disable_obj, 1, 2, qpy_usb_disable);

static mp_obj_t qpy_usb_deinit(mp_obj_t self_in) {
    (void)self_in;
    qpy_usb_callback = mp_const_none;
    qpy_usb_singleton = NULL;
    return MP_OBJ_NEW_SMALL_INT(qosa_usb_deinit() == QOSA_USB_SUCCESS ? 0 : -1);
}
static MP_DEFINE_CONST_FUN_OBJ_1(qpy_usb_deinit_obj, qpy_usb_deinit);

static const mp_rom_map_elem_t qpy_usb_locals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&qpy_usb_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR_getStatus), MP_ROM_PTR(&qpy_usb_get_status_obj) },
    { MP_ROM_QSTR(MP_QSTR_setCallback), MP_ROM_PTR(&qpy_usb_set_callback_obj) },
    { MP_ROM_QSTR(MP_QSTR_disable), MP_ROM_PTR(&qpy_usb_disable_obj) },
};
static MP_DEFINE_CONST_DICT(qpy_usb_locals, qpy_usb_locals_table);

MP_DEFINE_CONST_OBJ_TYPE(
    qpy_usb_type,
    MP_QSTR_USB,
    MP_TYPE_FLAG_NONE,
    make_new, qpy_usb_make_new,
    locals_dict, &qpy_usb_locals
    );
