#include <string.h>

#include "py/obj.h"
#include "py/objstr.h"
#include "py/runtime.h"
#include "qosa_sms.h"
#include "qosa_sys.h"
#include "mphalport.h"
#include "qpy_compat_common.h"

extern qosa_bool_t qosa_datacall_wait_attached(qosa_uint8_t simid, qosa_uint32_t timeout_s);
typedef int (*qpy_sms_event_callback_t)(void *user_argv, void *argv);
extern int qosa_event_notify_register(int event, qpy_sms_event_callback_t event_cb, void *user_argv);

#define QPY_SMS_ADDR_INTERNATIONAL (145)
#define QPY_SMS_ADDR_NATIONAL (129)
#define QPY_SMS_MAX_TEXT_LEN (512)
#define QPY_SMS_MAX_RECORDS (128)
#define QPY_SMS_WAIT_ATTACH_TIMEOUT_MS (10000)
#define QPY_SMS_WAIT_RSP_TIMEOUT_MS (10000)
#define QPY_QOSA_CS_GSM (1)
#define QPY_QOSA_CS_UCS2 (2)
#define QPY_QOSA_EVENT_MODEM_SMS_STATUS (9)
#define QPY_QOSA_EVENT_MODEM_SMS_NEW_MSG (10)
#define QPY_QOSA_EVENT_MODEM_SMS_STORAGE_FULL (11)

static mp_obj_t qpy_sms_callback = MP_OBJ_NULL;
static qosa_sem_t qpy_sms_sem = NULL;
static volatile qosa_sms_err_e qpy_sms_last_err = QOSA_SMS_ERROR;
static volatile int qpy_sms_last_stage = 0;
static volatile int qpy_sms_last_mr = -1;
static volatile int qpy_sms_initialized = 0;
static volatile int qpy_sms_ready[2] = {0, 0};
static volatile int qpy_sms_pending_cb = 0;
static volatile int qpy_sms_pending_simid = 0;
static volatile int qpy_sms_pending_index = 0;
static volatile qosa_sms_stor_e qpy_sms_pending_storage = QOSA_SMS_STOR_ME;
static qosa_sms_record_t qpy_sms_last_record;

enum {
    QPY_SMS_STAGE_OK = 0,
    QPY_SMS_STAGE_SEM = 1,
    QPY_SMS_STAGE_ATTACH = 2,
    QPY_SMS_STAGE_TEXT_TO_PDU = 3,
    QPY_SMS_STAGE_SEND_ASYNC = 4,
    QPY_SMS_STAGE_TIMEOUT = 5,
    QPY_SMS_STAGE_CALLBACK = 6,
    QPY_SMS_STAGE_PARAM = 7,
    QPY_SMS_STAGE_UCS2 = 8,
    QPY_SMS_STAGE_PDU = 9,
};

static int qpy_sms_ret(qosa_sms_err_e err) {
    return err == QOSA_SMS_SUCCESS ? 0 : -1;
}

static void qpy_sms_set_last(int stage, qosa_sms_err_e err, int mr) {
    qpy_sms_last_stage = stage;
    qpy_sms_last_err = err;
    qpy_sms_last_mr = mr;
}


static int qpy_sms_event_status_cb(void *user_argv, void *argv) {
    (void)user_argv;
    qosa_sms_init_status_event_t *event = (qosa_sms_init_status_event_t *)argv;
    if (event != NULL && event->simid < 2) {
        qpy_sms_ready[event->simid] = event->status == QOSA_SMS_INIT_STATUS_READY ? 1 : 0;
    }
    return 0;
}

static int qpy_sms_event_new_msg_cb(void *user_argv, void *argv) {
    (void)user_argv;
    qosa_sms_new_msg_event_t *event = (qosa_sms_new_msg_event_t *)argv;
    if (event != NULL && qpy_sms_callback != MP_OBJ_NULL && qpy_sms_callback != mp_const_none) {
        int index = event->record.index;
        if (index > 0 && index != 0xffff) {
            index -= 1;
        }
        qpy_sms_pending_simid = event->simid;
        qpy_sms_pending_index = index;
        qpy_sms_pending_storage = event->storage;
        qpy_sms_pending_cb = 1;
        mp_hal_stdio_wake();
    }
    return 0;
}


void qpy_sms_poll_pending(void) {
    if (!qpy_sms_pending_cb || qpy_sms_callback == MP_OBJ_NULL || qpy_sms_callback == mp_const_none) {
        return;
    }
    qpy_sms_pending_cb = 0;
    mp_obj_t items[3] = {
        mp_obj_new_int(qpy_sms_pending_simid),
        mp_obj_new_int(qpy_sms_pending_index),
        mp_obj_new_str(qpy_sms_pending_storage == QOSA_SMS_STOR_SM ? "SM" : "ME", 2),
    };
    mp_sched_schedule(qpy_sms_callback, mp_obj_new_tuple(3, items));
}

static int qpy_sms_event_noop_cb(void *user_argv, void *argv) {
    (void)user_argv;
    (void)argv;
    return 0;
}

