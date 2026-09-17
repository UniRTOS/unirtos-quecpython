#include "py/obj.h"
#include "py/runtime.h"

#include "qosa_power.h"
#include "qpy_compat_common.h"

extern void mp_hal_stdio_wake(void);

typedef struct _qpy_misc_powerkey_obj_t {
    mp_obj_base_t base;
    mp_obj_t callback;
} qpy_misc_powerkey_obj_t;

static qpy_misc_powerkey_obj_t *qpy_powerkey_singleton;
static volatile int qpy_powerkey_pending;
static volatile int qpy_powerkey_pending_level;

static void qpy_powerkey_event_callback(qosa_uint8_t pinlevel) {
    qpy_powerkey_pending_level = (int)pinlevel;
    qpy_powerkey_pending++;
    mp_hal_stdio_wake();
}

void qpy_misc_powerkey_poll_pending(void) {
    qpy_misc_powerkey_obj_t *self = qpy_powerkey_singleton;
    if (self == NULL || qpy_powerkey_pending <= 0 || self->callback == mp_const_none) {
        return;
    }
    int level = qpy_powerkey_pending_level;
    qpy_powerkey_pending--;
    mp_sched_schedule(self->callback, MP_OBJ_NEW_SMALL_INT(level));
}

static mp_obj_t qpy_powerkey_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 0, 0, false);
    if (qpy_powerkey_singleton == NULL) {
        qpy_powerkey_singleton = mp_obj_malloc_with_finaliser(qpy_misc_powerkey_obj_t, type);
        qpy_powerkey_singleton->callback = mp_const_none;
    }
    return MP_OBJ_FROM_PTR(qpy_powerkey_singleton);
}

static mp_obj_t qpy_powerkey_event_register(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    qpy_misc_powerkey_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    mp_obj_t callback = args[1];
    if (callback != mp_const_none && !mp_obj_is_callable(callback)) {
        mp_raise_TypeError(MP_ERROR_TEXT("callback must be callable"));
    }
    self->callback = callback;
    if (callback == mp_const_none) {
        qosa_pwrkey_callback_register(QOSA_NULL);
        return MP_OBJ_NEW_SMALL_INT(0);
    }
    qosa_power_error_e ret = qosa_pwrkey_callback_register(qpy_powerkey_event_callback);
    return MP_OBJ_NEW_SMALL_INT(ret == QOSA_POWER_SUCCESS ? 0 : -1);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_powerkey_event_register_obj, 2, 3, qpy_powerkey_event_register);

static mp_obj_t qpy_powerkey_deinit(mp_obj_t self_in) {
    qpy_misc_powerkey_obj_t *self = MP_OBJ_TO_PTR(self_in);
    self->callback = mp_const_none;
    qpy_powerkey_pending = 0;
    qosa_pwrkey_callback_register(QOSA_NULL);
    qpy_powerkey_singleton = NULL;
    return MP_OBJ_NEW_SMALL_INT(0);
}
static MP_DEFINE_CONST_FUN_OBJ_1(qpy_powerkey_deinit_obj, qpy_powerkey_deinit);

static mp_obj_t qpy_powerkey_pull_mode_set(mp_obj_t self_in, mp_obj_t is_pullup) {
    (void)self_in;
    mp_int_t value = mp_obj_get_int(is_pullup);
    if (value < 0 || value > 1) {
        mp_raise_OSError(22);
    }
    return MP_OBJ_NEW_SMALL_INT(-1);
}
static MP_DEFINE_CONST_FUN_OBJ_2(qpy_powerkey_pull_mode_set_obj, qpy_powerkey_pull_mode_set);

static mp_obj_t qpy_powerkey_get_status(mp_obj_t self_in) {
    (void)self_in;
    qosa_uint8_t level = 0;
    if (qosa_get_pwrkey_level(&level) != QOSA_POWER_SUCCESS) {
        return MP_OBJ_NEW_SMALL_INT(-1);
    }
    return MP_OBJ_NEW_SMALL_INT((int)level);
}
static MP_DEFINE_CONST_FUN_OBJ_1(qpy_powerkey_get_status_obj, qpy_powerkey_get_status);

static const mp_rom_map_elem_t qpy_powerkey_locals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&qpy_powerkey_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR_powerKeyEventRegister), MP_ROM_PTR(&qpy_powerkey_event_register_obj) },
    { MP_ROM_QSTR(MP_QSTR_powerKeyPullModeSet), MP_ROM_PTR(&qpy_powerkey_pull_mode_set_obj) },
    { MP_ROM_QSTR(MP_QSTR_getpowerKeyStatus), MP_ROM_PTR(&qpy_powerkey_get_status_obj) },
};
static MP_DEFINE_CONST_DICT(qpy_powerkey_locals, qpy_powerkey_locals_table);

MP_DEFINE_CONST_OBJ_TYPE(
    qpy_powerkey_type,
    MP_QSTR_PowerKey,
    MP_TYPE_FLAG_NONE,
    make_new, qpy_powerkey_make_new,
    locals_dict, &qpy_powerkey_locals
    );
