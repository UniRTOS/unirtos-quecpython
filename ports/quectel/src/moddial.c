#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "py/obj.h"
#include "py/objstr.h"
#include "py/runtime.h"
#include "qpy_qosa_net_shim.h"
#include "qosa_datacall.h"
#include "qosa_event_notify.h"
#include "qpy_compat_common.h"

#define QPY_DIAL_MAX_SIM 2
#define QPY_DIAL_MAX_PROFILE 8

typedef struct {
    qosa_bool_t valid;
    qosa_uint8_t ip_type;
    qosa_uint8_t auth_type;
    char apn[QOSA_APN_MAX_LEN + 1];
    char username[QOSA_PDP_USER_NAME_MAX_LEN + 1];
    char password[QOSA_PDP_USER_PWD_MAX_LEN + 1];
} qpy_dial_pdp_cache_t;

static qpy_dial_pdp_cache_t qpy_dial_pdp_cache[QPY_DIAL_MAX_SIM][QPY_DIAL_MAX_PROFILE];
static mp_obj_t qpy_dial_callback = mp_const_none;
static volatile int qpy_dial_cb_pending;
static volatile int qpy_dial_cb_profile;
static volatile int qpy_dial_cb_status;
static volatile int qpy_dial_cb_simid;
static bool qpy_dial_cb_registered;

extern void mp_hal_stdio_wake(void);

static void qpy_dial_queue_callback(int profile, int status, int simid) {
    qpy_dial_cb_profile = profile;
    qpy_dial_cb_status = status;
    qpy_dial_cb_simid = simid;
    qpy_dial_cb_pending++;
    mp_hal_stdio_wake();
}

static int qpy_dial_pdp_act_event_cb(void *user_argv, void *argv) {
    (void)user_argv;
    qosa_datacall_act_event_t *event = (qosa_datacall_act_event_t *)argv;
    if (event != NULL) {
        qpy_dial_queue_callback(event->pdpid, event->opt == QOSA_PDP_OPT_ACTIVE ? 1 : 0, event->simid);
    }
    return 0;
}

static int qpy_dial_pdn_deact_event_cb(void *user_argv, void *argv) {
    (void)user_argv;
    qosa_datacall_nw_deact_event_t *event = (qosa_datacall_nw_deact_event_t *)argv;
    if (event != NULL) {
        qpy_dial_queue_callback(event->pdpid, 0, event->simid);
    }
    return 0;
}

void qpy_dial_poll_pending(void) {
    if (qpy_dial_callback == mp_const_none || qpy_dial_cb_pending <= 0) {
        return;
    }
    int profile = qpy_dial_cb_profile;
    int status = qpy_dial_cb_status;
    int simid = qpy_dial_cb_simid;
    qpy_dial_cb_pending--;
    mp_obj_t items[3] = {
        MP_OBJ_NEW_SMALL_INT(profile),
        MP_OBJ_NEW_SMALL_INT(status),
        MP_OBJ_NEW_SMALL_INT(simid),
    };
    mp_sched_schedule(qpy_dial_callback, mp_obj_new_tuple(3, items));
}

static qosa_uint8_t qpy_dial_get_simid_arg(size_t n_args, const mp_obj_t *args, size_t index) {
    if (n_args > index) {
        int simid = mp_obj_get_int(args[index]);
        return (qosa_uint8_t)(simid == 1 ? 1 : 0);
    }
    return 0;
}

static int qpy_dial_iptype_to_qosa(int ip_type) {
    if (ip_type == 1) {
        return QOSA_PDP_TYPE_IPV6;
    }
    if (ip_type == 2) {
        return QOSA_PDP_TYPE_IPV4V6;
    }
    return QOSA_PDP_TYPE_IP;
}

static int qpy_dial_qosa_to_iptype(int pdp_type) {
    if (pdp_type == QOSA_PDP_TYPE_IPV6) {
        return 1;
    }
    if (pdp_type == QOSA_PDP_TYPE_IPV4V6) {
        return 2;
    }
    return 0;
}

static qpy_dial_pdp_cache_t *qpy_dial_cache_get(qosa_uint8_t simid, qosa_uint8_t cid) {
    if (simid >= QPY_DIAL_MAX_SIM || cid >= QPY_DIAL_MAX_PROFILE) {
        return NULL;
    }
    return &qpy_dial_pdp_cache[simid][cid];
}

