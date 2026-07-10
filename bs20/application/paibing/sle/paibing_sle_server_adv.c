/**
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026.
 *
 * Description: paibing 星闪(SLE) 广播配置实现.
 */

#include <string.h>
#include <stdio.h>
#include "securec.h"
#include "common_def.h"
#include "soc_osal.h"
#include "sle_common.h"
#include "sle_device_manager.h"
#include "sle_device_discovery.h"
#include "sle_errcode.h"
#include "paibing_sle_server_adv.h"
#include "mac_config.h"
#include "sle_connection_manager.h"

#define SLE_ADV_HANDLE_DEFAULT             1
#define SLE_ADV_DATA_LEN_MAX               251
/* 协议栈参数上限 20dBm；实际输出取决于 sle_customize_max_pwr()，BS20 约 6~8dBm */
#define SLE_ADV_TX_POWER                   8
#define PAIBING_SLE_ANNOUNCE_TX_POWER_DBM  8

#define SLE_CONN_INTV_MIN_DEFAULT          0x64
#define SLE_CONN_INTV_MAX_DEFAULT          0x64
/* 125us 单位：0x28=5ms，上一版 5m 约 80% 正确率；0x50 发包密度减半 */
#define SLE_ADV_INTERVAL_MIN_DEFAULT       0x28
#define SLE_ADV_INTERVAL_MAX_DEFAULT       0x28
#define PAIBING_ADV_STOP_WAIT_MS           40
#define PAIBING_ADV_STOP_POLL_MS           2
#define SLE_CONN_SUPERVISION_TIMEOUT_DEFAULT 0x1F4
#define SLE_CONN_MAX_LATENCY               0x1F3

#define PAIBING_SLE_LOG "[paibing sle adv]"

#if PAIBING_SLE_SENSOR_BROADCAST
#define PAIBING_ADV_ASCII_MAX              96
#endif

#if PAIBING_USE_FIXED_LOCAL_MAC
static const uint8_t g_paibing_sle_public_addr[SLE_ADDR_LEN] = {
    PAIBING_LOCAL_MAC_B0, PAIBING_LOCAL_MAC_B1, PAIBING_LOCAL_MAC_B2,
    PAIBING_LOCAL_MAC_B3, PAIBING_LOCAL_MAC_B4, PAIBING_LOCAL_MAC_B5
};
#endif

#if PAIBING_PEER_WHITELIST_ENABLE
static const uint8_t g_paibing_sle_allowed_peer[SLE_ADDR_LEN] = {
    PAIBING_ALLOWED_PEER_MAC_B0, PAIBING_ALLOWED_PEER_MAC_B1, PAIBING_ALLOWED_PEER_MAC_B2,
    PAIBING_ALLOWED_PEER_MAC_B3, PAIBING_ALLOWED_PEER_MAC_B4, PAIBING_ALLOWED_PEER_MAC_B5
};
static sle_adv_ext_param_t g_paibing_sle_adv_ext = {
    .adv_filter_policy = SLE_ANNOUNCE_FLT_ANY_SEEK_WHITE_CONNECT,
};
#endif

static uint8_t g_sle_local_name[] = "paibing_imu";
static const uint8_t g_sle_local_name_len = sizeof(g_sle_local_name) - 1;

#if PAIBING_SLE_SENSOR_BROADCAST
static uint8_t g_announce_buf[SLE_ADV_DATA_LEN_MAX];
static uint8_t g_seek_rsp_buf[SLE_ADV_DATA_LEN_MAX];
static uint16_t g_announce_len;
static uint16_t g_seek_rsp_len;
static bool g_adv_active;
static bool g_adv_started;
static uint8_t g_last_payload[PAIBING_ADV_ASCII_MAX];
static uint8_t g_last_payload_len;
static bool g_have_last_mfg;
static uint8_t g_adv_log_div;
#endif

static uint16_t sle_set_adv_local_name(uint8_t *adv_data, uint16_t max_len)
{
    uint8_t idx = 0;
    adv_data[idx++] = g_sle_local_name_len + 1;
    adv_data[idx++] = SLE_ADV_DATA_TYPE_COMPLETE_LOCAL_NAME;
    if (memcpy_s(&adv_data[idx], max_len - idx, g_sle_local_name, g_sle_local_name_len) != EOK) {
        return 0;
    }
    return idx + g_sle_local_name_len;
}

