#include <string.h>

#include "py/obj.h"
#include "py/runtime.h"
#include "py/mperrno.h"
#include "py/mphal.h"
#include "qosa_gpio.h"
#include "qosa_pinctrl.h"
#include "machine_qosa.h"
#include "qpy_gpio_map.h"

#define QPY_EXTINT_EDGE_RISING (0)
#define QPY_EXTINT_EDGE_FALLING (1)
#define QPY_EXTINT_EDGE_BOTH (2)

typedef struct _qpy_machine_extint_obj_t {
    mp_obj_base_t base;
    int export_gpio;
    qosa_gpio_num_e gpio;
    qosa_gpio_trigger_e trigger;
    qosa_gpio_pull_e pull;
    qosa_pin_num_e pin;
    mp_int_t mode;
    mp_int_t pull_value;
    mp_int_t filter_time;
    mp_obj_t callback;
    volatile int pending;
    volatile int pending_edge;
    mp_int_t rising_count;
    mp_int_t falling_count;
    bool registered;
} qpy_machine_extint_obj_t;

static qpy_machine_extint_obj_t *qpy_extint_objects[QPY_EXPORT_GPIO_COUNT];

static qosa_gpio_trigger_e qpy_extint_trigger_from_int(mp_int_t mode) {
    if (mode == QPY_EXTINT_EDGE_RISING) {
        return QOSA_GPIO_TRIGGER_RISING_EDGE;
    }
    if (mode == QPY_EXTINT_EDGE_FALLING) {
        return QOSA_GPIO_TRIGGER_FALLING_EDGE;
    }
    if (mode == QPY_EXTINT_EDGE_BOTH) {
        return QOSA_GPIO_TRIGGER_BOTH_EDGE;
    }
    mp_raise_ValueError(MP_ERROR_TEXT("invalid mode value"));
}

static qosa_gpio_pull_e qpy_extint_pull_from_int(mp_int_t pull) {
    if (pull == 0) {
        return QOSA_GPIO_PULL_NONE;
    }
    if (pull == 1) {
        return QOSA_GPIO_PULL_UP;
    }
    if (pull == 2) {
        return QOSA_GPIO_PULL_DOWN;
    }
    mp_raise_ValueError(MP_ERROR_TEXT("invalid pull value"));
}

static int qpy_extint_edge_from_level(qpy_machine_extint_obj_t *self) {
    if (self->mode == QPY_EXTINT_EDGE_RISING) {
        return QPY_EXTINT_EDGE_RISING;
    }
    if (self->mode == QPY_EXTINT_EDGE_FALLING) {
        return QPY_EXTINT_EDGE_FALLING;
    }
    qosa_gpio_level_e level = QOSA_GPIO_LEVEL_LOW;
    if (qosa_gpio_get_level(self->gpio, &level) == QOSA_GPIO_SUCCESS && level == QOSA_GPIO_LEVEL_HIGH) {
        return QPY_EXTINT_EDGE_RISING;
    }
    return QPY_EXTINT_EDGE_FALLING;
}

static void qpy_extint_irq(void *argv) {
    qpy_machine_extint_obj_t *self = (qpy_machine_extint_obj_t *)argv;
    if (self == NULL) {
        return;
    }
    int edge = qpy_extint_edge_from_level(self);
    self->pending_edge = edge;
    self->pending++;
    if (edge == QPY_EXTINT_EDGE_RISING) {
        self->rising_count++;
    } else {
        self->falling_count++;
    }
    mp_hal_stdio_wake();
}

void qpy_machine_extint_poll_pending(void) {
    for (size_t i = 0; i < MP_ARRAY_SIZE(qpy_extint_objects); i++) {
        qpy_machine_extint_obj_t *self = qpy_extint_objects[i];
        if (self == NULL || self->pending <= 0 || self->callback == mp_const_none) {
            continue;
        }
        self->pending--;
        mp_obj_t items[2] = {
            MP_OBJ_NEW_SMALL_INT(self->export_gpio),
            MP_OBJ_NEW_SMALL_INT(self->pending_edge),
        };
        mp_sched_schedule(self->callback, mp_obj_new_list(2, items));
    }
}

