#include "py/obj.h"
#include "py/runtime.h"
#include "qosa_power.h"
#include "qpy_compat_common.h"

#define QPY_DEFINE_STUB_TYPE(type_name, qstr_name) \
    static MP_DEFINE_CONST_OBJ_TYPE( \
        type_name, \
        qstr_name, \
        MP_TYPE_FLAG_NONE, \
        make_new, qpy_stub_make_new, \
        locals_dict, &qpy_stub_type_locals \
        )


extern const mp_obj_type_t qpy_powerkey_type;
extern const mp_obj_type_t qpy_pwm_type;
extern const mp_obj_type_t qpy_pwm_v2_type;
extern const mp_obj_type_t qpy_adc_type;
extern const mp_obj_type_t qpy_usb_type;
extern const mp_obj_type_t qpy_temp_type;
extern const mp_obj_module_t qpy_usbnet_module;

static mp_obj_t qpy_power_power_on_reason(void) {
    qosa_boot_cause_e cause = QOSA_BOOT_CAUSE_UNKNOWN;
    if (qosa_power_get_boot_cause(&cause) != QOSA_POWER_SUCCESS) {
        return MP_OBJ_NEW_SMALL_INT(0);
    }
    return MP_OBJ_NEW_SMALL_INT((int)cause);
}
static MP_DEFINE_CONST_FUN_OBJ_0(qpy_power_power_on_reason_obj, qpy_power_power_on_reason);

static qosa_int32_t qpy_power_mode(size_t n_args, const mp_obj_t *args) {
    qosa_int32_t mode = 1;
    if (n_args == 1) {
        mode = mp_obj_get_int(args[0]);
        if (mode != 0 && mode != 1) {
            mp_raise_msg(&mp_type_ValueError, MP_ERROR_TEXT("invalid value, mode should be in 0 or 1."));
        }
    }
    return mode;
}

static mp_obj_t qpy_power_down(size_t n_args, const mp_obj_t *args) {
    qosa_int32_t mode = qpy_power_mode(n_args, args);
    qosa_power_down(mode == 0 ? QOSA_POWD_IMMDLY : QOSA_POWD_NORMAL);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_power_down_obj, 0, 1, qpy_power_down);

static mp_obj_t qpy_power_restart(size_t n_args, const mp_obj_t *args) {
    qosa_int32_t mode = qpy_power_mode(n_args, args);
    qosa_power_reset(mode == 0 ? QOSA_RESET_QUICK : QOSA_RESET_NORMAL);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_power_restart_obj, 0, 1, qpy_power_restart);

static mp_obj_t qpy_power_down_reason(void) {
    // UniRTOS has no equivalent to Helios_Power_GetDownReason().
    return MP_OBJ_NEW_SMALL_INT(-1);
}
static MP_DEFINE_CONST_FUN_OBJ_0(qpy_power_down_reason_obj, qpy_power_down_reason);

static mp_obj_t qpy_power_get_vbatt(void) {
    qosa_charge_status_e charge_status;
    qosa_uint8_t battery_level;
    qosa_uint32_t voltage = 0;
    if (qosa_power_get_charger_status(&charge_status, &battery_level, &voltage) != QOSA_POWER_SUCCESS) {
        return MP_OBJ_NEW_SMALL_INT(0);
    }
    return mp_obj_new_int_from_uint(voltage);
}
static MP_DEFINE_CONST_FUN_OBJ_0(qpy_power_get_vbatt_obj, qpy_power_get_vbatt);

static const mp_rom_map_elem_t qpy_power_locals_table[] = {
    { MP_ROM_QSTR(MP_QSTR_powerOnReason), MP_ROM_PTR(&qpy_power_power_on_reason_obj) },
    { MP_ROM_QSTR(MP_QSTR_powerDownReason), MP_ROM_PTR(&qpy_power_down_reason_obj) },
    { MP_ROM_QSTR(MP_QSTR_getVbatt), MP_ROM_PTR(&qpy_power_get_vbatt_obj) },
    { MP_ROM_QSTR(MP_QSTR_powerDown), MP_ROM_PTR(&qpy_power_down_obj) },
    { MP_ROM_QSTR(MP_QSTR_powerRestart), MP_ROM_PTR(&qpy_power_restart_obj) },
};
static MP_DEFINE_CONST_DICT(qpy_power_locals, qpy_power_locals_table);

static const mp_obj_module_t qpy_power_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&qpy_power_locals,
};

static const mp_rom_map_elem_t qpy_misc_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_misc) },
#if MICROPY_QPY_MISC_POWER
    { MP_ROM_QSTR(MP_QSTR_Power), MP_ROM_PTR(&qpy_power_module) },
#endif
#if MICROPY_QPY_MISC_POWERKEY
    { MP_ROM_QSTR(MP_QSTR_PowerKey), MP_ROM_PTR(&qpy_powerkey_type) },
#endif
#if MICROPY_QPY_MISC_PWM
    { MP_ROM_QSTR(MP_QSTR_PWM), MP_ROM_PTR(&qpy_pwm_type) },
    { MP_ROM_QSTR(MP_QSTR_PWM_V2), MP_ROM_PTR(&qpy_pwm_v2_type) },
#endif
#if MICROPY_QPY_MISC_ADC
    { MP_ROM_QSTR(MP_QSTR_ADC), MP_ROM_PTR(&qpy_adc_type) },
#endif
#if MICROPY_QPY_MISC_USB
    { MP_ROM_QSTR(MP_QSTR_USB), MP_ROM_PTR(&qpy_usb_type) },
#endif
#if MICROPY_QPY_MISC_USBNET
    { MP_ROM_QSTR(MP_QSTR_USBNET), MP_ROM_PTR(&qpy_usbnet_module) },
#endif
#if MICROPY_QPY_MISC_TEMPERATURE
    { MP_ROM_QSTR(MP_QSTR_Temperature), MP_ROM_PTR(&qpy_temp_type) },
#endif
    { MP_ROM_QSTR(MP_QSTR_antennaSecRXOffCtrl), MP_ROM_PTR(&qpy_stub_ret_minus_one_obj) },
    { MP_ROM_QSTR(MP_QSTR_setReplEnable), MP_ROM_PTR(&qpy_stub_ret_minus_one_obj) },
    { MP_ROM_QSTR(MP_QSTR_setReplPassword), MP_ROM_PTR(&qpy_stub_ret_minus_one_obj) },
    { MP_ROM_QSTR(MP_QSTR_IncCoreVoltage), MP_ROM_PTR(&qpy_stub_ret_minus_one_obj) },
    { MP_ROM_QSTR(MP_QSTR_net_light_enable), MP_ROM_PTR(&qpy_stub_ret_minus_one_obj) },
    { MP_ROM_QSTR(MP_QSTR_log), MP_ROM_PTR(&qpy_stub_ret_minus_one_obj) },
    { MP_ROM_QSTR(MP_QSTR_lpmFastExtInt), MP_ROM_PTR(&qpy_stub_ret_minus_one_obj) },
};
static MP_DEFINE_CONST_DICT(qpy_misc_globals, qpy_misc_globals_table);

const mp_obj_module_t mp_module_misc = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&qpy_misc_globals,
};

#if MICROPY_QPY_MODULE_MISC
MP_REGISTER_MODULE(MP_QSTR_misc, mp_module_misc);
#endif
