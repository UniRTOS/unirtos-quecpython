#include "py/obj.h"
#include "py/runtime.h"
#include "py/mperrno.h"
#include "qosa_iic.h"
#include "qosa_gpio.h"
#include "qosa_pinctrl.h"
#include "qosa_sys.h"
#include "machine_qosa.h"

typedef struct {
    mp_int_t export_id;
    qosa_i2c_channel_e channel;
    qosa_pin_num_e scl_pin[2];
    qosa_pin_num_e sda_pin[2];
    uint8_t pin_func;
    bool pull_low_before_select;
} qpy_i2c_map_t;

/*
 * Helios TYPE_EC718PM I2C map.  I2C0 is backed by the internal I2C2
 * controller and has two mutually exclusive pin groups; I2C1 is backed by
 * internal I2C1.  These pins are the same for the supported EC718PM boards.
 */
static const qpy_i2c_map_t qpy_i2c_map[] = {
    { 0, QOSA_I2C_2, { 81, 67 }, { 80, 66 }, 2, false },
    { 1, QOSA_I2C_1, { 57, 57 }, { 58, 58 }, 2, true },
};

static int qpy_i2c0_active_group = -1;

typedef struct _qpy_machine_i2c_obj_t {
    mp_obj_base_t base;
    mp_int_t export_id;
    qosa_i2c_channel_e channel;
    qosa_i2c_mode_e mode;
    int group;
    bool active;
} qpy_machine_i2c_obj_t;

static const qpy_i2c_map_t *qpy_i2c_get_map(mp_int_t export_id) {
    for (size_t i = 0; i < MP_ARRAY_SIZE(qpy_i2c_map); ++i) {
        if (qpy_i2c_map[i].export_id == export_id) {
            return &qpy_i2c_map[i];
        }
    }
    mp_raise_ValueError(MP_ERROR_TEXT("unsupported I2C id"));
}

static qosa_i2c_mode_e qpy_i2c_mode_from_old(mp_int_t mode) {
    switch (mode) {
        case 0:
            return QOSA_IIC_STANDARD_MODE;
        case 1:
            return QOSA_IIC_FAST_MODE;
        case 2:
            return QOSA_IIC_FAST_PLUS_MODE;
        default:
            if (mode >= QOSA_IIC_SLOW_MODE && mode <= QOSA_IIC_HIGH_SPEED_MODE) {
                return (qosa_i2c_mode_e)mode;
            }
            mp_raise_ValueError(MP_ERROR_TEXT("invalid i2c mode"));
    }
}

static uint16_t qpy_i2c_regaddr_from_obj(mp_obj_t regaddr_obj) {
    if (mp_obj_is_int(regaddr_obj)) {
        return (uint16_t)mp_obj_get_int(regaddr_obj);
    }

    mp_buffer_info_t regaddr;
    mp_get_buffer_raise(regaddr_obj, &regaddr, MP_BUFFER_READ);
    const uint8_t *buf = (const uint8_t *)regaddr.buf;
    if (regaddr.len == 0) {
        return 0;
    }
    if (regaddr.len == 1) {
        return buf[0];
    }
    return ((uint16_t)buf[0] << 8) | buf[1];
}

static int qpy_i2c_result(qosa_i2c_error_e ret) {
    return ret == QOSA_I2C_SUCCESS ? 0 : -1;
}

static bool qpy_i2c_restore_pin(qosa_pin_num_e pin) {
    qosa_pin_cfg_t cfg;
    return qosa_get_pin_default_cfg(pin, &cfg) == QOSA_GPIO_SUCCESS &&
        qosa_pin_set_func(pin, cfg.default_func) == QOSA_PINCTRL_SUCCESS;
}

