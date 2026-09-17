#include "py/obj.h"
#include "py/runtime.h"
#include "py/mperrno.h"
#include "qosa_def.h"
#include "qosa_sys.h"
#include "mphalport.h"
#include "machine_qosa.h"

#define QPY_MACHINE_TIMER_MAX (4)
#define QPY_TIMER_PERIODIC (1)
#define QPY_TIMER_ONE_SHOT (2)

typedef struct _qpy_machine_timer_obj_t {
    mp_obj_base_t base;
    int id;
    qosa_timer_t timer;
    mp_uint_t mode;
    mp_uint_t period;
    mp_obj_t callback;
    volatile int pending;
    bool active;
} qpy_machine_timer_obj_t;

static qpy_machine_timer_obj_t *qpy_machine_timers[QPY_MACHINE_TIMER_MAX];

static void qpy_timer_callback(void *argument) {
    qpy_machine_timer_obj_t *self = (qpy_machine_timer_obj_t *)argument;
    if (self != NULL && self->callback != MP_OBJ_NULL && self->callback != mp_const_none) {
        self->pending++;
        mp_hal_stdio_wake();
    }
}

void qpy_machine_timer_poll_pending(void) {
    for (int i = 0; i < QPY_MACHINE_TIMER_MAX; ++i) {
        qpy_machine_timer_obj_t *self = qpy_machine_timers[i];
        if (self != NULL && self->pending > 0 && self->callback != MP_OBJ_NULL && self->callback != mp_const_none) {
            self->pending--;
            mp_sched_schedule(self->callback, MP_OBJ_FROM_PTR(self));
        }
    }
}

static bool qpy_timer_ensure_created(qpy_machine_timer_obj_t *self) {
    if (self->timer == QOSA_NULL && qosa_timer_create(&self->timer, qpy_timer_callback, self) != QOSA_OK) {
        return false;
    }
    return true;
}

static mp_obj_t qpy_timer_start_helper(qpy_machine_timer_obj_t *self, size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_mode, ARG_callback, ARG_period, ARG_tick_hz, ARG_freq };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_mode, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = QPY_TIMER_PERIODIC} },
        { MP_QSTR_callback, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_period, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0xffffffff} },
        { MP_QSTR_tick_hz, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 1000} },
        { MP_QSTR_freq, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = mp_const_none} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    mp_uint_t period;
    if (args[ARG_freq].u_obj != mp_const_none) {
        period = (mp_uint_t)(1000 / mp_obj_get_float(args[ARG_freq].u_obj));
    } else {
        period = (mp_uint_t)(((uint64_t)args[ARG_period].u_int * 1000) / args[ARG_tick_hz].u_int);
    }
    if (args[ARG_callback].u_obj != mp_const_none && !mp_obj_is_callable(args[ARG_callback].u_obj)) {
        mp_raise_TypeError(MP_ERROR_TEXT("callback must be callable"));
    }

    self->period = period;
    self->mode = args[ARG_mode].u_int;
    self->callback = args[ARG_callback].u_obj;
    self->pending = 0;
    if (self->active) {
        return MP_OBJ_NEW_SMALL_INT(0);
    }
    if (!qpy_timer_ensure_created(self)) {
        return mp_const_false;
    }
    qosa_timer_start(self->timer, (uint32_t)self->period, self->mode == QPY_TIMER_PERIODIC ? QOSA_TRUE : QOSA_FALSE);
    self->active = true;
    return MP_OBJ_NEW_SMALL_INT(0);
}