static void qpy_dial_copy_cache_str(char *dst, size_t dst_len, const char *src) {
    if (dst == NULL || dst_len == 0) {
        return;
    }
    dst[0] = '\0';
    if (src != NULL && src[0] != '\0') {
        strncpy(dst, src, dst_len - 1);
        dst[dst_len - 1] = '\0';
    }
}

static void qpy_dial_cache_update(qosa_uint8_t simid, qosa_uint8_t cid, int ip_type, const char *apn,
    const char *username, const char *password, int auth_type) {
    qpy_dial_pdp_cache_t *cache = qpy_dial_cache_get(simid, cid);
    if (cache == NULL) {
        return;
    }
    cache->valid = QOSA_TRUE;
    cache->ip_type = (qosa_uint8_t)ip_type;
    cache->auth_type = (qosa_uint8_t)auth_type;
    qpy_dial_copy_cache_str(cache->apn, sizeof(cache->apn), apn);
    qpy_dial_copy_cache_str(cache->username, sizeof(cache->username), username);
    qpy_dial_copy_cache_str(cache->password, sizeof(cache->password), password);
}

static void qpy_dial_ip_to_str(const qosa_ip_addr_t *addr, char *buf, size_t len) {
    if (buf == NULL || len == 0) {
        return;
    }
    buf[0] = '\0';
    if (addr == NULL) {
        return;
    }
    if (addr->ip_vsn == QOSA_PDP_IPV4) {
        qosa_uint32_t ip = addr->apptcpip_ipv4_addr.s_addr;
        snprintf(buf, len, "%u.%u.%u.%u", (unsigned)(ip & 0xff), (unsigned)((ip >> 8) & 0xff), (unsigned)((ip >> 16) & 0xff), (unsigned)((ip >> 24) & 0xff));
    } else if (addr->ip_vsn == QOSA_PDP_IPV6) {
        const qosa_uint8_t *ip6 = (const qosa_uint8_t *)&addr->apptcpip_ipv6_addr;
        snprintf(buf, len, "%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x",
            ip6[0], ip6[1], ip6[2], ip6[3], ip6[4], ip6[5], ip6[6], ip6[7],
            ip6[8], ip6[9], ip6[10], ip6[11], ip6[12], ip6[13], ip6[14], ip6[15]);
    }
}

static mp_obj_t qpy_dial_get_pdp_context(size_t n_args, const mp_obj_t *args) {
    qosa_uint8_t cid = (qosa_uint8_t)mp_obj_get_int(args[0]);
    qosa_uint8_t simid = qpy_dial_get_simid_arg(n_args, args, 1);
    qosa_pdp_context_t ctx = {0};
    qosa_pdp_auth_context_t auth = {0};
    if (qosa_datacall_get_pdp_context(simid, cid, &ctx) != QOSA_DATACALL_OK) {
        return qpy_int_minus_one();
    }
    qosa_datacall_get_pdp_auth(simid, cid, &auth);
    const qpy_dial_pdp_cache_t *cache = qpy_dial_cache_get(simid, cid);
    qosa_bool_t has_cache = (cache != NULL && cache->valid);
    int ip_type = has_cache ? cache->ip_type : qpy_dial_qosa_to_iptype(ctx.pdp_type);
    const char *apn = (has_cache && cache->apn[0] != '\0') ? cache->apn : (ctx.apn_valid ? ctx.apn : "");
    const char *username = (has_cache && cache->username[0] != '\0') ? cache->username : (auth.user_valid ? auth.username : "");
    const char *password = (has_cache && cache->password[0] != '\0') ? cache->password : (auth.pass_valid ? auth.password : "");
    int auth_type = has_cache ? cache->auth_type : (auth.auth_valid ? auth.auth_type : 0);
    mp_obj_t items[6] = {
        MP_OBJ_NEW_SMALL_INT(ip_type),
        mp_obj_new_str(apn, strlen(apn)),
        mp_obj_new_str(username, strlen(username)),
        mp_obj_new_str(password, strlen(password)),
        MP_OBJ_NEW_SMALL_INT(auth_type),
        MP_OBJ_NEW_SMALL_INT(simid),
    };
    return mp_obj_new_tuple(6, items);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_dial_get_pdp_context_obj, 1, 2, qpy_dial_get_pdp_context);

