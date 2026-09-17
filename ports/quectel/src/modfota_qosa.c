#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "py/obj.h"
#include "py/objstr.h"
#include "py/runtime.h"
#include "py/mpstate.h"
#include "mphalport.h"
#include "qosa_fota.h"
#include "qosa_power.h"
#include "qosa_sys.h"
#include "qurl_api.h"
#include "qurl_code.h"
#include "qurl_def.h"

// UniRTOS accepts one FotaToolkit-merged DFOTA .par, unlike legacy platforms
// which could consume two independent delta files.
#define QPY_FOTA_URL_MAX_LEN (256)
#define QPY_FOTA_TASK_STACK_SIZE (8192)
#define QPY_FOTA_TASK_PRIORITY QOSA_PRIORITY_NORMAL
#define QPY_FOTA_PDP_ID (1)
#define QPY_FOTA_SIM_ID (0)
#define QPY_FOTA_IDLE_TIMEOUT_SEC (60)
#define QPY_FOTA_PACKAGE_NAME "qpy_dfota.par"

// qcm_file_api.h is not part of the MicroPython global QSTR include set.
// Use the stable public declaration without widening that preprocessor path.
extern qosa_int64_t qcm_file_free_size(const char *path);

// Keep the data-call ABI local.  Including qosa_datacall.h pulls lwIP into
// the global MicroPython qstr preprocessor, where it shadows <errno.h>.
// These are the stable UniRTOS declarations used by the HTTP FOTA demo.
typedef long qpy_datacall_conn_t;
extern qpy_datacall_conn_t qosa_datacall_conn_new(qosa_uint8_t simid, qosa_uint8_t pdpid, int conn_type);
extern int qosa_datacall_start(qpy_datacall_conn_t conn, qosa_uint32_t max_wait_time);
extern int qosa_datacall_get_status(qpy_datacall_conn_t conn);

#define QPY_FOTA_DATACALL_CONN_TCPIP (3)
#define QPY_FOTA_DATACALL_CONN_INVALID ((qpy_datacall_conn_t)0x7fffffff)

typedef struct _qpy_fota_obj_t {
    mp_obj_base_t base;
    mp_obj_t callback;
    char url[QPY_FOTA_URL_MAX_LEN];
    qosa_task_t task;
    volatile int event_status;
    volatile int event_progress;
    volatile bool event_pending;
    volatile bool active;
    volatile bool release_after_event;
    qosa_fota_t *fota;
    qurl_core_t http;
    qosa_uint32_t expected_size;
    qosa_uint32_t received_size;
    int transfer_error;
} qpy_fota_obj_t;

MP_REGISTER_ROOT_POINTER(mp_obj_t qpy_fota_active_obj);

static void qpy_fota_queue_event(qpy_fota_obj_t *self, int status, int progress, bool release_after_event) {
    self->event_status = status;
    self->event_progress = progress;
    self->release_after_event = release_after_event;
    self->event_pending = true;
    mp_hal_stdio_wake();
}

void qpy_fota_poll_pending(void) {
    mp_obj_t active = MP_STATE_VM(qpy_fota_active_obj);
    if (active == MP_OBJ_NULL) {
        return;
    }
    qpy_fota_obj_t *self = MP_OBJ_TO_PTR(active);
    if (!self->event_pending) {
        return;
    }
    int status = self->event_status;
    int progress = self->event_progress;
    bool release_after_event = self->release_after_event;
    self->event_pending = false;
    if (self->callback != mp_const_none && self->callback != MP_OBJ_NULL) {
        mp_obj_t items[2] = { MP_OBJ_NEW_SMALL_INT(status), MP_OBJ_NEW_SMALL_INT(progress) };
        mp_sched_schedule(self->callback, mp_obj_new_list(2, items));
    }
    if (release_after_event) {
        MP_STATE_VM(qpy_fota_active_obj) = MP_OBJ_NULL;
    }
}

static int qpy_fota_activate_pdp(void) {
    qpy_datacall_conn_t conn = qosa_datacall_conn_new(QPY_FOTA_SIM_ID, QPY_FOTA_PDP_ID, QPY_FOTA_DATACALL_CONN_TCPIP);
    if (conn == QPY_FOTA_DATACALL_CONN_INVALID) {
        return -1;
    }
    if (!qosa_datacall_get_status(conn)
        && qosa_datacall_start(conn, 30) != 0) {
        return -1;
    }
    return 0;
}

