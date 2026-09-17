#include <stdio.h>
#include <string.h>

#include "py/obj.h"
#include "py/objstr.h"
#include "py/runtime.h"
#include "qosa_network.h"
#include "qosa_sys.h"
#include "qpy_compat_common.h"

static mp_obj_t qpy_net_callback = MP_OBJ_NULL;
static qosa_sem_t qpy_net_op_sem = QOSA_NULL;
static qosa_nw_err_e qpy_net_op_err = QOSA_NW_ERR_EXECUTE;

#define QPY_NET_OP_TIMEOUT_MS (5000)

static qosa_bool_t qpy_net_get_scell(qosa_uint8_t simid, qosa_nw_scell_info_t *scell);
static qosa_uint64_t qpy_net_scell_ci(const qosa_nw_scell_info_t *scell);
static qosa_uint32_t qpy_net_scell_lac(const qosa_nw_scell_info_t *scell);

static void qpy_net_op_callback(void *ctx, void *argv) {
    (void)ctx;
    qosa_nw_general_cnf_t *cnf = (qosa_nw_general_cnf_t *)argv;
    qpy_net_op_err = cnf == NULL ? QOSA_NW_ERR_EXECUTE : (qosa_nw_err_e)cnf->err_code;
    if (qpy_net_op_sem != QOSA_NULL) {
        qosa_sem_release(qpy_net_op_sem);
    }
}

static int qpy_net_prepare_op(void) {
    if (qpy_net_op_sem == QOSA_NULL && qosa_sem_create(&qpy_net_op_sem, 0) != QOSA_OK) {
        return -1;
    }
    qpy_net_op_err = QOSA_NW_ERR_EXECUTE;
    return 0;
}

static int qpy_signal_db100_to_dbm(qosa_int16_t value) {
    if (value == QOSA_NW_PI_INT16) {
        return 0;
    }
    return value / 100;
}

static int qpy_rssi_dbm_to_csq(int dbm) {
    if (dbm == 0 || dbm <= -113) {
        return 99;
    }
    if (dbm >= -51) {
        return 31;
    }
    return (dbm + 113) / 2;
}

static void qpy_net_read_signal(qosa_uint8_t simid, int *rssi, int *rsrp, int *rsrq, int *sinr, int *act, qosa_nw_scell_info_t *out_scell) {
    qosa_nw_scell_info_t scell = {0};
    *rssi = 0;
    *rsrp = 0;
    *rsrq = 0;
    *sinr = 0;
    *act = QOSA_NW_ACT_UNKNOWN;
    if (qosa_nw_get_scell_info(simid, &scell) != QOSA_NW_ERR_OK) {
        return;
    }
    *act = (int)scell.act;
    if (out_scell != NULL) {
        *out_scell = scell;
    }
    switch (scell.act) {
        case QOSA_NW_ACT_EUTRAN:
        case QOSA_NW_ACT_EUTRAN_NB_S1_MODE:
        case QOSA_NW_ACT_EUTRA_CONNECTED_TO_A_5GCN:
        case QOSA_NW_ACT_EUTRA_NR_DUAL_CONNECTIVITY:
            *rssi = qpy_signal_db100_to_dbm(scell.lte.rssi);
            *rsrp = qpy_signal_db100_to_dbm(scell.lte.rsrp);
            *rsrq = qpy_signal_db100_to_dbm(scell.lte.rsrq);
            *sinr = qpy_signal_db100_to_dbm(scell.lte.sinr);
            break;
        case QOSA_NW_ACT_NR_CONNECTED_TO_A_5GCN:
        case QOSA_NW_ACT_NG_RAN:
            *rsrp = qpy_signal_db100_to_dbm(scell.nr5g.rsrp);
            *rsrq = qpy_signal_db100_to_dbm(scell.nr5g.rsrq);
            *sinr = qpy_signal_db100_to_dbm(scell.nr5g.sinr);
            break;
        case QOSA_NW_ACT_UTRAN:
        case QOSA_NW_ACT_UTRAN_HSDPA:
        case QOSA_NW_ACT_UTRAN_HSUPA:
        case QOSA_NW_ACT_UTRAN_HSPA:
            *rssi = qpy_signal_db100_to_dbm(scell.wcdma.rscp);
            *rsrp = qpy_signal_db100_to_dbm(scell.wcdma.rscp);
            *rsrq = qpy_signal_db100_to_dbm(scell.wcdma.ecno);
            break;
        case QOSA_NW_ACT_GSM:
        case QOSA_NW_ACT_GSM_COMPACT:
        case QOSA_NW_ACT_GSM_EGPRS:
        case QOSA_NW_ACT_EC_GSM_IOT:
            *rssi = scell.gsm.signal_level == QOSA_NW_PI_INT16 ? 0 : scell.gsm.signal_level;
            break;
        default:
            break;
    }
}

static mp_obj_t qpy_net_get_state(size_t n_args, const mp_obj_t *args) {
    qosa_uint8_t simid = qpy_get_simid(n_args, args, 0);
    qosa_uint8_t cs_status = QOSA_NW_REG_UNKNOWN;
    qosa_uint8_t ps_status = QOSA_NW_REG_UNKNOWN;
    if (qosa_nw_get_reg_status(simid, &cs_status, &ps_status) != QOSA_NW_ERR_OK) {
        return qpy_int_minus_one();
    }
    qosa_nw_scell_info_t scell;
    qosa_uint64_t cid = 0;
    qosa_uint32_t lac = 0;
    int act = 0;
    if (qpy_net_get_scell(simid, &scell)) {
        cid = qpy_net_scell_ci(&scell);
        lac = qpy_net_scell_lac(&scell);
        act = scell.act;
    }
    // Legacy layout: status, lac, cid, act, reject_cause, psc.
    mp_obj_t voice_list[6] = { MP_OBJ_NEW_SMALL_INT(cs_status), mp_obj_new_int_from_uint(lac), mp_obj_new_int_from_ull(cid), MP_OBJ_NEW_SMALL_INT(act), MP_OBJ_NEW_SMALL_INT(0), MP_OBJ_NEW_SMALL_INT(0) };
    mp_obj_t data_list[6] = { MP_OBJ_NEW_SMALL_INT(ps_status), mp_obj_new_int_from_uint(lac), mp_obj_new_int_from_ull(cid), MP_OBJ_NEW_SMALL_INT(act), MP_OBJ_NEW_SMALL_INT(0), MP_OBJ_NEW_SMALL_INT(0) };
    mp_obj_t items[2] = { mp_obj_new_list(6, voice_list), mp_obj_new_list(6, data_list) };
    return mp_obj_new_tuple(2, items);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_net_get_state_obj, 0, 1, qpy_net_get_state);

