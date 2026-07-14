/**
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026.
 *
 * @brief  paibing BLE 传感器厂商广播实现（协议与星闪版 22 字节一致）。
 */

#include <string.h>
#include "securec.h"
#include "common_def.h"
#include "soc_osal.h"
#include "bts_def.h"
#include "bts_device_manager.h"
#include "bts_le_gap.h"
#include "errcode.h"
#include "paibing_ble_sensor_adv.h"
#include "mac_config.h"
#include "paibing_ble_policy.h"

#define PAIBING_BLE_ADV_LOG            "[paibing ble adv]"
#define PAIBING_BLE_ADV_HANDLE         1
#define PAIBING_BLE_ADV_DATA_MAX       251
#define PAIBING_BLE_ADV_MIN_INTERVAL   0x30
#define PAIBING_BLE_ADV_MAX_INTERVAL   0x60
#define PAIBING_BLE_ADV_MFG_TYPE       0xFF
#define PAIBING_BLE_ADV_FLAG_TYPE      0x01
#define PAIBING_BLE_ADV_NAME_TYPE      0x09
#define PAIBING_BLE_ADV_TX_POWER_TYPE  0x0A

#define PAIBING_MFG_ID_LO              0xEB
#define PAIBING_MFG_ID_HI              0x1A
#define PAIBING_MFG_PROTO_SENSOR       0x02
#define PAIBING_MFG_SENSOR_PAYLOAD_LEN 22
#define PAIBING_BLE_ADV_STOP_WAIT_MS   60
#define PAIBING_BLE_ADV_STOP_POLL_MS   2

static uint8_t g_ble_local_name[] = "paibing_imu";
static const uint8_t g_ble_local_name_len = sizeof(g_ble_local_name);

static uint8_t g_adv_buf[PAIBING_BLE_ADV_DATA_MAX];
static uint8_t g_scan_rsp_buf[PAIBING_BLE_ADV_DATA_MAX];
static uint16_t g_adv_len;
static uint16_t g_scan_rsp_len;
static bool g_adv_active;
static bool g_adv_started;
static uint8_t g_last_mfg[PAIBING_MFG_SENSOR_PAYLOAD_LEN];
static bool g_have_last_mfg;
static uint8_t g_adv_log_div;

#if PAIBING_USE_FIXED_LOCAL_MAC
static const uint8_t g_paibing_ble_public_addr[PAIBING_MAC_LEN] = {
    PAIBING_LOCAL_MAC_B0, PAIBING_LOCAL_MAC_B1, PAIBING_LOCAL_MAC_B2,
    PAIBING_LOCAL_MAC_B3, PAIBING_LOCAL_MAC_B4, PAIBING_LOCAL_MAC_B5
};
#endif

static void paibing_put_u16_le(uint8_t *dst, uint16_t v)
{
    dst[0] = (uint8_t)(v & 0xFF);
    dst[1] = (uint8_t)((v >> 8) & 0xFF);
}

static void paibing_put_i16_le(uint8_t *dst, int16_t v)
{
    paibing_put_u16_le(dst, (uint16_t)v);
}

static uint8_t paibing_build_mfg_sensor(uint8_t *payload, uint32_t uptime_ms,
    int32_t ax_mg, int32_t ay_mg, int32_t az_mg, int32_t gx_mdps, int32_t gy_mdps,
    float roll_deg, float pitch_deg, float magnitude)
{
    int16_t ax = (int16_t)(ax_mg / 10);
    int16_t ay = (int16_t)(ay_mg / 10);
    int16_t az = (int16_t)(az_mg / 10);
    int16_t gx = (int16_t)(gx_mdps / 1000);
    int16_t gy = (int16_t)(gy_mdps / 1000);
    int16_t roll = (int16_t)(roll_deg * 10.0f);
    int16_t pitch = (int16_t)(pitch_deg * 10.0f);
    uint16_t mag = (uint16_t)(magnitude * 100.0f);

    payload[0] = PAIBING_MFG_ID_LO;
    payload[1] = PAIBING_MFG_ID_HI;
    payload[2] = PAIBING_MFG_PROTO_SENSOR;
    payload[3] = PAIBING_DEVICE_ID;
    paibing_put_u16_le(&payload[4], (uint16_t)(uptime_ms & 0xFFFF));
    paibing_put_i16_le(&payload[6], ax);
    paibing_put_i16_le(&payload[8], ay);
    paibing_put_i16_le(&payload[10], az);
    paibing_put_i16_le(&payload[12], gx);
    paibing_put_i16_le(&payload[14], gy);
    paibing_put_i16_le(&payload[16], roll);
    paibing_put_i16_le(&payload[18], pitch);
    paibing_put_u16_le(&payload[20], mag);
    return PAIBING_MFG_SENSOR_PAYLOAD_LEN;
}

