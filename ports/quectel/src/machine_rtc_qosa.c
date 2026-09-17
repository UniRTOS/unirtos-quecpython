#include "py/obj.h"
#include "py/runtime.h"
#include "py/mperrno.h"
#include "qosa_rtc.h"
#include "mphalport.h"
#include "machine_qosa.h"

typedef struct _qpy_machine_rtc_obj_t {
    mp_obj_base_t base;
} qpy_machine_rtc_obj_t;

static qpy_machine_rtc_obj_t *qpy_machine_rtc_singleton;
static mp_obj_t qpy_machine_rtc_callback = mp_const_none;
static volatile int qpy_machine_rtc_pending = 0;

static void qpy_rtc_fill_from_tuple(qosa_rtc_time_t *tm, mp_obj_t tuple) {
    mp_obj_t *items;
    mp_obj_get_array_fixed_n(tuple, 8, &items);
    int year = mp_obj_get_int(items[0]);
    int mon = mp_obj_get_int(items[1]);
    tm->tm_year = year >= 1900 ? year - 1900 : year;
    tm->tm_mon = mon >= 1 ? mon - 1 : mon;
    tm->tm_mday = mp_obj_get_int(items[2]);
    tm->tm_wday = mp_obj_get_int(items[3]);
    tm->tm_hour = mp_obj_get_int(items[4]);
    tm->tm_min = mp_obj_get_int(items[5]);
    tm->tm_sec = mp_obj_get_int(items[6]);
}

static mp_obj_t qpy_rtc_tuple_from_tm(const qosa_rtc_time_t *tm) {
    mp_obj_t tuple[8] = {
        mp_obj_new_int(tm->tm_year + 1900),
        mp_obj_new_int(tm->tm_mon + 1),
        mp_obj_new_int(tm->tm_mday),
        mp_obj_new_int(tm->tm_wday),
        mp_obj_new_int(tm->tm_hour),
        mp_obj_new_int(tm->tm_min),
        mp_obj_new_int(tm->tm_sec),
        MP_OBJ_NEW_SMALL_INT(0),
    };
    return mp_obj_new_tuple(8, tuple);
}

static mp_obj_t qpy_rtc_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    (void)args;
    mp_arg_check_num(n_args, n_kw, 0, 0, false);
    if (qpy_machine_rtc_singleton == NULL) {
        qpy_machine_rtc_singleton = mp_obj_malloc(qpy_machine_rtc_obj_t, type);
    }
    return MP_OBJ_FROM_PTR(qpy_machine_rtc_singleton);
}

static mp_obj_t qpy_rtc_datetime(size_t n_args, const mp_obj_t *args) {
    if (n_args == 1) {
        qosa_time_t local_time = 0;
        qosa_rtc_time_t tm = {0};
        if (qosa_rtc_get_localtime(&local_time) != 0 || qosa_rtc_gmtime_r(&local_time, &tm) == NULL) {
            mp_raise_OSError(MP_EIO);
        }
        return qpy_rtc_tuple_from_tm(&tm);
    }

    qosa_rtc_time_t tm = {0};
    qosa_time_t unix_time = 0;
    qpy_rtc_fill_from_tuple(&tm, args[1]);
    if (qosa_rtc_mktime(&tm, &unix_time) != 0) {
        return MP_OBJ_NEW_SMALL_INT(-1);
    }
    int tz_quarters = qosa_rtc_get_timezone();
    unix_time -= (qosa_time_t)tz_quarters * 15 * 60;
    if (qosa_rtc_set_time(unix_time) != 0) {
        return MP_OBJ_NEW_SMALL_INT(-1);
    }
    return MP_OBJ_NEW_SMALL_INT(0);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_rtc_datetime_obj, 1, 2, qpy_rtc_datetime);