static mp_obj_t qpy_net_csq(size_t n_args, const mp_obj_t *args) {
    qosa_uint8_t simid = qpy_get_simid(n_args, args, 0);
    int rssi, rsrp, rsrq, sinr, act;
    qpy_net_read_signal(simid, &rssi, &rsrp, &rsrq, &sinr, &act, NULL);
    return qpy_tuple2_int(qpy_rssi_dbm_to_csq(rssi != 0 ? rssi : rsrp), 99);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_net_csq_obj, 0, 1, qpy_net_csq);

static mp_obj_t qpy_net_signal(size_t n_args, const mp_obj_t *args) {
    qosa_uint8_t simid = qpy_get_simid(n_args, args, 0);
    int rssi, rsrp, rsrq, sinr, act;
    qpy_net_read_signal(simid, &rssi, &rsrp, &rsrq, &sinr, &act, NULL);
    mp_obj_t items[4] = {
        MP_OBJ_NEW_SMALL_INT(rssi),
        MP_OBJ_NEW_SMALL_INT(rsrp),
        MP_OBJ_NEW_SMALL_INT(rsrq),
        MP_OBJ_NEW_SMALL_INT(sinr),
    };
    return mp_obj_new_tuple(4, items);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_net_signal_obj, 0, 1, qpy_net_signal);

static mp_obj_t qpy_net_operator_name(size_t n_args, const mp_obj_t *args) {
    qosa_uint8_t simid = qpy_get_simid(n_args, args, 0);
    qosa_nw_oper_name_t long_name = {0};
    qosa_nw_oper_name_t short_name = {0};
    if (qosa_nw_get_oper_name(simid, &long_name, &short_name) == QOSA_NW_ERR_OK) {
        mp_obj_t ok_items[4] = {
            mp_obj_new_str(long_name.name, strlen(long_name.name)),
            mp_obj_new_str(short_name.name, strlen(short_name.name)),
            mp_obj_new_str("", 0),
            mp_obj_new_str("", 0),
        };
        return mp_obj_new_tuple(4, ok_items);
    }
    return qpy_int_minus_one();
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_net_operator_name_obj, 0, 1, qpy_net_operator_name);

static mp_obj_t qpy_net_get_band(mp_obj_t net_type) {
    int band_type = mp_obj_get_int(net_type);
    if (band_type < 0 || band_type > 3) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid value, net_type should be in [0,3]."));
    }
    qosa_nw_band_t band = {0};
    if (qosa_nw_get_cfg_band(0, &band) != QOSA_NW_ERR_OK) {
        return qpy_int_minus_one();
    }
    char value[40];
    if (band_type == 0) {
        snprintf(value, sizeof(value), "0x%x", band.gwbandval & 0x0f);
    } else if (band.ltebandval[3] != 0) {
        snprintf(value, sizeof(value), "0x%x%08x%08x%08x", band.ltebandval[3], band.ltebandval[2], band.ltebandval[1], band.ltebandval[0]);
    } else if (band.ltebandval[2] != 0) {
        snprintf(value, sizeof(value), "0x%x%08x%08x", band.ltebandval[2], band.ltebandval[1], band.ltebandval[0]);
    } else if (band.ltebandval[1] != 0) {
        snprintf(value, sizeof(value), "0x%x%08x", band.ltebandval[1], band.ltebandval[0]);
    } else {
        snprintf(value, sizeof(value), "0x%x", band.ltebandval[0]);
    }
    return mp_obj_new_str(value, strlen(value));
}
static MP_DEFINE_CONST_FUN_OBJ_1(qpy_net_get_band_obj, qpy_net_get_band);

static mp_obj_t qpy_net_set_band(mp_obj_t arg0, mp_obj_t arg1, mp_obj_t arg2) {
    const mp_obj_t args[3] = { arg0, arg1, arg2 };
    int band_type = mp_obj_get_int(args[0]);
    int gsm_band = mp_obj_get_int(args[1]);
    if (band_type < 0 || band_type > 3) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid value, net_type should be in [0,3]."));
    }
    if (gsm_band < 0 || gsm_band > 0x0f) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid value, gsm_band should be in [0,f]."));
    }
    size_t len;
    mp_obj_t *items;
    mp_obj_get_array(args[2], &len, &items);
    if (len != 4) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid value, band tuple should be 4 uint32 elements."));
    }
    qosa_nw_band_t band = {0};
    if (band_type == 0) {
        band.gwbandval = gsm_band;
    } else {
        band.ltebandval[0] = mp_obj_get_int_truncated(items[3]);
        band.ltebandval[1] = mp_obj_get_int_truncated(items[2]);
        band.ltebandval[2] = mp_obj_get_int_truncated(items[1]);
        band.ltebandval[3] = mp_obj_get_int_truncated(items[0]);
    }
    if (qpy_net_prepare_op() != 0 || qosa_nw_set_cfg_band(0, &band, QOSA_TRUE, qpy_net_op_callback, NULL) != QOSA_NW_ERR_OK || qosa_sem_wait(qpy_net_op_sem, QPY_NET_OP_TIMEOUT_MS) != QOSA_OK || qpy_net_op_err != QOSA_NW_ERR_OK) {
        return qpy_int_minus_one();
    }
    return MP_OBJ_NEW_SMALL_INT(0);
}
static MP_DEFINE_CONST_FUN_OBJ_3(qpy_net_set_band_obj, qpy_net_set_band);

static mp_obj_t qpy_net_get_reject_cause(void) {
    qosa_nw_reject_info_t reject_info = {0};
    if (qosa_nw_get_reject_cause_event_info(0, &reject_info) != QOSA_NW_ERR_OK) {
        return qpy_int_minus_one();
    }
    char value[8];
    snprintf(value, sizeof(value), "%u", reject_info.reject_cause);
    return mp_obj_new_str(value, strlen(value));
}
static MP_DEFINE_CONST_FUN_OBJ_0(qpy_net_get_reject_cause_obj, qpy_net_get_reject_cause);