static uint16_t paibing_ble_append_mfg(uint8_t *adv_data, uint16_t max_len, const uint8_t *payload, uint8_t plen)
{
    if ((plen == 0) || (max_len < (uint16_t)(plen + 2))) {
        return 0;
    }
    uint16_t idx = 0;
    adv_data[idx++] = (uint8_t)(plen + 1);
    adv_data[idx++] = PAIBING_BLE_ADV_MFG_TYPE;
    if (memcpy_s(&adv_data[idx], max_len - idx, payload, plen) != EOK) {
        return 0;
    }
    return (uint16_t)(idx + plen);
}

static uint16_t paibing_ble_build_adv_data(uint8_t *adv_data, const uint8_t *mfg_payload, uint8_t mfg_len)
{
    uint16_t idx = 0;

    adv_data[idx++] = 0x02;
    adv_data[idx++] = PAIBING_BLE_ADV_FLAG_TYPE;
    adv_data[idx++] = 0x06;

    if (mfg_payload != NULL && mfg_len > 0) {
        idx += paibing_ble_append_mfg(&adv_data[idx], (uint16_t)(PAIBING_BLE_ADV_DATA_MAX - idx), mfg_payload, mfg_len);
    }
    return idx;
}

static uint16_t paibing_ble_build_scan_rsp(uint8_t *scan_rsp_data)
{
    uint16_t idx = 0;

    scan_rsp_data[idx++] = 0x02;
    scan_rsp_data[idx++] = PAIBING_BLE_ADV_TX_POWER_TYPE;
    scan_rsp_data[idx++] = 0x00;

    scan_rsp_data[idx++] = (uint8_t)(g_ble_local_name_len + 1);
    scan_rsp_data[idx++] = PAIBING_BLE_ADV_NAME_TYPE;
    if (memcpy_s(&scan_rsp_data[idx], PAIBING_BLE_ADV_DATA_MAX - idx, g_ble_local_name, g_ble_local_name_len) != EOK) {
        return 0;
    }
    return (uint16_t)(idx + g_ble_local_name_len);
}

static errcode_t paibing_ble_commit_adv_data(void)
{
    gap_ble_config_adv_data_t data = {0};

    data.adv_data = g_adv_buf;
    data.adv_length = g_adv_len;
    data.scan_rsp_data = g_scan_rsp_buf;
    data.scan_rsp_length = g_scan_rsp_len;
    return gap_ble_set_adv_data(PAIBING_BLE_ADV_HANDLE, &data);
}

static errcode_t paibing_ble_stage_adv_buffers(const uint8_t *mfg_payload, uint8_t mfg_len)
{
    if (mfg_payload == NULL || mfg_len == 0) {
        return ERRCODE_FAIL;
    }

    g_adv_len = paibing_ble_build_adv_data(g_adv_buf, mfg_payload, mfg_len);
    g_scan_rsp_len = paibing_ble_build_scan_rsp(g_scan_rsp_buf);
    if (g_adv_len == 0 || g_scan_rsp_len == 0) {
        return ERRCODE_FAIL;
    }

    if (memcpy_s(g_last_mfg, sizeof(g_last_mfg), mfg_payload, mfg_len) == EOK) {
        g_have_last_mfg = true;
    }
    return ERRCODE_SUCC;
}

static void paibing_ble_wait_adv_stopped(void)
{
    uint32_t waited = 0;

    while (g_adv_active && (waited < PAIBING_BLE_ADV_STOP_WAIT_MS)) {
        (void)osal_msleep(PAIBING_BLE_ADV_STOP_POLL_MS);
        waited += PAIBING_BLE_ADV_STOP_POLL_MS;
    }
}

static errcode_t paibing_ble_set_adv_param(void)
{
    gap_ble_adv_params_t param = {
        .min_interval = PAIBING_BLE_ADV_MIN_INTERVAL,
        .max_interval = PAIBING_BLE_ADV_MAX_INTERVAL,
        .duration = 0,
        .peer_addr.type = BT_ADDRESS_TYPE_PUBLIC_DEVICE_ADDRESS,
        .channel_map = 0x07,
        .adv_type = GAP_BLE_ADV_NONCONN_SCAN_UNDIR,
        .adv_filter_policy = GAP_BLE_ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
    };

    (void)memset_s(param.peer_addr.addr, BD_ADDR_LEN, 0, BD_ADDR_LEN);
    return gap_ble_set_adv_param(PAIBING_BLE_ADV_HANDLE, &param);
}

static errcode_t paibing_ble_adv_rf_publish(void)
{
    errcode_t ret;

    if (!g_have_last_mfg || g_adv_len == 0) {
        return ERRCODE_FAIL;
    }

    if (g_adv_active) {
        ret = gap_ble_stop_adv(PAIBING_BLE_ADV_HANDLE);
        if (ret != ERRCODE_BT_SUCCESS) {
            osal_printk("%s stop adv fail:0x%x\r\n", PAIBING_BLE_ADV_LOG, ret);
            return ret;
        }
        paibing_ble_wait_adv_stopped();
        if (g_adv_active) {
            osal_printk("%s stop timeout, force republish\r\n", PAIBING_BLE_ADV_LOG);
            g_adv_active = false;
        }
    }

    ret = paibing_ble_commit_adv_data();
    if (ret != ERRCODE_BT_SUCCESS) {
        osal_printk("%s set adv data fail:0x%x\r\n", PAIBING_BLE_ADV_LOG, ret);
        return ret;
    }

    ret = gap_ble_start_adv(PAIBING_BLE_ADV_HANDLE);
    if (ret != ERRCODE_BT_SUCCESS) {
        osal_printk("%s start adv fail:0x%x\r\n", PAIBING_BLE_ADV_LOG, ret);
        return ret;
    }

    g_adv_started = true;
    g_adv_active = true;
    return ERRCODE_SUCC;
}

