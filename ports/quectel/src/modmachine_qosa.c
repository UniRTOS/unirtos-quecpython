#include "py/obj.h"
#include "py/runtime.h"
#include "py/mphal.h"
#include "py/mperrno.h"
#include "qosa_def.h"
#include "qosa_power.h"
#include "qosa_uart.h"
#include "qosa_pinctrl.h"
#include "qosa_gpio.h"
#include "machine_qosa.h"

typedef int qosa_dev_error_e;
extern qosa_dev_error_e qosa_dev_get_cpu_uid(qosa_uint64_t *chip_id);
extern void qosa_dev_watch_dog_update(void);

typedef struct _qpy_machine_uart_obj_t {
    mp_obj_base_t base;
    mp_int_t uart_num;
    qosa_uart_port_number_e port;
    qosa_uart_config_t config;
    bool is_open;
    mp_obj_t callback;
    volatile int pending;
    volatile qosa_uint32_t pending_event;
    volatile int pending_size;
    qosa_gpio_num_e rs485_gpio;
    mp_int_t rs485_direction;
    bool rs485_active;
} qpy_machine_uart_obj_t;

static const mp_obj_type_t qpy_machine_uart_type;
// UART instances are also owned by the platform UART driver via user_data.
// Keep them in the MicroPython GC root set while that driver can report an
// event; otherwise a collection can reclaim an instance and leave the driver
// (and this polling table) with a dangling pointer.
MP_REGISTER_ROOT_POINTER(void *qpy_uart_objects[4]);

static bool qpy_uart_is_repl_port(qosa_uart_port_number_e port) {
    return (int)port == CONFIG_QPY_REPL_PORT;
}

typedef struct {
    mp_int_t uart_num;
    mp_int_t group;
    qosa_uart_port_number_e port;
    qosa_pin_num_e tx_pin;
    qosa_pin_num_e rx_pin;
    unsigned int pin_func;
} qpy_uart_pin_map_t;

/*
 * Keep the public UART numbering and pins compatible with the EIGEN Helios
 * implementation. QOSA port 1 and port 2 are intentionally swapped here:
 * legacy UART1 is the AUX UART and legacy UART2 is the MAIN UART. Legacy
 * UART1 additionally supports group 0 (TX29/RX28) and group 1 (TX63/RX64).
 */
static const qpy_uart_pin_map_t qpy_uart_pin_map[] = {
    { 0, 0, QOSA_UART_PORT_0, QOSA_PIN_39, QOSA_PIN_38, 1 },
    { 1, 0, QOSA_UART_PORT_2, QOSA_PIN_29, QOSA_PIN_28, 3 },
    { 1, 1, QOSA_UART_PORT_2, QOSA_PIN_63, QOSA_PIN_64, 3 },
    { 2, 0, QOSA_UART_PORT_1, QOSA_PIN_18, QOSA_PIN_17, 1 },
};

static int qpy_uart1_active_group = -1;

static const qpy_uart_pin_map_t *qpy_uart_get_pin_map(mp_int_t uart_num, mp_int_t group) {
    for (size_t i = 0; i < MP_ARRAY_SIZE(qpy_uart_pin_map); ++i) {
        if (qpy_uart_pin_map[i].uart_num == uart_num
            && (uart_num != 1 || qpy_uart_pin_map[i].group == group)) {
            return &qpy_uart_pin_map[i];
        }
    }
    return NULL;
}

static bool qpy_uart_restore_pin_default(qosa_pin_num_e pin) {
    qosa_pin_cfg_t cfg;

    return qosa_get_pin_default_cfg(pin, &cfg) == QOSA_GPIO_SUCCESS
        && qosa_pin_set_func(pin, cfg.default_func) == QOSA_PINCTRL_SUCCESS;
}

static bool qpy_uart_select_pins(mp_int_t uart_num, mp_int_t group) {
    const qpy_uart_pin_map_t *map = qpy_uart_get_pin_map(uart_num, group);

    if (map == NULL) {
        return uart_num == QOSA_UART_PORT_3;
    }
    if (uart_num == 1 && qpy_uart1_active_group >= 0 && qpy_uart1_active_group != group) {
        const qpy_uart_pin_map_t *previous = qpy_uart_get_pin_map(1, qpy_uart1_active_group);
        if (previous == NULL
            || !qpy_uart_restore_pin_default(previous->tx_pin)
            || !qpy_uart_restore_pin_default(previous->rx_pin)) {
            return false;
        }
    }
    if (qosa_pin_set_func(map->tx_pin, map->pin_func) != QOSA_PINCTRL_SUCCESS
        || qosa_pin_set_func(map->rx_pin, map->pin_func) != QOSA_PINCTRL_SUCCESS) {
        return false;
    }
    if (uart_num == 1) {
        qpy_uart1_active_group = group;
    }
    return true;
}

typedef struct _qpy_machine_stub_obj_t {
    mp_obj_base_t base;
} qpy_machine_stub_obj_t;

