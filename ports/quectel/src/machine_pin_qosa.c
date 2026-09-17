#include "py/obj.h"
#include "py/runtime.h"
#include "qosa_gpio.h"
#include "qosa_sys.h"

#include "machine_qosa.h"
#include "qpy_gpio_map.h"

typedef struct _qpy_machine_pin_obj_t {
    mp_obj_base_t base;
    int export_gpio;
    qosa_gpio_num_e gpio;
    qosa_gpio_direction_e dir;
    qosa_gpio_pull_e pull;
    qosa_gpio_level_e level;
    qosa_timer_t timer;
} qpy_machine_pin_obj_t;

extern const mp_obj_type_t qpy_machine_pin_type;
static qpy_machine_pin_obj_t *qpy_pin_objects[QPY_EXPORT_GPIO_COUNT];

static void qpy_pin_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    (void)kind;
    qpy_machine_pin_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "<GPIO(%u)>", (unsigned)self->export_gpio);
}

static qosa_gpio_direction_e qpy_pin_dir_from_int(mp_int_t dir) {
    if (dir == 0) {
        return QOSA_GPIO_DIRECTION_INPUT;
    }
    if (dir == 1) {
        return QOSA_GPIO_DIRECTION_OUTPUT;
    }
    mp_raise_ValueError(MP_ERROR_TEXT("invalid pin dir, range{0:dir in, 1:dir out}"));
}

static qosa_gpio_pull_e qpy_pin_pull_from_int(mp_int_t pull) {
    if (pull == 0) {
        return QOSA_GPIO_PULL_NONE;
    }
    if (pull == 1) {
        return QOSA_GPIO_PULL_UP;
    }
    if (pull == 2) {
        return QOSA_GPIO_PULL_DOWN;
    }
    mp_raise_ValueError(MP_ERROR_TEXT("invalid pin pull, range{0:PIN_PULL_DISABLE, 1:PIN_PULL_PU, 2:PIN_PULL_PD}"));
}

static void qpy_pin_init_helper(qpy_machine_pin_obj_t *self, size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_dir, ARG_pull, ARG_value };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_dir, MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_pull, MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_value, MP_ARG_INT, {.u_int = -1} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    if (args[ARG_dir].u_int != -1) {
        self->dir = qpy_pin_dir_from_int(args[ARG_dir].u_int);
    }
    if (args[ARG_pull].u_int != -1) {
        self->pull = qpy_pin_pull_from_int(args[ARG_pull].u_int);
    }
    if (args[ARG_value].u_int != -1) {
        if (args[ARG_value].u_int != 0 && args[ARG_value].u_int != 1) {
            mp_raise_ValueError(MP_ERROR_TEXT("invalid pin value, range{0:PIN_LEVEL_LOW, 1:PIN_LEVEL_HIGH}"));
        }
        self->level = args[ARG_value].u_int ? QOSA_GPIO_LEVEL_HIGH : QOSA_GPIO_LEVEL_LOW;
    }
    if (qpy_gpio_map_select(qpy_gpio_map_resolve(self->export_gpio)) != 0
        || qosa_gpio_init(self->gpio, self->dir, self->pull, self->level) != QOSA_GPIO_SUCCESS) {
        mp_raise_ValueError(MP_ERROR_TEXT("GPIO initialization failed"));
    }
}

static mp_obj_t qpy_pin_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args) {
    mp_arg_check_num(n_args, n_kw, 1, MP_OBJ_FUN_ARGS_MAX, true);
    mp_int_t gpio = mp_obj_get_int(all_args[0]);
    const qpy_gpio_map_t *map = qpy_gpio_map_resolve(gpio);
    if (gpio < 0 || gpio >= QPY_EXPORT_GPIO_COUNT || map == NULL) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid pin"));
    }

    qpy_machine_pin_obj_t *self = qpy_pin_objects[gpio];
    if (self == NULL) {
        self = mp_obj_malloc_with_finaliser(qpy_machine_pin_obj_t, type);
        qpy_pin_objects[gpio] = self;
    }
    self->export_gpio = gpio;
    self->gpio = map->gpio;
    self->dir = QOSA_GPIO_DIRECTION_OUTPUT;
    self->pull = QOSA_GPIO_PULL_NONE;
    self->level = QOSA_GPIO_LEVEL_LOW;
    self->timer = QOSA_NULL;

    if (n_args > 1 || n_kw > 0) {
        mp_map_t kw_args;
        mp_map_init_fixed_table(&kw_args, n_kw, all_args + n_args);
        qpy_pin_init_helper(self, n_args - 1, all_args + 1, &kw_args);
    } else if (qpy_gpio_map_select(map) != 0
        || qosa_gpio_init(self->gpio, self->dir, self->pull, self->level) != QOSA_GPIO_SUCCESS) {
        mp_raise_ValueError(MP_ERROR_TEXT("GPIO initialization failed"));
    }
    return MP_OBJ_FROM_PTR(self);
}