static mp_obj_t qpy_net_get_cell_info(size_t n_args, const mp_obj_t *args) {
    qosa_uint8_t simid = qpy_get_simid(n_args, args, 0);
    qosa_nw_scell_info_t scell = {0};
    int rssi, rsrp, rsrq, sinr, act;
    qpy_net_read_signal(simid, &rssi, &rsrp, &rsrq, &sinr, &act, &scell);
    if (act == QOSA_NW_ACT_UNKNOWN) {
        return mp_obj_new_list(0, NULL);
    }
    mp_obj_t cell[8] = { MP_OBJ_NEW_SMALL_INT(act), MP_OBJ_NEW_SMALL_INT(0), MP_OBJ_NEW_SMALL_INT(0), MP_OBJ_NEW_SMALL_INT(0), MP_OBJ_NEW_SMALL_INT(0), MP_OBJ_NEW_SMALL_INT(rsrp), MP_OBJ_NEW_SMALL_INT(rsrq), MP_OBJ_NEW_SMALL_INT(sinr) };
    switch (act) {
        case QOSA_NW_ACT_EUTRAN:
        case QOSA_NW_ACT_EUTRAN_NB_S1_MODE:
        case QOSA_NW_ACT_EUTRA_CONNECTED_TO_A_5GCN:
        case QOSA_NW_ACT_EUTRA_NR_DUAL_CONNECTIVITY:
            cell[1] = mp_obj_new_int_from_uint(scell.lte.cellid);
            cell[2] = MP_OBJ_NEW_SMALL_INT(scell.lte.tac);
            cell[3] = MP_OBJ_NEW_SMALL_INT(scell.lte.pcid);
            cell[4] = mp_obj_new_int_from_uint(scell.lte.earfcn);
            break;
        case QOSA_NW_ACT_NR_CONNECTED_TO_A_5GCN:
        case QOSA_NW_ACT_NG_RAN:
            cell[1] = mp_obj_new_int_from_ull(scell.nr5g.cellid);
            cell[2] = mp_obj_new_int_from_uint(scell.nr5g.tac);
            cell[3] = MP_OBJ_NEW_SMALL_INT(scell.nr5g.pcid);
            cell[4] = mp_obj_new_int_from_uint(scell.nr5g.arfcn);
            break;
        case QOSA_NW_ACT_UTRAN:
        case QOSA_NW_ACT_UTRAN_HSDPA:
        case QOSA_NW_ACT_UTRAN_HSUPA:
        case QOSA_NW_ACT_UTRAN_HSPA:
            cell[1] = mp_obj_new_int_from_uint(scell.wcdma.cellid);
            cell[2] = MP_OBJ_NEW_SMALL_INT(scell.wcdma.lac);
            cell[3] = MP_OBJ_NEW_SMALL_INT(scell.wcdma.psc);
            cell[4] = MP_OBJ_NEW_SMALL_INT(scell.wcdma.uarfcn);
            break;
        case QOSA_NW_ACT_GSM:
        case QOSA_NW_ACT_GSM_COMPACT:
        case QOSA_NW_ACT_GSM_EGPRS:
        case QOSA_NW_ACT_EC_GSM_IOT:
            cell[1] = mp_obj_new_int_from_uint(scell.gsm.cellid);
            cell[2] = MP_OBJ_NEW_SMALL_INT(scell.gsm.lac);
            cell[3] = MP_OBJ_NEW_SMALL_INT(scell.gsm.bsic);
            cell[4] = MP_OBJ_NEW_SMALL_INT(scell.gsm.arfcn);
            break;
        default:
            break;
    }
    mp_obj_t item = mp_obj_new_tuple(8, cell);
    return mp_obj_new_list(1, &item);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_net_get_cell_info_obj, 0, 1, qpy_net_get_cell_info);


static qosa_plmn_t *qpy_net_scell_plmn(qosa_nw_scell_info_t *scell) {
    switch (scell->act) {
        case QOSA_NW_ACT_EUTRAN:
        case QOSA_NW_ACT_EUTRAN_NB_S1_MODE:
        case QOSA_NW_ACT_EUTRA_CONNECTED_TO_A_5GCN:
        case QOSA_NW_ACT_EUTRA_NR_DUAL_CONNECTIVITY:
            return &scell->lte.plmn;
        case QOSA_NW_ACT_NR_CONNECTED_TO_A_5GCN:
        case QOSA_NW_ACT_NG_RAN:
            return &scell->nr5g.plmn;
        case QOSA_NW_ACT_UTRAN:
        case QOSA_NW_ACT_UTRAN_HSDPA:
        case QOSA_NW_ACT_UTRAN_HSUPA:
        case QOSA_NW_ACT_UTRAN_HSPA:
            return &scell->wcdma.plmn;
        case QOSA_NW_ACT_GSM:
        case QOSA_NW_ACT_GSM_COMPACT:
        case QOSA_NW_ACT_GSM_EGPRS:
        case QOSA_NW_ACT_EC_GSM_IOT:
            return &scell->gsm.plmn;
        default:
            return NULL;
    }
}

static qosa_bool_t qpy_net_get_scell(qosa_uint8_t simid, qosa_nw_scell_info_t *scell) {
    memset(scell, 0, sizeof(*scell));
    return qosa_nw_get_scell_info(simid, scell) == QOSA_NW_ERR_OK && scell->act != QOSA_NW_ACT_UNKNOWN;
}

static qosa_uint64_t qpy_net_scell_ci(const qosa_nw_scell_info_t *scell) {
    switch (scell->act) {
        case QOSA_NW_ACT_EUTRAN:
        case QOSA_NW_ACT_EUTRAN_NB_S1_MODE:
        case QOSA_NW_ACT_EUTRA_CONNECTED_TO_A_5GCN:
        case QOSA_NW_ACT_EUTRA_NR_DUAL_CONNECTIVITY:
            return scell->lte.cellid;
        case QOSA_NW_ACT_NR_CONNECTED_TO_A_5GCN:
        case QOSA_NW_ACT_NG_RAN:
            return scell->nr5g.cellid;
        case QOSA_NW_ACT_UTRAN:
        case QOSA_NW_ACT_UTRAN_HSDPA:
        case QOSA_NW_ACT_UTRAN_HSUPA:
        case QOSA_NW_ACT_UTRAN_HSPA:
            return scell->wcdma.cellid;
        case QOSA_NW_ACT_GSM:
        case QOSA_NW_ACT_GSM_COMPACT:
        case QOSA_NW_ACT_GSM_EGPRS:
        case QOSA_NW_ACT_EC_GSM_IOT:
            return scell->gsm.cellid;
        default:
            return 0;
    }
}

static qosa_uint32_t qpy_net_scell_lac(const qosa_nw_scell_info_t *scell) {
    switch (scell->act) {
        case QOSA_NW_ACT_EUTRAN:
        case QOSA_NW_ACT_EUTRAN_NB_S1_MODE:
        case QOSA_NW_ACT_EUTRA_CONNECTED_TO_A_5GCN:
        case QOSA_NW_ACT_EUTRA_NR_DUAL_CONNECTIVITY:
            return scell->lte.tac;
        case QOSA_NW_ACT_NR_CONNECTED_TO_A_5GCN:
        case QOSA_NW_ACT_NG_RAN:
            return scell->nr5g.tac;
        case QOSA_NW_ACT_UTRAN:
        case QOSA_NW_ACT_UTRAN_HSDPA:
        case QOSA_NW_ACT_UTRAN_HSUPA:
        case QOSA_NW_ACT_UTRAN_HSPA:
            return scell->wcdma.lac;
        case QOSA_NW_ACT_GSM:
        case QOSA_NW_ACT_GSM_COMPACT:
        case QOSA_NW_ACT_GSM_EGPRS:
        case QOSA_NW_ACT_EC_GSM_IOT:
            return scell->gsm.lac;
        default:
            return 0;
    }
}