static bool qpy_i2c_apply_pins(const qpy_i2c_map_t *map, int group) {
    qosa_pin_num_e scl_pin = map->scl_pin[group];
    qosa_pin_num_e sda_pin = map->sda_pin[group];

    if (map->export_id == 0 && qpy_i2c0_active_group >= 0 && qpy_i2c0_active_group != group) {
        int old_group = qpy_i2c0_active_group;
        if (!qpy_i2c_restore_pin(map->scl_pin[old_group]) ||
            !qpy_i2c_restore_pin(map->sda_pin[old_group])) {
            return false;
        }
    }

    if (map->pull_low_before_select) {
        qosa_pin_cfg_t scl_cfg;
        qosa_pin_cfg_t sda_cfg;
        if (qosa_get_pin_default_cfg(scl_pin, &scl_cfg) != QOSA_GPIO_SUCCESS ||
            qosa_get_pin_default_cfg(sda_pin, &sda_cfg) != QOSA_GPIO_SUCCESS ||
            qosa_gpio_init(scl_cfg.gpio_num, QOSA_GPIO_DIRECTION_OUTPUT, QOSA_GPIO_PULL_NONE, QOSA_GPIO_LEVEL_LOW) != QOSA_GPIO_SUCCESS ||
            qosa_gpio_init(sda_cfg.gpio_num, QOSA_GPIO_DIRECTION_OUTPUT, QOSA_GPIO_PULL_NONE, QOSA_GPIO_LEVEL_LOW) != QOSA_GPIO_SUCCESS) {
            return false;
        }
    }

    if (qosa_pin_set_func(scl_pin, map->pin_func) != QOSA_PINCTRL_SUCCESS ||
        qosa_pin_set_func(sda_pin, map->pin_func) != QOSA_PINCTRL_SUCCESS) {
        return false;
    }
    if (map->export_id == 0) {
        qpy_i2c0_active_group = group;
    }
    return true;
}

static void qpy_i2c_init_bus(qpy_machine_i2c_obj_t *self, qosa_i2c_mode_e mode) {
    qosa_i2c_error_e ret = qosa_i2c_init(self->channel, mode);
    if (ret != QOSA_I2C_SUCCESS) {
        mp_raise_OSError(MP_EIO);
    }
    self->mode = mode;
    self->active = true;
}

static mp_obj_t qpy_i2c_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args) {
    enum { ARG_id, ARG_fastmode, ARG_group };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_id, MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_fastmode, MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 1} },
        { MP_QSTR_group, MP_ARG_INT, {.u_int = 0} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    qpy_machine_i2c_obj_t *self = mp_obj_malloc(qpy_machine_i2c_obj_t, type);
    self->base.type = type;
    const qpy_i2c_map_t *map = qpy_i2c_get_map(args[ARG_id].u_int);
    if (args[ARG_group].u_int < 0 || args[ARG_group].u_int > 1) {
        mp_raise_ValueError(MP_ERROR_TEXT("group must be (0~1)"));
    }
    self->export_id = map->export_id;
    self->channel = map->channel;
    self->group = args[ARG_group].u_int;
    self->active = false;

    if (!qpy_i2c_apply_pins(map, self->group)) {
        mp_raise_OSError(MP_EIO);
    }
    qpy_i2c_init_bus(self, qpy_i2c_mode_from_old(args[ARG_fastmode].u_int));
    return MP_OBJ_FROM_PTR(self);
}

static void qpy_i2c_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    (void)kind;
    qpy_machine_i2c_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "I2C%d", self->export_id);
}

static mp_obj_t qpy_i2c_close(mp_obj_t self_in) {
    qpy_machine_i2c_obj_t *self = MP_OBJ_TO_PTR(self_in);
    qosa_i2c_error_e ret = qosa_i2c_deinit(self->channel);
    self->active = false;
    return MP_OBJ_NEW_SMALL_INT(qpy_i2c_result(ret));
}
static MP_DEFINE_CONST_FUN_OBJ_1(qpy_i2c_close_obj, qpy_i2c_close);

static const mp_arg_t qpy_i2c_mem_write_args[] = {
    { MP_QSTR_slaveaddr, MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0} },
    { MP_QSTR_regaddr, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
    { MP_QSTR_regaddr_len, MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0} },
    { MP_QSTR_databuf, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
    { MP_QSTR_datasize, MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 8} },
};