static mp_obj_t qpy_machine_stub_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    (void)n_args;
    (void)n_kw;
    (void)args;
    qpy_machine_stub_obj_t *self = mp_obj_malloc(qpy_machine_stub_obj_t, type);
    return MP_OBJ_FROM_PTR(self);
}

static mp_obj_t qpy_machine_stub_ret_minus_one(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    (void)args;
    return MP_OBJ_NEW_SMALL_INT(-1);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_machine_stub_ret_minus_one_obj, 0, MP_OBJ_FUN_ARGS_MAX, qpy_machine_stub_ret_minus_one);

static const mp_rom_map_elem_t qpy_machine_stub_locals_table[] = {
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&qpy_machine_stub_ret_minus_one_obj) },
    { MP_ROM_QSTR(MP_QSTR_deinit), MP_ROM_PTR(&qpy_machine_stub_ret_minus_one_obj) },
    { MP_ROM_QSTR(MP_QSTR_read), MP_ROM_PTR(&qpy_machine_stub_ret_minus_one_obj) },
    { MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&qpy_machine_stub_ret_minus_one_obj) },
    { MP_ROM_QSTR(MP_QSTR_open), MP_ROM_PTR(&qpy_machine_stub_ret_minus_one_obj) },
    { MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&qpy_machine_stub_ret_minus_one_obj) },
    { MP_ROM_QSTR(MP_QSTR_start), MP_ROM_PTR(&qpy_machine_stub_ret_minus_one_obj) },
    { MP_ROM_QSTR(MP_QSTR_stop), MP_ROM_PTR(&qpy_machine_stub_ret_minus_one_obj) },
    { MP_ROM_QSTR(MP_QSTR_feed), MP_ROM_PTR(&qpy_machine_stub_ret_minus_one_obj) },
    { MP_ROM_QSTR(MP_QSTR_value), MP_ROM_PTR(&qpy_machine_stub_ret_minus_one_obj) },
    { MP_ROM_QSTR(MP_QSTR_on), MP_ROM_PTR(&qpy_machine_stub_ret_minus_one_obj) },
    { MP_ROM_QSTR(MP_QSTR_off), MP_ROM_PTR(&qpy_machine_stub_ret_minus_one_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_dir), MP_ROM_PTR(&qpy_machine_stub_ret_minus_one_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_dir), MP_ROM_PTR(&qpy_machine_stub_ret_minus_one_obj) },
    { MP_ROM_QSTR(MP_QSTR_blink), MP_ROM_PTR(&qpy_machine_stub_ret_minus_one_obj) },
    { MP_ROM_QSTR(MP_QSTR_write_read), MP_ROM_PTR(&qpy_machine_stub_ret_minus_one_obj) },
    { MP_ROM_QSTR(MP_QSTR_write_block), MP_ROM_PTR(&qpy_machine_stub_ret_minus_one_obj) },
    { MP_ROM_QSTR(MP_QSTR_read_block), MP_ROM_PTR(&qpy_machine_stub_ret_minus_one_obj) },
    { MP_ROM_QSTR(MP_QSTR_write_read_block), MP_ROM_PTR(&qpy_machine_stub_ret_minus_one_obj) },
    { MP_ROM_QSTR(MP_QSTR_pulse_gen), MP_ROM_PTR(&qpy_machine_stub_ret_minus_one_obj) },
    { MP_ROM_QSTR(MP_QSTR_pulse_cap_start), MP_ROM_PTR(&qpy_machine_stub_ret_minus_one_obj) },
    { MP_ROM_QSTR(MP_QSTR_pulse_cap_stop), MP_ROM_PTR(&qpy_machine_stub_ret_minus_one_obj) },
    { MP_ROM_QSTR(MP_QSTR_pulse_cap_reset), MP_ROM_PTR(&qpy_machine_stub_ret_minus_one_obj) },
    { MP_ROM_QSTR(MP_QSTR_IN), MP_ROM_INT(0) },
    { MP_ROM_QSTR(MP_QSTR_OUT), MP_ROM_INT(1) },
    { MP_ROM_QSTR(MP_QSTR_PULL_UP), MP_ROM_INT(1) },
    { MP_ROM_QSTR(MP_QSTR_PULL_PU), MP_ROM_INT(1) },
    { MP_ROM_QSTR(MP_QSTR_PULL_PD), MP_ROM_INT(2) },
    { MP_ROM_QSTR(MP_QSTR_PULL_DOWN), MP_ROM_INT(2) },
    { MP_ROM_QSTR(MP_QSTR_PULL_DISABLE), MP_ROM_INT(0) },
    { MP_ROM_QSTR(MP_QSTR_PULL_NONE), MP_ROM_INT(0) },
    { MP_ROM_QSTR(MP_QSTR_BLINK_ON), MP_ROM_INT(1) },
    { MP_ROM_QSTR(MP_QSTR_BLINK_OFF), MP_ROM_INT(0) },
    { MP_ROM_QSTR(MP_QSTR_ONE_SHOT), MP_ROM_INT(0) },
    { MP_ROM_QSTR(MP_QSTR_PERIODIC), MP_ROM_INT(1) },
    { MP_ROM_QSTR(MP_QSTR_Timer0), MP_ROM_INT(0) },
    { MP_ROM_QSTR(MP_QSTR_Timer1), MP_ROM_INT(1) },
    { MP_ROM_QSTR(MP_QSTR_Timer2), MP_ROM_INT(2) },
    { MP_ROM_QSTR(MP_QSTR_Timer3), MP_ROM_INT(3) },
    { MP_ROM_QSTR(MP_QSTR_I2C0), MP_ROM_INT(0) },
    { MP_ROM_QSTR(MP_QSTR_I2C1), MP_ROM_INT(1) },
    { MP_ROM_QSTR(MP_QSTR_I2C2), MP_ROM_INT(2) },
    { MP_ROM_QSTR(MP_QSTR_I2C3), MP_ROM_INT(3) },
    { MP_ROM_QSTR(MP_QSTR_STANDARD_MODE), MP_ROM_INT(0) },
    { MP_ROM_QSTR(MP_QSTR_FAST_MODE), MP_ROM_INT(1) },
    { MP_ROM_QSTR(MP_QSTR_ENHANCED_FAST_MODE), MP_ROM_INT(2) },
    { MP_ROM_QSTR(MP_QSTR_SPI0), MP_ROM_INT(0) },
    { MP_ROM_QSTR(MP_QSTR_SPI1), MP_ROM_INT(1) },
    { MP_ROM_QSTR(MP_QSTR_SPI2), MP_ROM_INT(2) },
    { MP_ROM_QSTR(MP_QSTR_CLK_812_5K), MP_ROM_INT(0) },
    { MP_ROM_QSTR(MP_QSTR_CLK_1_625M), MP_ROM_INT(1) },
    { MP_ROM_QSTR(MP_QSTR_CLK_3_25M), MP_ROM_INT(2) },
    { MP_ROM_QSTR(MP_QSTR_CLK_6_5M), MP_ROM_INT(3) },
    { MP_ROM_QSTR(MP_QSTR_CLK_13M), MP_ROM_INT(4) },
    { MP_ROM_QSTR(MP_QSTR_CLK_100K), MP_ROM_INT(5) },
    { MP_ROM_QSTR(MP_QSTR_CLK_200K), MP_ROM_INT(6) },
    { MP_ROM_QSTR(MP_QSTR_CLK_300K), MP_ROM_INT(7) },
    { MP_ROM_QSTR(MP_QSTR_CLK_400K), MP_ROM_INT(8) },
};
static MP_DEFINE_CONST_DICT(qpy_machine_stub_locals_dict, qpy_machine_stub_locals_table);