#if PAIBING_SLE_SENSOR_BROADCAST
/* ASCII 行与 BLE Notify 一致，主控 sscanf 解析；5ms 间隔提高扫到概率 */
static int paibing_build_ascii_line(uint8_t *payload, uint8_t max_len, uint32_t uptime_ms,
    int32_t ax_mg, int32_t ay_mg, int32_t az_mg, int32_t gx_mdps, int32_t gy_mdps,
    float roll_deg, float pitch_deg, float magnitude)
{
    int len = snprintf((char *)payload, max_len,
        "@%lu,A%+ld,%+ld,%+ld,G%+ld,%+ld,R%+d,P%+d,M%lu\n",
        (unsigned long)uptime_ms,
        (long)(ax_mg / 10), (long)(ay_mg / 10), (long)(az_mg / 10),
        (long)(gx_mdps / 1000), (long)(gy_mdps / 1000),
        (int)(roll_deg * 10), (int)(pitch_deg * 10), (unsigned long)(magnitude * 100));

    if (len <= 0 || len >= (int)max_len) {
        return 0;
    }
    return len;
}

static uint16_t sle_append_mfg_sensor(uint8_t *adv_data, uint16_t max_len, const uint8_t *payload, uint8_t plen)
{
    if ((plen == 0) || (max_len < (uint16_t)(plen + 2))) {
        return 0;
    }
    uint16_t idx = 0;
    adv_data[idx++] = (uint8_t)(plen + 1);
    adv_data[idx++] = SLE_ADV_DATA_TYPE_MANUFACTURER_SPECIFIC;
    if (memcpy_s(&adv_data[idx], max_len - idx, payload, plen) != EOK) {
        return 0;
    }
    return (uint16_t)(idx + plen);
}
#endif

static uint16_t sle_set_adv_data(uint8_t *adv_data, const uint8_t *mfg_payload, uint8_t mfg_len)
{
    uint16_t idx = 0;
    size_t len = sizeof(struct sle_adv_common_value);

    struct sle_adv_common_value adv_disc_level = {
        .length = len - 1,
        .type = SLE_ADV_DATA_TYPE_DISCOVERY_LEVEL,
        .value = SLE_ANNOUNCE_LEVEL_NORMAL,
    };
    if (memcpy_s(&adv_data[idx], SLE_ADV_DATA_LEN_MAX - idx, &adv_disc_level, len) != EOK) {
        return 0;
    }
    idx += (uint16_t)len;

    struct sle_adv_common_value adv_access_mode = {
        .length = len - 1,
        .type = SLE_ADV_DATA_TYPE_ACCESS_MODE,
        .value = 0,
    };
    if (memcpy_s(&adv_data[idx], SLE_ADV_DATA_LEN_MAX - idx, &adv_access_mode, len) != EOK) {
        return 0;
    }
    idx += (uint16_t)len;

#if PAIBING_SLE_SENSOR_BROADCAST
    if (mfg_payload != NULL && mfg_len > 0) {
        idx += sle_append_mfg_sensor(&adv_data[idx], (uint16_t)(SLE_ADV_DATA_LEN_MAX - idx), mfg_payload, mfg_len);
    }
#else
    unused(mfg_payload);
    unused(mfg_len);
    idx += sle_set_adv_local_name(&adv_data[idx], SLE_ADV_DATA_LEN_MAX - idx);
#endif
    return idx;
}

static uint16_t sle_set_scan_response_data(uint8_t *scan_rsp_data, const uint8_t *mfg_payload, uint8_t mfg_len)
{
    uint16_t idx = 0;
    size_t len = sizeof(struct sle_adv_common_value);

    struct sle_adv_common_value tx_power_level = {
        .length = len - 1,
        .type = SLE_ADV_DATA_TYPE_TX_POWER_LEVEL,
        .value = SLE_ADV_TX_POWER,
    };
    if (memcpy_s(scan_rsp_data, SLE_ADV_DATA_LEN_MAX, &tx_power_level, len) != EOK) {
        return 0;
    }
    idx += (uint16_t)len;
    idx += sle_set_adv_local_name(&scan_rsp_data[idx], SLE_ADV_DATA_LEN_MAX - idx);
#if PAIBING_SLE_SENSOR_BROADCAST
    /* Scan Response 也带同一份 ASCII，远距离多一路接收机会 */
    if (mfg_payload != NULL && mfg_len > 0) {
        idx += sle_append_mfg_sensor(&scan_rsp_data[idx], (uint16_t)(SLE_ADV_DATA_LEN_MAX - idx), mfg_payload, mfg_len);
    }
#else
    unused(mfg_payload);
    unused(mfg_len);
#endif
    return idx;
}