static mp_obj_t qpy_net_current_cell_info(size_t n_args, const mp_obj_t *args) {
    int timeout = 20;
    if (n_args == 1) {
        timeout = mp_obj_get_int(args[0]);
    }
    if (timeout < 5 || timeout > 3600) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid value, timeout should be in [5,3600]."));
    }
    qosa_nw_scell_info_t scell = {0};
    if (!qpy_net_get_scell(0, &scell)) {
        return qpy_int_minus_one();
    }
    qosa_plmn_t *plmn = qpy_net_scell_plmn(&scell);
    if (plmn == NULL) {
        return qpy_int_minus_one();
    }
    mp_obj_t item[9];
    switch (scell.act) {
        case QOSA_NW_ACT_EUTRAN:
        case QOSA_NW_ACT_EUTRAN_NB_S1_MODE:
        case QOSA_NW_ACT_EUTRA_CONNECTED_TO_A_5GCN:
        case QOSA_NW_ACT_EUTRA_NR_DUAL_CONNECTIVITY:
            item[0] = MP_OBJ_NEW_SMALL_INT(1); item[1] = mp_obj_new_int_from_ull(scell.lte.cellid);
            item[2] = MP_OBJ_NEW_SMALL_INT(plmn->mcc); item[3] = MP_OBJ_NEW_SMALL_INT(plmn->mnc);
            item[4] = MP_OBJ_NEW_SMALL_INT(scell.lte.pcid); item[5] = MP_OBJ_NEW_SMALL_INT(scell.lte.tac);
            item[6] = mp_obj_new_int_from_uint(scell.lte.earfcn); item[7] = MP_OBJ_NEW_SMALL_INT(qpy_signal_db100_to_dbm(scell.lte.rssi));
            item[8] = MP_OBJ_NEW_SMALL_INT(qpy_signal_db100_to_dbm(scell.lte.rsrq));
            return mp_obj_new_list(1, (mp_obj_t[]){mp_obj_new_tuple(9, item)});
        case QOSA_NW_ACT_GSM:
        case QOSA_NW_ACT_GSM_COMPACT:
        case QOSA_NW_ACT_GSM_EGPRS:
        case QOSA_NW_ACT_EC_GSM_IOT:
            item[0] = MP_OBJ_NEW_SMALL_INT(1); item[1] = mp_obj_new_int_from_uint(scell.gsm.cellid);
            item[2] = MP_OBJ_NEW_SMALL_INT(plmn->mcc); item[3] = MP_OBJ_NEW_SMALL_INT(plmn->mnc);
            item[4] = MP_OBJ_NEW_SMALL_INT(scell.gsm.lac); item[5] = MP_OBJ_NEW_SMALL_INT(0);
            item[6] = MP_OBJ_NEW_SMALL_INT(scell.gsm.bsic); item[7] = MP_OBJ_NEW_SMALL_INT(scell.gsm.signal_level);
            return mp_obj_new_list(1, (mp_obj_t[]){mp_obj_new_tuple(8, item)});
        default:
            return qpy_int_minus_one();
    }
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_net_current_cell_info_obj, 0, 1, qpy_net_current_cell_info);

/*
 * The legacy API returns a tuple of GSM, UMTS and LTE cell lists.  UniRTOS
 * exposes complete identifiers for the serving cell, but its synchronous
 * neighbour-cell API explicitly omits PLMN and cell ID.  Do not manufacture
 * those legacy fields: publish the fully-described serving cell in its
 * corresponding list instead.
 */
static mp_obj_t qpy_net_get_cell_infos(size_t n_args, const mp_obj_t *args) {
    qosa_uint8_t simid = qpy_get_simid(n_args, args, 0);
    qosa_nw_scell_info_t scell = {0};
    qosa_plmn_t *plmn;
    mp_obj_t lists[3] = {
        mp_obj_new_list(0, NULL),
        mp_obj_new_list(0, NULL),
        mp_obj_new_list(0, NULL),
    };

    if (!qpy_net_get_scell(simid, &scell)) {
        return qpy_int_minus_one();
    }
    plmn = qpy_net_scell_plmn(&scell);
    if (plmn == NULL) {
        return qpy_int_minus_one();
    }

    switch (scell.act) {
        case QOSA_NW_ACT_EUTRAN:
        case QOSA_NW_ACT_EUTRAN_NB_S1_MODE:
        case QOSA_NW_ACT_EUTRA_CONNECTED_TO_A_5GCN:
        case QOSA_NW_ACT_EUTRA_NR_DUAL_CONNECTIVITY: {
            mp_obj_t lte[9] = {
                MP_OBJ_NEW_SMALL_INT(1),
                mp_obj_new_int_from_uint(scell.lte.cellid),
                MP_OBJ_NEW_SMALL_INT(plmn->mcc),
                MP_OBJ_NEW_SMALL_INT(plmn->mnc),
                MP_OBJ_NEW_SMALL_INT(scell.lte.pcid),
                MP_OBJ_NEW_SMALL_INT(scell.lte.tac),
                mp_obj_new_int_from_uint(scell.lte.earfcn),
                MP_OBJ_NEW_SMALL_INT(qpy_signal_db100_to_dbm(scell.lte.rssi)),
                MP_OBJ_NEW_SMALL_INT(qpy_signal_db100_to_dbm(scell.lte.rsrq)),
            };
            mp_obj_list_append(lists[2], mp_obj_new_tuple(9, lte));
            break;
        }
        case QOSA_NW_ACT_GSM:
        case QOSA_NW_ACT_GSM_COMPACT:
        case QOSA_NW_ACT_GSM_EGPRS:
        case QOSA_NW_ACT_EC_GSM_IOT: {
            mp_obj_t gsm[8] = {
                MP_OBJ_NEW_SMALL_INT(1),
                mp_obj_new_int_from_uint(scell.gsm.cellid),
                MP_OBJ_NEW_SMALL_INT(plmn->mcc),
                MP_OBJ_NEW_SMALL_INT(plmn->mnc),
                MP_OBJ_NEW_SMALL_INT(scell.gsm.lac),
                MP_OBJ_NEW_SMALL_INT(scell.gsm.arfcn),
                MP_OBJ_NEW_SMALL_INT(scell.gsm.bsic),
                MP_OBJ_NEW_SMALL_INT(scell.gsm.signal_level),
            };
            mp_obj_list_append(lists[0], mp_obj_new_tuple(8, gsm));
            break;
        }
        default:
            break;
    }
    return mp_obj_new_tuple(3, lists);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_net_get_cell_infos_obj, 0, 1, qpy_net_get_cell_infos);