static mp_obj_t qpy_dial_set_pdp_context(size_t n_args, const mp_obj_t *args) {
    mp_arg_check_num(n_args, 0, 3, 7, false);
    qosa_uint8_t cid = (qosa_uint8_t)mp_obj_get_int(args[0]);
    qosa_uint8_t simid = qpy_dial_get_simid_arg(n_args, args, 6);
    qosa_pdp_context_t ctx = {0};
    int ip_type = mp_obj_get_int(args[1]);
    ctx.pdp_type = (qosa_uint8_t)qpy_dial_iptype_to_qosa(ip_type);
    const char *apn = mp_obj_str_get_str(args[2]);
    if (apn[0] != '\0') {
        ctx.apn_valid = QOSA_TRUE;
        strncpy(ctx.apn, apn, sizeof(ctx.apn) - 1);
    }
    if (qosa_datacall_set_pdp_context(simid, cid, &ctx) != QOSA_DATACALL_OK) {
        return qpy_int_minus_one();
    }
    const char *username = "";
    const char *password = "";
    int auth_type = 0;
    if (n_args >= 6) {
        qosa_pdp_auth_context_t auth = {0};
        username = mp_obj_str_get_str(args[3]);
        password = mp_obj_str_get_str(args[4]);
        auth_type = mp_obj_get_int(args[5]);
        auth.auth_valid = QOSA_TRUE;
        auth.auth_type = (qosa_pdp_auth_type_e)auth_type;
        if (username[0] != '\0') {
            auth.user_valid = QOSA_TRUE;
            strncpy(auth.username, username, sizeof(auth.username) - 1);
        }
        if (password[0] != '\0') {
            auth.pass_valid = QOSA_TRUE;
            strncpy(auth.password, password, sizeof(auth.password) - 1);
        }
        if (qosa_datacall_set_pdp_auth(simid, cid, &auth) != QOSA_DATACALL_OK) {
            return qpy_int_minus_one();
        }
    }
    qpy_dial_cache_update(simid, cid, ip_type, apn, username, password, auth_type);
    return MP_OBJ_NEW_SMALL_INT(0);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_dial_set_pdp_context_obj, 3, 7, qpy_dial_set_pdp_context);