#define QPY_DEFINE_MACHINE_STUB_TYPE(type_name, qstr_name) \
    static MP_DEFINE_CONST_OBJ_TYPE( \
        type_name, \
        qstr_name, \
        MP_TYPE_FLAG_NONE, \
        make_new, qpy_machine_stub_make_new, \
        locals_dict, &qpy_machine_stub_locals_dict \
        )

QPY_DEFINE_MACHINE_STUB_TYPE(qpy_machine_soft_spi_type, MP_QSTR_SoftSPI);

static qosa_uart_port_number_e qpy_uart_get_port(mp_int_t id) {
    const qpy_uart_pin_map_t *map = qpy_uart_get_pin_map(id, 0);
    if (map != NULL) {
        return map->port;
    }
    // Legacy EG800Z exposes UART3 as the REPL UART.  UniRTOS uses a USB
    // ACM port for the REPL, so retain the legacy number while using the
    // physical REPL port internally.
    if (id == QOSA_UART_PORT_3) {
        return (qosa_uart_port_number_e)CONFIG_QPY_REPL_PORT;
    }
    mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("UART(%d) does not exist"), id);
}

static qosa_uart_baud_e qpy_uart_get_baud(mp_int_t baudrate) {
    return (qosa_uart_baud_e)baudrate;
}