static mp_obj_t qpy_rtc_init(mp_obj_t self_in, mp_obj_t datetime) {
    mp_obj_t args[2] = {self_in, datetime};
    qpy_rtc_datetime(2, args);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(qpy_rtc_init_obj, qpy_rtc_init);

static mp_obj_t qpy_rtc_delete(mp_obj_t self_in) {
    if (qpy_machine_rtc_singleton == MP_OBJ_TO_PTR(self_in)) {
        qpy_machine_rtc_singleton = NULL;
    }
    qpy_machine_rtc_callback = mp_const_none;
    qpy_machine_rtc_pending = 0;
    return MP_OBJ_NEW_SMALL_INT(0);
}
static MP_DEFINE_CONST_FUN_OBJ_1(qpy_rtc_delete_obj, qpy_rtc_delete);

static mp_obj_t qpy_rtc_set_alarm(mp_obj_t self_in, mp_obj_t datetime) {
    (void)self_in;
    qosa_rtc_time_t tm = {0};
    qpy_rtc_fill_from_tuple(&tm, datetime);
    qosa_rtc_error_e ret = qosa_rtc_set_alarm(&tm);
    return MP_OBJ_NEW_SMALL_INT(ret == QOSA_RTC_ERR_OK ? 0 : -1);
}
static MP_DEFINE_CONST_FUN_OBJ_2(qpy_rtc_set_alarm_obj, qpy_rtc_set_alarm);

static mp_obj_t qpy_rtc_enable_alarm(mp_obj_t self_in, mp_obj_t enable) {
    (void)self_in;
    qosa_rtc_error_e ret = qosa_rtc_enable_alarm((qosa_uint8_t)mp_obj_is_true(enable));
    return MP_OBJ_NEW_SMALL_INT(ret == QOSA_RTC_ERR_OK ? 0 : -1);
}
static MP_DEFINE_CONST_FUN_OBJ_2(qpy_rtc_enable_alarm_obj, qpy_rtc_enable_alarm);

static void qpy_rtc_alarm_cb(void) {
    qpy_machine_rtc_pending = 1;
    mp_hal_stdio_wake();
}

static mp_obj_t qpy_rtc_register_callback(mp_obj_t self_in, mp_obj_t callback) {
    (void)self_in;
    if (callback != mp_const_none && !mp_obj_is_callable(callback)) {
        mp_raise_TypeError(MP_ERROR_TEXT("callback must be callable"));
    }
    qpy_machine_rtc_callback = callback;
    qosa_rtc_error_e ret = qosa_rtc_register_cb(qpy_rtc_alarm_cb);
    return MP_OBJ_NEW_SMALL_INT(ret == QOSA_RTC_ERR_OK ? 0 : -1);
}
static MP_DEFINE_CONST_FUN_OBJ_2(qpy_rtc_register_callback_obj, qpy_rtc_register_callback);

void qpy_machine_rtc_poll_pending(void) {
    if (!qpy_machine_rtc_pending || qpy_machine_rtc_callback == mp_const_none) {
        return;
    }
    qpy_machine_rtc_pending = 0;
    mp_sched_schedule(qpy_machine_rtc_callback, mp_const_none);
}

static const mp_rom_map_elem_t qpy_rtc_locals_table[] = {
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&qpy_rtc_init_obj) },
    { MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&qpy_rtc_delete_obj) },
    { MP_ROM_QSTR(MP_QSTR_datetime), MP_ROM_PTR(&qpy_rtc_datetime_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_alarm), MP_ROM_PTR(&qpy_rtc_set_alarm_obj) },
    { MP_ROM_QSTR(MP_QSTR_enable_alarm), MP_ROM_PTR(&qpy_rtc_enable_alarm_obj) },
    { MP_ROM_QSTR(MP_QSTR_register_callback), MP_ROM_PTR(&qpy_rtc_register_callback_obj) },
};
static MP_DEFINE_CONST_DICT(qpy_rtc_locals_dict, qpy_rtc_locals_table);

MP_DEFINE_CONST_OBJ_TYPE(
    qpy_machine_rtc_type,
    MP_QSTR_RTC,
    MP_TYPE_FLAG_NONE,
    make_new, qpy_rtc_make_new,
    locals_dict, &qpy_rtc_locals_dict
    );