#if PAIBING_PEER_WHITELIST_ENABLE
static bool paibing_mac_all_zero(const uint8_t *mac, uint8_t len)
{
    for (uint8_t i = 0; i < len; i++) {
        if (mac[i] != 0) {
            return false;
        }
    }
    return true;
}
#endif

errcode_t paibing_sle_apply_peer_whitelist(void)
{
#if !PAIBING_PEER_WHITELIST_ENABLE
    return ERRCODE_SLE_SUCCESS;
#else
    if (paibing_mac_all_zero(g_paibing_sle_allowed_peer, SLE_ADDR_LEN)) {
        osal_printk("%s peer whitelist disabled (allowed peer MAC is all zero)\r\n", PAIBING_SLE_LOG);
        return ERRCODE_SLE_SUCCESS;
    }
    sle_addr_t peer = {0};
    peer.type = SLE_ADDRESS_TYPE_PUBLIC;
    if (memcpy_s(peer.addr, SLE_ADDR_LEN, g_paibing_sle_allowed_peer, SLE_ADDR_LEN) != EOK) {
        return ERRCODE_SLE_FAIL;
    }
    (void)sle_clear_access_filter_list();
    errcode_t ret = sle_add_device_to_access_filter_list(&peer);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s add peer whitelist fail:0x%x\r\n", PAIBING_SLE_LOG, ret);
        return ret;
    }
    return ERRCODE_SLE_SUCCESS;
#endif
}

bool paibing_sle_peer_whitelist_enabled(void)
{
#if PAIBING_PEER_WHITELIST_ENABLE
    return !paibing_mac_all_zero(g_paibing_sle_allowed_peer, SLE_ADDR_LEN);
#else
    return false;
#endif
}

bool paibing_sle_is_peer_allowed(const uint8_t *peer_mac)
{
#if PAIBING_PEER_WHITELIST_ENABLE
    if (!paibing_sle_peer_whitelist_enabled() || (peer_mac == NULL)) {
        return true;
    }
    return memcmp(peer_mac, g_paibing_sle_allowed_peer, SLE_ADDR_LEN) == 0;
#else
    unused(peer_mac);
    return true;
#endif
}

errcode_t paibing_sle_set_local_addr(void)
{
#if !PAIBING_USE_FIXED_LOCAL_MAC
    return ERRCODE_SLE_SUCCESS;
#else
    sle_addr_t local_addr = {0};
    local_addr.type = SLE_ADDRESS_TYPE_PUBLIC;
    if (memcpy_s(local_addr.addr, SLE_ADDR_LEN, g_paibing_sle_public_addr, SLE_ADDR_LEN) != EOK) {
        return ERRCODE_SLE_FAIL;
    }
    errcode_t ret = sle_set_local_addr(&local_addr);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s set addr fail:0x%x\r\n", PAIBING_SLE_LOG, ret);
        return ret;
    }
    return ERRCODE_SLE_SUCCESS;
#endif
}

static errcode_t sle_set_default_announce_param(void)
{
    sle_announce_param_t param = {0};

#if PAIBING_SLE_SENSOR_BROADCAST
    /* 非连接广播：面板只扫数据，不占连接数 */
    param.announce_mode = SLE_ANNOUNCE_MODE_NONCONN_SCANABLE;
#else
    param.announce_mode = SLE_ANNOUNCE_MODE_CONNECTABLE_SCANABLE;
#endif
    param.announce_handle = SLE_ADV_HANDLE_DEFAULT;
    param.announce_gt_role = SLE_ANNOUNCE_ROLE_T_CAN_NEGO;
    param.announce_level = SLE_ANNOUNCE_LEVEL_NORMAL;
    param.announce_channel_map = SLE_ADV_CHANNEL_MAP_DEFAULT;
    param.announce_tx_power = PAIBING_SLE_ANNOUNCE_TX_POWER_DBM;
    param.announce_interval_min = SLE_ADV_INTERVAL_MIN_DEFAULT;
    param.announce_interval_max = SLE_ADV_INTERVAL_MAX_DEFAULT;
    param.conn_interval_min = SLE_CONN_INTV_MIN_DEFAULT;
    param.conn_interval_max = SLE_CONN_INTV_MAX_DEFAULT;
    param.conn_max_latency = SLE_CONN_MAX_LATENCY;
    param.conn_supervision_timeout = SLE_CONN_SUPERVISION_TIMEOUT_DEFAULT;
    param.own_addr.type = SLE_ADDRESS_TYPE_PUBLIC;
#if PAIBING_USE_FIXED_LOCAL_MAC
    if (memcpy_s(param.own_addr.addr, SLE_ADDR_LEN, g_paibing_sle_public_addr, SLE_ADDR_LEN) != EOK) {
        return ERRCODE_SLE_FAIL;
    }
#else
    (void)memset_s(param.own_addr.addr, SLE_ADDR_LEN, 0, SLE_ADDR_LEN);
#endif
#if PAIBING_PEER_WHITELIST_ENABLE
    if (paibing_sle_peer_whitelist_enabled()) {
        param.ext_param = &g_paibing_sle_adv_ext;
    }
#endif
    return sle_set_announce_param(param.announce_handle, &param);
}