static void qpy_uart_configure(qpy_machine_uart_obj_t *self, size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_baudrate, ARG_bits, ARG_parity, ARG_stop, ARG_flow, ARG_group };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_baudrate, MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_bits, MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_parity, MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_stop, MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_flow, MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_group, MP_ARG_INT, {.u_int = 0} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    mp_int_t baudrate = args[ARG_baudrate].u_int;
    mp_int_t bits = args[ARG_bits].u_int;
    mp_int_t parity = args[ARG_parity].u_int;
    mp_int_t stop = args[ARG_stop].u_int;
    mp_int_t flow = args[ARG_flow].u_int;
    mp_int_t group = args[ARG_group].u_int;

    qosa_uart_config_t cfg = {0};
    if (baudrate == 0 || (!qpy_uart_is_repl_port(self->port) && !qosa_uart_check_support_baudrate(self->port, baudrate))) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid baudrate"));
    }
    cfg.baudrate = qpy_uart_get_baud(baudrate);
    // The legacy EG800Z (Unisoc) UART interface accepts only 8 data bits.
    // Keep both the accepted input and diagnostic compatible with it.
    if (bits != 8) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid data bits, Unisoc platform only support 8 data bit."));
    }
    cfg.data_bit = QOSA_UART_DATABIT_8;
    if (stop != 1 && stop != 2) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid stop bits"));
    }
    cfg.stop_bit = stop == 2 ? QOSA_UART_STOP_2 : QOSA_UART_STOP_1;
    if (flow == 0) {
        cfg.flow_ctrl = QOSA_FC_NONE;
    } else if (flow == 1) {
        cfg.flow_ctrl = QOSA_FC_HW;
    } else {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid flow bits"));
    }

    if (parity == 0) {
        cfg.parity_bit = QOSA_UART_PARITY_NONE;
    } else if (parity == 1) {
        cfg.parity_bit = QOSA_UART_PARITY_EVEN;
    } else if (parity == 2) {
        cfg.parity_bit = QOSA_UART_PARITY_ODD;
    } else {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid parity bits"));
    }
    if (group < 0 || group > 1) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid group bits"));
    }
    if (qpy_uart_get_pin_map(self->uart_num, group) == NULL && self->uart_num != QOSA_UART_PORT_3) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid group bits"));
    }

    self->config = cfg;
    if (qpy_uart_is_repl_port(self->port)) {
        self->is_open = true;
        return;
    }

    if (self->is_open) {
        qosa_uart_close(self->port);
        self->is_open = false;
    }
    if (!qpy_uart_select_pins(self->uart_num, group)) {
        mp_raise_OSError(MP_EIO);
    }

    if (qosa_uart_ioctl(self->port, QOSA_UART_IOCTL_SET_DCB_CFG, &cfg) != QOSA_UART_SUCCESS) {
        mp_raise_OSError(MP_EIO);
    }
    qosa_uart_error_e ret = qosa_uart_open(self->port);
    if (ret != QOSA_UART_SUCCESS && ret != QOSA_UART_OPEN_REPEAT_ERR) {
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("UART(%d) init fail"), self->uart_num);
    }
    // qosa_uart_open may restore the platform's default UART pins. Apply the
    // selected group again so the active pair matches the legacy API.
    if (!qpy_uart_select_pins(self->uart_num, group)) {
        qosa_uart_close(self->port);
        mp_raise_OSError(MP_EIO);
    }
    self->is_open = true;
}

static mp_obj_t qpy_uart_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args) {
    mp_arg_check_num(n_args, n_kw, 1, MP_OBJ_FUN_ARGS_MAX, true);
    mp_int_t uart_num = mp_obj_get_int(all_args[0]);
    qosa_uart_port_number_e port = qpy_uart_get_port(uart_num);
    qpy_machine_uart_obj_t *self = MP_STATE_VM(qpy_uart_objects)[uart_num];
    if (self == NULL) {
        self = mp_obj_malloc(qpy_machine_uart_obj_t, type);
        MP_STATE_VM(qpy_uart_objects)[uart_num] = self;
    } else if (self->is_open && !qpy_uart_is_repl_port(self->port)) {
        qosa_uart_close(self->port);
    }
    self->base.type = type;
    self->uart_num = uart_num;
    self->port = port;
    self->config.baudrate = QOSA_UART_BAUD_115200;
    self->config.data_bit = QOSA_UART_DATABIT_8;
    self->config.stop_bit = QOSA_UART_STOP_1;
    self->config.parity_bit = QOSA_UART_PARITY_NONE;
    self->config.flow_ctrl = QOSA_FC_NONE;
    self->is_open = false;
    self->callback = mp_const_none;
    self->pending = 0;
    self->rs485_gpio = QOSA_GPIO_MAX;
    self->rs485_direction = 0;
    self->rs485_active = false;

    mp_map_t kw_args;
    mp_map_init_fixed_table(&kw_args, n_kw, all_args + n_args);
    qpy_uart_configure(self, n_args - 1, all_args + 1, &kw_args);
    return MP_OBJ_FROM_PTR(self);
}