static mp_obj_t qpy_net_get_cell_lock(mp_obj_t net_mode) {
    int net_type = mp_obj_get_int(net_mode);
    qosa_nw_freq_lock_list_t locks = {0};
    mp_obj_t result = mp_obj_new_list(0, NULL);
    if (net_type < 0 || net_type > 2) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid value, net_type should be in [0,2]."));
    }
    if (net_type != 2 || qosa_nw_get_freq_lock_config(0, QOSA_NW_RAT_4G, &locks) != QOSA_NW_ERR_OK || locks.num == 0) {
        return result;
    }
    mp_obj_list_append(result, mp_obj_new_int_from_uint(locks.lock[0].freq));
    if (locks.lock[0].pci != QOSA_NW_PI_UINT16) {
        mp_obj_list_append(result, MP_OBJ_NEW_SMALL_INT(locks.lock[0].pci));
    }
    return result;
}
static MP_DEFINE_CONST_FUN_OBJ_1(qpy_net_get_cell_lock_obj, qpy_net_get_cell_lock);

static mp_obj_t qpy_net_set_cell_lock(size_t n_args, const mp_obj_t *args) {
    int net_type = mp_obj_get_int(args[0]);
    int earfcn = mp_obj_get_int(args[1]);
    int pci = -1;
    qosa_nw_freq_lock_list_t locks = {0};
    if (net_type < 0 || net_type > 2) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid value, net_type should be in [0,2]."));
    }
    if (n_args == 3 && net_type != 0) {
        pci = mp_obj_get_int(args[2]);
        if (pci < 0 || pci > 503) {
            mp_raise_ValueError(MP_ERROR_TEXT("invalid value, pci should be in [0,503]."));
        }
    } else if (n_args != 2 || net_type != 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("pci not supported on 2G."));
    }
    if (earfcn < 0 || earfcn > 65535) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid value, earfcn should be in [0,65535]."));
    }
    if (net_type != 2) {
        return qpy_int_minus_one();
    }
    locks.num = 1;
    locks.lock[0].freq = earfcn;
    locks.lock[0].pci = pci;
    if (qpy_net_prepare_op() != 0 || qosa_nw_freq_lock(0, QOSA_NW_RAT_4G, QOSA_NW_LOCK_FREQ_OPCODE_LOCK_CELL, &locks, qpy_net_op_callback, NULL) != QOSA_NW_ERR_OK || qosa_sem_wait(qpy_net_op_sem, QPY_NET_OP_TIMEOUT_MS) != QOSA_OK || qpy_net_op_err != QOSA_NW_ERR_OK) {
        return qpy_int_minus_one();
    }
    return MP_OBJ_NEW_SMALL_INT(0);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_net_set_cell_lock_obj, 2, 3, qpy_net_set_cell_lock);

static mp_obj_t qpy_net_cell_restore(size_t n_args, const mp_obj_t *args) {
    int net_type = 2;
    qosa_nw_freq_lock_list_t locks = {0};
    if (n_args == 1) {
        net_type = mp_obj_get_int(args[0]);
        if (net_type < 0 || net_type > 2) {
            mp_raise_ValueError(MP_ERROR_TEXT("invalid value, net_type should be in [0,2]."));
        }
    }
    if (net_type != 2) {
        return qpy_int_minus_one();
    }
    if (qpy_net_prepare_op() != 0 || qosa_nw_freq_lock(0, QOSA_NW_RAT_4G, QOSA_NW_LOCK_FREQ_OPCODE_UNLOCK, &locks, qpy_net_op_callback, NULL) != QOSA_NW_ERR_OK || qosa_sem_wait(qpy_net_op_sem, QPY_NET_OP_TIMEOUT_MS) != QOSA_OK || qpy_net_op_err != QOSA_NW_ERR_OK) {
        return qpy_int_minus_one();
    }
    return MP_OBJ_NEW_SMALL_INT(0);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_net_cell_restore_obj, 0, 1, qpy_net_cell_restore);

static mp_obj_t qpy_net_cell_list_value(size_t n_args, const mp_obj_t *args, int field) {
    qosa_uint8_t simid = qpy_get_simid(n_args, args, 0);
    qosa_nw_scell_info_t scell;
    if (!qpy_net_get_scell(simid, &scell)) {
        return qpy_int_minus_one();
    }
    qosa_plmn_t *plmn = qpy_net_scell_plmn(&scell);
    mp_obj_t value = qpy_int_minus_one();
    if (field == 0) {
        value = mp_obj_new_int_from_ull(qpy_net_scell_ci(&scell));
    } else if (field == 1) {
        value = mp_obj_new_int_from_uint(qpy_net_scell_lac(&scell));
    } else if (plmn != NULL) {
        value = MP_OBJ_NEW_SMALL_INT(field == 2 ? plmn->mnc : plmn->mcc);
    }
    mp_obj_t list = mp_obj_new_list(0, NULL);
    mp_obj_list_append(list, value);
    return list;
}

static mp_obj_t qpy_net_serving_value(size_t n_args, const mp_obj_t *args, int field) {
    qosa_uint8_t simid = qpy_get_simid(n_args, args, 0);
    qosa_nw_scell_info_t scell;
    if (!qpy_net_get_scell(simid, &scell)) {
        return qpy_int_minus_one();
    }
    qosa_plmn_t *plmn = qpy_net_scell_plmn(&scell);
    if (field == 0) {
        return mp_obj_new_int_from_ull(qpy_net_scell_ci(&scell));
    }
    if (field == 1) {
        return mp_obj_new_int_from_uint(qpy_net_scell_lac(&scell));
    }
    if (plmn == NULL) {
        return qpy_int_minus_one();
    }
    return MP_OBJ_NEW_SMALL_INT(field == 2 ? plmn->mnc : plmn->mcc);
}

static mp_obj_t qpy_net_get_ci(size_t n_args, const mp_obj_t *args) {
    return qpy_net_cell_list_value(n_args, args, 0);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_net_get_ci_obj, 0, 1, qpy_net_get_ci);

static mp_obj_t qpy_net_get_lac(size_t n_args, const mp_obj_t *args) {
    return qpy_net_cell_list_value(n_args, args, 1);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_net_get_lac_obj, 0, 1, qpy_net_get_lac);

static mp_obj_t qpy_net_get_mnc(size_t n_args, const mp_obj_t *args) {
    return qpy_net_cell_list_value(n_args, args, 2);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_net_get_mnc_obj, 0, 1, qpy_net_get_mnc);

static mp_obj_t qpy_net_get_mcc(size_t n_args, const mp_obj_t *args) {
    return qpy_net_cell_list_value(n_args, args, 3);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_net_get_mcc_obj, 0, 1, qpy_net_get_mcc);

static mp_obj_t qpy_net_get_serving_ci(size_t n_args, const mp_obj_t *args) {
    return qpy_net_serving_value(n_args, args, 0);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_net_get_serving_ci_obj, 0, 1, qpy_net_get_serving_ci);

static mp_obj_t qpy_net_get_serving_lac(size_t n_args, const mp_obj_t *args) {
    return qpy_net_serving_value(n_args, args, 1);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_net_get_serving_lac_obj, 0, 1, qpy_net_get_serving_lac);

static mp_obj_t qpy_net_get_serving_mnc(size_t n_args, const mp_obj_t *args) {
    return qpy_net_serving_value(n_args, args, 2);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_net_get_serving_mnc_obj, 0, 1, qpy_net_get_serving_mnc);

