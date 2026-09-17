#include "py/obj.h"
#include "py/runtime.h"
#include "machine_qosa.h"

typedef struct _qpy_machine_wdt_obj_t {
    mp_obj_base_t base;
    mp_int_t period;
} qpy_machine_wdt_obj_t;

extern void qosa_dev_watch_dog_update(void);

static qpy_machine_wdt_obj_t qpy_machine_wdt_singleton = {{&qpy_machine_wdt_type}, 0};

static mp_obj_t qpy_wdt_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 0, 1, true);
    qpy_machine_wdt_singleton.base.type = type;
    qpy_machine_wdt_singleton.period = n_args > 0 ? mp_obj_get_int(args[0]) : 0;
    qosa_dev_watch_dog_update();
    return MP_OBJ_FROM_PTR(&qpy_machine_wdt_singleton);
}

static mp_obj_t qpy_wdt_feed(mp_obj_t self_in) {
    (void)self_in;
    qosa_dev_watch_dog_update();
    return MP_OBJ_NEW_SMALL_INT(0);
}
static MP_DEFINE_CONST_FUN_OBJ_1(qpy_wdt_feed_obj, qpy_wdt_feed);

static mp_obj_t qpy_wdt_stop(mp_obj_t self_in) {
    (void)self_in;
    return MP_OBJ_NEW_SMALL_INT(0);
}
static MP_DEFINE_CONST_FUN_OBJ_1(qpy_wdt_stop_obj, qpy_wdt_stop);

static const mp_rom_map_elem_t qpy_wdt_locals_table[] = {
    { MP_ROM_QSTR(MP_QSTR_feed), MP_ROM_PTR(&qpy_wdt_feed_obj) },
    { MP_ROM_QSTR(MP_QSTR_stop), MP_ROM_PTR(&qpy_wdt_stop_obj) },
};
static MP_DEFINE_CONST_DICT(qpy_wdt_locals_dict, qpy_wdt_locals_table);

MP_DEFINE_CONST_OBJ_TYPE(
    qpy_machine_wdt_type,
    MP_QSTR_WDT,
    MP_TYPE_FLAG_NONE,
    make_new, qpy_wdt_make_new,
    locals_dict, &qpy_wdt_locals_dict
    );
