#include "py/obj.h"
#include "py/runtime.h"
#include "py/mperrno.h"
#include "qosa_spi.h"
#include "qosa_gpio.h"
#include "qosa_pinctrl.h"
#include "machine_qosa.h"

typedef struct {
    qosa_spi_port_e port;
    qosa_pin_num_e mosi_pin;
    qosa_pin_num_e miso_pin;
    qosa_pin_num_e clk_pin;
    qosa_pin_num_e cs_pin;
    qosa_gpio_num_e cs_gpio;
} qpy_spi_map_t;

/*
 * Helios TYPE_EC718PM SPI pin map.  Chip select stays under software
 * control, matching Helios_SPI_Init/Read/Write behaviour.
 */
static const qpy_spi_map_t qpy_spi_map[] = {
    { QOSA_SPI_PORT0, 67, 28, 29, 66, QOSA_GPIO_8 },
    { QOSA_SPI_PORT1, 63, 62, 49, 64, QOSA_GPIO_12 },
};

typedef struct _qpy_machine_spi_obj_t {
    mp_obj_base_t base;
    qosa_spi_port_e port;
    qosa_spi_transmit_mode_e transmit_mode;
    qosa_spi_clk_e clk;
    mp_int_t frame_mode;
    int group;
    const qpy_spi_map_t *map;
    bool active;
} qpy_machine_spi_obj_t;

static const qpy_spi_map_t *qpy_spi_get_map(mp_int_t port) {
    for (size_t i = 0; i < MP_ARRAY_SIZE(qpy_spi_map); ++i) {
        if (qpy_spi_map[i].port == port) {
            return &qpy_spi_map[i];
        }
    }
    mp_raise_ValueError(MP_ERROR_TEXT("unsupported SPI port"));
}

static qosa_spi_clk_e qpy_spi_clk_from_old(mp_int_t clk) {
    switch (clk) {
        case 0:
            return QOSA_SPI_CLK_812_5KHZ;
        case 1:
            return QOSA_SPI_CLK_1_625MHZ;
        case 2:
            return QOSA_SPI_CLK_3_25MHZ;
        case 3:
            return QOSA_SPI_CLK_6_5MHZ;
        case 4:
            return QOSA_SPI_CLK_13MHZ;
        case 5:
            return QOSA_SPI_CLK_812_5KHZ;
        case 6:
            return QOSA_SPI_CLK_1_625MHZ;
        case 7:
            return QOSA_SPI_CLK_3_25MHZ;
        case 8:
            return QOSA_SPI_CLK_6_5MHZ;
        default:
            if (clk > 10000) {
                return (qosa_spi_clk_e)clk;
            }
            mp_raise_ValueError(MP_ERROR_TEXT("invalid spi clk"));
    }
}

static bool qpy_spi_select_pins(const qpy_spi_map_t *map) {
    int ret;

    ret = qosa_pin_set_func(map->mosi_pin, 1);
    if (ret != QOSA_PINCTRL_SUCCESS) {
        return false;
    }
    ret = qosa_pin_set_func(map->miso_pin, 1);
    if (ret != QOSA_PINCTRL_SUCCESS) {
        return false;
    }
    ret = qosa_pin_set_func(map->clk_pin, 1);
    if (ret != QOSA_PINCTRL_SUCCESS) {
        return false;
    }
    return true;
}

/*
 * qosa_spi_init() and the NSS-mode ioctl can reset the CS pin state.  Select
 * the GPIO function only after both calls, as required by the QOSA SPI demo.
 */
static bool qpy_spi_select_cs(const qpy_machine_spi_obj_t *self) {
    qosa_pin_cfg_t cs_cfg;
    int ret = qosa_get_pin_default_cfg(self->map->cs_pin, &cs_cfg);
    if (ret != QOSA_GPIO_SUCCESS) {
        return false;
    }
    if (cs_cfg.gpio_num != self->map->cs_gpio) {
        return false;
    }
    ret = qosa_pin_set_func(self->map->cs_pin, cs_cfg.gpio_func);
    if (ret != QOSA_PINCTRL_SUCCESS) {
        return false;
    }
    ret = qosa_gpio_init(self->map->cs_gpio, QOSA_GPIO_DIRECTION_OUTPUT, QOSA_GPIO_PULL_NONE, QOSA_GPIO_LEVEL_HIGH);
    if (ret != QOSA_GPIO_SUCCESS) {
        return false;
    }
    return true;
}

static int qpy_spi_set_cs(const qpy_machine_spi_obj_t *self, qosa_gpio_level_e level) {
    return qosa_gpio_set_level(self->map->cs_gpio, level);
}