static mp_obj_t qpy_net_get_serving_mcc(size_t n_args, const mp_obj_t *args) {
    return qpy_net_serving_value(n_args, args, 3);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_net_get_serving_mcc_obj, 0, 1, qpy_net_get_serving_mcc);

static mp_obj_t qpy_net_get_config(size_t n_args, const mp_obj_t *args) {
    qosa_uint8_t simid = qpy_get_simid(n_args, args, 0);
    qosa_uint8_t rat_mode = 0;
    qosa_uint8_t roaming = QOSA_NW_ROAMING_DISABLE;
    if (qosa_nw_get_rat_mode_from_nv(simid, &rat_mode) != QOSA_NW_ERR_OK ||
        qosa_nw_get_roaming_pref(simid, &roaming) != QOSA_NW_ERR_OK) {
        return qpy_int_minus_one();
    }
    mp_obj_t tuple[2] = {
        MP_OBJ_NEW_SMALL_INT(rat_mode),
        roaming == QOSA_NW_ROAMING_DISABLE ? mp_const_false : mp_const_true,
    };
    return mp_obj_new_tuple(2, tuple);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_net_get_config_obj, 0, 1, qpy_net_get_config);

static mp_obj_t qpy_net_set_config(size_t n_args, const mp_obj_t *args) {
    mp_arg_check_num(n_args, 0, 1, 3, false);
    qosa_uint8_t mode = (qosa_uint8_t)mp_obj_get_int(args[0]);
    qosa_uint8_t simid = qpy_get_simid(n_args, args, 2);
    int roaming = 0;
    if (n_args >= 2) {
        roaming = mp_obj_get_int(args[1]);
        if (roaming != 0 && roaming != 1) {
            return qpy_int_minus_one();
        }
    }
    if (qosa_nw_set_rat_mode(simid, mode, QOSA_TRUE, NULL, NULL) != QOSA_NW_ERR_OK) {
        return qpy_int_minus_one();
    }
    return MP_OBJ_NEW_SMALL_INT(qosa_nw_set_roaming_pref(
        simid, roaming ? QOSA_NW_ROAMING_ON_ANY_NETWORK : QOSA_NW_ROAMING_DISABLE,
        QOSA_TRUE, NULL, NULL) == QOSA_NW_ERR_OK ? 0 : -1);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_net_set_config_obj, 1, 3, qpy_net_set_config);

static mp_obj_t qpy_net_get_net_mode(size_t n_args, const mp_obj_t *args) {
    qosa_uint8_t simid = qpy_get_simid(n_args, args, 0);
    qosa_nw_scell_info_t scell;
    if (!qpy_net_get_scell(simid, &scell)) {
        return qpy_int_minus_one();
    }
    qosa_plmn_t *plmn = qpy_net_scell_plmn(&scell);
    char mcc[8] = {0};
    char mnc[8] = {0};
    if (plmn != NULL) {
        snprintf(mcc, sizeof(mcc), "%u", (unsigned)plmn->mcc);
        snprintf(mnc, sizeof(mnc), plmn->mnc_digit_num == 3 ? "%03u" : "%02u", (unsigned)plmn->mnc);
    }
    mp_obj_t tuple[4] = {
        MP_OBJ_NEW_SMALL_INT(0),
        mp_obj_new_str(mcc, strlen(mcc)),
        mp_obj_new_str(mnc, strlen(mnc)),
        MP_OBJ_NEW_SMALL_INT(scell.act),
    };
    return mp_obj_new_tuple(4, tuple);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_net_get_net_mode_obj, 0, 1, qpy_net_get_net_mode);

static mp_obj_t qpy_net_get_modem_fun(size_t n_args, const mp_obj_t *args) {
    qosa_uint8_t simid = qpy_get_simid(n_args, args, 0);
    qosa_uint8_t mode = 0;
    if (qosa_nw_get_ue_operation_mode(simid, &mode) != QOSA_NW_ERR_OK) {
        return qpy_int_minus_one();
    }
    (void)mode;
    return MP_OBJ_NEW_SMALL_INT(1);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_net_get_modem_fun_obj, 0, 1, qpy_net_get_modem_fun);

static mp_obj_t qpy_net_set_modem_fun(size_t n_args, const mp_obj_t *args) {
    int modem_fun = mp_obj_get_int(args[0]);
    qosa_uint8_t simid = qpy_get_simid(n_args, args, 2);
    qosa_uint8_t mode;
    if (modem_fun == 1) {
        mode = QOSA_NW_CS_PS_MODE_2;
    } else if (modem_fun >= 0 && modem_fun < QOSA_NW_UE_OPERATION_MODE_MAX) {
        mode = (qosa_uint8_t)modem_fun;
    } else {
        return qpy_int_minus_one();
    }
    return MP_OBJ_NEW_SMALL_INT(qosa_nw_set_ue_operation_mode(simid, mode, NULL, NULL) == QOSA_NW_ERR_OK ? 0 : -1);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_net_set_modem_fun_obj, 1, 3, qpy_net_set_modem_fun);


static int qpy_net_jamdet_value(qosa_uint8_t simid, qosa_nw_jdcfg_type_e type, qosa_int32_t *value) {
    qosa_nw_jamm_detect_setting_param_t config = {0};
    config.param.qjdcfg_enum = type;
    if (qosa_nw_get_jamm_detect_param(simid, &config) != QOSA_NW_ERR_OK) {
        return -1;
    }
    *value = config.param.value;
    return 0;
}

static mp_obj_t qpy_net_set_jamdet_switch(size_t n_args, const mp_obj_t *args) {
    int enable = mp_obj_get_int(args[0]);
    if (enable != 0 && enable != 1) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid value, opt shuould be 0 or 1,"));
    }
    qosa_uint8_t simid = qpy_get_simid(n_args, args, 1);
    qosa_nw_jamm_detect_setting_param_t config = {0};
    config.param.qjdcfg_enum = QOSA_NW_JDCFG_TYPE_MODE;
    config.param.value = enable;
    return MP_OBJ_NEW_SMALL_INT(qosa_nw_set_jamm_detect_func(simid, &config, NULL, NULL) == QOSA_NW_ERR_OK ? 0 : -1);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_net_set_jamdet_switch_obj, 1, 2, qpy_net_set_jamdet_switch);

static mp_obj_t qpy_net_get_jamdet_switch(size_t n_args, const mp_obj_t *args) {
    qosa_int32_t value = 0;
    if (qpy_net_jamdet_value(qpy_get_simid(n_args, args, 0), QOSA_NW_JDCFG_TYPE_MODE, &value) != 0) {
        return qpy_int_minus_one();
    }
    return mp_obj_new_int(value);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_net_get_jamdet_switch_obj, 0, 1, qpy_net_get_jamdet_switch);

