#include "py/obj.h"
#include "py/runtime.h"
#include "qosa_adc.h"

typedef struct _qpy_misc_adc_obj_t {
    mp_obj_base_t base;
    qosa_adc_aux_scale_e scale;
} qpy_misc_adc_obj_t;

static qosa_adc_channel_e qpy_adc_channel(mp_int_t channel) {
    if (channel >= QOSA_ADC0_CHANNEL && channel < QOSA_ADC_CHANNEL_MAX) {
        return (qosa_adc_channel_e)channel;
    }
    return QOSA_ADC_CHANNEL_MAX;
}

static mp_obj_t qpy_adc_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 0, MP_OBJ_FUN_ARGS_MAX, true);
    qpy_misc_adc_obj_t *self = mp_obj_malloc(qpy_misc_adc_obj_t, type);
    self->scale = QOSA_ADC_SCALE_LEVEL_0;
    return MP_OBJ_FROM_PTR(self);
}

static mp_obj_t qpy_adc_open(mp_obj_t self_in) {
    (void)self_in;
    return MP_OBJ_NEW_SMALL_INT(0);
}
static MP_DEFINE_CONST_FUN_OBJ_1(qpy_adc_open_obj, qpy_adc_open);

static mp_obj_t qpy_adc_read(mp_obj_t self_in, mp_obj_t channel_in) {
    qpy_misc_adc_obj_t *self = MP_OBJ_TO_PTR(self_in);
    qosa_adc_channel_e channel = qpy_adc_channel(mp_obj_get_int(channel_in));
    if (channel == QOSA_ADC_CHANNEL_MAX) {
        return MP_OBJ_NEW_SMALL_INT(-1);
    }

    qosa_adc_aux_scale_e scale = self->scale;

    if (qosa_adc_ioctl(channel, QOSA_ADC_IOCTL_SET_SCALE, &scale) != QOSA_ADC_SUCCESS) {
        return MP_OBJ_NEW_SMALL_INT(-1);
    }

    int value = 0;
    if (qosa_adc_get_volt(channel, &value) != QOSA_ADC_SUCCESS) {
        return MP_OBJ_NEW_SMALL_INT(-1);
    }
    return mp_obj_new_int(value);
}
static MP_DEFINE_CONST_FUN_OBJ_2(qpy_adc_read_obj, qpy_adc_read);

static mp_obj_t qpy_adc_close(mp_obj_t self_in) {
    (void)self_in;
    return MP_OBJ_NEW_SMALL_INT(0);
}
static MP_DEFINE_CONST_FUN_OBJ_1(qpy_adc_close_obj, qpy_adc_close);

static const mp_rom_map_elem_t qpy_adc_locals_table[] = {
    { MP_ROM_QSTR(MP_QSTR_open), MP_ROM_PTR(&qpy_adc_open_obj) },
    { MP_ROM_QSTR(MP_QSTR_read), MP_ROM_PTR(&qpy_adc_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&qpy_adc_close_obj) },
    { MP_ROM_QSTR(MP_QSTR_ADC0), MP_ROM_INT(QOSA_ADC0_CHANNEL) },
    { MP_ROM_QSTR(MP_QSTR_ADC1), MP_ROM_INT(QOSA_ADC1_CHANNEL) },
    { MP_ROM_QSTR(MP_QSTR_ADC2), MP_ROM_INT(2) },
    { MP_ROM_QSTR(MP_QSTR_ADC3), MP_ROM_INT(3) },
};
static MP_DEFINE_CONST_DICT(qpy_adc_locals, qpy_adc_locals_table);

MP_DEFINE_CONST_OBJ_TYPE(
    qpy_adc_type,
    MP_QSTR_ADC,
    MP_TYPE_FLAG_NONE,
    make_new, qpy_adc_make_new,
    locals_dict, &qpy_adc_locals
    );