static mp_obj_t qpy_timer_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args) {
    mp_arg_check_num(n_args, n_kw, 0, 1, true);
    mp_int_t id = n_args == 0 ? 0 : mp_obj_get_int(all_args[0]);
    if (id < 0 || id >= QPY_MACHINE_TIMER_MAX) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid value, Timern should be in (Timer0~Timer3)."));
    }
    qpy_machine_timer_obj_t *self = qpy_machine_timers[id];
    if (self != NULL) {
        return MP_OBJ_FROM_PTR(self);
    }
    self = mp_obj_malloc(qpy_machine_timer_obj_t, type);
    self->id = id;
    self->timer = QOSA_NULL;
    self->mode = QPY_TIMER_PERIODIC;
    self->period = 0xffffffff;
    self->callback = MP_OBJ_NULL;
    self->pending = 0;
    self->active = false;
    qpy_machine_timers[id] = self;
    if (n_kw > 0) {
        mp_map_t kw_args;
        mp_map_init_fixed_table(&kw_args, n_kw, all_args + n_args);
        qpy_timer_start_helper(self, 0, NULL, &kw_args);
    }
    return MP_OBJ_FROM_PTR(self);
}

static mp_obj_t qpy_timer_start(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    return qpy_timer_start_helper(MP_OBJ_TO_PTR(pos_args[0]), n_args - 1, pos_args + 1, kw_args);
}
static MP_DEFINE_CONST_FUN_OBJ_KW(qpy_timer_start_obj, 1, qpy_timer_start);

static mp_obj_t qpy_timer_initialize(void) {
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(qpy_timer_initialize_obj, qpy_timer_initialize);

static mp_obj_t qpy_timer_stop(mp_obj_t self_in) {
    qpy_machine_timer_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->timer == QOSA_NULL) {
        return MP_OBJ_NEW_SMALL_INT(-1);
    }
    qosa_timer_stop(self->timer);
    self->active = false;
    self->pending = 0;
    return MP_OBJ_NEW_SMALL_INT(0);
}
static MP_DEFINE_CONST_FUN_OBJ_1(qpy_timer_stop_obj, qpy_timer_stop);

static mp_obj_t qpy_timer_deinit(mp_obj_t self_in) {
    qpy_machine_timer_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->timer == QOSA_NULL) {
        return MP_OBJ_NEW_SMALL_INT(-1);
    }
    qosa_timer_stop(self->timer);
    qosa_timer_delete(self->timer);
    self->timer = QOSA_NULL;
    self->active = false;
    self->pending = 0;
    return MP_OBJ_NEW_SMALL_INT(0);
}
static MP_DEFINE_CONST_FUN_OBJ_1(qpy_timer_deinit_obj, qpy_timer_deinit);

static void qpy_timer_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    (void)print;
    (void)self_in;
    (void)kind;
}

static const mp_rom_map_elem_t qpy_timer_locals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&qpy_timer_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR___init__), MP_ROM_PTR(&qpy_timer_initialize_obj) },
    { MP_ROM_QSTR(MP_QSTR_start), MP_ROM_PTR(&qpy_timer_start_obj) },
    { MP_ROM_QSTR(MP_QSTR_stop), MP_ROM_PTR(&qpy_timer_stop_obj) },
    { MP_ROM_QSTR(MP_QSTR_ONE_SHOT), MP_ROM_INT(QPY_TIMER_ONE_SHOT) },
    { MP_ROM_QSTR(MP_QSTR_PERIODIC), MP_ROM_INT(QPY_TIMER_PERIODIC) },
    { MP_ROM_QSTR(MP_QSTR_Timer0), MP_ROM_INT(0) },
    { MP_ROM_QSTR(MP_QSTR_Timer1), MP_ROM_INT(1) },
    { MP_ROM_QSTR(MP_QSTR_Timer2), MP_ROM_INT(2) },
    { MP_ROM_QSTR(MP_QSTR_Timer3), MP_ROM_INT(3) },
};
static MP_DEFINE_CONST_DICT(qpy_timer_locals_dict, qpy_timer_locals_table);

MP_DEFINE_CONST_OBJ_TYPE(
    qpy_machine_timer_type,
    MP_QSTR_Timer,
    MP_TYPE_FLAG_NONE,
    print, qpy_timer_print,
    make_new, qpy_timer_make_new,
    locals_dict, &qpy_timer_locals_dict
    );
