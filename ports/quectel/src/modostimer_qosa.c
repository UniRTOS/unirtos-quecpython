#include "py/obj.h"
#include "py/runtime.h"
#include "py/mperrno.h"
#include "qosa_def.h"
#include "qosa_sys.h"
#include "mphalport.h"

#if MICROPY_QPY_MODULE_OSTIMER

typedef struct _qpy_ostimer_obj_t {
    mp_obj_base_t base;
    qosa_timer_t timer;
    mp_obj_t callback;
    volatile int pending;
    bool deleted;
} qpy_ostimer_obj_t;

#define QPY_OSTIMER_MAX (16)
static qpy_ostimer_obj_t *qpy_ostimers[QPY_OSTIMER_MAX];

static void qpy_ostimer_track(qpy_ostimer_obj_t *self) {
    for (size_t i = 0; i < QPY_OSTIMER_MAX; ++i) {
        if (qpy_ostimers[i] == NULL) {
            qpy_ostimers[i] = self;
            return;
        }
    }
}

static void qpy_ostimer_untrack(qpy_ostimer_obj_t *self) {
    for (size_t i = 0; i < QPY_OSTIMER_MAX; ++i) {
        if (qpy_ostimers[i] == self) {
            qpy_ostimers[i] = NULL;
            return;
        }
    }
}

static void qpy_ostimer_cb(void *argument) {
    qpy_ostimer_obj_t *self = (qpy_ostimer_obj_t *)argument;
    if (self != NULL && !self->deleted && self->callback != MP_OBJ_NULL && self->callback != mp_const_none) {
        self->pending = 1;
        mp_hal_stdio_wake();
    }
}

void qpy_ostimer_poll_pending(void) {
    for (size_t i = 0; i < QPY_OSTIMER_MAX; ++i) {
        qpy_ostimer_obj_t *self = qpy_ostimers[i];
        if (self != NULL && self->pending && !self->deleted && self->callback != MP_OBJ_NULL && self->callback != mp_const_none) {
            self->pending = 0;
            mp_sched_schedule(self->callback, mp_const_none);
        }
    }
}

static int qpy_ostimer_create_if_needed(qpy_ostimer_obj_t *self) {
    if (self->timer == QOSA_NULL && qosa_timer_create(&self->timer, qpy_ostimer_cb, self) != QOSA_OK) {
        return -1;
    }
    return 0;
}

static mp_obj_t qpy_ostimer_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    // Legacy osTimer accepted and ignored constructor arguments.
    (void)n_args;
    (void)n_kw;
    (void)args;
    qpy_ostimer_obj_t *self = mp_obj_malloc_with_finaliser(qpy_ostimer_obj_t, type);
    self->timer = QOSA_NULL;
    self->callback = MP_OBJ_NULL;
    self->pending = 0;
    self->deleted = false;
    qpy_ostimer_track(self);
    if (qpy_ostimer_create_if_needed(self) != 0) {
        mp_raise_OSError(MP_EIO);
    }
    return MP_OBJ_FROM_PTR(self);
}

static mp_obj_t qpy_ostimer_start(size_t n_args, const mp_obj_t *args) {
    qpy_ostimer_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    if (self->deleted || qpy_ostimer_create_if_needed(self) != 0) {
        return MP_OBJ_NEW_SMALL_INT(-1);
    }

    mp_int_t period = mp_obj_get_int(args[1]);
    mp_int_t cyclic = mp_obj_get_int(args[2]);
    mp_obj_t callback = args[3];
    if (!mp_obj_is_callable(callback)) {
        mp_raise_TypeError(MP_ERROR_TEXT("callback must be callable"));
    }

    self->callback = callback;
    self->pending = 0;
    int ret = qosa_timer_start(self->timer, (uint32_t)period, cyclic ? QOSA_TRUE : QOSA_FALSE);
    return MP_OBJ_NEW_SMALL_INT(ret == QOSA_OK ? 0 : -1);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_ostimer_start_obj, 4, 5, qpy_ostimer_start);

static mp_obj_t qpy_ostimer_stop(mp_obj_t self_in) {
    qpy_ostimer_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->deleted || self->timer == QOSA_NULL) {
        return MP_OBJ_NEW_SMALL_INT(-1);
    }
    self->pending = 0;
    return MP_OBJ_NEW_SMALL_INT(qosa_timer_stop(self->timer) == QOSA_OK ? 0 : -1);
}
static MP_DEFINE_CONST_FUN_OBJ_1(qpy_ostimer_stop_obj, qpy_ostimer_stop);

static mp_obj_t qpy_ostimer_delete(mp_obj_t self_in) {
    qpy_ostimer_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (!self->deleted) {
        self->deleted = true;
        self->pending = 0;
        if (self->timer != QOSA_NULL) {
            qosa_timer_stop(self->timer);
            qosa_timer_delete(self->timer);
            self->timer = QOSA_NULL;
        }
        qpy_ostimer_untrack(self);
    }
    return MP_OBJ_NEW_SMALL_INT(0);
}
static MP_DEFINE_CONST_FUN_OBJ_1(qpy_ostimer_delete_obj, qpy_ostimer_delete);

static const mp_rom_map_elem_t qpy_ostimer_locals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&qpy_ostimer_delete_obj) },
    { MP_ROM_QSTR(MP_QSTR_start), MP_ROM_PTR(&qpy_ostimer_start_obj) },
    { MP_ROM_QSTR(MP_QSTR_stop), MP_ROM_PTR(&qpy_ostimer_stop_obj) },
    { MP_ROM_QSTR(MP_QSTR_delete_timer), MP_ROM_PTR(&qpy_ostimer_delete_obj) },
};
static MP_DEFINE_CONST_DICT(qpy_ostimer_locals_dict, qpy_ostimer_locals_table);

MP_DEFINE_CONST_OBJ_TYPE(
    mp_ostimer_type,
    MP_QSTR_osTimer,
    MP_TYPE_FLAG_NONE,
    make_new, qpy_ostimer_make_new,
    locals_dict, &qpy_ostimer_locals_dict
    );

MP_REGISTER_MODULE(MP_QSTR_osTimer, mp_ostimer_type);

#endif