#if PAIBING_SLE_SENSOR_BROADCAST
static errcode_t paibing_sle_commit_announce_data(void)
{
    sle_announce_data_t data = {0};

    data.announce_data = g_announce_buf;
    data.announce_data_len = g_announce_len;
    data.seek_rsp_data = g_seek_rsp_buf;
    data.seek_rsp_data_len = g_seek_rsp_len;
    return sle_set_announce_data(SLE_ADV_HANDLE_DEFAULT, &data);
}

static errcode_t paibing_sle_stage_announce_buffers(const uint8_t *mfg_payload, uint8_t mfg_len)
{
    if (mfg_payload == NULL || mfg_len == 0) {
        return ERRCODE_SLE_FAIL;
    }

    g_announce_len = sle_set_adv_data(g_announce_buf, mfg_payload, mfg_len);
    g_seek_rsp_len = sle_set_scan_response_data(g_seek_rsp_buf, mfg_payload, mfg_len);
    if (g_announce_len == 0 || g_seek_rsp_len == 0) {
        return ERRCODE_SLE_FAIL;
    }

    if (memcpy_s(g_last_payload, sizeof(g_last_payload), mfg_payload, mfg_len) == EOK) {
        g_last_payload_len = mfg_len;
        g_have_last_mfg = true;
    }
    return ERRCODE_SLE_SUCCESS;
}

static errcode_t paibing_sle_adv_rf_start(void)
{
    errcode_t ret;

    ret = paibing_sle_commit_announce_data();
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s commit announce fail:0x%x\r\n", PAIBING_SLE_LOG, ret);
        return ret;
    }

    ret = sle_start_announce(SLE_ADV_HANDLE_DEFAULT);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s start announce fail:0x%x\r\n", PAIBING_SLE_LOG, ret);
        return ret;
    }

    g_adv_started = true;
    g_adv_active = true;
    return ERRCODE_SLE_SUCCESS;
}

/*
 * 每 100ms 同步 stop→等 disable→commit+start（热更新不生效）。
 * 比纯异步 restart 缩短停播空窗；间隔 0x28 保持高发包密度。
 */
static errcode_t paibing_sle_adv_rf_publish(void)
{
    errcode_t ret;
    uint32_t waited = 0;

    if (!g_have_last_mfg || g_announce_len == 0) {
        return ERRCODE_SLE_FAIL;
    }

    if (!g_adv_started) {
        osal_printk("%s ASCII adv 10Hz sync-restart\r\n", PAIBING_SLE_LOG);
        return paibing_sle_adv_rf_start();
    }

    if (g_adv_active) {
        ret = sle_stop_announce(SLE_ADV_HANDLE_DEFAULT);
        if (ret != ERRCODE_SLE_SUCCESS) {
            osal_printk("%s stop announce fail:0x%x\r\n", PAIBING_SLE_LOG, ret);
            return ret;
        }
        while (g_adv_active && waited < PAIBING_ADV_STOP_WAIT_MS) {
            osal_msleep(PAIBING_ADV_STOP_POLL_MS);
            waited += PAIBING_ADV_STOP_POLL_MS;
        }
        if (g_adv_active) {
            osal_printk("%s stop timeout, force refresh\r\n", PAIBING_SLE_LOG);
            g_adv_active = false;
        }
    }

    return paibing_sle_adv_rf_start();
}
#endif