static int qpy_spi_result(qosa_spi_errcode_e ret) {
    return ret == QOSA_SPI_SUCCESS ? 0 : -1;
}

static bool qpy_spi_apply_mode(qpy_machine_spi_obj_t *self) {
    qosa_uint32_t frame = (qosa_uint32_t)self->frame_mode;
    qosa_spi_nss_mode_e nss_mode = QOSA_SPI_NSS_MASTER_SOFTWARE;
    qosa_spi_errcode_e ret = qosa_spi_ioctl(self->port, QOSA_SPI_IOCTL_SET_CLK_POLARITY_PHASE, &frame);
    if (ret != QOSA_SPI_SUCCESS) {
        return false;
    }
    ret = qosa_spi_ioctl(self->port, QOSA_SPI_IOCTL_SET_NSS_MODE, &nss_mode);
    if (ret != QOSA_SPI_SUCCESS) {
        return false;
    }
    return true;
}

static void qpy_spi_init_bus(qpy_machine_spi_obj_t *self) {
    qosa_spi_errcode_e ret = qosa_spi_init(self->port, self->transmit_mode, self->clk);
    if (ret != QOSA_SPI_SUCCESS) {
        mp_raise_OSError(MP_EIO);
    }
    if (!qpy_spi_apply_mode(self)) {
        qosa_spi_deinit(self->port);
        mp_raise_OSError(MP_EIO);
    }
    if (!qpy_spi_select_cs(self)) {
        qosa_spi_deinit(self->port);
        mp_raise_OSError(MP_EIO);
    }
    self->active = true;
}

static mp_obj_t qpy_spi_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args) {
    enum { ARG_port, ARG_mode, ARG_clk, ARG_group };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_port, MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_mode, MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_clk, MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_group, MP_ARG_INT, {.u_int = 0} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    qpy_machine_spi_obj_t *self = mp_obj_malloc(qpy_machine_spi_obj_t, type);
    self->base.type = type;
    self->map = qpy_spi_get_map(args[ARG_port].u_int);
    self->port = self->map->port;
    self->frame_mode = args[ARG_mode].u_int;
    if (self->frame_mode < 0 || self->frame_mode > 3) {
        mp_raise_ValueError(MP_ERROR_TEXT("mode must be (0~3)"));
    }
    self->clk = qpy_spi_clk_from_old(args[ARG_clk].u_int);
    self->group = args[ARG_group].u_int;
    self->transmit_mode = QOSA_SPI_TRANSMIT_POLLING;
    self->active = false;

    if (!qpy_spi_select_pins(self->map)) {
        mp_raise_OSError(MP_EIO);
    }
    qpy_spi_init_bus(self);
    return MP_OBJ_FROM_PTR(self);
}

static void qpy_spi_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    (void)kind;
    qpy_machine_spi_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "spi%d", self->port);
}

static mp_obj_t qpy_spi_close(mp_obj_t self_in) {
    qpy_machine_spi_obj_t *self = MP_OBJ_TO_PTR(self_in);
    qosa_spi_errcode_e ret = qosa_spi_deinit(self->port);
    if (qosa_gpio_deinit(self->map->cs_gpio) != QOSA_GPIO_SUCCESS) {
        ret = QOSA_SPI_EXECUTE_ERR;
    }
    self->active = false;
    return MP_OBJ_NEW_SMALL_INT(qpy_spi_result(ret));
}
static MP_DEFINE_CONST_FUN_OBJ_1(qpy_spi_close_obj, qpy_spi_close);

static const mp_arg_t qpy_spi_rw_args[] = {
    { MP_QSTR_databuf, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
    { MP_QSTR_datasize, MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0} },
};