static mp_obj_t qpy_dial_get_info(size_t n_args, const mp_obj_t *args) {
    qosa_uint8_t cid = (qosa_uint8_t)mp_obj_get_int(args[0]);
    int ip_type = n_args > 1 ? mp_obj_get_int(args[1]) : 0;
    qosa_uint8_t simid = qpy_dial_get_simid_arg(n_args, args, 2);
    qosa_bool_t active = qosa_datacall_get_pdp_status(simid, cid);
    qosa_datacall_ip_info_t info = {0};
    if (active) {
        qosa_datacall_get_pdp_ip_info(simid, cid, &info);
    }

    char ipv4[48] = "0.0.0.0";
    char ipv4_dns1[48] = "0.0.0.0";
    char ipv4_dns2[48] = "0.0.0.0";
    char ipv6[64] = "::";
    char ipv6_dns1[64] = "::";
    char ipv6_dns2[64] = "::";
    if (info.ipv4_ip.ip_vsn == QOSA_PDP_IPV4) {
        qpy_dial_ip_to_str(&info.ipv4_ip, ipv4, sizeof(ipv4));
    }
    if (info.pri_dns.ip_vsn == QOSA_PDP_IPV4) {
        qpy_dial_ip_to_str(&info.pri_dns, ipv4_dns1, sizeof(ipv4_dns1));
    }
    if (info.sec_dns.ip_vsn == QOSA_PDP_IPV4) {
        qpy_dial_ip_to_str(&info.sec_dns, ipv4_dns2, sizeof(ipv4_dns2));
    }
    if (info.ipv6_ip.ip_vsn == QOSA_PDP_IPV6) {
        qpy_dial_ip_to_str(&info.ipv6_ip, ipv6, sizeof(ipv6));
    }
    if (info.pri6_dns.ip_vsn == QOSA_PDP_IPV6) {
        qpy_dial_ip_to_str(&info.pri6_dns, ipv6_dns1, sizeof(ipv6_dns1));
    }
    if (info.sec6_dns.ip_vsn == QOSA_PDP_IPV6) {
        qpy_dial_ip_to_str(&info.sec6_dns, ipv6_dns2, sizeof(ipv6_dns2));
    }

    int ipv4_state = active && (info.ip_type == QOSA_PDP_IPV4 || info.ip_type == QOSA_PDP_IPV4V6 || info.ipv4_ip.ip_vsn == QOSA_PDP_IPV4);
    int ipv6_state = active && (info.ip_type == QOSA_PDP_IPV6 || info.ip_type == QOSA_PDP_IPV4V6 || info.ipv6_ip.ip_vsn == QOSA_PDP_IPV6);
    int info_ip_type = qpy_dial_qosa_to_iptype(info.ip_type);
    mp_obj_t ipv4_list[5] = {
        MP_OBJ_NEW_SMALL_INT(ipv4_state ? 1 : 0),
        MP_OBJ_NEW_SMALL_INT(0),
        mp_obj_new_str(ipv4, strlen(ipv4)),
        mp_obj_new_str(ipv4_dns1, strlen(ipv4_dns1)),
        mp_obj_new_str(ipv4_dns2, strlen(ipv4_dns2)),
    };
    mp_obj_t ipv6_list[5] = {
        MP_OBJ_NEW_SMALL_INT(ipv6_state ? 1 : 0),
        MP_OBJ_NEW_SMALL_INT(0),
        mp_obj_new_str(ipv6, strlen(ipv6)),
        mp_obj_new_str(ipv6_dns1, strlen(ipv6_dns1)),
        mp_obj_new_str(ipv6_dns2, strlen(ipv6_dns2)),
    };
    if (ip_type == 1) {
        mp_obj_t items[3] = { MP_OBJ_NEW_SMALL_INT(cid), MP_OBJ_NEW_SMALL_INT(info_ip_type), mp_obj_new_list(5, ipv6_list) };
        return mp_obj_new_tuple(3, items);
    }
    if (ip_type == 2) {
        mp_obj_t items[4] = { MP_OBJ_NEW_SMALL_INT(cid), MP_OBJ_NEW_SMALL_INT(info_ip_type), mp_obj_new_list(5, ipv4_list), mp_obj_new_list(5, ipv6_list) };
        return mp_obj_new_tuple(4, items);
    }
    mp_obj_t items[3] = { MP_OBJ_NEW_SMALL_INT(cid), MP_OBJ_NEW_SMALL_INT(info_ip_type), mp_obj_new_list(5, ipv4_list) };
    return mp_obj_new_tuple(3, items);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_dial_get_info_obj, 1, 3, qpy_dial_get_info);

static mp_obj_t qpy_dial_start(size_t n_args, const mp_obj_t *args) {
    mp_arg_check_num(n_args, 0, 1, 7, false);
    qosa_uint8_t cid = (qosa_uint8_t)mp_obj_get_int(args[0]);
    qosa_uint8_t simid = qpy_dial_get_simid_arg(n_args, args, 6);
    if (n_args >= 6) {
        qpy_dial_set_pdp_context(n_args, args);
    }
    qosa_datacall_conn_t conn = qosa_datacall_conn_new(simid, cid, QOSA_DATACALL_CONN_TCPIP);
    if (conn == QOSA_DATACALL_CONN_INVALID) {
        return qpy_int_minus_one();
    }
    return MP_OBJ_NEW_SMALL_INT(qosa_datacall_start(conn, 60) == QOSA_DATACALL_OK ? 0 : -1);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_dial_start_obj, 1, 7, qpy_dial_start);

static mp_obj_t qpy_dial_stop(size_t n_args, const mp_obj_t *args) {
    mp_arg_check_num(n_args, 0, 1, 3, false);
    qosa_uint8_t cid = (qosa_uint8_t)mp_obj_get_int(args[0]);
    qosa_uint8_t simid = qpy_dial_get_simid_arg(n_args, args, 2);
    qosa_datacall_conn_t conn = qosa_datacall_conn_new(simid, cid, QOSA_DATACALL_CONN_TCPIP);
    if (conn == QOSA_DATACALL_CONN_INVALID) {
        return qpy_int_minus_one();
    }
    return MP_OBJ_NEW_SMALL_INT(qosa_datacall_stop(conn, 60) == QOSA_DATACALL_OK ? 0 : -1);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_dial_stop_obj, 1, 3, qpy_dial_stop);

static mp_obj_t qpy_dial_set_auto_connect(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    (void)args;
    return MP_OBJ_NEW_SMALL_INT(0);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_dial_set_auto_connect_obj, 2, 3, qpy_dial_set_auto_connect);