static mp_obj_t qpy_uart_init(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    qpy_machine_uart_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    qpy_uart_configure(self, n_args - 1, pos_args + 1, kw_args);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(qpy_uart_init_obj, 1, qpy_uart_init);

static mp_obj_t qpy_uart_deinit(mp_obj_t self_in) {
    qpy_machine_uart_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (qpy_uart_is_repl_port(self->port)) {
        self->is_open = false;
        return MP_OBJ_NEW_SMALL_INT(0);
    }
    qosa_uart_error_e ret = QOSA_UART_SUCCESS;
    if (self->is_open) {
        ret = qosa_uart_close(self->port);
        self->is_open = false;
    }
    return mp_obj_new_int(ret == QOSA_UART_SUCCESS ? 0 : -1);
}
static MP_DEFINE_CONST_FUN_OBJ_1(qpy_uart_deinit_obj, qpy_uart_deinit);

static mp_obj_t qpy_uart_delete(mp_obj_t self_in) {
    qpy_machine_uart_obj_t *self = MP_OBJ_TO_PTR(self_in);
    self->callback = mp_const_none;
    self->pending = 0;
    if (!qpy_uart_is_repl_port(self->port)) {
        qosa_uart_status_monitor_t monitor = {0};
        qosa_uart_register_cb(self->port, &monitor);
    }
    if (self->uart_num >= 0 && self->uart_num < MP_ARRAY_SIZE(MP_STATE_VM(qpy_uart_objects))) {
        MP_STATE_VM(qpy_uart_objects)[self->uart_num] = NULL;
    }
    return qpy_uart_deinit(self_in);
}
static MP_DEFINE_CONST_FUN_OBJ_1(qpy_uart_delete_obj, qpy_uart_delete);

static mp_obj_t qpy_uart_any(mp_obj_t self_in) {
    qpy_machine_uart_obj_t *self = MP_OBJ_TO_PTR(self_in);
    int available = qosa_uart_read_available(self->port);
    return MP_OBJ_NEW_SMALL_INT(available > 0 ? available : 0);
}
static MP_DEFINE_CONST_FUN_OBJ_1(qpy_uart_any_obj, qpy_uart_any);

static mp_obj_t qpy_uart_read(size_t n_args, const mp_obj_t *args) {
    qpy_machine_uart_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    int len = n_args > 1 ? mp_obj_get_int(args[1]) : qosa_uart_read_available(self->port);
    if (len <= 0) {
        return mp_obj_new_bytes(NULL, 0);
    }

    vstr_t vstr;
    vstr_init_len(&vstr, (size_t)len);
    int got = qosa_uart_read(self->port, (unsigned char *)vstr.buf, (unsigned int)len);
    if (got < 0) {
        vstr_clear(&vstr);
        mp_raise_OSError(MP_EIO);
    }
    if (got == 0) {
        vstr_clear(&vstr);
        return mp_const_none;
    }
    vstr.len = got;
    mp_obj_t bytes = mp_obj_new_bytes((const byte *)vstr.buf, (size_t)got);
    vstr_clear(&vstr);
    return bytes;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_uart_read_obj, 1, 2, qpy_uart_read);

static mp_obj_t qpy_uart_readinto(size_t n_args, const mp_obj_t *args) {
    qpy_machine_uart_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args[1], &bufinfo, MP_BUFFER_WRITE);
    size_t len = bufinfo.len;
    if (n_args > 2) {
        mp_int_t requested = mp_obj_get_int(args[2]);
        if (requested >= 0 && (size_t)requested < len) {
            len = (size_t)requested;
        }
    }
    int got = qosa_uart_read(self->port, (unsigned char *)bufinfo.buf, (unsigned int)len);
    if (got < 0) {
        mp_raise_OSError(MP_EIO);
    }
    if (got == 0) {
        return mp_const_none;
    }
    return MP_OBJ_NEW_SMALL_INT(got);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_uart_readinto_obj, 2, 3, qpy_uart_readinto);

static mp_obj_t qpy_uart_readline(mp_obj_t self_in) {
    qpy_machine_uart_obj_t *self = MP_OBJ_TO_PTR(self_in);
    int available = qosa_uart_read_available(self->port);
    if (available <= 0) {
        return mp_obj_new_bytes(NULL, 0);
    }

    vstr_t vstr;
    vstr_init(&vstr, (size_t)available);
    for (int i = 0; i < available; i++) {
        unsigned char ch = 0;
        int got = qosa_uart_read(self->port, &ch, 1);
        if (got < 0) {
            vstr_clear(&vstr);
            mp_raise_OSError(MP_EIO);
        }
        if (got == 0) {
            break;
        }
        vstr_add_byte(&vstr, ch);
        if (ch == '\n') {
            break;
        }
    }
    if (vstr.len == 0) {
        vstr_clear(&vstr);
        return mp_const_none;
    }
    mp_obj_t bytes = mp_obj_new_bytes((const byte *)vstr.buf, vstr.len);
    vstr_clear(&vstr);
    return bytes;
}
static MP_DEFINE_CONST_FUN_OBJ_1(qpy_uart_readline_obj, qpy_uart_readline);

static mp_obj_t qpy_uart_write(mp_obj_t self_in, mp_obj_t buf_in) {
    qpy_machine_uart_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf_in, &bufinfo, MP_BUFFER_READ);
    if (self->rs485_gpio != QOSA_GPIO_MAX && bufinfo.len > 0) {
        qosa_gpio_set_level(self->rs485_gpio, self->rs485_direction == 0 ? QOSA_GPIO_LEVEL_HIGH : QOSA_GPIO_LEVEL_LOW);
        self->rs485_active = true;
    }
    size_t total = 0;
    while (total < bufinfo.len) {
        int written = qosa_uart_write(self->port, (unsigned char *)bufinfo.buf + total, (unsigned int)(bufinfo.len - total));
        if (written < 0) {
            mp_raise_OSError(MP_EIO);
        }
        if (written == 0) {
            break;
        }
        total += (size_t)written;
    }
    return mp_obj_new_int_from_uint(total);
}
static MP_DEFINE_CONST_FUN_OBJ_2(qpy_uart_write_obj, qpy_uart_write);