static mp_obj_t qpy_pin_call(mp_obj_t self_in, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 0, 1, false);
    qpy_machine_pin_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (n_args == 0) {
        // UniRTOS does not reliably read back the output latch on all GPIOs.
        // The Helios API returns the output level after on/off/write.
        if (self->dir == QOSA_GPIO_DIRECTION_OUTPUT) {
            return MP_OBJ_NEW_SMALL_INT(self->level == QOSA_GPIO_LEVEL_HIGH ? 1 : 0);
        }
        qosa_gpio_level_e level = QOSA_GPIO_LEVEL_LOW;
        if (qosa_gpio_get_level(self->gpio, &level) != QOSA_GPIO_SUCCESS) {
            return MP_OBJ_NEW_SMALL_INT(-1);
        }
        self->level = level;
        return MP_OBJ_NEW_SMALL_INT(level == QOSA_GPIO_LEVEL_HIGH ? 1 : 0);
    }
    qosa_gpio_level_e level = mp_obj_is_true(args[0]) ? QOSA_GPIO_LEVEL_HIGH : QOSA_GPIO_LEVEL_LOW;
    if (qosa_gpio_set_level(self->gpio, level) == QOSA_GPIO_SUCCESS) {
        self->level = level;
    }
    return mp_const_none;
}