static mp_obj_t qpy_dial_set_dns(size_t n_args, const mp_obj_t *args) {
    mp_arg_check_num(n_args, 0, 4, 4, false);
    qosa_uint8_t cid = (qosa_uint8_t)mp_obj_get_int(args[0]);
    qosa_uint8_t simid = (qosa_uint8_t)mp_obj_get_int(args[1]);
    mp_buffer_info_t primary = {0};
    mp_buffer_info_t secondary = {0};
    mp_get_buffer_raise(args[2], &primary, MP_BUFFER_READ);
    mp_get_buffer_raise(args[3], &secondary, MP_BUFFER_READ);

    char primary_text[16] = {0};
    char secondary_text[16] = {0};
    if (primary.len >= sizeof(primary_text) || secondary.len >= sizeof(secondary_text)) {
        return qpy_int_minus_one();
    }
    memcpy(primary_text, primary.buf, primary.len);
    memcpy(secondary_text, secondary.buf, secondary.len);

    unsigned int a, b, c, d;
    qosa_datacall_dns_t dns = {0};
    if (sscanf(primary_text, "%u.%u.%u.%u", &a, &b, &c, &d) != 4 ||
        a > 255 || b > 255 || c > 255 || d > 255) {
        return qpy_int_minus_one();
    }
    dns.pri_dns.ip_vsn = QOSA_PDP_IPV4;
    dns.pri_dns.apptcpip_ipv4_addr.s_addr =
        (qosa_uint32_t)a | ((qosa_uint32_t)b << 8) | ((qosa_uint32_t)c << 16) | ((qosa_uint32_t)d << 24);
    if (sscanf(secondary_text, "%u.%u.%u.%u", &a, &b, &c, &d) != 4 ||
        a > 255 || b > 255 || c > 255 || d > 255) {
        return qpy_int_minus_one();
    }
    dns.sec_dns.ip_vsn = QOSA_PDP_IPV4;
    dns.sec_dns.apptcpip_ipv4_addr.s_addr =
        (qosa_uint32_t)a | ((qosa_uint32_t)b << 8) | ((qosa_uint32_t)c << 16) | ((qosa_uint32_t)d << 24);
    return MP_OBJ_NEW_SMALL_INT(
        qosa_datacall_set_dns_addr(simid, cid, &dns) == QOSA_DATACALL_OK ? 0 : -1);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_dial_set_dns_obj, 4, 4, qpy_dial_set_dns);

static mp_obj_t qpy_dial_set_callback(mp_obj_t callback) {
    if (callback != mp_const_none && !mp_obj_is_callable(callback)) {
        mp_raise_TypeError(MP_ERROR_TEXT("callback must be callable"));
    }
    qpy_dial_callback = callback;
    if (callback == mp_const_none) {
        if (qpy_dial_cb_registered) {
            qosa_event_notify_unregister(QOSA_EVENT_NET_PDP_ACT, qpy_dial_pdp_act_event_cb);
            qosa_event_notify_unregister(QOSA_EVENT_NW_PDN_DEACT, qpy_dial_pdn_deact_event_cb);
            qpy_dial_cb_registered = false;
        }
        qpy_dial_cb_pending = 0;
        return MP_OBJ_NEW_SMALL_INT(0);
    }
    qosa_e_n_error_e ret1 = qosa_event_notify_register(QOSA_EVENT_NET_PDP_ACT, qpy_dial_pdp_act_event_cb, QOSA_NULL);
    qosa_e_n_error_e ret2 = qosa_event_notify_register(QOSA_EVENT_NW_PDN_DEACT, qpy_dial_pdn_deact_event_cb, QOSA_NULL);
    qpy_dial_cb_registered = (ret1 == QOSA_EVENT_NOTIFY_OK) && (ret2 == QOSA_EVENT_NOTIFY_OK);
    return MP_OBJ_NEW_SMALL_INT(qpy_dial_cb_registered ? 0 : -1);
}
static MP_DEFINE_CONST_FUN_OBJ_1(qpy_dial_set_callback_obj, qpy_dial_set_callback);

static mp_obj_t qpy_dial_get_pdp_range(void) {
    return qpy_tuple2_int(QOSA_PDP_CID_MIN, QOSA_PDP_CID_MAX > 3 ? 3 : QOSA_PDP_CID_MAX);
}
static MP_DEFINE_CONST_FUN_OBJ_0(qpy_dial_get_pdp_range_obj, qpy_dial_get_pdp_range);

