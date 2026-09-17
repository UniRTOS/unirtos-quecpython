#include <string.h>

#include "py/obj.h"
#include "py/runtime.h"
#include "py/mphal.h"
#include "qosa_gpio.h"
#include "qosa_pinctrl.h"
#include "qosa_sys.h"
#include "machine_qosa.h"
#include "qpy_gpio_map.h"

#define QPY_KEY_EVENT_SHORT_PRESS (1)
#define QPY_KEY_EVENT_DOUBLE_CLICK (2)
#define QPY_KEY_EVENT_LONG_PRESS (3)

#define QPY_KEY_STATUS_INIT (0)
#define QPY_KEY_STATUS_PRESS_FIRST (1)
#define QPY_KEY_STATUS_WAIT_DOUBLE_CLICK (2)
#define QPY_KEY_TIMER_MS (10)

typedef struct _qpy_machine_key_obj_t {
    mp_obj_base_t base;
    int export_gpio;
    qosa_gpio_num_e gpio;
    qosa_pin_num_e pin;
    qosa_gpio_pull_e pull;
    qosa_gpio_level_e press_level;
    mp_int_t timer_short_press;
    mp_int_t timer_long_press;
    mp_int_t timer_double_click_int;
    mp_obj_t callback;
    qosa_timer_t timer;
    volatile int pending_edge;
    volatile int pending_timer;
    volatile int pending_event;
    volatile int pending_event_count;
    int press_time;
    int wait_double_click_time;
    int status;
    bool registered;
} qpy_machine_key_obj_t;

static qpy_machine_key_obj_t *qpy_key_objects[QPY_EXPORT_GPIO_COUNT];