static int qpy_extint_register(qpy_machine_extint_obj_t *self) {
    const qpy_gpio_map_t *map = qpy_gpio_map_resolve(self->export_gpio);
    if (map == NULL || qpy_gpio_map_select(map) != 0) {
        return -1;
    }
    self->pin = (qosa_pin_num_e)map->pin;

    if (qosa_gpio_init(self->gpio, QOSA_GPIO_DIRECTION_INPUT, self->pull, QOSA_GPIO_LEVEL_LOW) != QOSA_GPIO_SUCCESS) {
        return -1;
    }

    qosa_int_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.gpio_num = self->gpio;
    cfg.gpio_debounce = self->filter_time > 0 ? QOSA_GPIO_DEBOUNCE_EN : QOSA_GPIO_DEBOUNCE_DIS;
    cfg.gpio_pull = self->pull;
    cfg.interrupt_cb = qpy_extint_irq;
    cfg.options = 1;
    cfg.user_ctx = self;
    qosa_interrupt_disable(self->gpio);
    qosa_interrupt_unregister(self->gpio);
    if (qosa_interrupt_register(&cfg) != QOSA_GPIO_SUCCESS) {
        return -1;
    }
    if (qosa_interrupt_enable(self->gpio, self->trigger) != QOSA_GPIO_SUCCESS) {
        qosa_interrupt_unregister(self->gpio);
        return -1;
    }
    self->registered = true;
    qpy_extint_objects[self->export_gpio] = self;
    return 0;
}

static const mp_arg_t qpy_extint_make_new_args[] = {
    { MP_QSTR_pin, MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0} },
    { MP_QSTR_mode, MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0} },
    { MP_QSTR_pull, MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0} },
    { MP_QSTR_callback, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
    { MP_QSTR_filter_time, MP_ARG_INT, {.u_int = 0} },
};

static mp_obj_t qpy_extint_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_val_t vals[MP_ARRAY_SIZE(qpy_extint_make_new_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, args, MP_ARRAY_SIZE(qpy_extint_make_new_args), qpy_extint_make_new_args, vals);

    mp_int_t gpio = vals[0].u_int;
    const qpy_gpio_map_t *map = qpy_gpio_map_resolve(gpio);
    if (gpio < 0 || gpio >= QPY_EXPORT_GPIO_COUNT || map == NULL) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid pin value"));
    }
    if (vals[4].u_int < 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid fliter time value"));
    }

    qpy_machine_extint_obj_t *self = qpy_extint_objects[gpio];
    if (self == NULL) {
        self = mp_obj_malloc(qpy_machine_extint_obj_t, type);
    }
    self->base.type = type;
    self->export_gpio = gpio;
    self->gpio = map->gpio;
    self->pin = (qosa_pin_num_e)map->pin;
    self->mode = vals[1].u_int;
    self->trigger = qpy_extint_trigger_from_int(self->mode);
    self->pull_value = vals[2].u_int;
    self->pull = qpy_extint_pull_from_int(self->pull_value);
    self->filter_time = vals[4].u_int;
    self->callback = vals[3].u_obj == MP_OBJ_NULL ? mp_const_none : vals[3].u_obj;
    self->pending = 0;
    self->pending_edge = QPY_EXTINT_EDGE_RISING;
    self->rising_count = 0;
    self->falling_count = 0;
    self->registered = false;

    if (qpy_extint_register(self) != 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("Interrupt initialization failed"));
    }
    return MP_OBJ_FROM_PTR(self);
}

static void qpy_extint_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    (void)kind;
    qpy_machine_extint_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "<ExtInt line=%u>", (unsigned)self->export_gpio);
}

static mp_obj_t qpy_extint_line(mp_obj_t self_in) {
    qpy_machine_extint_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return MP_OBJ_NEW_SMALL_INT(self->export_gpio);
}
static MP_DEFINE_CONST_FUN_OBJ_1(qpy_extint_line_obj, qpy_extint_line);

static mp_obj_t qpy_extint_enable(mp_obj_t self_in) {
    qpy_machine_extint_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (!self->registered && qpy_extint_register(self) != 0) {
        return MP_OBJ_NEW_SMALL_INT(-1);
    }
    return MP_OBJ_NEW_SMALL_INT(qosa_interrupt_enable(self->gpio, self->trigger) == QOSA_GPIO_SUCCESS ? 0 : -1);
}
static MP_DEFINE_CONST_FUN_OBJ_1(qpy_extint_enable_obj, qpy_extint_enable);

