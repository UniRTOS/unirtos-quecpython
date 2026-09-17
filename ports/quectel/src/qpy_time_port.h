#ifndef MICROPY_INCLUDED_QUECTEL_UNIRTOS_QPY_TIME_PORT_H
#define MICROPY_INCLUDED_QUECTEL_UNIRTOS_QPY_TIME_PORT_H

#include "py/obj.h"
#include "py/runtime.h"
#include "qosa_rtc.h"
#include "shared/timeutils/timeutils.h"

#define QPY_UNIX_TO_2000_EPOCH_OFFSET (946684800LL)

static inline void mp_time_localtime_get(timeutils_struct_time_t *tm) {
    qosa_time_t seconds = qosa_get_system_time_seconds();
    timeutils_seconds_since_epoch_to_struct_time((mp_timestamp_t)seconds, tm);
}

static inline mp_obj_t mp_time_time_get(void) {
    return mp_obj_new_int_from_ull(qosa_get_system_time_seconds());
}

static inline mp_obj_t qpy_time_tuple_from_epoch(qosa_time_t seconds) {
    timeutils_struct_time_t tm;
    timeutils_seconds_since_epoch_to_struct_time((mp_timestamp_t)(seconds - QPY_UNIX_TO_2000_EPOCH_OFFSET), &tm);
    mp_obj_t tuple[8] = {
        mp_obj_new_int(tm.tm_year),
        mp_obj_new_int(tm.tm_mon),
        mp_obj_new_int(tm.tm_mday),
        mp_obj_new_int(tm.tm_hour),
        mp_obj_new_int(tm.tm_min),
        mp_obj_new_int(tm.tm_sec),
        mp_obj_new_int(tm.tm_wday),
        mp_obj_new_int(tm.tm_yday),
    };
    return mp_obj_new_tuple(8, tuple);
}

// EG800Z legacy utime uses Unix timestamps with a fixed UTC+8 conversion.
static mp_obj_t qpy_time_localtime_legacy(size_t n_args, const mp_obj_t *args) {
    qosa_time_t seconds;
    if (n_args == 0 || args[0] == mp_const_none) {
        seconds = qosa_get_system_time_seconds();
    } else {
        seconds = (qosa_time_t)timeutils_obj_get_timestamp(args[0]) + 8 * 60 * 60;
    }
    return qpy_time_tuple_from_epoch(seconds);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_time_localtime_legacy_obj, 0, 1, qpy_time_localtime_legacy);

static mp_obj_t qpy_time_mktime_legacy(mp_obj_t tuple) {
    size_t len;
    mp_obj_t *elem;
    mp_obj_get_array(tuple, &len, &elem);
    if (len < 8 || len > 9) {
        mp_raise_TypeError(MP_ERROR_TEXT("mktime needs a tuple of length 8 or 9"));
    }
    mp_timestamp_t seconds = timeutils_mktime(mp_obj_get_int(elem[0]),
        mp_obj_get_int(elem[1]), mp_obj_get_int(elem[2]), mp_obj_get_int(elem[3]),
        mp_obj_get_int(elem[4]), mp_obj_get_int(elem[5]));
    return timeutils_obj_from_timestamp(seconds + QPY_UNIX_TO_2000_EPOCH_OFFSET - 8 * 60 * 60);
}
static MP_DEFINE_CONST_FUN_OBJ_1(qpy_time_mktime_legacy_obj, qpy_time_mktime_legacy);

static mp_obj_t qpy_time_time_legacy(void) {
    return mp_obj_new_int_from_uint(mp_hal_ticks_ms() / 1000U);
}
static MP_DEFINE_CONST_FUN_OBJ_0(qpy_time_time_legacy_obj, qpy_time_time_legacy);

static inline int qpy_time_timezone_quarters(void) {
    int tz = qosa_rtc_get_timezone();
    if (tz < -48 || tz > 56) {
        return 0;
    }
    return tz;
}

static mp_obj_t qpy_time_localtime_ex(size_t n_args, const mp_obj_t *args) {
    qosa_time_t seconds;
    if (n_args == 0 || args[0] == mp_const_none) {
        seconds = qosa_get_system_time_seconds() + (qosa_time_t)qpy_time_timezone_quarters() * 15 * 60;
    } else {
        seconds = (qosa_time_t)timeutils_obj_get_timestamp(args[0]) + (qosa_time_t)qpy_time_timezone_quarters() * 15 * 60;
    }
    return qpy_time_tuple_from_epoch(seconds);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_time_localtime_ex_obj, 0, 1, qpy_time_localtime_ex);