static long qpy_fota_http_write_cb(unsigned char *buf, long size, void *arg) {
    qpy_fota_obj_t *self = (qpy_fota_obj_t *)arg;
    if (buf == NULL || size <= 0) {
        return 0;
    }
    if (self->received_size == 0) {
        long response_code = 0;
        long content_length = -1;
        qurl_core_getinfo(self->http, QURL_INFO_RESP_CODE, &response_code);
        qurl_core_getinfo(self->http, QURL_INFO_RESP_CONTENT_LENGTH, &content_length);
        if (response_code < 200 || response_code >= 300 || content_length == 0) {
            self->transfer_error = -1;
            return 0;
        }
        if (content_length > 0) {
            // qosa_fota_init() stores the incoming package in the mounted
            // LFS.  qosa_fota_get_partition_space() is not the available
            // LFS capacity on this platform and can return zero here; use
            // the same filesystem-space query as the UniRTOS HTTP FOTA demo.
            qosa_int64_t free_size = qcm_file_free_size("/");
            if (free_size < content_length) {
                self->transfer_error = -1;
                return 0;
            }
            self->expected_size = (qosa_uint32_t)content_length;
        }
    }
    if (qosa_fota_write_packet_data(self->fota, buf, (qosa_size_t)size) != QOSA_FOTA_OK) {
        self->transfer_error = -1;
        return 0;
    }
    self->received_size += (qosa_uint32_t)size;
    int progress = self->expected_size == 0 ? 0 : (int)((self->received_size * 100U) / self->expected_size);
    qpy_fota_queue_event(self, 1, progress > 100 ? 100 : progress, false);
    return size;
}

static int qpy_fota_http_download(qpy_fota_obj_t *self) {
    if (qurl_global_init() != QURL_OK || qurl_core_create(&self->http) != QURL_OK) {
        return -1;
    }
    qurl_core_setopt(self->http, QURL_OPT_URL, self->url);
    qurl_core_setopt(self->http, QURL_OPT_NETWORK_ID, QPY_FOTA_PDP_ID);
    qurl_core_setopt(self->http, QURL_OPT_HTTP_GET, 1L);
    qurl_core_setopt(self->http, QURL_OPT_WRITE_CB, qpy_fota_http_write_cb);
    qurl_core_setopt(self->http, QURL_OPT_WRITE_CB_ARG, self);
    qurl_core_setopt(self->http, QURL_OPT_TIMEOUT_MS, 0L);
    qurl_core_setopt(self->http, QURL_OPT_IDLE_TIMEOUT_MS, QPY_FOTA_IDLE_TIMEOUT_SEC * 1000L);
    qurl_core_setopt(self->http, QURL_OPT_FOLLOWLOCATION, 1L);
    qurl_ecode_t ret = qurl_core_perform(self->http);
    qurl_core_delete(self->http);
    self->http = QOSA_NULL;
    return ret == QURL_OK && self->transfer_error == 0 && self->received_size > 0 ? 0 : -1;
}

static void qpy_fota_task(void *arg) {
    qpy_fota_obj_t *self = (qpy_fota_obj_t *)arg;
    qosa_fota_mode_e mode = QOSA_FOTA_DFOTA_MODE;
    int success = 0;
    if (qpy_fota_activate_pdp() == 0
        && qosa_fota_set_opt(QOSA_FOTA_OPT_CMD_SET_FOTA_MODE, &mode) == QOSA_FOTA_OK) {
        self->fota = qosa_fota_init(QPY_FOTA_PACKAGE_NAME, QOSA_TRUE);
        if (self->fota != QOSA_NULL) {
            qpy_fota_queue_event(self, 0, 0, false);
            success = qpy_fota_http_download(self) == 0 && qosa_fota_verify_image(self->fota) == QOSA_FOTA_OK;
        }
    }
    if (!success) {
        if (self->fota != QOSA_NULL) {
            qosa_fota_deinit(self->fota);
            self->fota = QOSA_NULL;
        }
        self->task = QOSA_NULL;
        self->active = false;
        int progress = self->expected_size == 0 ? 0 : (int)((self->received_size * 100U) / self->expected_size);
        qpy_fota_queue_event(self, -1, progress > 100 ? 100 : progress, true);
        return;
    }
    qosa_fota_flag_set();
    self->task = QOSA_NULL;
    self->active = false;
    qpy_fota_queue_event(self, 2, 100, false);
    qosa_task_sleep_ms(250);
    qosa_power_reset(QOSA_RESET_FOTA);
}