static mp_obj_t qpy_extint_disable(mp_obj_t self_in) {
    qpy_machine_extint_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return MP_OBJ_NEW_SMALL_INT(qosa_interrupt_disable(self->gpio) == QOSA_GPIO_SUCCESS ? 0 : -1);
}
static MP_DEFINE_CONST_FUN_OBJ_1(qpy_extint_disable_obj, qpy_extint_disable);

static mp_obj_t qpy_extint_close(mp_obj_t self_in) {
    qpy_machine_extint_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (!self->registered) {
        return MP_OBJ_NEW_SMALL_INT(0);
    }
    self->registered = false;
    self->pending = 0;
    self->rising_count = 0;
    self->falling_count = 0;
    int ret = 0;
    if (qosa_interrupt_disable(self->gpio) != QOSA_GPIO_SUCCESS) {
        ret = -1;
    }
    if (qosa_interrupt_unregister(self->gpio) != QOSA_GPIO_SUCCESS) {
        ret = -1;
    }
    return MP_OBJ_NEW_SMALL_INT(ret);
}
static MP_DEFINE_CONST_FUN_OBJ_1(qpy_extint_close_obj, qpy_extint_close);

static mp_obj_t qpy_extint_delete(mp_obj_t self_in) {
    qpy_machine_extint_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_obj_t ret = qpy_extint_close(self_in);
    if (self->export_gpio >= 0 && self->export_gpio < QPY_EXPORT_GPIO_COUNT && qpy_extint_objects[self->export_gpio] == self) {
        qpy_extint_objects[self->export_gpio] = NULL;
    }
    return ret;
}
static MP_DEFINE_CONST_FUN_OBJ_1(qpy_extint_delete_obj, qpy_extint_delete);

static mp_obj_t qpy_extint_read_level(mp_obj_t self_in) {
    qpy_machine_extint_obj_t *self = MP_OBJ_TO_PTR(self_in);
    qosa_gpio_level_e level = QOSA_GPIO_LEVEL_LOW;
    if (qosa_gpio_get_level(self->gpio, &level) != QOSA_GPIO_SUCCESS) {
        return MP_OBJ_NEW_SMALL_INT(-1);
    }
    return MP_OBJ_NEW_SMALL_INT(level == QOSA_GPIO_LEVEL_HIGH ? 1 : 0);
}
static MP_DEFINE_CONST_FUN_OBJ_1(qpy_extint_read_level_obj, qpy_extint_read_level);

static mp_obj_t qpy_extint_read_count(mp_obj_t self_in, mp_obj_t reset_in) {
    qpy_machine_extint_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_int_t reset = mp_obj_get_int(reset_in);
    if (reset != 0 && reset != 1) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid is_reset value, must in [0,1]"));
    }
    mp_obj_t items[2] = {
        MP_OBJ_NEW_SMALL_INT(self->rising_count),
        MP_OBJ_NEW_SMALL_INT(self->falling_count),
    };
    if (reset) {
        self->rising_count = 0;
        self->falling_count = 0;
    }
    return mp_obj_new_list(2, items);
}
static MP_DEFINE_CONST_FUN_OBJ_2(qpy_extint_read_count_obj, qpy_extint_read_count);

static mp_obj_t qpy_extint_count_reset(mp_obj_t self_in) {
    qpy_machine_extint_obj_t *self = MP_OBJ_TO_PTR(self_in);
    self->rising_count = 0;
    self->falling_count = 0;
    return MP_OBJ_NEW_SMALL_INT(0);
}
static MP_DEFINE_CONST_FUN_OBJ_1(qpy_extint_count_reset_obj, qpy_extint_count_reset);