static mp_obj_t qpy_time_mktime_ex(mp_obj_t tuple) {
    size_t len;
    mp_obj_t *elem;
    mp_obj_get_array(tuple, &len, &elem);
    if (len < 8 || len > 9) {
        mp_raise_TypeError(MP_ERROR_TEXT("mktime needs a tuple of length 8 or 9"));
    }
    mp_timestamp_t seconds = timeutils_mktime(mp_obj_get_int(elem[0]),
        mp_obj_get_int(elem[1]), mp_obj_get_int(elem[2]), mp_obj_get_int(elem[3]),
        mp_obj_get_int(elem[4]), mp_obj_get_int(elem[5]));
    seconds += QPY_UNIX_TO_2000_EPOCH_OFFSET;
    seconds -= (mp_timestamp_t)qpy_time_timezone_quarters() * 15 * 60;
    return timeutils_obj_from_timestamp(seconds);
}
static MP_DEFINE_CONST_FUN_OBJ_1(qpy_time_mktime_ex_obj, qpy_time_mktime_ex);

static mp_obj_t qpy_time_set_timezone(mp_obj_t tz) {
    int offset = mp_obj_get_int(tz);
    if (offset < -12 || offset > 12) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid value, timezone should be in [-12, +12]."));
    }
    return MP_OBJ_NEW_SMALL_INT(qosa_rtc_set_timezone(offset * 4) == 0 ? 0 : -1);
}
static MP_DEFINE_CONST_FUN_OBJ_1(qpy_time_set_timezone_obj, qpy_time_set_timezone);

static mp_obj_t qpy_time_get_timezone(void) {
    return MP_OBJ_NEW_SMALL_INT(qpy_time_timezone_quarters() / 4);
}
static MP_DEFINE_CONST_FUN_OBJ_0(qpy_time_get_timezone_obj, qpy_time_get_timezone);

static mp_obj_t qpy_time_set_timezone_ex(mp_obj_t tz) {
    mp_float_t offset = mp_obj_get_float(tz);
    int quarters = (int)(offset * 4 + (offset >= 0 ? 0.5f : -0.5f));
    if (quarters < -48 || quarters > 56 || (mp_float_t)quarters != offset * 4) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid value, timezone should be in [-12, +14] and minimum unit is a quarter of one hour."));
    }
    return MP_OBJ_NEW_SMALL_INT(qosa_rtc_set_timezone(quarters) == 0 ? 0 : -1);
}
static MP_DEFINE_CONST_FUN_OBJ_1(qpy_time_set_timezone_ex_obj, qpy_time_set_timezone_ex);

static mp_obj_t qpy_time_get_timezone_ex(void) {
    return mp_obj_new_float((mp_float_t)qpy_time_timezone_quarters() / 4);
}
static MP_DEFINE_CONST_FUN_OBJ_0(qpy_time_get_timezone_ex_obj, qpy_time_get_timezone_ex);

static mp_obj_t qpy_time_nitz_switch(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    (void)args;
    return MP_OBJ_NEW_SMALL_INT(0);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_time_nitz_switch_obj, 0, 1, qpy_time_nitz_switch);

#define MICROPY_PY_TIME_EXTRA_GLOBALS \
    { MP_ROM_QSTR(MP_QSTR_localtime_ex), MP_ROM_PTR(&qpy_time_localtime_ex_obj) }, \
    { MP_ROM_QSTR(MP_QSTR_mktime_ex), MP_ROM_PTR(&qpy_time_mktime_ex_obj) }, \
    { MP_ROM_QSTR(MP_QSTR_setTimeZone), MP_ROM_PTR(&qpy_time_set_timezone_obj) }, \
    { MP_ROM_QSTR(MP_QSTR_getTimeZone), MP_ROM_PTR(&qpy_time_get_timezone_obj) }, \
    { MP_ROM_QSTR(MP_QSTR_nitzSwitch), MP_ROM_PTR(&qpy_time_nitz_switch_obj) },

#endif