static void qpy_uart_callback(qosa_uart_cb_param_t *param) {
    qpy_machine_uart_obj_t *self = param == NULL ? NULL : param->user_data;
    if (self == NULL) {
        return;
    }
    self->pending_event = param->event_id;
    if (self->rs485_active && (param->event_id & QOSA_UART_EVENT_TX_COMPLETE)) {
        qosa_gpio_set_level(self->rs485_gpio, self->rs485_direction == 0 ? QOSA_GPIO_LEVEL_LOW : QOSA_GPIO_LEVEL_HIGH);
        self->rs485_active = false;
    }
    int size = qosa_uart_read_available(self->port);
    self->pending_size = size > 0 ? size : 0;
    self->pending++;
    mp_hal_stdio_wake();
}

void qpy_machine_uart_reset_all(void) {
    qosa_uart_status_monitor_t monitor = {0};

    // Do not dereference the saved Python objects here: this function also
    // runs before gc_init(), when a preceding soft reset may have invalidated
    // every object in the old heap.
    for (int port = QOSA_UART_PORT_0; port <= QOSA_UART_PORT_2; ++port) {
        qosa_uart_register_cb((qosa_uart_port_number_e)port, &monitor);
        qosa_uart_close((qosa_uart_port_number_e)port);
    }
    for (size_t i = 0; i < MP_ARRAY_SIZE(MP_STATE_VM(qpy_uart_objects)); ++i) {
        MP_STATE_VM(qpy_uart_objects)[i] = NULL;
    }
    qpy_uart1_active_group = -1;
}

void qpy_machine_uart_poll_pending(void) {
    for (size_t i = 0; i < MP_ARRAY_SIZE(MP_STATE_VM(qpy_uart_objects)); i++) {
        qpy_machine_uart_obj_t *self = MP_STATE_VM(qpy_uart_objects)[i];
        if (self == NULL || self->pending <= 0 || self->callback == mp_const_none) {
            continue;
        }
        self->pending--;
        mp_obj_t items[3] = {
            mp_obj_new_int(self->pending_event),
            mp_obj_new_int(self->uart_num),
            mp_obj_new_int(self->pending_size),
        };
        mp_sched_schedule(self->callback, mp_obj_new_list(3, items));
    }
}

static mp_obj_t qpy_uart_set_callback(mp_obj_t self_in, mp_obj_t callback_in) {
    qpy_machine_uart_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (callback_in != mp_const_none && !mp_obj_is_callable(callback_in)) {
        mp_raise_TypeError(MP_ERROR_TEXT("callback must be callable"));
    }
    self->callback = callback_in;
    if (qpy_uart_is_repl_port(self->port)) {
        return MP_OBJ_NEW_SMALL_INT(0);
    }
    qosa_uart_status_monitor_t monitor = {0};
    monitor.event_mask = QOSA_UART_EVENT_RX_INDICATE | QOSA_UART_EVENT_TX_COMPLETE | QOSA_UART_EVENT_TX_LOW;
    monitor.callback = qpy_uart_callback;
    monitor.user_data = self;
    return MP_OBJ_NEW_SMALL_INT(qosa_uart_register_cb(self->port, &monitor) == QOSA_UART_SUCCESS ? 0 : -1);
}
static MP_DEFINE_CONST_FUN_OBJ_2(qpy_uart_set_callback_obj, qpy_uart_set_callback);

static mp_obj_t qpy_uart_control_485(size_t n_args, const mp_obj_t *args) {
    qpy_machine_uart_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    mp_int_t gpio = mp_obj_get_int(args[1]);
    mp_int_t direction = mp_obj_get_int(args[2]);
    if (gpio < 0 || gpio >= 43 || (direction != 0 && direction != 1)) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid parameter"));
    }
    qosa_gpio_level_e idle = direction == 0 ? QOSA_GPIO_LEVEL_LOW : QOSA_GPIO_LEVEL_HIGH;
    if (qosa_gpio_init((qosa_gpio_num_e)gpio, QOSA_GPIO_DIRECTION_OUTPUT, QOSA_GPIO_PULL_NONE, idle) != QOSA_GPIO_SUCCESS) {
        return MP_OBJ_NEW_SMALL_INT(-1);
    }
    self->rs485_gpio = (qosa_gpio_num_e)gpio;
    self->rs485_direction = direction;
    self->rs485_active = false;
    if (!qpy_uart_is_repl_port(self->port)) {
        qosa_uart_status_monitor_t monitor = {0};
        monitor.event_mask = QOSA_UART_EVENT_RX_INDICATE | QOSA_UART_EVENT_TX_COMPLETE | QOSA_UART_EVENT_TX_LOW;
        monitor.callback = qpy_uart_callback;
        monitor.user_data = self;
        if (qosa_uart_register_cb(self->port, &monitor) != QOSA_UART_SUCCESS) {
            return MP_OBJ_NEW_SMALL_INT(-1);
        }
    }
    return MP_OBJ_NEW_SMALL_INT(0);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_uart_control_485_obj, 3, 4, qpy_uart_control_485);