static mp_obj_t qpy_pin_init(size_t n_args, const mp_obj_t *args, mp_map_t *kw_args) {
    qpy_pin_init_helper(MP_OBJ_TO_PTR(args[0]), n_args - 1, args + 1, kw_args);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(qpy_pin_init_obj, 1, qpy_pin_init);

static mp_obj_t qpy_pin_value(size_t n_args, const mp_obj_t *args) {
    return qpy_pin_call(args[0], n_args - 1, 0, args + 1);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_pin_value_obj, 1, 2, qpy_pin_value);

static mp_obj_t qpy_pin_on(mp_obj_t self_in) {
    qpy_machine_pin_obj_t *self = MP_OBJ_TO_PTR(self_in);
    qosa_gpio_set_level(self->gpio, QOSA_GPIO_LEVEL_HIGH);
    self->level = QOSA_GPIO_LEVEL_HIGH;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(qpy_pin_on_obj, qpy_pin_on);

static mp_obj_t qpy_pin_off(mp_obj_t self_in) {
    qpy_machine_pin_obj_t *self = MP_OBJ_TO_PTR(self_in);
    qosa_gpio_set_level(self->gpio, QOSA_GPIO_LEVEL_LOW);
    self->level = QOSA_GPIO_LEVEL_LOW;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(qpy_pin_off_obj, qpy_pin_off);

static mp_obj_t qpy_pin_write(mp_obj_t self_in, mp_obj_t value_in) {
    qpy_machine_pin_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_int_t value = mp_obj_get_int(value_in);
    int ret = qosa_gpio_set_level(self->gpio, (qosa_gpio_level_e)value);
    if (ret == QOSA_GPIO_SUCCESS) {
        self->level = value ? QOSA_GPIO_LEVEL_HIGH : QOSA_GPIO_LEVEL_LOW;
    }
    return mp_obj_new_int(ret);
}
static MP_DEFINE_CONST_FUN_OBJ_2(qpy_pin_write_obj, qpy_pin_write);

static mp_obj_t qpy_pin_read(size_t n_args, const mp_obj_t *args) {
    return qpy_pin_call(args[0], n_args - 1, 0, args + 1);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_pin_read_obj, 1, 2, qpy_pin_read);

static mp_obj_t qpy_pin_set_dir(mp_obj_t self_in, mp_obj_t dir_in) {
    qpy_machine_pin_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_int_t dir = mp_obj_get_int(dir_in);
    if (dir != 0 && dir != 1) {
        mp_raise_ValueError(MP_ERROR_TEXT("Invalid direction parameter"));
    }
    int ret = qosa_gpio_set_direction(self->gpio, qpy_pin_dir_from_int(dir));
    if (ret == QOSA_GPIO_SUCCESS) {
        self->dir = qpy_pin_dir_from_int(dir);
    }
    return mp_obj_new_int(ret);
}
static MP_DEFINE_CONST_FUN_OBJ_2(qpy_pin_set_dir_obj, qpy_pin_set_dir);

static mp_obj_t qpy_pin_get_dir(mp_obj_t self_in) {
    qpy_machine_pin_obj_t *self = MP_OBJ_TO_PTR(self_in);
    qosa_gpio_direction_e dir = QOSA_GPIO_DIRECTION_INPUT;
    int ret = qosa_gpio_get_direction(self->gpio, &dir);
    if (ret == QOSA_GPIO_SUCCESS) {
        self->dir = dir;
        return mp_obj_new_int(dir == QOSA_GPIO_DIRECTION_OUTPUT ? 1 : 0);
    }
    return mp_obj_new_int(ret);
}
static MP_DEFINE_CONST_FUN_OBJ_1(qpy_pin_get_dir_obj, qpy_pin_get_dir);

static void qpy_pin_blink_cb(void *arg) {
    qpy_machine_pin_obj_t *self = arg;
    qosa_gpio_level_e next = self->level == QOSA_GPIO_LEVEL_HIGH ? QOSA_GPIO_LEVEL_LOW : QOSA_GPIO_LEVEL_HIGH;
    if (qosa_gpio_set_level(self->gpio, next) == QOSA_GPIO_SUCCESS) {
        self->level = next;
    }
}

static mp_obj_t qpy_pin_blink(size_t n_args, const mp_obj_t *args) {
    qpy_machine_pin_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    mp_int_t blink_switch = n_args > 1 ? mp_obj_get_int(args[1]) : 1;
    mp_int_t period = n_args > 2 ? mp_obj_get_int(args[2]) : 500;
    if (self->dir == QOSA_GPIO_DIRECTION_INPUT) {
        mp_raise_ValueError(MP_ERROR_TEXT("The use of this interface needs to set gpio to output mode"));
    }
    if (blink_switch != 0 && blink_switch != 1) {
        mp_raise_ValueError(MP_ERROR_TEXT("The switch parameter is wrongly passed. range [0,1]"));
    }
    if (blink_switch == 0) {
        if (self->timer != QOSA_NULL) {
            qosa_timer_stop(self->timer);
            qosa_timer_delete(self->timer);
            self->timer = QOSA_NULL;
        }
        return mp_obj_new_int(0);
    }
    if (self->timer == QOSA_NULL && qosa_timer_create(&self->timer, qpy_pin_blink_cb, self) != QOSA_ERROR_OK) {
        return mp_obj_new_int(-1);
    }
    qpy_pin_blink_cb(self);
    if (qosa_timer_start(self->timer, (qosa_uint32_t)period, 1) != QOSA_ERROR_OK) {
        return mp_obj_new_int(-1);
    }
    return mp_obj_new_int(0);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_pin_blink_obj, 1, 3, qpy_pin_blink);

static mp_obj_t qpy_pin_deinit(mp_obj_t self_in) {
    qpy_machine_pin_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->timer != QOSA_NULL) {
        qosa_timer_stop(self->timer);
        qosa_timer_delete(self->timer);
        self->timer = QOSA_NULL;
    }
    if (self->export_gpio >= 0 && self->export_gpio < QPY_EXPORT_GPIO_COUNT) {
        qpy_pin_objects[self->export_gpio] = NULL;
    }
    return mp_obj_new_int(qosa_gpio_deinit(self->gpio) == QOSA_GPIO_SUCCESS ? 0 : -1);
}
static MP_DEFINE_CONST_FUN_OBJ_1(qpy_pin_deinit_obj, qpy_pin_deinit);

#define QPY_PIN_GPIO_ENTRY(n) { MP_ROM_QSTR(MP_QSTR_GPIO##n), MP_ROM_INT(n) }
static const mp_rom_map_elem_t qpy_pin_locals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&qpy_pin_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&qpy_pin_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_value), MP_ROM_PTR(&qpy_pin_value_obj) },
    { MP_ROM_QSTR(MP_QSTR_off), MP_ROM_PTR(&qpy_pin_off_obj) },
    { MP_ROM_QSTR(MP_QSTR_on), MP_ROM_PTR(&qpy_pin_on_obj) },
    { MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&qpy_pin_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_read), MP_ROM_PTR(&qpy_pin_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_dir), MP_ROM_PTR(&qpy_pin_set_dir_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_dir), MP_ROM_PTR(&qpy_pin_get_dir_obj) },
    { MP_ROM_QSTR(MP_QSTR_blink), MP_ROM_PTR(&qpy_pin_blink_obj) },
    { MP_ROM_QSTR(MP_QSTR_IN), MP_ROM_INT(0) },
    { MP_ROM_QSTR(MP_QSTR_OUT), MP_ROM_INT(1) },
    { MP_ROM_QSTR(MP_QSTR_PULL_PU), MP_ROM_INT(1) },
    { MP_ROM_QSTR(MP_QSTR_PULL_PD), MP_ROM_INT(2) },
    { MP_ROM_QSTR(MP_QSTR_PULL_DISABLE), MP_ROM_INT(0) },
    { MP_ROM_QSTR(MP_QSTR_BLINK_ON), MP_ROM_INT(1) },
    { MP_ROM_QSTR(MP_QSTR_BLINK_OFF), MP_ROM_INT(0) },
    QPY_PIN_GPIO_ENTRY(0), QPY_PIN_GPIO_ENTRY(1), QPY_PIN_GPIO_ENTRY(2), QPY_PIN_GPIO_ENTRY(3), QPY_PIN_GPIO_ENTRY(4),
    QPY_PIN_GPIO_ENTRY(5), QPY_PIN_GPIO_ENTRY(6), QPY_PIN_GPIO_ENTRY(7), QPY_PIN_GPIO_ENTRY(8),
    QPY_PIN_GPIO_ENTRY(9), QPY_PIN_GPIO_ENTRY(10), QPY_PIN_GPIO_ENTRY(11), QPY_PIN_GPIO_ENTRY(12),
    QPY_PIN_GPIO_ENTRY(13), QPY_PIN_GPIO_ENTRY(14), QPY_PIN_GPIO_ENTRY(15), QPY_PIN_GPIO_ENTRY(16),
    QPY_PIN_GPIO_ENTRY(17), QPY_PIN_GPIO_ENTRY(18), QPY_PIN_GPIO_ENTRY(19), QPY_PIN_GPIO_ENTRY(20),
    QPY_PIN_GPIO_ENTRY(21), QPY_PIN_GPIO_ENTRY(22), QPY_PIN_GPIO_ENTRY(23), QPY_PIN_GPIO_ENTRY(24),
    QPY_PIN_GPIO_ENTRY(25), QPY_PIN_GPIO_ENTRY(26), QPY_PIN_GPIO_ENTRY(27), QPY_PIN_GPIO_ENTRY(28),
    QPY_PIN_GPIO_ENTRY(29), QPY_PIN_GPIO_ENTRY(30), QPY_PIN_GPIO_ENTRY(31), QPY_PIN_GPIO_ENTRY(32),
    QPY_PIN_GPIO_ENTRY(33), QPY_PIN_GPIO_ENTRY(34), QPY_PIN_GPIO_ENTRY(35), QPY_PIN_GPIO_ENTRY(36),
    QPY_PIN_GPIO_ENTRY(37), QPY_PIN_GPIO_ENTRY(38), QPY_PIN_GPIO_ENTRY(39), QPY_PIN_GPIO_ENTRY(40),
    QPY_PIN_GPIO_ENTRY(41), QPY_PIN_GPIO_ENTRY(42), QPY_PIN_GPIO_ENTRY(43),
};
static MP_DEFINE_CONST_DICT(qpy_pin_locals_dict, qpy_pin_locals_table);

MP_DEFINE_CONST_OBJ_TYPE(
    qpy_machine_pin_type,
    MP_QSTR_Pin,
    MP_TYPE_FLAG_NONE,
    print, qpy_pin_print,
    make_new, qpy_pin_make_new,
    call, qpy_pin_call,
    locals_dict, &qpy_pin_locals_dict
    );