static mp_obj_t qpy_dial_get_range(mp_obj_t kind) {
    switch (mp_obj_get_int(kind)) {
        case 0: return MP_OBJ_NEW_SMALL_INT(QOSA_APN_MAX_LEN);
        case 1: return MP_OBJ_NEW_SMALL_INT(QOSA_PDP_USER_PWD_MAX_LEN);
        case 2: return MP_OBJ_NEW_SMALL_INT(QOSA_PDP_USER_NAME_MAX_LEN);
        case 3: return MP_OBJ_NEW_SMALL_INT(3);
        case 4: return MP_OBJ_NEW_SMALL_INT(3);
        case 5: return MP_OBJ_NEW_SMALL_INT(4);
        default: return qpy_int_minus_one();
    }
}
static MP_DEFINE_CONST_FUN_OBJ_1(qpy_dial_get_range_obj, qpy_dial_get_range);

static mp_obj_t qpy_dial_get_speed(void) {
    // The legacy EG800Z implementation returned zeroes on this platform.
    return qpy_tuple2_int(0, 0);
}
static MP_DEFINE_CONST_FUN_OBJ_0(qpy_dial_get_speed_obj, qpy_dial_get_speed);

static mp_obj_t qpy_dial_get_traffic(void) {
    qosa_datacall_traffic_statistics_t traffic = {0};
    if (qosa_datacall_get_traffic_statistics(0, &traffic) != QOSA_DATACALL_OK) {
        return qpy_tuple2_int(0, 0);
    }
    mp_obj_t items[2] = {
        mp_obj_new_int_from_ull(traffic.total_downlink_bytes),
        mp_obj_new_int_from_ull(traffic.total_uplink_bytes),
    };
    return mp_obj_new_tuple(2, items);
}
static MP_DEFINE_CONST_FUN_OBJ_0(qpy_dial_get_traffic_obj, qpy_dial_get_traffic);

static mp_obj_t qpy_dial_get_addressinfo(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    (void)args;
    // UniRTOS does not expose the legacy netif MAC/mask/gateway structure.
    // Returning -1 preserves dataCall.py's legacy inactive/fallback behavior.
    return qpy_int_minus_one();
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_dial_get_addressinfo_obj, 1, 2, qpy_dial_get_addressinfo);

static mp_obj_t qpy_dial_get_siminfo(mp_obj_t simid) {
    (void)simid;
    // Legacy dataCall.py converts -1 to the documented zero usage counters.
    return qpy_int_minus_one();
}
static MP_DEFINE_CONST_FUN_OBJ_1(qpy_dial_get_siminfo_obj, qpy_dial_get_siminfo);

static mp_obj_t qpy_dial_use_attach_apn(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    (void)args;
    // Matches the legacy non-ASR target behavior.
    mp_raise_ValueError(MP_ERROR_TEXT("NOT SUPPORT"));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_dial_use_attach_apn_obj, 0, 1, qpy_dial_use_attach_apn);

mp_obj_t qpy_dial_net_set_apn(size_t n_args, const mp_obj_t *args) {
    if (n_args == 2) {
        mp_obj_t pdp_args[7] = {
            MP_OBJ_NEW_SMALL_INT(1),
            MP_OBJ_NEW_SMALL_INT(0),
            args[0],
            mp_obj_new_str("", 0),
            mp_obj_new_str("", 0),
            MP_OBJ_NEW_SMALL_INT(0),
            args[1],
        };
        return qpy_dial_set_pdp_context(7, pdp_args);
    }
    return qpy_dial_set_pdp_context(n_args, args);
}

