#include "sle_tp_server_adv.h"

#include <stdio.h>
#include <string.h>

#include "securec.h"
#include "sle_common.h"
#include "sle_device_discovery.h"
#include "sle_errcode.h"
#include "sle_tp_common.h"
#include "sle_tp_server.h"

#define SLE_TP_ADV_HANDLE           1
#define SLE_TP_ADV_INTERVAL         0xC8
#define SLE_TP_ADV_TX_POWER         10
#define SLE_TP_ADV_DATA_LEN_MAX     251

static uint8_t g_local_name[] = SLE_TP_ADV_NAME;

static uint16_t sle_tp_set_adv_name(uint8_t *adv_data, uint16_t max_len)
{
    uint8_t name_len = (uint8_t)(sizeof(g_local_name) - 1);
    uint8_t index = 0;

    if (adv_data == NULL || max_len < name_len + 2) {
        return 0;
    }
    adv_data[index++] = (uint8_t)(name_len + 1);
    adv_data[index++] = SLE_ADV_DATA_TYPE_COMPLETE_LOCAL_NAME;
    if (memcpy_s(&adv_data[index], max_len - index, g_local_name, name_len) != EOK) {
        return 0;
    }
    return (uint16_t)(index + name_len);
}

static uint16_t sle_tp_set_adv_common(uint8_t *adv_data, uint16_t max_len)
{
    uint16_t idx = 0;
    struct sle_adv_common_value adv_disc_level = {
        .length = sizeof(struct sle_adv_common_value) - 1,
        .type = SLE_ADV_DATA_TYPE_DISCOVERY_LEVEL,
        .value = SLE_ANNOUNCE_LEVEL_NORMAL,
    };
    struct sle_adv_common_value adv_access_mode = {
        .length = sizeof(struct sle_adv_common_value) - 1,
        .type = SLE_ADV_DATA_TYPE_ACCESS_MODE,
        .value = 0,
    };

    if (adv_data == NULL || max_len < 8) {
        return 0;
    }
    if (memcpy_s(&adv_data[idx], max_len - idx, &adv_disc_level, sizeof(adv_disc_level)) != EOK) {
        return 0;
    }
    idx += sizeof(adv_disc_level);
    if (memcpy_s(&adv_data[idx], max_len - idx, &adv_access_mode, sizeof(adv_access_mode)) != EOK) {
        return 0;
    }
    idx += sizeof(adv_access_mode);
    return idx;
}

static errcode_t sle_tp_set_announce_param(void)
{
    sle_announce_param_t param = { 0 };
    uint8_t local_addr[SLE_ADDR_LEN] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x02 };

    param.announce_mode = SLE_ANNOUNCE_MODE_CONNECTABLE_SCANABLE;
    param.announce_handle = SLE_TP_ADV_HANDLE;
    param.announce_gt_role = SLE_ANNOUNCE_ROLE_T_CAN_NEGO;
    param.announce_level = SLE_ANNOUNCE_LEVEL_NORMAL;
    param.announce_channel_map = SLE_ADV_CHANNEL_MAP_DEFAULT;
    param.announce_interval_min = SLE_TP_ADV_INTERVAL;
    param.announce_interval_max = SLE_TP_ADV_INTERVAL;
    param.conn_interval_min = SLE_TP_DEFAULT_CONN_INTV;
    param.conn_interval_max = SLE_TP_DEFAULT_CONN_INTV;
    param.conn_max_latency = 0;
    param.conn_supervision_timeout = SLE_TP_DEFAULT_TIMEOUT;
    param.own_addr.type = SLE_ADDRESS_TYPE_PUBLIC;
    if (memcpy_s(param.own_addr.addr, SLE_ADDR_LEN, local_addr, SLE_ADDR_LEN) != EOK) {
        return ERRCODE_SLE_FAIL;
    }
    return sle_set_announce_param(param.announce_handle, &param);
}

static errcode_t sle_tp_set_announce_data(void)
{
    sle_announce_data_t data = { 0 };
    uint8_t announce_data[SLE_TP_ADV_DATA_LEN_MAX] = { 0 };
    uint8_t seek_rsp_data[SLE_TP_ADV_DATA_LEN_MAX] = { 0 };
    uint16_t announce_len;
    uint16_t seek_len;

    announce_len = sle_tp_set_adv_common(announce_data, sizeof(announce_data));
    data.announce_data = announce_data;
    data.announce_data_len = announce_len;

    seek_len = sle_tp_set_adv_name(seek_rsp_data, sizeof(seek_rsp_data));
    data.seek_rsp_data = seek_rsp_data;
    data.seek_rsp_data_len = seek_len;
    return sle_set_announce_data(SLE_TP_ADV_HANDLE, &data);
}

static void sle_tp_enable_cbk(errcode_t status)
{
    if (status != ERRCODE_SLE_SUCCESS) {
        printf("%s enable failed: 0x%x\n", SLE_TP_LOG, status);
        return;
    }
    printf("%s SLE enabled, starting throughput server\n", SLE_TP_LOG);
    sle_tp_server_start_service();
}

errcode_t sle_tp_server_adv_register_cbks(void)
{
    sle_announce_seek_callbacks_t cbk = { 0 };

    cbk.sle_enable_cb = sle_tp_enable_cbk;
    return sle_announce_seek_register_callbacks(&cbk);
}

errcode_t sle_tp_server_adv_start(void)
{
    errcode_t ret;

    ret = sle_tp_set_announce_param();
    if (ret != ERRCODE_SLE_SUCCESS) {
        return ret;
    }
    ret = sle_tp_set_announce_data();
    if (ret != ERRCODE_SLE_SUCCESS) {
        return ret;
    }
    return sle_start_announce(SLE_TP_ADV_HANDLE);
}

errcode_t sle_tp_server_adv_stop(void)
{
    return sle_stop_announce(SLE_TP_ADV_HANDLE);
}