static qosa_gpio_pull_e qpy_key_pull_from_int(mp_int_t pull) {
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

static void qpy_key_queue_event(qpy_machine_key_obj_t *self, int event) {
    self->pending_event = event;
    self->pending_event_count++;
    mp_hal_stdio_wake();
}

static void qpy_key_timer_cb(void *argument) {
    qpy_machine_key_obj_t *self = (qpy_machine_key_obj_t *)argument;
    if (self != NULL) {
        self->pending_timer++;
        mp_hal_stdio_wake();
    }
}

static void qpy_key_irq(void *argv) {
    qpy_machine_key_obj_t *self = (qpy_machine_key_obj_t *)argv;
    if (self != NULL) {
        self->pending_edge++;
        mp_hal_stdio_wake();
    }
}

static void qpy_key_stop_timer(qpy_machine_key_obj_t *self) {
    if (self->timer != QOSA_NULL) {
        qosa_timer_stop(self->timer);
    }
}

static void qpy_key_start_timer(qpy_machine_key_obj_t *self) {
    if (self->timer == QOSA_NULL) {
        qosa_timer_create(&self->timer, qpy_key_timer_cb, self);
    }
    if (self->timer != QOSA_NULL) {
        qosa_timer_start(self->timer, QPY_KEY_TIMER_MS, QOSA_TRUE);
    }
}

static void qpy_key_reset_state(qpy_machine_key_obj_t *self) {
    self->status = QPY_KEY_STATUS_INIT;
    self->press_time = 0;
    self->wait_double_click_time = 0;
    qpy_key_stop_timer(self);
}

static void qpy_key_handle_timer(qpy_machine_key_obj_t *self) {
    if (self->status == QPY_KEY_STATUS_PRESS_FIRST) {
        self->press_time++;
        if (self->press_time * QPY_KEY_TIMER_MS >= self->timer_long_press * 1000) {
            qpy_key_queue_event(self, QPY_KEY_EVENT_LONG_PRESS);
            qpy_key_reset_state(self);
        }
    } else if (self->status == QPY_KEY_STATUS_WAIT_DOUBLE_CLICK) {
        self->wait_double_click_time++;
        if (self->wait_double_click_time * QPY_KEY_TIMER_MS > self->timer_double_click_int) {
            qpy_key_queue_event(self, QPY_KEY_EVENT_SHORT_PRESS);
            qpy_key_reset_state(self);
        }
    }
}

static void qpy_key_handle_edge(qpy_machine_key_obj_t *self) {
    qosa_gpio_level_e level = QOSA_GPIO_LEVEL_LOW;
    if (qosa_gpio_get_level(self->gpio, &level) != QOSA_GPIO_SUCCESS) {
        return;
    }
    bool pressed = level == self->press_level;
    if (pressed) {
        if (self->status == QPY_KEY_STATUS_INIT) {
            self->press_time = 0;
            self->wait_double_click_time = 0;
            self->status = QPY_KEY_STATUS_PRESS_FIRST;
            qpy_key_start_timer(self);
        } else if (self->status == QPY_KEY_STATUS_WAIT_DOUBLE_CLICK && self->wait_double_click_time >= 2) {
            qpy_key_queue_event(self, QPY_KEY_EVENT_DOUBLE_CLICK);
            qpy_key_reset_state(self);
        }
    } else if (self->status == QPY_KEY_STATUS_PRESS_FIRST) {
        int elapsed = self->press_time * QPY_KEY_TIMER_MS;
        if (elapsed >= self->timer_short_press && elapsed <= self->timer_long_press * 1000) {
            self->status = QPY_KEY_STATUS_WAIT_DOUBLE_CLICK;
            self->wait_double_click_time = 0;
        } else if (elapsed < self->timer_short_press) {
            qpy_key_reset_state(self);
        }
    }
}

void qpy_machine_key_poll_pending(void) {
    for (size_t i = 0; i < MP_ARRAY_SIZE(qpy_key_objects); i++) {
        qpy_machine_key_obj_t *self = qpy_key_objects[i];
        if (self == NULL) {
            continue;
        }
        while (self->pending_edge > 0) {
            self->pending_edge--;
            qpy_key_handle_edge(self);
        }
        while (self->pending_timer > 0) {
            self->pending_timer--;
            qpy_key_handle_timer(self);
        }
        while (self->pending_event_count > 0) {
            self->pending_event_count--;
            if (self->callback != MP_OBJ_NULL && self->callback != mp_const_none) {
                mp_obj_t items[2] = {
                    MP_OBJ_NEW_SMALL_INT(self->export_gpio),
                    MP_OBJ_NEW_SMALL_INT(self->pending_event),
                };
                mp_sched_schedule(self->callback, mp_obj_new_tuple(2, items));
            }
        }
    }
}

static int qpy_key_register(qpy_machine_key_obj_t *self) {
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
    cfg.gpio_debounce = QOSA_GPIO_DEBOUNCE_EN;
    cfg.gpio_pull = self->pull;
    cfg.interrupt_cb = qpy_key_irq;
    cfg.options = 1;
    cfg.user_ctx = self;
    qosa_interrupt_disable(self->gpio);
    qosa_interrupt_unregister(self->gpio);
    if (qosa_interrupt_register(&cfg) != QOSA_GPIO_SUCCESS) {
        return -1;
    }
    if (qosa_interrupt_enable(self->gpio, QOSA_GPIO_TRIGGER_BOTH_EDGE) != QOSA_GPIO_SUCCESS) {
        qosa_interrupt_unregister(self->gpio);
        return -1;
    }
    self->registered = true;
    qpy_key_objects[self->export_gpio] = self;
    return 0;
}

static const mp_arg_t qpy_key_make_new_args[] = {
    { MP_QSTR_gpio, MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0xff} },
    { MP_QSTR_pull, MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0} },
    { MP_QSTR_press_lvl, MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0} },
    { MP_QSTR_timer_long_press, MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0} },
    { MP_QSTR_timer_double_click_int, MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0} },
    { MP_QSTR_callback, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
    { MP_QSTR_timer_short_press, MP_ARG_INT, {.u_int = 50} },
};