static void qpy_uart_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    (void)kind;
    qpy_machine_uart_obj_t *self = MP_OBJ_TO_PTR(self_in);
    const char *parity = self->config.parity_bit == QOSA_UART_PARITY_NONE ? "None" :
        (self->config.parity_bit == QOSA_UART_PARITY_EVEN ? "1" : "0");
    mp_printf(print, "UART(%d, baudrate=%u, bits=%u, parity=%s, stop=%u, flow=%u)",
        self->uart_num, (unsigned)self->config.baudrate, (unsigned)self->config.data_bit,
        parity, (unsigned)self->config.stop_bit, (unsigned)self->config.flow_ctrl);
}

#define QPY_UART_GPIO_ENTRY(n) { MP_ROM_QSTR(MP_QSTR_GPIO##n), MP_ROM_INT(n) }
static const mp_rom_map_elem_t qpy_uart_locals_table[] = {
    { MP_ROM_QSTR(MP_QSTR_UART0), MP_ROM_INT(QOSA_UART_PORT_0) },
    { MP_ROM_QSTR(MP_QSTR_UART1), MP_ROM_INT(QOSA_UART_PORT_1) },
    { MP_ROM_QSTR(MP_QSTR_UART2), MP_ROM_INT(QOSA_UART_PORT_2) },
    { MP_ROM_QSTR(MP_QSTR_UART3), MP_ROM_INT(QOSA_UART_PORT_3) },
    { MP_ROM_QSTR(MP_QSTR_REPL_UART), MP_ROM_INT(QOSA_UART_PORT_3) },
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&qpy_uart_init_obj) },
    { MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&qpy_uart_delete_obj) },
    { MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&qpy_uart_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR_any), MP_ROM_PTR(&qpy_uart_any_obj) },
    { MP_ROM_QSTR(MP_QSTR_read), MP_ROM_PTR(&qpy_uart_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_readOnce), MP_ROM_PTR(&qpy_uart_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_readline), MP_ROM_PTR(&qpy_uart_readline_obj) },
    { MP_ROM_QSTR(MP_QSTR_readinto), MP_ROM_PTR(&qpy_uart_readinto_obj) },
    { MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&qpy_uart_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_writeOnce), MP_ROM_PTR(&qpy_uart_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_callback), MP_ROM_PTR(&qpy_uart_set_callback_obj) },
    { MP_ROM_QSTR(MP_QSTR_control_485), MP_ROM_PTR(&qpy_uart_control_485_obj) },
    QPY_UART_GPIO_ENTRY(0), QPY_UART_GPIO_ENTRY(1), QPY_UART_GPIO_ENTRY(2), QPY_UART_GPIO_ENTRY(3), QPY_UART_GPIO_ENTRY(4),
    QPY_UART_GPIO_ENTRY(5), QPY_UART_GPIO_ENTRY(6), QPY_UART_GPIO_ENTRY(7), QPY_UART_GPIO_ENTRY(8),
    QPY_UART_GPIO_ENTRY(9), QPY_UART_GPIO_ENTRY(10), QPY_UART_GPIO_ENTRY(11), QPY_UART_GPIO_ENTRY(12),
    QPY_UART_GPIO_ENTRY(13), QPY_UART_GPIO_ENTRY(14), QPY_UART_GPIO_ENTRY(15), QPY_UART_GPIO_ENTRY(16),
    QPY_UART_GPIO_ENTRY(17), QPY_UART_GPIO_ENTRY(18), QPY_UART_GPIO_ENTRY(19), QPY_UART_GPIO_ENTRY(20),
    QPY_UART_GPIO_ENTRY(21), QPY_UART_GPIO_ENTRY(22), QPY_UART_GPIO_ENTRY(23), QPY_UART_GPIO_ENTRY(24),
    QPY_UART_GPIO_ENTRY(25), QPY_UART_GPIO_ENTRY(26), QPY_UART_GPIO_ENTRY(27), QPY_UART_GPIO_ENTRY(28),
    QPY_UART_GPIO_ENTRY(29), QPY_UART_GPIO_ENTRY(30), QPY_UART_GPIO_ENTRY(31), QPY_UART_GPIO_ENTRY(32),
    QPY_UART_GPIO_ENTRY(33), QPY_UART_GPIO_ENTRY(34), QPY_UART_GPIO_ENTRY(35), QPY_UART_GPIO_ENTRY(36),
    QPY_UART_GPIO_ENTRY(37), QPY_UART_GPIO_ENTRY(38), QPY_UART_GPIO_ENTRY(39), QPY_UART_GPIO_ENTRY(40),
    QPY_UART_GPIO_ENTRY(41), QPY_UART_GPIO_ENTRY(42),
};
static MP_DEFINE_CONST_DICT(qpy_uart_locals_dict, qpy_uart_locals_table);

static MP_DEFINE_CONST_OBJ_TYPE(
    qpy_machine_uart_type,
    MP_QSTR_UART,
    MP_TYPE_FLAG_NONE,
    print, qpy_uart_print,
    make_new, qpy_uart_make_new,
    locals_dict, &qpy_uart_locals_dict
    );

