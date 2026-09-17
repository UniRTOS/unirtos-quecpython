#ifndef QPY_MACHINE_QOSA_H
#define QPY_MACHINE_QOSA_H

#include "py/obj.h"
#include "py/runtime.h"

extern const mp_obj_type_t qpy_machine_pin_type;
extern const mp_obj_type_t qpy_machine_timer_type;
extern const mp_obj_type_t qpy_machine_i2c_type;
extern const mp_obj_type_t qpy_machine_i2c_simulation_type;
extern const mp_obj_type_t qpy_machine_spi_type;
extern const mp_obj_type_t qpy_machine_rtc_type;
extern const mp_obj_type_t qpy_machine_wdt_type;
extern const mp_obj_type_t qpy_machine_extint_type;
extern const mp_obj_type_t qpy_machine_key_type;

void qpy_machine_timer_poll_pending(void);
void qpy_machine_rtc_poll_pending(void);
void qpy_machine_extint_poll_pending(void);
void qpy_machine_key_poll_pending(void);

static inline mp_map_elem_t *qpy_machine_kw_find(mp_map_t *kw_args, qstr key) {
    return mp_map_lookup(kw_args, MP_OBJ_NEW_QSTR(key), MP_MAP_LOOKUP);
}

static inline mp_int_t qpy_machine_get_int_kw(mp_map_t *kw_args, qstr key, mp_int_t fallback) {
    mp_map_elem_t *elem = qpy_machine_kw_find(kw_args, key);
    return elem == NULL ? fallback : mp_obj_get_int(elem->value);
}

static inline mp_obj_t qpy_machine_get_obj_kw(mp_map_t *kw_args, qstr key, mp_obj_t fallback) {
    mp_map_elem_t *elem = qpy_machine_kw_find(kw_args, key);
    return elem == NULL ? fallback : elem->value;
}

#endif