static void paibing_ble_start_adv_cbk(uint8_t adv_id, adv_status_t status)
{
    unused(adv_id);
    g_adv_active = (status == ADV_STATUS_ADVERTISING);
}

static void paibing_ble_stop_adv_cbk(uint8_t adv_id, adv_status_t status)
{
    unused(adv_id);
    if (status == ADV_STATUS_STOPPED) {
        g_adv_active = false;
    }
}

static void paibing_ble_power_on_cbk(uint8_t status)
{
    unused(status);
    (void)enable_ble();
}

static void paibing_ble_enable_cbk(uint8_t status)
{
    bd_addr_t ble_addr = {0};

    if (status != 0) {
        osal_printk("%s ble enable fail:%u\r\n", PAIBING_BLE_ADV_LOG, status);
        return;
    }

#if PAIBING_USE_FIXED_LOCAL_MAC
    ble_addr.type = BT_ADDRESS_TYPE_PUBLIC_DEVICE_ADDRESS;
    if (memcpy_s(ble_addr.addr, BD_ADDR_LEN, g_paibing_ble_public_addr, PAIBING_MAC_LEN) != EOK) {
        return;
    }
    (void)gap_ble_set_local_addr(&ble_addr);
#endif
    (void)gap_ble_set_local_name(g_ble_local_name, g_ble_local_name_len);
    (void)paibing_ble_apply_peer_whitelist();
    (void)paibing_ble_set_adv_param();
}

static errcode_t paibing_ble_register_callbacks(void)
{
    bts_dev_manager_callbacks_t dev_cb = {0};
    gap_ble_callbacks_t gap_cb = {0};

    dev_cb.power_on_cb = paibing_ble_power_on_cbk;
    dev_cb.ble_enable_cb = paibing_ble_enable_cbk;
    gap_cb.start_adv_cb = paibing_ble_start_adv_cbk;
    gap_cb.stop_adv_cb = paibing_ble_stop_adv_cbk;

    errcode_t ret = bts_dev_manager_register_callbacks(&dev_cb);
    if (ret != ERRCODE_BT_SUCCESS) {
        return ret;
    }
    ret = gap_ble_register_callbacks(&gap_cb);
    if (ret != ERRCODE_BT_SUCCESS) {
        return ret;
    }
#if (CORE_NUMS < 2)
    (void)enable_ble();
#endif
    return ERRCODE_SUCC;
}

errcode_t paibing_ble_sensor_init(void)
{
    return paibing_ble_register_callbacks();
}

errcode_t paibing_ble_sensor_adv_prepare(void)
{
    return paibing_ble_set_adv_param();
}

errcode_t paibing_ble_sensor_adv_push_sensor(uint32_t uptime_ms, int32_t ax_mg, int32_t ay_mg, int32_t az_mg,
    int32_t gx_mdps, int32_t gy_mdps, float roll_deg, float pitch_deg, float magnitude)
{
    uint8_t mfg[PAIBING_MFG_SENSOR_PAYLOAD_LEN];
    errcode_t ret;

    (void)paibing_build_mfg_sensor(mfg, uptime_ms, ax_mg, ay_mg, az_mg, gx_mdps, gy_mdps,
        roll_deg, pitch_deg, magnitude);
    ret = paibing_ble_stage_adv_buffers(mfg, sizeof(mfg));
    if (ret != ERRCODE_SUCC) {
        return ret;
    }

    if (!g_adv_started) {
        osal_printk("%s sensor adv 10Hz BLE, MAC cc:ad:c9:00:22:01\r\n", PAIBING_BLE_ADV_LOG);
    }

    ret = paibing_ble_adv_rf_publish();
    if (ret != ERRCODE_SUCC) {
        return ret;
    }

    if (++g_adv_log_div >= 100) {
        g_adv_log_div = 0;
        osal_printk("%s adv ms:%u ax:%d ay:%d az:%d\r\n", PAIBING_BLE_ADV_LOG,
            (unsigned)(mfg[4] | ((uint16_t)mfg[5] << 8)),
            (int)(int16_t)(mfg[6] | ((uint16_t)mfg[7] << 8)),
            (int)(int16_t)(mfg[8] | ((uint16_t)mfg[9] << 8)),
            (int)(int16_t)(mfg[10] | ((uint16_t)mfg[11] << 8)));
    }
    return ERRCODE_SUCC;
}