static mp_obj_t qpy_i2c_write(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    qpy_machine_i2c_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    mp_arg_val_t args[MP_ARRAY_SIZE(qpy_i2c_mem_write_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(qpy_i2c_mem_write_args), qpy_i2c_mem_write_args, args);

    mp_buffer_info_t data;
    mp_get_buffer_raise(args[3].u_obj, &data, MP_BUFFER_READ);
    mp_int_t size = (size_t)args[4].u_int > data.len ? (mp_int_t)data.len : args[4].u_int;
    qosa_i2c_error_e ret = qosa_i2c_write(self->channel, (qosa_uint8_t)args[0].u_int, qpy_i2c_regaddr_from_obj(args[1].u_obj), (qosa_uint8_t *)data.buf, (qosa_uint32_t)size);
    return MP_OBJ_NEW_SMALL_INT(qpy_i2c_result(ret));
}
static MP_DEFINE_CONST_FUN_OBJ_KW(qpy_i2c_write_obj, 1, qpy_i2c_write);

static const mp_arg_t qpy_i2c_mem_read_args[] = {
    { MP_QSTR_slaveaddr, MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0} },
    { MP_QSTR_regaddr, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
    { MP_QSTR_regaddr_len, MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0} },
    { MP_QSTR_databuf, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
    { MP_QSTR_datasize, MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 8} },
    { MP_QSTR_dalay, MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 8} },
};

static mp_obj_t qpy_i2c_read(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    qpy_machine_i2c_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    mp_arg_val_t args[MP_ARRAY_SIZE(qpy_i2c_mem_read_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(qpy_i2c_mem_read_args), qpy_i2c_mem_read_args, args);

    mp_buffer_info_t data;
    mp_get_buffer_raise(args[3].u_obj, &data, MP_BUFFER_WRITE);
    mp_int_t size = (size_t)args[4].u_int > data.len ? (mp_int_t)data.len : args[4].u_int;
    if (args[5].u_int > 0) {
        qosa_task_sleep_ms((qosa_uint32_t)args[5].u_int);
    }
    qosa_i2c_error_e ret = qosa_i2c_read(self->channel, (qosa_uint8_t)args[0].u_int, qpy_i2c_regaddr_from_obj(args[1].u_obj), (qosa_uint8_t *)data.buf, (qosa_uint32_t)size);
    return MP_OBJ_NEW_SMALL_INT(qpy_i2c_result(ret));
}
static MP_DEFINE_CONST_FUN_OBJ_KW(qpy_i2c_read_obj, 1, qpy_i2c_read);

static const mp_rom_map_elem_t qpy_i2c_locals_table[] = {
    { MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&qpy_i2c_close_obj) },
    { MP_ROM_QSTR(MP_QSTR_read), MP_ROM_PTR(&qpy_i2c_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&qpy_i2c_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_I2C0), MP_ROM_INT(0) },
    { MP_ROM_QSTR(MP_QSTR_I2C1), MP_ROM_INT(1) },
    { MP_ROM_QSTR(MP_QSTR_STANDARD_MODE), MP_ROM_INT(0) },
    { MP_ROM_QSTR(MP_QSTR_FAST_MODE), MP_ROM_INT(1) },
};
static MP_DEFINE_CONST_DICT(qpy_i2c_locals_dict, qpy_i2c_locals_table);

MP_DEFINE_CONST_OBJ_TYPE(
    qpy_machine_i2c_type,
    MP_QSTR_I2C,
    MP_TYPE_FLAG_NONE,
    print, qpy_i2c_print,
    make_new, qpy_i2c_make_new,
    locals_dict, &qpy_i2c_locals_dict
    );

MP_DEFINE_CONST_OBJ_TYPE(
    qpy_machine_i2c_simulation_type,
    MP_QSTR_I2C_simulation,
    MP_TYPE_FLAG_NONE,
    print, qpy_i2c_print,
    make_new, qpy_i2c_make_new,
    locals_dict, &qpy_i2c_locals_dict
    );
