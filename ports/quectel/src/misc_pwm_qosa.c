#include "py/obj.h"
#include "py/runtime.h"
#include "py/mperrno.h"
#include "qosa_pwm.h"
#include "qosa_pinctrl.h"

typedef struct {
    uint8_t export_channel;
    uint8_t internal_channel;
    qosa_pin_num_e pin;
} qpy_pwm_map_t;

/* Helios TYPE_EC718PM exported PWM channel map. */
static const qpy_pwm_map_t qpy_pwm_map[] = {
    { 0, 4, 20 }, { 1, 3, 33 }, { 2, 2, 32 }, { 3, 1, 31 }, { 4, 0, 30 },
    { 5, 4, 6 },  { 6, 3, 5 },  { 7, 2, 16 }, { 8, 1, 49 }, { 9, 0, 22 },
    { 10, 4, 26 }, { 11, 3, 25 }, { 12, 2, 54 }, { 13, 1, 101 }, { 14, 0, 100 },
};

typedef enum {
    QPY_PWM_CYCLE_ABOVE_1US = 0,
    QPY_PWM_CYCLE_ABOVE_MS = 1,
    QPY_PWM_CYCLE_ABOVE_10US = 2,
    QPY_PWM_CYCLE_ABOVE_BELOW_US = 3,
} qpy_pwm_cycle_range_t;

typedef struct _qpy_misc_pwm_obj_t {
    mp_obj_base_t base;
    mp_int_t export_channel;
    qosa_uint8_t channel;
    qosa_pin_num_e pin;
    mp_int_t cycle_range;
    mp_int_t high_time;
    mp_int_t cycle_time;
    mp_int_t duty;
    mp_float_t frequency;
    mp_int_t pwm_psc;
    qosa_pwm_clk_src_e clk_src;
    bool v2;
} qpy_misc_pwm_obj_t;

static mp_int_t qpy_pwm_kw_int(mp_map_t *kw_args, qstr key, mp_int_t fallback) {
    mp_map_elem_t *elem = mp_map_lookup(kw_args, MP_OBJ_NEW_QSTR(key), MP_MAP_LOOKUP);
    return elem == NULL ? fallback : mp_obj_get_int(elem->value);
}

static mp_float_t qpy_pwm_kw_float(mp_map_t *kw_args, qstr key, mp_float_t fallback) {
    mp_map_elem_t *elem = mp_map_lookup(kw_args, MP_OBJ_NEW_QSTR(key), MP_MAP_LOOKUP);
    return elem == NULL ? fallback : mp_obj_get_float(elem->value);
}

static int qpy_pwm_result(qosa_pwm_error_e ret) {
    return ret == QOSA_PWM_SUCESS ? 0 : -1;
}

static qosa_pwm_clk_src_e qpy_pwm_clk(mp_int_t clk_src) {
    if (clk_src >= QOSA_FCLK_SEL_32K && clk_src <= QOSA_FCLK_SEL_26M) {
        return (qosa_pwm_clk_src_e)clk_src;
    }
    return QOSA_FCLK_SEL_26M;
}

static const qpy_pwm_map_t *qpy_pwm_get_map(mp_int_t export_channel) {
    for (size_t i = 0; i < MP_ARRAY_SIZE(qpy_pwm_map); ++i) {
        if (qpy_pwm_map[i].export_channel == export_channel) {
            return &qpy_pwm_map[i];
        }
    }
    return NULL;
}

static void qpy_pwm_apply_pin(const qpy_pwm_map_t *map, mp_map_t *kw_args) {
    mp_map_elem_t *pin = mp_map_lookup(kw_args, MP_OBJ_NEW_QSTR(MP_QSTR_pin), MP_MAP_LOOKUP);
    mp_map_elem_t *func = mp_map_lookup(kw_args, MP_OBJ_NEW_QSTR(MP_QSTR_pin_func), MP_MAP_LOOKUP);
    if ((pin != NULL && mp_obj_get_int(pin->value) != map->pin) ||
        (func != NULL && mp_obj_get_int(func->value) != 5)) {
        mp_raise_ValueError(MP_ERROR_TEXT("PWM pin is fixed by PWM channel"));
    }
    if (qosa_pin_set_func(map->pin, 5) != QOSA_PINCTRL_SUCCESS) {
        mp_raise_OSError(MP_EIO);
    }
}