static mp_obj_t qpy_net_set_jamdet_param(size_t n_args, const mp_obj_t *args) {
    static const qosa_nw_jdcfg_type_e types[7] = { QOSA_NW_JDCFG_TYPE_MINCH, QOSA_NW_JDCFG_TYPE_SINR, QOSA_NW_JDCFG_TYPE_RSSI_GSM, QOSA_NW_JDCFG_TYPE_RSRP, QOSA_NW_JDCFG_TYPE_RSRQ, QOSA_NW_JDCFG_TYPE_RSSI, QOSA_NW_JDCFG_TYPE_SHAKEPERIOD };
    const int minimum[7] = {0, -50, -110, -140, -19, -120, 1};
    const int maximum[7] = {254, 50, -50, -44, -3, -20, 10};
    qosa_uint8_t simid = qpy_get_simid(n_args, args, 7);
    for (size_t i = 0; i < 7; ++i) {
        int value = mp_obj_get_int(args[i]);
        if (value < minimum[i] || value > maximum[i]) {
            mp_raise_ValueError(MP_ERROR_TEXT("invalid value, jamdet parameter out of range."));
        }
        qosa_nw_jamm_detect_setting_param_t config = {0};
        config.param.qjdcfg_enum = types[i];
        config.param.value = value;
        if (qosa_nw_set_jamm_detect_param(simid, &config) != QOSA_NW_ERR_OK) {
            return qpy_int_minus_one();
        }
    }
    return MP_OBJ_NEW_SMALL_INT(0);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_net_set_jamdet_param_obj, 7, 8, qpy_net_set_jamdet_param);

static mp_obj_t qpy_net_get_jamdet_param(size_t n_args, const mp_obj_t *args) {
    static const qosa_nw_jdcfg_type_e types[7] = { QOSA_NW_JDCFG_TYPE_MINCH, QOSA_NW_JDCFG_TYPE_SINR, QOSA_NW_JDCFG_TYPE_RSSI_GSM, QOSA_NW_JDCFG_TYPE_RSRP, QOSA_NW_JDCFG_TYPE_RSRQ, QOSA_NW_JDCFG_TYPE_RSSI, QOSA_NW_JDCFG_TYPE_SHAKEPERIOD };
    qosa_uint8_t simid = qpy_get_simid(n_args, args, 0);
    mp_obj_t values[7];
    for (size_t i = 0; i < 7; ++i) {
        qosa_int32_t value = 0;
        if (qpy_net_jamdet_value(simid, types[i], &value) != 0) {
            return qpy_int_minus_one();
        }
        values[i] = mp_obj_new_int(value);
    }
    return mp_obj_new_tuple(7, values);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_net_get_jamdet_param_obj, 0, 1, qpy_net_get_jamdet_param);

static mp_obj_t qpy_net_get_jamdet_status(size_t n_args, const mp_obj_t *args) {
    qosa_uint8_t simid = qpy_get_simid(n_args, args, 0);
    qosa_int32_t enabled = 0;
    if (qpy_net_jamdet_value(simid, QOSA_NW_JDCFG_TYPE_MODE, &enabled) != 0) {
        return qpy_int_minus_one();
    }
    if (enabled == 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("Please enable the jamdet function first."));
    }
    qosa_int32_t value = 0;
    return mp_obj_new_int(qpy_net_jamdet_value(simid, QOSA_NW_JDCFG_TYPE_JAMM_DETECT_RESULT, &value) == 0 ? value : -1);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_net_get_jamdet_status_obj, 0, 1, qpy_net_get_jamdet_status);

static mp_obj_t qpy_net_set_callback(mp_obj_t callback) {
    if (callback != mp_const_none && !mp_obj_is_callable(callback)) {
        mp_raise_TypeError(MP_ERROR_TEXT("callback must be callable"));
    }
    qpy_net_callback = callback;
    return MP_OBJ_NEW_SMALL_INT(0);
}
static MP_DEFINE_CONST_FUN_OBJ_1(qpy_net_set_callback_obj, qpy_net_set_callback);

// Kept for applications that explicitly unload QuecPython's net module.
// UniRTOS network state is system-owned, so only the Python callback needs
// releasing here; this mirrors the observable legacy success result.
static mp_obj_t qpy_module_net_deinit(void) {
    qpy_net_callback = MP_OBJ_NULL;
    return MP_OBJ_NEW_SMALL_INT(0);
}
static MP_DEFINE_CONST_FUN_OBJ_0(qpy_module_net_deinit_obj, qpy_module_net_deinit);