static void qpy_sms_apply_default_config(qosa_uint8_t simid) {
    qosa_sms_cfg_t cfg = {0};
    if (qosa_sms_get_config(simid, &cfg) == QOSA_SMS_SUCCESS) {
        cfg.mem1 = QOSA_SMS_STOR_ME;
        cfg.mem2 = QOSA_SMS_STOR_ME;
        cfg.mem3 = QOSA_SMS_STOR_ME;
        cfg.text_fo = 0x11;
        cfg.text_pid = 0x00;
        cfg.text_dcs = 0x00;
        cfg.text_vp = 0xff;
        qosa_sms_set_config(simid, &cfg);
        qpy_sms_ready[simid] = 1;
    }
}

static void qpy_sms_init_once(void) {
    if (qpy_sms_initialized) {
        return;
    }
    qpy_sms_initialized = 1;
    qpy_sms_apply_default_config(0);
    qpy_sms_apply_default_config(1);
    qosa_event_notify_register(QPY_QOSA_EVENT_MODEM_SMS_STATUS, qpy_sms_event_status_cb, NULL);
    qosa_event_notify_register(QPY_QOSA_EVENT_MODEM_SMS_NEW_MSG, qpy_sms_event_new_msg_cb, NULL);
    qosa_event_notify_register(QPY_QOSA_EVENT_MODEM_SMS_STORAGE_FULL, qpy_sms_event_noop_cb, NULL);
}

static qosa_sms_stor_e qpy_sms_storage_from_obj(mp_obj_t obj) {
    const char *mem = mp_obj_str_get_str(obj);
    if (strcmp(mem, "ME") == 0) {
        return QOSA_SMS_STOR_ME;
    }
    if (strcmp(mem, "SM") == 0) {
        return QOSA_SMS_STOR_SM;
    }
    mp_raise_ValueError(MP_ERROR_TEXT("invalid value, mem can only be SM, ME."));
}

static const char *qpy_sms_storage_name(qosa_uint8_t mem) {
    return mem == QOSA_SMS_STOR_SM ? "SM" : "ME";
}

static int qpy_sms_sane_used(qosa_uint16_t used, qosa_uint16_t total) {
    if (total == 0 || total == 0xffff || used == 0xffff || used > total) {
        return 0;
    }
    return used;
}

static int qpy_sms_used_is_sane(qosa_uint16_t used, qosa_uint16_t total) {
    return total != 0 && total != 0xffff && total <= QPY_SMS_MAX_RECORDS && used != 0xffff && used <= total;
}

static int qpy_sms_sane_total(qosa_uint16_t total) {
    return total == 0xffff || total > QPY_SMS_MAX_RECORDS ? 0 : total;
}