static void qpy_pwm_load_v1(qpy_misc_pwm_obj_t *self, size_t n_args, const mp_obj_t *args, mp_map_t *kw_args) {
    self->cycle_range = n_args > 0 ? mp_obj_get_int(args[0]) : qpy_pwm_kw_int(kw_args, MP_QSTR_cycle_range, self->cycle_range);
    self->high_time = n_args > 1 ? mp_obj_get_int(args[1]) : qpy_pwm_kw_int(kw_args, MP_QSTR_high_time, self->high_time);
    self->cycle_time = n_args > 2 ? mp_obj_get_int(args[2]) : qpy_pwm_kw_int(kw_args, MP_QSTR_cycle_time, self->cycle_time);
    self->pwm_psc = qpy_pwm_kw_int(kw_args, MP_QSTR_pwm_psc, self->pwm_psc);
    self->clk_src = qpy_pwm_clk(qpy_pwm_kw_int(kw_args, MP_QSTR_clk_src, self->clk_src));
    self->v2 = false;
}

static void qpy_pwm_load_v2(qpy_misc_pwm_obj_t *self, size_t n_args, const mp_obj_t *args, mp_map_t *kw_args) {
    self->frequency = n_args > 0 ? mp_obj_get_float(args[0]) : qpy_pwm_kw_float(kw_args, MP_QSTR_frequency, self->frequency);
    self->duty = n_args > 1 ? mp_obj_get_int(args[1]) : qpy_pwm_kw_int(kw_args, MP_QSTR_duty, self->duty);
    if (self->duty < 0 || self->duty > 100) {
        mp_raise_ValueError(MP_ERROR_TEXT("PWM parameter error,range duty should be 0~100"));
    }
    self->pwm_psc = qpy_pwm_kw_int(kw_args, MP_QSTR_pwm_psc, self->pwm_psc);
    self->clk_src = qpy_pwm_clk(qpy_pwm_kw_int(kw_args, MP_QSTR_clk_src, self->clk_src));
    self->v2 = true;
}

static void qpy_pwm_configure(qpy_misc_pwm_obj_t *self) {
    qosa_pwm_info_t info;
    info.pwm_psc = self->pwm_psc <= 0 ? 1 : self->pwm_psc;
    info.clk_src = self->clk_src;
    if (self->v2) {
        uint32_t base = self->clk_src == QOSA_FCLK_SEL_32K ? 32000 : (self->clk_src == QOSA_FCLK_SEL_13M ? 13000000 : 26000000);
        uint32_t total = self->frequency <= 0 ? 2000 : (uint32_t)((mp_float_t)base / self->frequency);
        if (total == 0) {
            total = 1;
        }
        info.total_one_cycle_duration = total;
        info.high_one_cycle_duration = (total * (uint32_t)self->duty) / 100;
        self->cycle_time = info.total_one_cycle_duration;
        self->high_time = info.high_one_cycle_duration;
    } else {
        info.high_one_cycle_duration = self->high_time < 0 ? 0 : (uint32_t)self->high_time;
        info.total_one_cycle_duration = self->cycle_time <= 0 ? 2000 : (uint32_t)self->cycle_time;
    }
    qosa_pwm_config(self->channel, &info);
}

static mp_obj_t qpy_pwm_make_new_common(const mp_obj_type_t *type, bool v2, size_t n_args, size_t n_kw, const mp_obj_t *all_args) {
    mp_arg_check_num(n_args, n_kw, v2 ? 3 : 4, MP_OBJ_FUN_ARGS_MAX, true);
    qpy_misc_pwm_obj_t *self = mp_obj_malloc(qpy_misc_pwm_obj_t, type);
    self->export_channel = mp_obj_get_int(all_args[0]);
    const qpy_pwm_map_t *map = qpy_pwm_get_map(self->export_channel);
    if (map == NULL) {
        mp_raise_ValueError(MP_ERROR_TEXT("unsupported PWM channel"));
    }
    self->channel = map->internal_channel;
    self->pin = map->pin;
    self->cycle_range = QPY_PWM_CYCLE_ABOVE_1US;
    self->high_time = 1000;
    self->cycle_time = 2000;
    self->duty = 50;
    self->frequency = 13000;
    self->pwm_psc = 1;
    self->clk_src = QOSA_FCLK_SEL_26M;

    mp_map_t kw_args;
    mp_map_init_fixed_table(&kw_args, n_kw, all_args + n_args);
    if (v2) {
        qpy_pwm_load_v2(self, n_args - 1, all_args + 1, &kw_args);
    } else {
        qpy_pwm_load_v1(self, n_args - 1, all_args + 1, &kw_args);
    }
    qpy_pwm_apply_pin(map, &kw_args);
    qpy_pwm_configure(self);
    return MP_OBJ_FROM_PTR(self);
}

static mp_obj_t qpy_pwm_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args) {
    return qpy_pwm_make_new_common(type, false, n_args, n_kw, all_args);
}

static mp_obj_t qpy_pwm_v2_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args) {
    return qpy_pwm_make_new_common(type, true, n_args, n_kw, all_args);
}