static mp_obj_t qpy_machine_reset(void) {
    qosa_power_reset(QOSA_RESET_NORMAL);
    for (;;) {
        mp_hal_delay_ms(1000);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(qpy_machine_reset_obj, qpy_machine_reset);

static mp_obj_t qpy_machine_freq(void) {
    return mp_obj_new_int(0);
}
static MP_DEFINE_CONST_FUN_OBJ_0(qpy_machine_freq_obj, qpy_machine_freq);

static mp_obj_t qpy_machine_unique_id(void) {
    qosa_uint64_t chip_id = 0;
    if (qosa_dev_get_cpu_uid(&chip_id) != 0) {
        uint32_t seed = mp_hal_get_random();
        return mp_obj_new_bytes((const byte *)&seed, sizeof(seed));
    }
    return mp_obj_new_bytes((const byte *)&chip_id, sizeof(chip_id));
}
static MP_DEFINE_CONST_FUN_OBJ_0(qpy_machine_unique_id_obj, qpy_machine_unique_id);

static mp_obj_t qpy_machine_sys_wdt_feed(void) {
    qosa_dev_watch_dog_update();
    return MP_OBJ_NEW_SMALL_INT(0);
}
static MP_DEFINE_CONST_FUN_OBJ_0(qpy_machine_sys_wdt_feed_obj, qpy_machine_sys_wdt_feed);

static const mp_rom_map_elem_t qpy_machine_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_machine) },
    { MP_ROM_QSTR(MP_QSTR_reset), MP_ROM_PTR(&qpy_machine_reset_obj) },
    { MP_ROM_QSTR(MP_QSTR_freq), MP_ROM_PTR(&qpy_machine_freq_obj) },
    { MP_ROM_QSTR(MP_QSTR_unique_id), MP_ROM_PTR(&qpy_machine_unique_id_obj) },
    { MP_ROM_QSTR(MP_QSTR_sys_wdt_feed), MP_ROM_PTR(&qpy_machine_sys_wdt_feed_obj) },
#if MICROPY_QPY_MACHINE_UART
    { MP_ROM_QSTR(MP_QSTR_UART), MP_ROM_PTR(&qpy_machine_uart_type) },
#endif
#if MICROPY_QPY_MACHINE_PIN
    { MP_ROM_QSTR(MP_QSTR_Pin), MP_ROM_PTR(&qpy_machine_pin_type) },
#endif
#if MICROPY_QPY_MACHINE_I2C
    { MP_ROM_QSTR(MP_QSTR_I2C), MP_ROM_PTR(&qpy_machine_i2c_type) },
    { MP_ROM_QSTR(MP_QSTR_I2C_simulation), MP_ROM_PTR(&qpy_machine_i2c_simulation_type) },
#endif
#if MICROPY_QPY_MACHINE_SPI
    { MP_ROM_QSTR(MP_QSTR_SPI), MP_ROM_PTR(&qpy_machine_spi_type) },
    { MP_ROM_QSTR(MP_QSTR_SoftSPI), MP_ROM_PTR(&qpy_machine_soft_spi_type) },
#endif
#if MICROPY_QPY_MACHINE_RTC
    { MP_ROM_QSTR(MP_QSTR_RTC), MP_ROM_PTR(&qpy_machine_rtc_type) },
#endif
#if MICROPY_QPY_MACHINE_WDT
    { MP_ROM_QSTR(MP_QSTR_WDT), MP_ROM_PTR(&qpy_machine_wdt_type) },
#endif
#if MICROPY_QPY_MACHINE_TIMER
    { MP_ROM_QSTR(MP_QSTR_Timer), MP_ROM_PTR(&qpy_machine_timer_type) },
#endif
#if MICROPY_QPY_MACHINE_EXTINT
    { MP_ROM_QSTR(MP_QSTR_ExtInt), MP_ROM_PTR(&qpy_machine_extint_type) },
#endif
#if MICROPY_QPY_MACHINE_KEY
    { MP_ROM_QSTR(MP_QSTR_Key), MP_ROM_PTR(&qpy_machine_key_type) },
#endif
    { MP_ROM_QSTR(MP_QSTR_UART0), MP_ROM_INT(QOSA_UART_PORT_0) },
    { MP_ROM_QSTR(MP_QSTR_UART1), MP_ROM_INT(QOSA_UART_PORT_1) },
    { MP_ROM_QSTR(MP_QSTR_UART2), MP_ROM_INT(QOSA_UART_PORT_2) },
    { MP_ROM_QSTR(MP_QSTR_UART3), MP_ROM_INT(QOSA_UART_PORT_3) },
    { MP_ROM_QSTR(MP_QSTR_UART4), MP_ROM_INT(QOSA_USB_PORT_AT) },
    { MP_ROM_QSTR(MP_QSTR_REPL_UART), MP_ROM_INT(CONFIG_QPY_REPL_PORT) },
};
static MP_DEFINE_CONST_DICT(qpy_machine_globals, qpy_machine_globals_table);

const mp_obj_module_t mp_module_machine_qosa = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&qpy_machine_globals,
};

#if MICROPY_QPY_MODULE_MACHINE
MP_REGISTER_MODULE(MP_QSTR_machine, mp_module_machine_qosa);
#endif