static mp_obj_t qpy_key_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_val_t vals[MP_ARRAY_SIZE(qpy_key_make_new_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, args, MP_ARRAY_SIZE(qpy_key_make_new_args), qpy_key_make_new_args, vals);

    mp_int_t gpio = vals[0].u_int;
    const qpy_gpio_map_t *map = qpy_gpio_map_resolve(gpio);
    if (gpio < 0 || gpio >= QPY_EXPORT_GPIO_COUNT || map == NULL) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid gpio value"));
    }
    mp_int_t press_lvl = vals[2].u_int;
    if (press_lvl != 0 && press_lvl != 1) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid press value"));
    }
    mp_int_t short_ms = vals[6].u_int;
    mp_int_t long_s = vals[3].u_int;
    if (short_ms < 30) {
        short_ms = 50;
    }
    if (long_s > 10 || long_s * 1000 <= short_ms) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid long press timer"));
    }
    mp_int_t double_ms = vals[4].u_int;
    if (double_ms > 1000 || double_ms <= 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid double click int timer"));
    }
    mp_obj_t callback = vals[5].u_obj;
    if (callback != mp_const_none && !mp_obj_is_callable(callback)) {
        mp_raise_TypeError(MP_ERROR_TEXT("callback must be callable"));
    }

    qpy_machine_key_obj_t *self = qpy_key_objects[gpio];
    if (self == NULL) {
        self = mp_obj_malloc(qpy_machine_key_obj_t, type);
    }
    self->base.type = type;
    self->export_gpio = gpio;
    self->gpio = map->gpio;
    self->pin = (qosa_pin_num_e)map->pin;
    self->pull = qpy_key_pull_from_int(vals[1].u_int);
    self->press_level = press_lvl ? QOSA_GPIO_LEVEL_HIGH : QOSA_GPIO_LEVEL_LOW;
    self->timer_short_press = short_ms;
    self->timer_long_press = long_s;
    self->timer_double_click_int = double_ms;
    self->callback = callback;
    self->timer = QOSA_NULL;
    self->pending_edge = 0;
    self->pending_timer = 0;
    self->pending_event = 0;
    self->pending_event_count = 0;
    self->registered = false;
    qpy_key_reset_state(self);

    if (qpy_key_register(self) != 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("Interrupt initialization failed"));
    }
    return MP_OBJ_FROM_PTR(self);
}

static void qpy_key_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    (void)kind;
    qpy_machine_key_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "<Key GPIO=%u>", (unsigned)self->export_gpio);
}

static mp_obj_t qpy_key_enable(mp_obj_t self_in) {
    qpy_machine_key_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (!self->registered && qpy_key_register(self) != 0) {
        return MP_OBJ_NEW_SMALL_INT(-1);
    }
    return MP_OBJ_NEW_SMALL_INT(qosa_interrupt_enable(self->gpio, QOSA_GPIO_TRIGGER_BOTH_EDGE) == QOSA_GPIO_SUCCESS ? 0 : -1);
}
static MP_DEFINE_CONST_FUN_OBJ_1(qpy_key_enable_obj, qpy_key_enable);

static mp_obj_t qpy_key_disable(mp_obj_t self_in) {
    qpy_machine_key_obj_t *self = MP_OBJ_TO_PTR(self_in);
    qpy_key_reset_state(self);
    return MP_OBJ_NEW_SMALL_INT(qosa_interrupt_disable(self->gpio) == QOSA_GPIO_SUCCESS ? 0 : -1);
}
static MP_DEFINE_CONST_FUN_OBJ_1(qpy_key_disable_obj, qpy_key_disable);

static mp_obj_t qpy_key_deinit(mp_obj_t self_in) {
    qpy_machine_key_obj_t *self = MP_OBJ_TO_PTR(self_in);
    qpy_key_reset_state(self);
    if (self->timer != QOSA_NULL) {
        qosa_timer_delete(self->timer);
        self->timer = QOSA_NULL;
    }
    qpy_key_objects[self->export_gpio] = NULL;
    self->registered = false;
    int ret = 0;
    if (qosa_interrupt_disable(self->gpio) != QOSA_GPIO_SUCCESS) {
        ret = -1;
    }
    if (qosa_interrupt_unregister(self->gpio) != QOSA_GPIO_SUCCESS) {
        ret = -1;
    }
    return MP_OBJ_NEW_SMALL_INT(ret);
}
static MP_DEFINE_CONST_FUN_OBJ_1(qpy_key_deinit_obj, qpy_key_deinit);