extern mp_obj_t qpy_dial_net_set_apn(size_t n_args, const mp_obj_t *args);
extern mp_obj_t qpy_dial_net_get_apn(size_t n_args, const mp_obj_t *args);
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_net_set_apn_obj, 2, 7, qpy_dial_net_set_apn);
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(qpy_net_get_apn_obj, 1, 2, qpy_dial_net_get_apn);
static const mp_rom_map_elem_t qpy_net_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_net) },
    { MP_ROM_QSTR(MP_QSTR___qpy_module_deinit__), MP_ROM_PTR(&qpy_module_net_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR_getState), MP_ROM_PTR(&qpy_net_get_state_obj) },
    { MP_ROM_QSTR(MP_QSTR_csqQueryPoll), MP_ROM_PTR(&qpy_net_csq_obj) },
    { MP_ROM_QSTR(MP_QSTR_getSignal), MP_ROM_PTR(&qpy_net_signal_obj) },
    { MP_ROM_QSTR(MP_QSTR_operatorName), MP_ROM_PTR(&qpy_net_operator_name_obj) },
    { MP_ROM_QSTR(MP_QSTR_getCellInfo), MP_ROM_PTR(&qpy_net_get_cell_info_obj) },
    { MP_ROM_QSTR(MP_QSTR_getCellInfos), MP_ROM_PTR(&qpy_net_get_cell_infos_obj) },
    { MP_ROM_QSTR(MP_QSTR_currentCellInfo), MP_ROM_PTR(&qpy_net_current_cell_info_obj) },
    { MP_ROM_QSTR(MP_QSTR_getCellLock), MP_ROM_PTR(&qpy_net_get_cell_lock_obj) },
    { MP_ROM_QSTR(MP_QSTR_setCellLock), MP_ROM_PTR(&qpy_net_set_cell_lock_obj) },
    { MP_ROM_QSTR(MP_QSTR_cellRst), MP_ROM_PTR(&qpy_net_cell_restore_obj) },
    { MP_ROM_QSTR(MP_QSTR_getModemFun), MP_ROM_PTR(&qpy_net_get_modem_fun_obj) },
    { MP_ROM_QSTR(MP_QSTR_setModemFun), MP_ROM_PTR(&qpy_net_set_modem_fun_obj) },
    { MP_ROM_QSTR(MP_QSTR_setCallback), MP_ROM_PTR(&qpy_net_set_callback_obj) },
    { MP_ROM_QSTR(MP_QSTR_setApn), MP_ROM_PTR(&qpy_net_set_apn_obj) },
    { MP_ROM_QSTR(MP_QSTR_getApn), MP_ROM_PTR(&qpy_net_get_apn_obj) },
    { MP_ROM_QSTR(MP_QSTR_getConfig), MP_ROM_PTR(&qpy_net_get_config_obj) },
    { MP_ROM_QSTR(MP_QSTR_setConfig), MP_ROM_PTR(&qpy_net_set_config_obj) },
    // UniRTOS exposes NITZ only through an event; without cached NITZ data,
    // preserve the legacy query failure result rather than returning None.
    { MP_ROM_QSTR(MP_QSTR_nitzTime), MP_ROM_PTR(&qpy_stub_ret_minus_one_obj) },
    { MP_ROM_QSTR(MP_QSTR_getNetMode), MP_ROM_PTR(&qpy_net_get_net_mode_obj) },
    { MP_ROM_QSTR(MP_QSTR_getCi), MP_ROM_PTR(&qpy_net_get_ci_obj) },
    { MP_ROM_QSTR(MP_QSTR_getLac), MP_ROM_PTR(&qpy_net_get_lac_obj) },
    { MP_ROM_QSTR(MP_QSTR_getMnc), MP_ROM_PTR(&qpy_net_get_mnc_obj) },
    { MP_ROM_QSTR(MP_QSTR_getMcc), MP_ROM_PTR(&qpy_net_get_mcc_obj) },
    { MP_ROM_QSTR(MP_QSTR_getServingCi), MP_ROM_PTR(&qpy_net_get_serving_ci_obj) },
    { MP_ROM_QSTR(MP_QSTR_getServingLac), MP_ROM_PTR(&qpy_net_get_serving_lac_obj) },
    { MP_ROM_QSTR(MP_QSTR_getServingMnc), MP_ROM_PTR(&qpy_net_get_serving_mnc_obj) },
    { MP_ROM_QSTR(MP_QSTR_getServingMcc), MP_ROM_PTR(&qpy_net_get_serving_mcc_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_networkled_config), MP_ROM_PTR(&qpy_stub_ret_minus_one_obj) },
    { MP_ROM_QSTR(MP_QSTR_setBand), MP_ROM_PTR(&qpy_net_set_band_obj) },
    { MP_ROM_QSTR(MP_QSTR_getBand), MP_ROM_PTR(&qpy_net_get_band_obj) },
    { MP_ROM_QSTR(MP_QSTR_bandRst), MP_ROM_PTR(&qpy_stub_ret_minus_one_obj) },
    { MP_ROM_QSTR(MP_QSTR_ftmModeSwitch), MP_ROM_PTR(&qpy_stub_ret_minus_one_obj) },
    { MP_ROM_QSTR(MP_QSTR_ftmTestStart), MP_ROM_PTR(&qpy_stub_ret_minus_one_obj) },
    { MP_ROM_QSTR(MP_QSTR_ftmTestStop), MP_ROM_PTR(&qpy_stub_ret_minus_one_obj) },
    { MP_ROM_QSTR(MP_QSTR_addBlackCell), MP_ROM_PTR(&qpy_stub_ret_minus_one_obj) },
    { MP_ROM_QSTR(MP_QSTR_getBlackCell), MP_ROM_PTR(&qpy_stub_ret_empty_list_obj) },
    { MP_ROM_QSTR(MP_QSTR_getBlackCellCfg), MP_ROM_PTR(&qpy_stub_ret_empty_tuple_obj) },
    { MP_ROM_QSTR(MP_QSTR_setBlackCellCfg), MP_ROM_PTR(&qpy_stub_ret_minus_one_obj) },
    { MP_ROM_QSTR(MP_QSTR_deleteBlackCell), MP_ROM_PTR(&qpy_stub_ret_minus_one_obj) },
    { MP_ROM_QSTR(MP_QSTR_dmiLinkReady), MP_ROM_PTR(&qpy_stub_ret_zero_obj) },
    { MP_ROM_QSTR(MP_QSTR_setDrxTm), MP_ROM_PTR(&qpy_stub_ret_minus_one_obj) },
    { MP_ROM_QSTR(MP_QSTR_getDrxTm), MP_ROM_PTR(&qpy_stub_ret_empty_tuple_obj) },
    { MP_ROM_QSTR(MP_QSTR_setJamdetSwitch), MP_ROM_PTR(&qpy_net_set_jamdet_switch_obj) },
    { MP_ROM_QSTR(MP_QSTR_getJamdetSwitch), MP_ROM_PTR(&qpy_net_get_jamdet_switch_obj) },
    { MP_ROM_QSTR(MP_QSTR_setJamdetParam), MP_ROM_PTR(&qpy_net_set_jamdet_param_obj) },
    { MP_ROM_QSTR(MP_QSTR_getJamdetParam), MP_ROM_PTR(&qpy_net_get_jamdet_param_obj) },
    { MP_ROM_QSTR(MP_QSTR_getJamdetStatus), MP_ROM_PTR(&qpy_net_get_jamdet_status_obj) },
    { MP_ROM_QSTR(MP_QSTR_getImsRegister), MP_ROM_PTR(&qpy_stub_ret_minus_one_obj) },
    { MP_ROM_QSTR(MP_QSTR_getT3402), MP_ROM_PTR(&qpy_stub_ret_minus_one_obj) },
    { MP_ROM_QSTR(MP_QSTR_getT3412), MP_ROM_PTR(&qpy_stub_ret_minus_one_obj) },
    { MP_ROM_QSTR(MP_QSTR_getT3324), MP_ROM_PTR(&qpy_stub_ret_minus_one_obj) },
    { MP_ROM_QSTR(MP_QSTR_getTeDRX), MP_ROM_PTR(&qpy_stub_ret_minus_one_obj) },
    { MP_ROM_QSTR(MP_QSTR_getTPTW), MP_ROM_PTR(&qpy_stub_ret_minus_one_obj) },
    { MP_ROM_QSTR(MP_QSTR_getqRxlevMin), MP_ROM_PTR(&qpy_stub_ret_minus_one_obj) },
    { MP_ROM_QSTR(MP_QSTR_getRejectCause), MP_ROM_PTR(&qpy_net_get_reject_cause_obj) },
    { MP_ROM_QSTR(MP_QSTR_causeInfo), MP_ROM_PTR(&qpy_stub_ret_minus_one_obj) },
    { MP_ROM_QSTR(MP_QSTR_setQualityFirst), MP_ROM_PTR(&qpy_stub_ret_minus_one_obj) },
};
static MP_DEFINE_CONST_DICT(qpy_net_globals, qpy_net_globals_table);

const mp_obj_module_t mp_module_net = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&qpy_net_globals,
};

#if MICROPY_QPY_MODULE_NET
MP_REGISTER_MODULE(MP_QSTR_net, mp_module_net);
#endif