static mp_obj_t qpy_fota_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args) {
    enum { ARG_key, ARG_cert, ARG_root_cert, ARG_reset_disable };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_key, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_cert, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_root_cert, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_reset_disable, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
    };
    mp_arg_val_t parsed[MP_ARRAY_SIZE(allowed_args)];
    mp_map_t kw_args;
    mp_map_init_fixed_table(&kw_args, n_kw, all_args + n_args);
    mp_arg_parse_all(n_args, all_args, &kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, parsed);
    // Retain the legacy constructor shape.  UniRTOS validates the package
    // using its configured platform policy, rather than per-instance keys.
    (void)parsed;
    qpy_fota_obj_t *self = mp_obj_malloc(qpy_fota_obj_t, type);
    self->callback = mp_const_none;
    self->url[0] = '\0';
    self->task = QOSA_NULL;
    self->event_pending = false;
    self->active = false;
    self->release_after_event = false;
    self->fota = QOSA_NULL;
    self->http = QOSA_NULL;
    self->expected_size = 0;
    self->received_size = 0;
    self->transfer_error = 0;
    return MP_OBJ_FROM_PTR(self);
}

static mp_obj_t qpy_fota_http_download_method(size_t n_args, const mp_obj_t *args, mp_map_t *kw_args) {
    enum { ARG_url1, ARG_url2, ARG_callback, ARG_header, ARG_ssl_params, ARG_public_key, ARG_mode };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_url1, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_url2, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_callback, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_header, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_ssl_params, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_public_key, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_mode, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = mp_const_none} },
    };
    qpy_fota_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    mp_arg_val_t parsed[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, parsed);
    if (self->active || MP_STATE_VM(qpy_fota_active_obj) != MP_OBJ_NULL
        || parsed[ARG_url1].u_obj == mp_const_none) {
        return MP_OBJ_NEW_SMALL_INT(-1);
    }
    if (parsed[ARG_url2].u_obj != mp_const_none) {
        if (!mp_obj_is_str_or_bytes(parsed[ARG_url2].u_obj)) {
            return MP_OBJ_NEW_SMALL_INT(-1);
        }
        size_t url2_len = 0;
        mp_obj_str_get_data(parsed[ARG_url2].u_obj, &url2_len);
        if (url2_len != 0) {
            return MP_OBJ_NEW_SMALL_INT(-1);
        }
    }
    if (parsed[ARG_callback].u_obj != mp_const_none && !mp_obj_is_callable(parsed[ARG_callback].u_obj)) {
        mp_raise_TypeError(MP_ERROR_TEXT("callback must be callable"));
    }
    if (!mp_obj_is_str_or_bytes(parsed[ARG_url1].u_obj)) {
        mp_raise_TypeError(MP_ERROR_TEXT("url1 must be a string"));
    }
    size_t url_len = 0;
    const char *url = mp_obj_str_get_data(parsed[ARG_url1].u_obj, &url_len);
    if (url_len == 0 || url_len >= sizeof(self->url)) {
        return MP_OBJ_NEW_SMALL_INT(-1);
    }
    // Preserve legacy keyword compatibility.  The UniRTOS implementation
    // intentionally uses the platform's configured TLS/signature policy.
    (void)parsed[ARG_header];
    (void)parsed[ARG_ssl_params];
    (void)parsed[ARG_public_key];
    (void)parsed[ARG_mode];
    memcpy(self->url, url, url_len);
    self->url[url_len] = '\0';
    self->callback = parsed[ARG_callback].u_obj;
    self->expected_size = 0;
    self->received_size = 0;
    self->transfer_error = 0;
    self->event_pending = false;
    self->active = true;
    MP_STATE_VM(qpy_fota_active_obj) = MP_OBJ_FROM_PTR(self);
    if (qosa_task_create(&self->task, QPY_FOTA_TASK_STACK_SIZE, QPY_FOTA_TASK_PRIORITY,
            "qpy_fota", qpy_fota_task, self, 1) != QOSA_OK) {
        self->task = QOSA_NULL;
        self->active = false;
        MP_STATE_VM(qpy_fota_active_obj) = MP_OBJ_NULL;
        return MP_OBJ_NEW_SMALL_INT(-1);
    }
    return MP_OBJ_NEW_SMALL_INT(0);
}
static MP_DEFINE_CONST_FUN_OBJ_KW(qpy_fota_http_download_obj, 1, qpy_fota_http_download_method);

static const mp_rom_map_elem_t qpy_fota_locals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_fota) },
    { MP_ROM_QSTR(MP_QSTR_httpDownload), MP_ROM_PTR(&qpy_fota_http_download_obj) },
};
static MP_DEFINE_CONST_DICT(qpy_fota_locals_dict, qpy_fota_locals_table);

MP_DEFINE_CONST_OBJ_TYPE(
    qpy_fota_type,
    MP_QSTR_fota,
    MP_TYPE_FLAG_NONE,
    make_new, qpy_fota_make_new,
    locals_dict, &qpy_fota_locals_dict
);

#if MICROPY_QPY_MODULE_FOTA
MP_REGISTER_MODULE(MP_QSTR_fota, qpy_fota_type);
#endif