#define QPY_KEY_GPIO_ENTRY(n) { MP_ROM_QSTR(MP_QSTR_GPIO##n), MP_ROM_INT(n) }
static const mp_rom_map_elem_t qpy_key_locals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&qpy_key_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&qpy_key_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR_deinit), MP_ROM_PTR(&qpy_key_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR_enable), MP_ROM_PTR(&qpy_key_enable_obj) },
    { MP_ROM_QSTR(MP_QSTR_disable), MP_ROM_PTR(&qpy_key_disable_obj) },
    { MP_ROM_QSTR(MP_QSTR_HIGH), MP_ROM_INT(1) },
    { MP_ROM_QSTR(MP_QSTR_LOW), MP_ROM_INT(0) },
    { MP_ROM_QSTR(MP_QSTR_PULL_PU), MP_ROM_INT(1) },
    { MP_ROM_QSTR(MP_QSTR_PULL_PD), MP_ROM_INT(2) },
    { MP_ROM_QSTR(MP_QSTR_PULL_DISABLE), MP_ROM_INT(0) },
    QPY_KEY_GPIO_ENTRY(0), QPY_KEY_GPIO_ENTRY(1), QPY_KEY_GPIO_ENTRY(2), QPY_KEY_GPIO_ENTRY(3),
    QPY_KEY_GPIO_ENTRY(4), QPY_KEY_GPIO_ENTRY(5), QPY_KEY_GPIO_ENTRY(6), QPY_KEY_GPIO_ENTRY(7),
    QPY_KEY_GPIO_ENTRY(8), QPY_KEY_GPIO_ENTRY(9), QPY_KEY_GPIO_ENTRY(10), QPY_KEY_GPIO_ENTRY(11),
    QPY_KEY_GPIO_ENTRY(12), QPY_KEY_GPIO_ENTRY(13), QPY_KEY_GPIO_ENTRY(14), QPY_KEY_GPIO_ENTRY(15),
    QPY_KEY_GPIO_ENTRY(16), QPY_KEY_GPIO_ENTRY(17), QPY_KEY_GPIO_ENTRY(18), QPY_KEY_GPIO_ENTRY(19),
    QPY_KEY_GPIO_ENTRY(20), QPY_KEY_GPIO_ENTRY(21), QPY_KEY_GPIO_ENTRY(22), QPY_KEY_GPIO_ENTRY(23),
    QPY_KEY_GPIO_ENTRY(24), QPY_KEY_GPIO_ENTRY(25), QPY_KEY_GPIO_ENTRY(26), QPY_KEY_GPIO_ENTRY(27),
    QPY_KEY_GPIO_ENTRY(28), QPY_KEY_GPIO_ENTRY(29), QPY_KEY_GPIO_ENTRY(30), QPY_KEY_GPIO_ENTRY(31),
    QPY_KEY_GPIO_ENTRY(32), QPY_KEY_GPIO_ENTRY(33), QPY_KEY_GPIO_ENTRY(34), QPY_KEY_GPIO_ENTRY(35),
    QPY_KEY_GPIO_ENTRY(36), QPY_KEY_GPIO_ENTRY(37), QPY_KEY_GPIO_ENTRY(38), QPY_KEY_GPIO_ENTRY(39),
    QPY_KEY_GPIO_ENTRY(40), QPY_KEY_GPIO_ENTRY(41), QPY_KEY_GPIO_ENTRY(42), QPY_KEY_GPIO_ENTRY(43),
    { MP_ROM_QSTR(MP_QSTR_EVENT_SHORT_PRESS), MP_ROM_INT(QPY_KEY_EVENT_SHORT_PRESS) },
    { MP_ROM_QSTR(MP_QSTR_EVENT_DOUBLE_CLICK), MP_ROM_INT(QPY_KEY_EVENT_DOUBLE_CLICK) },
    { MP_ROM_QSTR(MP_QSTR_EVENT_LONG_PRESS), MP_ROM_INT(QPY_KEY_EVENT_LONG_PRESS) },
};
static MP_DEFINE_CONST_DICT(qpy_key_locals_dict, qpy_key_locals_table);

MP_DEFINE_CONST_OBJ_TYPE(
    qpy_machine_key_type,
    MP_QSTR_Key,
    MP_TYPE_FLAG_NONE,
    print, qpy_key_print,
    make_new, qpy_key_make_new,
    locals_dict, &qpy_key_locals_dict
    );