static mp_obj_t qpy_spi_write(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    qpy_machine_spi_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    mp_arg_val_t args[MP_ARRAY_SIZE(qpy_spi_rw_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(qpy_spi_rw_args), qpy_spi_rw_args, args);
    mp_buffer_info_t buf;
    mp_get_buffer_raise(args[0].u_obj, &buf, MP_BUFFER_READ);
    mp_int_t len = (size_t)args[1].u_int > buf.len ? (mp_int_t)buf.len : args[1].u_int;
    int cs_ret = qpy_spi_set_cs(self, QOSA_GPIO_LEVEL_LOW);
    if (cs_ret != QOSA_GPIO_SUCCESS) {
        return MP_OBJ_NEW_SMALL_INT(-1);
    }
    qosa_spi_errcode_e ret = qosa_spi_write(self->port, buf.buf, (qosa_uint32_t)len);
    cs_ret = qpy_spi_set_cs(self, QOSA_GPIO_LEVEL_HIGH);
    if (cs_ret != QOSA_GPIO_SUCCESS) {
        ret = QOSA_SPI_EXECUTE_ERR;
    }
    return MP_OBJ_NEW_SMALL_INT(qpy_spi_result(ret));
}
static MP_DEFINE_CONST_FUN_OBJ_KW(qpy_spi_write_obj, 1, qpy_spi_write);

static mp_obj_t qpy_spi_read(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    qpy_machine_spi_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    mp_arg_val_t args[MP_ARRAY_SIZE(qpy_spi_rw_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(qpy_spi_rw_args), qpy_spi_rw_args, args);
    mp_buffer_info_t buf;
    mp_get_buffer_raise(args[0].u_obj, &buf, MP_BUFFER_WRITE);
    mp_int_t len = (size_t)args[1].u_int > buf.len ? (mp_int_t)buf.len : args[1].u_int;
    int cs_ret = qpy_spi_set_cs(self, QOSA_GPIO_LEVEL_LOW);
    if (cs_ret != QOSA_GPIO_SUCCESS) {
        return MP_OBJ_NEW_SMALL_INT(-1);
    }
    qosa_spi_errcode_e ret = qosa_spi_read(self->port, buf.buf, (qosa_uint32_t)len);
    cs_ret = qpy_spi_set_cs(self, QOSA_GPIO_LEVEL_HIGH);
    if (cs_ret != QOSA_GPIO_SUCCESS) {
        ret = QOSA_SPI_EXECUTE_ERR;
    }
    return MP_OBJ_NEW_SMALL_INT(qpy_spi_result(ret));
}
static MP_DEFINE_CONST_FUN_OBJ_KW(qpy_spi_read_obj, 1, qpy_spi_read);

static const mp_arg_t qpy_spi_write_read_args[] = {
    { MP_QSTR_readbuf, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
    { MP_QSTR_writebuf, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
    { MP_QSTR_datasize, MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 0} },
};

static mp_obj_t qpy_spi_write_read(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    qpy_machine_spi_obj_t *self = MP_OBJ_TO_PTR(pos_args[0]);
    mp_arg_val_t args[MP_ARRAY_SIZE(qpy_spi_write_read_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(qpy_spi_write_read_args), qpy_spi_write_read_args, args);
    mp_buffer_info_t readbuf;
    mp_buffer_info_t writebuf;
    mp_get_buffer_raise(args[0].u_obj, &readbuf, MP_BUFFER_WRITE);
    mp_get_buffer_raise(args[1].u_obj, &writebuf, MP_BUFFER_READ);
    mp_int_t len = readbuf.len < writebuf.len ? (mp_int_t)readbuf.len : (mp_int_t)writebuf.len;
    if (args[2].u_int >= 0 && args[2].u_int < len) {
        len = args[2].u_int;
    }
    int cs_ret = qpy_spi_set_cs(self, QOSA_GPIO_LEVEL_LOW);
    if (cs_ret != QOSA_GPIO_SUCCESS) {
        return MP_OBJ_NEW_SMALL_INT(-1);
    }
    qosa_spi_errcode_e ret = qosa_spi_write_read(self->port, readbuf.buf, writebuf.buf, (qosa_uint32_t)len);
    cs_ret = qpy_spi_set_cs(self, QOSA_GPIO_LEVEL_HIGH);
    if (cs_ret != QOSA_GPIO_SUCCESS) {
        ret = QOSA_SPI_EXECUTE_ERR;
    }
    return MP_OBJ_NEW_SMALL_INT(qpy_spi_result(ret));
}
static MP_DEFINE_CONST_FUN_OBJ_KW(qpy_spi_write_read_obj, 1, qpy_spi_write_read);

static const mp_rom_map_elem_t qpy_spi_locals_table[] = {
    { MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&qpy_spi_close_obj) },
    { MP_ROM_QSTR(MP_QSTR_read), MP_ROM_PTR(&qpy_spi_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&qpy_spi_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_write_read), MP_ROM_PTR(&qpy_spi_write_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_SPI0), MP_ROM_INT(QOSA_SPI_PORT0) },
    { MP_ROM_QSTR(MP_QSTR_SPI1), MP_ROM_INT(QOSA_SPI_PORT1) },
};
static MP_DEFINE_CONST_DICT(qpy_spi_locals_dict, qpy_spi_locals_table);

MP_DEFINE_CONST_OBJ_TYPE(
    qpy_machine_spi_type,
    MP_QSTR_SPI,
    MP_TYPE_FLAG_NONE,
    print, qpy_spi_print,
    make_new, qpy_spi_make_new,
    locals_dict, &qpy_spi_locals_dict
    );