static errcode_t sle_set_default_announce_data(void)
{
#if PAIBING_SLE_SENSOR_BROADCAST
    if (!g_have_last_mfg) {
        return ERRCODE_SLE_FAIL;
    }
    (void)paibing_sle_stage_announce_buffers(g_last_payload, g_last_payload_len);
    return paibing_sle_adv_rf_publish();
#else
    sle_announce_data_t data = {0};
    uint8_t announce_data[SLE_ADV_DATA_LEN_MAX] = {0};
    uint8_t seek_rsp_data[SLE_ADV_DATA_LEN_MAX] = {0};

    data.announce_data = announce_data;
    data.announce_data_len = sle_set_adv_data(announce_data, NULL, 0);
    data.seek_rsp_data = seek_rsp_data;
    data.seek_rsp_data_len = sle_set_scan_response_data(seek_rsp_data, NULL, 0);
    return sle_set_announce_data(SLE_ADV_HANDLE_DEFAULT, &data);
#endif
}

static void sle_announce_enable_cbk(uint32_t announce_id, errcode_t status)
{
    unused(announce_id);
#if PAIBING_SLE_SENSOR_BROADCAST
    g_adv_active = (status == ERRCODE_SLE_SUCCESS);
#else
    unused(status);
#endif
}

static void sle_announce_disable_cbk(uint32_t announce_id, errcode_t status)
{
    unused(announce_id);
    unused(status);
#if PAIBING_SLE_SENSOR_BROADCAST
    g_adv_active = false;
#endif
}

static void sle_announce_terminal_cbk(uint32_t announce_id)
{
    unused(announce_id);
#if PAIBING_SLE_SENSOR_BROADCAST
    g_adv_active = false;
#endif
}

errcode_t paibing_sle_announce_register_cbks(void)
{
    sle_announce_seek_callbacks_t seek_cbks = {0};
    seek_cbks.announce_enable_cb = sle_announce_enable_cbk;
    seek_cbks.announce_disable_cb = sle_announce_disable_cbk;
    seek_cbks.announce_terminal_cb = sle_announce_terminal_cbk;
    errcode_t ret = sle_announce_seek_register_callbacks(&seek_cbks);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s announce register fail:0x%x\r\n", PAIBING_SLE_LOG, ret);
    }
    return ret;
}

errcode_t paibing_sle_adv_start(void)
{
#if PAIBING_SLE_SENSOR_BROADCAST
    return paibing_sle_adv_prepare();
#else
    errcode_t ret = paibing_sle_adv_restart();
    if (ret == ERRCODE_SLE_SUCCESS) {
        osal_printk("%s adv started\r\n", PAIBING_SLE_LOG);
    }
    return ret;
#endif
}

errcode_t paibing_sle_adv_prepare(void)
{
#if PAIBING_SLE_SENSOR_BROADCAST
    (void)paibing_sle_set_local_addr();
    (void)paibing_sle_apply_peer_whitelist();
    return sle_set_default_announce_param();
#else
    return paibing_sle_adv_restart();
#endif
}

errcode_t paibing_sle_adv_restart(void)
{
    errcode_t ret;

    (void)paibing_sle_set_local_addr();
    (void)paibing_sle_apply_peer_whitelist();
    ret = sle_set_default_announce_param();
    if (ret != ERRCODE_SLE_SUCCESS) {
        return ret;
    }
    ret = sle_set_default_announce_data();
    if (ret != ERRCODE_SLE_SUCCESS) {
        return ret;
    }
#if !PAIBING_SLE_SENSOR_BROADCAST
    return sle_start_announce(SLE_ADV_HANDLE_DEFAULT);
#else
    return ret;
#endif
}

#if PAIBING_SLE_SENSOR_BROADCAST
errcode_t paibing_sle_adv_push_sensor(uint32_t uptime_ms, int32_t ax_mg, int32_t ay_mg, int32_t az_mg,
    int32_t gx_mdps, int32_t gy_mdps, float roll_deg, float pitch_deg, float magnitude)
{
    uint8_t payload[PAIBING_ADV_ASCII_MAX];
    int plen;
    errcode_t ret;

    plen = paibing_build_ascii_line(payload, sizeof(payload), uptime_ms, ax_mg, ay_mg, az_mg,
        gx_mdps, gy_mdps, roll_deg, pitch_deg, magnitude);
    if (plen == 0) {
        return ERRCODE_SLE_FAIL;
    }

    ret = paibing_sle_stage_announce_buffers(payload, (uint8_t)plen);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s stage adv fail\r\n", PAIBING_SLE_LOG);
        return ret;
    }

    ret = paibing_sle_adv_rf_publish();
    if (ret != ERRCODE_SLE_SUCCESS) {
        return ret;
    }

    if (++g_adv_log_div >= 100) {
        g_adv_log_div = 0;
        osal_printk("%s adv: %.*s", PAIBING_SLE_LOG, plen, (const char *)payload);
    }
    return ERRCODE_SLE_SUCCESS;
}
#endif