static mp_obj_t qpy_pwm_open(size_t n_args, const mp_obj_t *args) {
    qpy_misc_pwm_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    if (self->v2 && n_args == 3) {
        self->frequency = mp_obj_get_float(args[1]);
        self->duty = mp_obj_get_int(args[2]);
        if (self->duty < 0 || self->duty > 100) {
            mp_raise_ValueError(MP_ERROR_TEXT("PWM parameter error,range duty should be 0~100"));
        }
        qpy_pwm_configure(self);
    } else if (!self->v2 && n_args == 4) {
        self->cycle_range = mp_obj_get_int(args[1]);
        self->high_time = mp_obj_get_int(args[2]);
        self->cycle_time = mp_obj_get_int(args[3]);
        qpy_pwm_configure(self);
    }
    return MP_OBJ_NEW_SMALL_INT(qpy_pwm_result(qosa_pwm_enable((unsigned char)self->channel, (uint32_t)self->high_time)));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_pwm_open_obj, 1, 4, qpy_pwm_open);

static mp_obj_t qpy_pwm_close(mp_obj_t self_in) {
    qpy_misc_pwm_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return MP_OBJ_NEW_SMALL_INT(qpy_pwm_result(qosa_pwm_disable((unsigned char)self->channel)));
}
static MP_DEFINE_CONST_FUN_OBJ_1(qpy_pwm_close_obj, qpy_pwm_close);

static void qpy_pwm_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    (void)kind;
    qpy_misc_pwm_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "PWM(channel:%d pin:%d high time:%d cycle_time:%d)", self->export_channel, self->pin,
        self->v2 ? 0 : self->high_time, self->v2 ? 0 : self->cycle_time);
}

static const mp_rom_map_elem_t qpy_pwm_locals_table[] = {
    { MP_ROM_QSTR(MP_QSTR_open), MP_ROM_PTR(&qpy_pwm_open_obj) },
    { MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&qpy_pwm_close_obj) },
    { MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&qpy_pwm_close_obj) },
    { MP_ROM_QSTR(MP_QSTR_ABOVE_1US), MP_ROM_INT(QPY_PWM_CYCLE_ABOVE_1US) },
    { MP_ROM_QSTR(MP_QSTR_ABOVE_MS), MP_ROM_INT(QPY_PWM_CYCLE_ABOVE_MS) },
    { MP_ROM_QSTR(MP_QSTR_ABOVE_10US), MP_ROM_INT(QPY_PWM_CYCLE_ABOVE_10US) },
    { MP_ROM_QSTR(MP_QSTR_ABOVE_BELOW_US), MP_ROM_INT(QPY_PWM_CYCLE_ABOVE_BELOW_US) },
    { MP_ROM_QSTR(MP_QSTR_PWM0), MP_ROM_INT(0) },
    { MP_ROM_QSTR(MP_QSTR_PWM1), MP_ROM_INT(1) },
    { MP_ROM_QSTR(MP_QSTR_PWM2), MP_ROM_INT(2) },
    { MP_ROM_QSTR(MP_QSTR_PWM3), MP_ROM_INT(3) },
    { MP_ROM_QSTR(MP_QSTR_PWM4), MP_ROM_INT(4) },
    { MP_ROM_QSTR(MP_QSTR_PWM5), MP_ROM_INT(5) },
    { MP_ROM_QSTR(MP_QSTR_PWM6), MP_ROM_INT(6) },
    { MP_ROM_QSTR(MP_QSTR_PWM7), MP_ROM_INT(7) },
    { MP_ROM_QSTR(MP_QSTR_PWM8), MP_ROM_INT(8) },
    { MP_ROM_QSTR(MP_QSTR_PWM9), MP_ROM_INT(9) },
    { MP_ROM_QSTR(MP_QSTR_PWM10), MP_ROM_INT(10) },
    { MP_ROM_QSTR(MP_QSTR_PWM11), MP_ROM_INT(11) },
    { MP_ROM_QSTR(MP_QSTR_PWM12), MP_ROM_INT(12) },
    { MP_ROM_QSTR(MP_QSTR_PWM13), MP_ROM_INT(13) },
    { MP_ROM_QSTR(MP_QSTR_PWM14), MP_ROM_INT(14) },
};
static MP_DEFINE_CONST_DICT(qpy_pwm_locals, qpy_pwm_locals_table);

MP_DEFINE_CONST_OBJ_TYPE(
    qpy_pwm_type,
    MP_QSTR_PWM,
    MP_TYPE_FLAG_NONE,
    print, qpy_pwm_print,
    make_new, qpy_pwm_make_new,
    locals_dict, &qpy_pwm_locals
    );

MP_DEFINE_CONST_OBJ_TYPE(
    qpy_pwm_v2_type,
    MP_QSTR_PWM_V2,
    MP_TYPE_FLAG_NONE,
    print, qpy_pwm_print,
    make_new, qpy_pwm_v2_make_new,
    locals_dict, &qpy_pwm_locals
    );