mp_obj_t qpy_dial_net_get_apn(size_t n_args, const mp_obj_t *args) {
    qosa_uint8_t profile_id = 1;
    qosa_uint8_t simid;
    if (n_args == 1) {
        simid = qpy_dial_get_simid_arg(n_args, args, 0);
    } else {
        profile_id = (qosa_uint8_t)mp_obj_get_int(args[0]);
        simid = qpy_dial_get_simid_arg(n_args, args, 1);
    }
    qosa_pdp_context_t ctx = {0};
    qosa_pdp_auth_context_t auth = {0};
    if (qosa_datacall_get_pdp_context(simid, profile_id, &ctx) != QOSA_DATACALL_OK) {
        return qpy_int_minus_one();
    }
    qosa_datacall_get_pdp_auth(simid, profile_id, &auth);
    const qpy_dial_pdp_cache_t *cache = qpy_dial_cache_get(simid, profile_id);
    qosa_bool_t has_cache = (cache != NULL && cache->valid);
    int ip_type = has_cache ? cache->ip_type : qpy_dial_qosa_to_iptype(ctx.pdp_type);
    const char *apn = (has_cache && cache->apn[0] != '\0') ? cache->apn : (ctx.apn_valid ? ctx.apn : "");
    const char *username = (has_cache && cache->username[0] != '\0') ? cache->username : (auth.user_valid ? auth.username : "");
    const char *password = (has_cache && cache->password[0] != '\0') ? cache->password : (auth.pass_valid ? auth.password : "");
    int auth_type = has_cache ? cache->auth_type : (auth.auth_valid ? auth.auth_type : 0);
    if (n_args == 1) {
        return mp_obj_new_str(apn, strlen(apn));
    }
    mp_obj_t tuple[5] = {
        MP_OBJ_NEW_SMALL_INT(ip_type),
        mp_obj_new_str(apn, strlen(apn)),
        mp_obj_new_str(username, strlen(username)),
        mp_obj_new_str(password, strlen(password)),
        MP_OBJ_NEW_SMALL_INT(auth_type),
    };
    return mp_obj_new_tuple(5, tuple);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_dial_net_get_apn_obj, 1, 2, qpy_dial_net_get_apn);

static const mp_rom_map_elem_t qpy_dial_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_dial) },
    { MP_ROM_QSTR(MP_QSTR_setPDPContext), MP_ROM_PTR(&qpy_dial_set_pdp_context_obj) },
    { MP_ROM_QSTR(MP_QSTR_getPDPContext), MP_ROM_PTR(&qpy_dial_get_pdp_context_obj) },
    { MP_ROM_QSTR(MP_QSTR_start), MP_ROM_PTR(&qpy_dial_start_obj) },
    { MP_ROM_QSTR(MP_QSTR_stop), MP_ROM_PTR(&qpy_dial_stop_obj) },
    { MP_ROM_QSTR(MP_QSTR_setAutoConnect), MP_ROM_PTR(&qpy_dial_set_auto_connect_obj) },
    { MP_ROM_QSTR(MP_QSTR_setAsynMode), MP_ROM_PTR(&qpy_stub_ret_zero_obj) },
    { MP_ROM_QSTR(MP_QSTR_getInfo), MP_ROM_PTR(&qpy_dial_get_info_obj) },
    { MP_ROM_QSTR(MP_QSTR_setCallback), MP_ROM_PTR(&qpy_dial_set_callback_obj) },
    { MP_ROM_QSTR(MP_QSTR_getPdpRange), MP_ROM_PTR(&qpy_dial_get_pdp_range_obj) },
    { MP_ROM_QSTR(MP_QSTR_getRange), MP_ROM_PTR(&qpy_dial_get_range_obj) },
    { MP_ROM_QSTR(MP_QSTR_getApn), MP_ROM_PTR(&qpy_dial_net_get_apn_obj) },
    { MP_ROM_QSTR(MP_QSTR_setDnsserver), MP_ROM_PTR(&qpy_dial_set_dns_obj) },
    { MP_ROM_QSTR(MP_QSTR_useAttachApn), MP_ROM_PTR(&qpy_dial_use_attach_apn_obj) },
    { MP_ROM_QSTR(MP_QSTR_getSpeed), MP_ROM_PTR(&qpy_dial_get_speed_obj) },
    { MP_ROM_QSTR(MP_QSTR_getTraffic), MP_ROM_PTR(&qpy_dial_get_traffic_obj) },
    { MP_ROM_QSTR(MP_QSTR_getAddressinfo), MP_ROM_PTR(&qpy_dial_get_addressinfo_obj) },
    { MP_ROM_QSTR(MP_QSTR_getSiminfo), MP_ROM_PTR(&qpy_dial_get_siminfo_obj) },
};
static MP_DEFINE_CONST_DICT(qpy_dial_globals, qpy_dial_globals_table);

const mp_obj_module_t mp_module_dial = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&qpy_dial_globals,
};

#if MICROPY_QPY_MODULE_DATACALL
MP_REGISTER_MODULE(MP_QSTR_dial, mp_module_dial);
#endif