#define QPY_EXTINT_GPIO_ENTRY(n) { MP_ROM_QSTR(MP_QSTR_GPIO##n), MP_ROM_INT(n) }
static const mp_rom_map_elem_t qpy_extint_locals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&qpy_extint_delete_obj) },
    { MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&qpy_extint_close_obj) },
    { MP_ROM_QSTR(MP_QSTR_line), MP_ROM_PTR(&qpy_extint_line_obj) },
    { MP_ROM_QSTR(MP_QSTR_enable), MP_ROM_PTR(&qpy_extint_enable_obj) },
    { MP_ROM_QSTR(MP_QSTR_disable), MP_ROM_PTR(&qpy_extint_disable_obj) },
    { MP_ROM_QSTR(MP_QSTR_read_count), MP_ROM_PTR(&qpy_extint_read_count_obj) },
    { MP_ROM_QSTR(MP_QSTR_count_reset), MP_ROM_PTR(&qpy_extint_count_reset_obj) },
    { MP_ROM_QSTR(MP_QSTR_read_level), MP_ROM_PTR(&qpy_extint_read_level_obj) },
    { MP_ROM_QSTR(MP_QSTR_IRQ_RISING), MP_ROM_INT(QPY_EXTINT_EDGE_RISING) },
    { MP_ROM_QSTR(MP_QSTR_IRQ_FALLING), MP_ROM_INT(QPY_EXTINT_EDGE_FALLING) },
    { MP_ROM_QSTR(MP_QSTR_IRQ_RISING_FALLING), MP_ROM_INT(QPY_EXTINT_EDGE_BOTH) },
    { MP_ROM_QSTR(MP_QSTR_PULL_DISABLE), MP_ROM_INT(0) },
    { MP_ROM_QSTR(MP_QSTR_PULL_PU), MP_ROM_INT(1) },
    { MP_ROM_QSTR(MP_QSTR_PULL_PD), MP_ROM_INT(2) },
    QPY_EXTINT_GPIO_ENTRY(0), QPY_EXTINT_GPIO_ENTRY(1), QPY_EXTINT_GPIO_ENTRY(2), QPY_EXTINT_GPIO_ENTRY(3),
    QPY_EXTINT_GPIO_ENTRY(4), QPY_EXTINT_GPIO_ENTRY(5), QPY_EXTINT_GPIO_ENTRY(6), QPY_EXTINT_GPIO_ENTRY(7),
    QPY_EXTINT_GPIO_ENTRY(8), QPY_EXTINT_GPIO_ENTRY(9), QPY_EXTINT_GPIO_ENTRY(10), QPY_EXTINT_GPIO_ENTRY(11),
    QPY_EXTINT_GPIO_ENTRY(12), QPY_EXTINT_GPIO_ENTRY(13), QPY_EXTINT_GPIO_ENTRY(14), QPY_EXTINT_GPIO_ENTRY(15),
    QPY_EXTINT_GPIO_ENTRY(16), QPY_EXTINT_GPIO_ENTRY(17), QPY_EXTINT_GPIO_ENTRY(18), QPY_EXTINT_GPIO_ENTRY(19),
    QPY_EXTINT_GPIO_ENTRY(20), QPY_EXTINT_GPIO_ENTRY(21), QPY_EXTINT_GPIO_ENTRY(22), QPY_EXTINT_GPIO_ENTRY(23),
    QPY_EXTINT_GPIO_ENTRY(24), QPY_EXTINT_GPIO_ENTRY(25), QPY_EXTINT_GPIO_ENTRY(26), QPY_EXTINT_GPIO_ENTRY(27),
    QPY_EXTINT_GPIO_ENTRY(28), QPY_EXTINT_GPIO_ENTRY(29), QPY_EXTINT_GPIO_ENTRY(30), QPY_EXTINT_GPIO_ENTRY(31),
    QPY_EXTINT_GPIO_ENTRY(32), QPY_EXTINT_GPIO_ENTRY(33), QPY_EXTINT_GPIO_ENTRY(34), QPY_EXTINT_GPIO_ENTRY(35),
    QPY_EXTINT_GPIO_ENTRY(36), QPY_EXTINT_GPIO_ENTRY(37), QPY_EXTINT_GPIO_ENTRY(38), QPY_EXTINT_GPIO_ENTRY(39),
    QPY_EXTINT_GPIO_ENTRY(40), QPY_EXTINT_GPIO_ENTRY(41), QPY_EXTINT_GPIO_ENTRY(42), QPY_EXTINT_GPIO_ENTRY(43),
};
static MP_DEFINE_CONST_DICT(qpy_extint_locals_dict, qpy_extint_locals_table);

MP_DEFINE_CONST_OBJ_TYPE(
    qpy_machine_extint_type,
    MP_QSTR_ExtInt,
    MP_TYPE_FLAG_NONE,
    print, qpy_extint_print,
    make_new, qpy_extint_make_new,
    locals_dict, &qpy_extint_locals_dict
    );