static int qpy_sms_caseeq(const char *a, const char *b) {
    while (*a != '\0' && *b != '\0') {
        char ca = *a >= 'a' && *a <= 'z' ? *a - 32 : *a;
        char cb = *b >= 'a' && *b <= 'z' ? *b - 32 : *b;
        if (ca != cb) {
            return 0;
        }
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

static qosa_uint8_t qpy_sms_address_type(const char *addr) {
    return addr != NULL && addr[0] == '+' ? QPY_SMS_ADDR_INTERNATIONAL : QPY_SMS_ADDR_NATIONAL;
}

static int qpy_sms_ensure_sem(void) {
    if (qpy_sms_sem != NULL) {
        return 0;
    }
    return qosa_sem_create(&qpy_sms_sem, 0) == 0 ? 0 : -1;
}

static int qpy_sms_wait_result(void) {
    if (qpy_sms_sem == NULL) {
        qpy_sms_set_last(QPY_SMS_STAGE_SEM, QOSA_SMS_ERROR, -1);
        return -1;
    }
    if (qosa_sem_wait(qpy_sms_sem, QPY_SMS_WAIT_RSP_TIMEOUT_MS) != 0) {
        qpy_sms_set_last(QPY_SMS_STAGE_TIMEOUT, QOSA_SMS_ERROR, -1);
        return -1;
    }
    if (qpy_sms_last_err != QOSA_SMS_SUCCESS) {
        qpy_sms_last_stage = QPY_SMS_STAGE_CALLBACK;
        return -1;
    }
    qpy_sms_last_stage = QPY_SMS_STAGE_OK;
    return 0;
}

static void qpy_sms_send_rsp(void *ctx, void *argv) {
    (void)ctx;
    qosa_sms_send_pdu_cnf_t *cnf = (qosa_sms_send_pdu_cnf_t *)argv;
    qpy_sms_last_err = cnf == NULL ? QOSA_SMS_ERROR : (qosa_sms_err_e)cnf->err_code;
    qpy_sms_last_mr = cnf == NULL ? -1 : cnf->mr;
    if (qpy_sms_sem != NULL) {
        qosa_sem_release(qpy_sms_sem);
    }
}

static void qpy_sms_general_rsp(void *ctx, void *argv) {
    (void)ctx;
    qosa_sms_general_cnf_t *cnf = (qosa_sms_general_cnf_t *)argv;
    qpy_sms_last_err = cnf == NULL ? QOSA_SMS_ERROR : (qosa_sms_err_e)cnf->err_code;
    qpy_sms_last_mr = -1;
    if (qpy_sms_sem != NULL) {
        qosa_sem_release(qpy_sms_sem);
    }
}

static void qpy_sms_read_rsp(void *ctx, void *argv) {
    (void)ctx;
    qosa_sms_read_pdu_cnf_t *cnf = (qosa_sms_read_pdu_cnf_t *)argv;
    qpy_sms_last_err = cnf == NULL ? QOSA_SMS_ERROR : (qosa_sms_err_e)cnf->err_code;
    qpy_sms_last_mr = -1;
    if (cnf != NULL && cnf->err_code == QOSA_SMS_SUCCESS) {
        qpy_sms_last_record = cnf->record;
    }
    if (qpy_sms_sem != NULL) {
        qosa_sem_release(qpy_sms_sem);
    }
}

static qosa_sms_stor_e qpy_sms_get_read_storage(qosa_uint8_t simid);

static int qpy_sms_hex_nibble(unsigned char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static int qpy_sms_is_even_hex(const qosa_uint8_t *data, size_t len) {
    if (len == 0 || (len & 1) != 0) {
        return 0;
    }
    for (size_t i = 0; i < len; ++i) {
        if (qpy_sms_hex_nibble(data[i]) < 0) {
            return 0;
        }
    }
    return 1;
}

static int qpy_sms_is_ucs2_hex(const qosa_uint8_t *data, size_t len) {
    return (len % 4) == 0 && qpy_sms_is_even_hex(data, len);
}

static int qpy_sms_hex_to_bytes(const qosa_uint8_t *hex, size_t hex_len, qosa_uint8_t *out, size_t out_len) {
    if (!qpy_sms_is_even_hex(hex, hex_len) || out_len < (hex_len / 2)) {
        return -1;
    }
    for (size_t i = 0; i < hex_len; i += 2) {
        int hi = qpy_sms_hex_nibble(hex[i]);
        int lo = qpy_sms_hex_nibble(hex[i + 1]);
        if (hi < 0 || lo < 0) {
            return -1;
        }
        out[i / 2] = (qosa_uint8_t)((hi << 4) | lo);
    }
    return 0;
}

static int qpy_sms_put_utf8(uint32_t cp, char *out, size_t out_len, size_t *pos) {
    if (cp <= 0x7f) {
        if (*pos + 1 >= out_len) {
            return -1;
        }
        out[(*pos)++] = (char)cp;
    } else if (cp <= 0x7ff) {
        if (*pos + 2 >= out_len) {
            return -1;
        }
        out[(*pos)++] = (char)(0xc0 | (cp >> 6));
        out[(*pos)++] = (char)(0x80 | (cp & 0x3f));
    } else {
        if (*pos + 3 >= out_len) {
            return -1;
        }
        out[(*pos)++] = (char)(0xe0 | (cp >> 12));
        out[(*pos)++] = (char)(0x80 | ((cp >> 6) & 0x3f));
        out[(*pos)++] = (char)(0x80 | (cp & 0x3f));
    }
    out[*pos] = '\0';
    return 0;
}

static int qpy_sms_ucs2_hex_to_utf8(const qosa_uint8_t *hex, size_t hex_len, char *out, size_t out_len) {
    if (!qpy_sms_is_ucs2_hex(hex, hex_len) || out_len == 0) {
        return -1;
    }
    size_t pos = 0;
    out[0] = '\0';
    for (size_t i = 0; i < hex_len; i += 4) {
        int n0 = qpy_sms_hex_nibble(hex[i]);
        int n1 = qpy_sms_hex_nibble(hex[i + 1]);
        int n2 = qpy_sms_hex_nibble(hex[i + 2]);
        int n3 = qpy_sms_hex_nibble(hex[i + 3]);
        if (n0 < 0 || n1 < 0 || n2 < 0 || n3 < 0) {
            return -1;
        }
        uint32_t cp = (uint32_t)((n0 << 12) | (n1 << 8) | (n2 << 4) | n3);
        if (cp >= 0xd800 && cp <= 0xdfff) {
            cp = '?';
        }
        if (qpy_sms_put_utf8(cp, out, out_len, &pos) != 0) {
            return -1;
        }
    }
    return 0;
}

static mp_obj_t qpy_sms_send_text_msg(size_t n_args, const mp_obj_t *args) {
    qpy_sms_init_once();
    mp_buffer_info_t phone = {0};
    mp_buffer_info_t text = {0};
    mp_get_buffer_raise(args[0], &phone, MP_BUFFER_READ);
    mp_get_buffer_raise(args[1], &text, MP_BUFFER_READ);
    const char *codemode = mp_obj_str_get_str(args[2]);
    qosa_uint8_t simid = qpy_get_simid(n_args, args, 3);

    qosa_sms_msg_t msg = {0};
    if (phone.len == 0 || phone.len > QOSA_ADDRESS_MAX_LEN) {
        mp_raise_ValueError(MP_ERROR_TEXT("The [phonenumber] parameter is invalid."));
    }
    if (text.len == 0 || text.len > QPY_SMS_MAX_TEXT_LEN || text.len >= sizeof(msg.text.send.data)) {
        mp_raise_ValueError(MP_ERROR_TEXT("Empty SMS is not supported."));
    }
    if (qpy_sms_ensure_sem() != 0) {
        qpy_sms_set_last(QPY_SMS_STAGE_SEM, QOSA_SMS_ERROR, -1);
        return mp_obj_new_int(-1);
    }
    if (!qosa_datacall_wait_attached(simid, QPY_SMS_WAIT_ATTACH_TIMEOUT_MS / 1000)) {
        qpy_sms_set_last(QPY_SMS_STAGE_ATTACH, QOSA_SMS_ERROR, -1);
        return mp_obj_new_int(-1);
    }

    qosa_sms_record_t record = {0};
    qosa_sms_send_param_t send = {0};
    qosa_sms_cfg_t cfg = {0};
    qosa_sms_get_config(simid, &cfg);
    msg.msg_type = QOSA_SMS_SUBMIT;
    msg.text.send.status = 0xff;
    msg.text.send.toda = qpy_sms_address_type((const char *)phone.buf);
    msg.text.send.fo = 0x11;
    msg.text.send.pid = 0x00;
    msg.text.send.vp = 0xff;
    memcpy(msg.text.send.da, phone.buf, phone.len);
    msg.text.send.da[phone.len] = '\0';
    msg.is_concatenated = 0;
    msg.concat.msg_ref_number = 0;
    msg.concat.msg_seg = 0;
    msg.concat.msg_total = 0;

    qosa_sms_set_charset(QPY_QOSA_CS_GSM);
    if (qpy_sms_caseeq(codemode, "UCS2")) {
        msg.text.send.dcs = 0x08;
        msg.text.send.data_chset = QPY_QOSA_CS_UCS2;
        if (qpy_sms_is_ucs2_hex((const qosa_uint8_t *)text.buf, text.len)) {
            msg.text.send.data_len = (qosa_uint16_t)text.len;
            memcpy(msg.text.send.data, text.buf, text.len);
            msg.text.send.data[text.len] = '\0';
        } else {
            char utf8[QPY_SMS_MAX_TEXT_LEN + 1] = {0};
            size_t copy_len = text.len < QPY_SMS_MAX_TEXT_LEN ? text.len : QPY_SMS_MAX_TEXT_LEN;
            memcpy(utf8, text.buf, copy_len);
            qosa_sms_err_e err = qosa_sms_utf8_to_ucs2(utf8, (char *)msg.text.send.data, sizeof(msg.text.send.data));
            if (err != QOSA_SMS_SUCCESS) {
                qpy_sms_set_last(QPY_SMS_STAGE_UCS2, err, -1);
                return mp_obj_new_int(-1);
            }
            msg.text.send.data_len = (qosa_uint16_t)strlen((const char *)msg.text.send.data);
            if (msg.text.send.data_len == 0) {
                qpy_sms_set_last(QPY_SMS_STAGE_UCS2, QOSA_SMS_ERROR, -1);
                return mp_obj_new_int(-1);
            }
        }
    } else if (qpy_sms_caseeq(codemode, "GSM")) {
        msg.text.send.dcs = 0x00;
        msg.text.send.data_chset = QPY_QOSA_CS_GSM;
        msg.text.send.data_len = (qosa_uint16_t)text.len;
        memcpy(msg.text.send.data, text.buf, text.len);
        msg.text.send.data[text.len] = '\0';
    } else {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid value, codemode can only be UCS2 or GSM."));
    }

    qosa_sms_err_e err = qosa_sms_text_to_pdu(&msg, &record);
    if (err != QOSA_SMS_SUCCESS) {
        qpy_sms_set_last(QPY_SMS_STAGE_TEXT_TO_PDU, err, -1);
        return mp_obj_new_int(-1);
    }
    send.pdu.data_len = record.pdu.data_len;
    memcpy(send.pdu.data, record.pdu.data, record.pdu.data_len);
    qpy_sms_set_last(QPY_SMS_STAGE_SEND_ASYNC, QOSA_SMS_ERROR, -1);
    err = qosa_sms_send_pdu_async(simid, &send, qpy_sms_send_rsp, NULL);
    if (err != QOSA_SMS_SUCCESS) {
        qpy_sms_set_last(QPY_SMS_STAGE_SEND_ASYNC, err, -1);
        return mp_obj_new_int(-1);
    }
    return mp_obj_new_int(qpy_sms_wait_result());
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_sms_send_text_msg_obj, 3, 4, qpy_sms_send_text_msg);

static mp_obj_t qpy_sms_send_pdu_msg(size_t n_args, const mp_obj_t *args) {
    qpy_sms_init_once();
    mp_buffer_info_t pdu = {0};
    mp_get_buffer_raise(args[0], &pdu, MP_BUFFER_READ);
    qosa_uint16_t pdu_len = (qosa_uint16_t)mp_obj_get_int(args[1]);
    qosa_uint8_t simid = qpy_get_simid(n_args, args, 2);
    if (pdu.len == 0 || pdu_len == 0 || pdu.len > (QOSA_SMS_PDU_MAX_LEN * 2)) {
        qpy_sms_set_last(QPY_SMS_STAGE_PARAM, QOSA_SMS_ERROR, -1);
        return mp_obj_new_int(-1);
    }
    if (qpy_sms_ensure_sem() != 0) {
        qpy_sms_set_last(QPY_SMS_STAGE_SEM, QOSA_SMS_ERROR, -1);
        return mp_obj_new_int(-1);
    }
    if (!qosa_datacall_wait_attached(simid, QPY_SMS_WAIT_ATTACH_TIMEOUT_MS / 1000)) {
        qpy_sms_set_last(QPY_SMS_STAGE_ATTACH, QOSA_SMS_ERROR, -1);
        return mp_obj_new_int(-1);
    }
    qosa_sms_send_param_t send = {0};
    qosa_uint8_t pdu_bytes[QOSA_SMS_PDU_MAX_LEN] = {0};
    if (qpy_sms_hex_to_bytes((const qosa_uint8_t *)pdu.buf, pdu.len, pdu_bytes, sizeof(pdu_bytes)) != 0) {
        qpy_sms_set_last(QPY_SMS_STAGE_PDU, QOSA_SMS_ERROR, -1);
        return mp_obj_new_int(-1);
    }
    qosa_uint8_t sca_len = QOSA_SMS_GET_SCA_PART_LEN(pdu_bytes);
    if ((sca_len + pdu_len) != (pdu.len / 2)) {
        qpy_sms_set_last(QPY_SMS_STAGE_PDU, QOSA_SMS_ERROR, -1);
        return mp_obj_new_int(-1);
    }
    send.pdu.data_len = (qosa_uint8_t)(pdu.len / 2);
    memcpy(send.pdu.data, pdu_bytes, send.pdu.data_len);
    qpy_sms_set_last(QPY_SMS_STAGE_SEND_ASYNC, QOSA_SMS_ERROR, -1);
    qosa_sms_err_e err = qosa_sms_send_pdu_async(simid, &send, qpy_sms_send_rsp, NULL);
    if (err != QOSA_SMS_SUCCESS) {
        qpy_sms_set_last(QPY_SMS_STAGE_SEND_ASYNC, err, -1);
        return mp_obj_new_int(-1);
    }
    return mp_obj_new_int(qpy_sms_wait_result());
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_sms_send_pdu_msg_obj, 2, 3, qpy_sms_send_pdu_msg);

static mp_obj_t qpy_sms_delete_msg(size_t n_args, const mp_obj_t *args) {
    qpy_sms_init_once();
    int index = mp_obj_get_int(args[0]);
    int delmode = n_args > 1 ? mp_obj_get_int(args[1]) : 0;
    qosa_uint8_t simid = qpy_get_simid(n_args, args, 2);
    if (index < 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid value, index must be greater than or equal to 0."));
    }
    if (delmode != 0 && delmode != 4) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid value, delmode just can be 0 or 4."));
    }

    qosa_sms_delete_param_t param = {0};
    param.stor = qpy_sms_get_read_storage(simid);
    if (delmode == 4) {
        param.index_or_stat = QOSA_SMS_SELECT_BY_STATUS;
        param.delflag = QOSA_SMS_DEL_ALL;
    } else {
        param.index_or_stat = QOSA_SMS_SELECT_BY_INDEX;
        param.index = (qosa_uint16_t)index;
    }
    if (qpy_sms_ensure_sem() != 0) {
        return mp_obj_new_int(-1);
    }
    qpy_sms_set_last(QPY_SMS_STAGE_SEND_ASYNC, QOSA_SMS_ERROR, -1);
    qosa_sms_err_e err = qosa_sms_delete_async(simid, &param, qpy_sms_general_rsp, NULL);
    if (err != QOSA_SMS_SUCCESS) {
        return mp_obj_new_int(-1);
    }
    return mp_obj_new_int(qpy_sms_wait_result());
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_sms_delete_msg_obj, 1, 3, qpy_sms_delete_msg);

static qosa_sms_stor_e qpy_sms_get_read_storage(qosa_uint8_t simid) {
    qosa_sms_cfg_t cfg = {0};
    if (qosa_sms_get_config(simid, &cfg) == QOSA_SMS_SUCCESS) {
        return cfg.mem1;
    }
    return QOSA_SMS_STOR_ME;
}

static int qpy_sms_read_record_from_storage(qosa_uint8_t simid, qosa_sms_stor_e storage, int index) {
    qpy_sms_init_once();
    if (index < 0 || index > 255) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid value, index must be greater than or equal to 0."));
    }
    if (qpy_sms_ensure_sem() != 0) {
        qpy_sms_set_last(QPY_SMS_STAGE_SEM, QOSA_SMS_ERROR, -1);
        return -1;
    }
    qosa_sms_read_param_t read = {0};
    read.stor = storage;
    read.index = (qosa_uint16_t)index;
    memset(&qpy_sms_last_record, 0, sizeof(qpy_sms_last_record));
    qpy_sms_set_last(QPY_SMS_STAGE_SEND_ASYNC, QOSA_SMS_ERROR, -1);
    qosa_sms_err_e err = qosa_sms_read_pdu_async(simid, &read, qpy_sms_read_rsp, NULL);
    if (err != QOSA_SMS_SUCCESS) {
        qpy_sms_set_last(QPY_SMS_STAGE_SEND_ASYNC, err, -1);
        return -1;
    }
    return qpy_sms_wait_result();
}

static int qpy_sms_read_record(qosa_uint8_t simid, int index) {
    return qpy_sms_read_record_from_storage(simid, qpy_sms_get_read_storage(simid), index);
}

static int qpy_sms_count_storage(qosa_uint8_t simid, qosa_sms_stor_e storage, qosa_uint16_t total) {
    int count = 0;
    int last_stage = qpy_sms_last_stage;
    qosa_sms_err_e last_err = qpy_sms_last_err;
    int last_mr = qpy_sms_last_mr;
    int limit = qpy_sms_sane_total(total);
    if (limit <= 0) {
        return 0;
    }
    if (limit > QPY_SMS_MAX_RECORDS) {
        limit = QPY_SMS_MAX_RECORDS;
    }
    for (int i = 0; i < limit; ++i) {
        if (qpy_sms_read_record_from_storage(simid, storage, i) == 0) {
            ++count;
        }
    }
    qpy_sms_set_last(last_stage, last_err, last_mr);
    return count;
}

static mp_obj_t qpy_sms_record_to_text_tuple(void) {
    qosa_sms_msg_t msg = {0};
    if (qosa_sms_pdu_to_text(&qpy_sms_last_record, &msg) != QOSA_SMS_SUCCESS) {
        return mp_obj_new_int(-1);
    }

    const char *phone = "";
    const char *data = "";
    qosa_uint8_t dcs = 0;
    qosa_uint16_t data_len = 0;
    if (msg.msg_type == QOSA_SMS_DELIVER) {
        phone = msg.text.recv.oa;
        data = (const char *)msg.text.recv.data;
        dcs = msg.text.recv.dcs;
        data_len = msg.text.recv.data_len;
    } else if (msg.msg_type == QOSA_SMS_SUBMIT) {
        phone = msg.text.send.da;
        data = (const char *)msg.text.send.data;
        dcs = msg.text.send.dcs;
        data_len = msg.text.send.data_len;
    } else if (msg.msg_type == QOSA_SMS_STATUS_REPORT) {
        phone = msg.text.report.ra;
        data = "";
    } else {
        return mp_obj_new_int(-1);
    }

    char utf8[QPY_SMS_MAX_TEXT_LEN + 1] = {0};
    if (QOSA_SMS_ALPHABET(dcs) == QOSA_SMS_ALPHA_16BIT &&
        qpy_sms_ucs2_hex_to_utf8((const qosa_uint8_t *)data, strlen(data), utf8, sizeof(utf8)) == 0) {
        data = utf8;
        data_len = (qosa_uint16_t)strlen(utf8);
    } else if (data_len == 0) {
        data_len = (qosa_uint16_t)strlen(data);
    }

    mp_obj_t tuple[3] = {
        mp_obj_new_str(phone, strlen(phone)),
        mp_obj_new_str(data, strlen(data)),
        mp_obj_new_int(data_len),
    };
    return mp_obj_new_tuple(3, tuple);
}

static mp_obj_t qpy_sms_search_text_msg(size_t n_args, const mp_obj_t *args) {
    qosa_uint8_t simid = qpy_get_simid(n_args, args, 1);
    int index = mp_obj_get_int(args[0]);
    if (qpy_sms_read_record(simid, index) != 0) {
        return mp_obj_new_int(-1);
    }
    return qpy_sms_record_to_text_tuple();
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_sms_search_text_msg_obj, 1, 2, qpy_sms_search_text_msg);

static void qpy_sms_bytes_to_hex(const qosa_uint8_t *data, size_t len, char *out) {
    static const char hex[] = "0123456789ABCDEF";
    for (size_t i = 0; i < len; ++i) {
        out[i * 2] = hex[(data[i] >> 4) & 0x0f];
        out[i * 2 + 1] = hex[data[i] & 0x0f];
    }
    out[len * 2] = '\0';
}

static mp_obj_t qpy_sms_search_pdu_msg(size_t n_args, const mp_obj_t *args) {
    qosa_uint8_t simid = qpy_get_simid(n_args, args, 1);
    int index = mp_obj_get_int(args[0]);
    if (qpy_sms_read_record(simid, index) != 0 || qpy_sms_last_record.pdu.data_len == 0) {
        return mp_obj_new_int(-1);
    }
    char pdu_hex[(QOSA_SMS_PDU_MAX_LEN * 2) + 1] = {0};
    qpy_sms_bytes_to_hex(qpy_sms_last_record.pdu.data, qpy_sms_last_record.pdu.data_len, pdu_hex);
    return mp_obj_new_str(pdu_hex, strlen(pdu_hex));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_sms_search_pdu_msg_obj, 1, 2, qpy_sms_search_pdu_msg);

static mp_obj_t qpy_sms_get_pdu_length(mp_obj_t pdu_obj) {
    mp_buffer_info_t pdu = {0};
    mp_get_buffer_raise(pdu_obj, &pdu, MP_BUFFER_READ);
    qosa_uint8_t pdu_bytes[QOSA_SMS_PDU_MAX_LEN] = {0};
    if (qpy_sms_hex_to_bytes((const qosa_uint8_t *)pdu.buf, pdu.len, pdu_bytes, sizeof(pdu_bytes)) != 0) {
        return mp_obj_new_int(-1);
    }
    qosa_uint8_t sca_len = QOSA_SMS_GET_SCA_PART_LEN(pdu_bytes);
    if (sca_len > (pdu.len / 2)) {
        return mp_obj_new_int(-1);
    }
    return mp_obj_new_int((pdu.len / 2) - sca_len);
}
static MP_DEFINE_CONST_FUN_OBJ_1(qpy_sms_get_pdu_length_obj, qpy_sms_get_pdu_length);

static mp_obj_t qpy_sms_get_center_address(size_t n_args, const mp_obj_t *args) {
    qpy_sms_init_once();
    qosa_uint8_t simid = qpy_get_simid(n_args, args, 0);
    qosa_sms_address_info_t sca = {0};
    char text[QOSA_ADDRESS_MAX_LEN + 4] = {0};
    if (qosa_sms_get_sca(simid, &sca) != QOSA_SMS_SUCCESS) {
        return mp_obj_new_int(-1);
    }
    if (qosa_sms_address_to_text(&sca, text, sizeof(text)) != QOSA_SMS_SUCCESS) {
        return mp_obj_new_int(-1);
    }
    return mp_obj_new_str(text, strlen(text));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_sms_get_center_address_obj, 0, 1, qpy_sms_get_center_address);

static mp_obj_t qpy_sms_set_center_address(size_t n_args, const mp_obj_t *args) {
    qpy_sms_init_once();
    mp_buffer_info_t addr = {0};
    mp_get_buffer_raise(args[0], &addr, MP_BUFFER_READ);
    qosa_uint8_t simid = qpy_get_simid(n_args, args, 1);
    if (addr.len == 0 || addr.len > QOSA_ADDRESS_MAX_LEN) {
        return mp_obj_new_int(-1);
    }
    char buf[QOSA_ADDRESS_MAX_LEN + 2] = {0};
    memcpy(buf, addr.buf, addr.len);
    qosa_sms_address_info_t sca = {0};
    if (qosa_sms_text_to_address(buf, (qosa_uint16_t)strlen(buf), qpy_sms_address_type(buf), &sca) != QOSA_SMS_SUCCESS) {
        return mp_obj_new_int(-1);
    }
    return mp_obj_new_int(qpy_sms_ret(qosa_sms_set_sca(simid, &sca)));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_sms_set_center_address_obj, 1, 2, qpy_sms_set_center_address);

static mp_obj_t qpy_sms_set_save_location(size_t n_args, const mp_obj_t *args) {
    qpy_sms_init_once();
    qosa_uint8_t simid = qpy_get_simid(n_args, args, 3);
    qosa_sms_cfg_t cfg = {0};
    if (qosa_sms_get_config(simid, &cfg) != QOSA_SMS_SUCCESS) {
        return mp_obj_new_int(-1);
    }
    cfg.mem1 = qpy_sms_storage_from_obj(args[0]);
    cfg.mem2 = qpy_sms_storage_from_obj(args[1]);
    cfg.mem3 = qpy_sms_storage_from_obj(args[2]);
    return mp_obj_new_int(qpy_sms_ret(qosa_sms_set_config(simid, &cfg)));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_sms_set_save_location_obj, 3, 4, qpy_sms_set_save_location);

static mp_obj_t qpy_sms_get_save_location(size_t n_args, const mp_obj_t *args) {
    qpy_sms_init_once();
    qosa_uint8_t simid = qpy_get_simid(n_args, args, 0);
    qosa_sms_cfg_t cfg = {0};
    qosa_sms_stor_info_t info = {0};
    if (qosa_sms_get_config(simid, &cfg) != QOSA_SMS_SUCCESS ||
        qosa_sms_get_stor_info(simid, &info) != QOSA_SMS_SUCCESS) {
        return mp_obj_new_int(-1);
    }
    int used_sm = qpy_sms_used_is_sane(info.used_sm, info.total_sm)
        ? qpy_sms_sane_used(info.used_sm, info.total_sm)
        : qpy_sms_count_storage(simid, QOSA_SMS_STOR_SM, info.total_sm);
    int used_me = qpy_sms_used_is_sane(info.used_me, info.total_me)
        ? qpy_sms_sane_used(info.used_me, info.total_me)
        : qpy_sms_count_storage(simid, QOSA_SMS_STOR_ME, info.total_me);

    mp_obj_t mem1[] = {
        mp_obj_new_str(qpy_sms_storage_name(cfg.mem1), 2),
        mp_obj_new_int(cfg.mem1 == QOSA_SMS_STOR_SM ? used_sm : used_me),
        mp_obj_new_int(cfg.mem1 == QOSA_SMS_STOR_SM ? qpy_sms_sane_total(info.total_sm) : qpy_sms_sane_total(info.total_me)),
    };
    mp_obj_t mem2[] = {
        mp_obj_new_str(qpy_sms_storage_name(cfg.mem2), 2),
        mp_obj_new_int(cfg.mem2 == QOSA_SMS_STOR_SM ? used_sm : used_me),
        mp_obj_new_int(cfg.mem2 == QOSA_SMS_STOR_SM ? qpy_sms_sane_total(info.total_sm) : qpy_sms_sane_total(info.total_me)),
    };
    mp_obj_t mem3[] = {
        mp_obj_new_str(qpy_sms_storage_name(cfg.mem3), 2),
        mp_obj_new_int(cfg.mem3 == QOSA_SMS_STOR_SM ? used_sm : used_me),
        mp_obj_new_int(cfg.mem3 == QOSA_SMS_STOR_SM ? qpy_sms_sane_total(info.total_sm) : qpy_sms_sane_total(info.total_me)),
    };
    mp_obj_t tuple[] = {
        mp_obj_new_list(3, mem1),
        mp_obj_new_list(3, mem2),
        mp_obj_new_list(3, mem3),
    };
    return mp_obj_new_tuple(3, tuple);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_sms_get_save_location_obj, 0, 1, qpy_sms_get_save_location);

static mp_obj_t qpy_sms_get_msg_nums(size_t n_args, const mp_obj_t *args) {
    qpy_sms_init_once();
    qosa_uint8_t simid = qpy_get_simid(n_args, args, 0);
    qosa_sms_cfg_t cfg = {0};
    qosa_sms_stor_info_t info = {0};
    if (qosa_sms_get_config(simid, &cfg) != QOSA_SMS_SUCCESS ||
        qosa_sms_get_stor_info(simid, &info) != QOSA_SMS_SUCCESS) {
        return mp_obj_new_int(-1);
    }
    if (cfg.mem1 == QOSA_SMS_STOR_SM) {
        int used_sm = qpy_sms_used_is_sane(info.used_sm, info.total_sm)
            ? qpy_sms_sane_used(info.used_sm, info.total_sm)
            : qpy_sms_count_storage(simid, QOSA_SMS_STOR_SM, info.total_sm);
        return mp_obj_new_int(used_sm);
    }
    int used_me = qpy_sms_used_is_sane(info.used_me, info.total_me)
        ? qpy_sms_sane_used(info.used_me, info.total_me)
        : qpy_sms_count_storage(simid, QOSA_SMS_STOR_ME, info.total_me);
    return mp_obj_new_int(used_me);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_sms_get_msg_nums_obj, 0, 1, qpy_sms_get_msg_nums);

static mp_obj_t qpy_sms_get_msg_status(void) {
    qpy_sms_init_once();
    qosa_sms_stor_info_t info = {0};
    return mp_obj_new_int(qpy_sms_ready[0] || qosa_sms_get_stor_info(0, &info) == QOSA_SMS_SUCCESS ? 1 : 0);
}
static MP_DEFINE_CONST_FUN_OBJ_0(qpy_sms_get_msg_status_obj, qpy_sms_get_msg_status);

static mp_obj_t qpy_sms_set_callback(mp_obj_t handler) {
    qpy_sms_init_once();
    if (handler != mp_const_none && !mp_obj_is_callable(handler)) {
        mp_raise_TypeError(MP_ERROR_TEXT("callback must be callable"));
    }
    qpy_sms_callback = handler;
    return mp_obj_new_int(0);
}
static MP_DEFINE_CONST_FUN_OBJ_1(qpy_sms_set_callback_obj, qpy_sms_set_callback);


static mp_obj_t qpy_sms_get_last_error(void) {
    mp_obj_t tuple[3] = {
        mp_obj_new_int(qpy_sms_last_stage),
        mp_obj_new_int(qpy_sms_last_err),
        mp_obj_new_int(qpy_sms_last_mr),
    };
    return mp_obj_new_tuple(3, tuple);
}
static MP_DEFINE_CONST_FUN_OBJ_0(qpy_sms_get_last_error_obj, qpy_sms_get_last_error);

static const mp_rom_map_elem_t qpy_sms_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_sms) },
    { MP_ROM_QSTR(MP_QSTR_decodePdu), MP_ROM_PTR(&qpy_stub_ret_empty_tuple_obj) },
    { MP_ROM_QSTR(MP_QSTR_sendTextMsg), MP_ROM_PTR(&qpy_sms_send_text_msg_obj) },
    { MP_ROM_QSTR(MP_QSTR_sendPduMsg), MP_ROM_PTR(&qpy_sms_send_pdu_msg_obj) },
    { MP_ROM_QSTR(MP_QSTR_readMsg), MP_ROM_PTR(&qpy_sms_search_text_msg_obj) },
    { MP_ROM_QSTR(MP_QSTR_deleteMsg), MP_ROM_PTR(&qpy_sms_delete_msg_obj) },
    { MP_ROM_QSTR(MP_QSTR_setCallback), MP_ROM_PTR(&qpy_sms_set_callback_obj) },
    { MP_ROM_QSTR(MP_QSTR_searchTextMsg), MP_ROM_PTR(&qpy_sms_search_text_msg_obj) },
    { MP_ROM_QSTR(MP_QSTR_searchPduMsg), MP_ROM_PTR(&qpy_sms_search_pdu_msg_obj) },
    { MP_ROM_QSTR(MP_QSTR_getPduLength), MP_ROM_PTR(&qpy_sms_get_pdu_length_obj) },
    { MP_ROM_QSTR(MP_QSTR_getMsgNums), MP_ROM_PTR(&qpy_sms_get_msg_nums_obj) },
    { MP_ROM_QSTR(MP_QSTR_getMsgStatus), MP_ROM_PTR(&qpy_sms_get_msg_status_obj) },
    { MP_ROM_QSTR(MP_QSTR_getLastError), MP_ROM_PTR(&qpy_sms_get_last_error_obj) },
    { MP_ROM_QSTR(MP_QSTR_getCenterAddr), MP_ROM_PTR(&qpy_sms_get_center_address_obj) },
    { MP_ROM_QSTR(MP_QSTR_setCenterAddr), MP_ROM_PTR(&qpy_sms_set_center_address_obj) },
    { MP_ROM_QSTR(MP_QSTR_setSaveLoc), MP_ROM_PTR(&qpy_sms_set_save_location_obj) },
    { MP_ROM_QSTR(MP_QSTR_getSaveLoc), MP_ROM_PTR(&qpy_sms_get_save_location_obj) },
};
static MP_DEFINE_CONST_DICT(qpy_sms_globals, qpy_sms_globals_table);

const mp_obj_module_t mp_module_sms = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&qpy_sms_globals,
};

#if MICROPY_QPY_MODULE_SMS
MP_REGISTER_MODULE(MP_QSTR_sms, mp_module_sms);
#endif
