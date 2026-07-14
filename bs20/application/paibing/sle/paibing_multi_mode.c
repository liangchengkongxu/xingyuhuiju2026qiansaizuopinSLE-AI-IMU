/**
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026.
 *
 * @brief  多人模式：解析面板 Manufacturer Specific Data (0xFF) 测量指令。
 */

#include "paibing_multi_mode.h"
#include "mac_config.h"
#include "soc_osal.h"
#include "sle_device_discovery.h"
#include "sle_errcode.h"
#include "common_def.h"

#define PAIBING_MULTI_LOG "[paibing multi]"

#define SLE_SEEK_INTERVAL_DEFAULT 100
#define SLE_SEEK_WINDOW_DEFAULT   100

#if PAIBING_MULTI_MODE_ENABLE
static bool g_measure_active = false;
static uint16_t g_measure_session = 0;

static bool paibing_mfg_dev_matches(uint8_t dev)
{
    if (dev == PAIBING_MFG_DEV_ALL) {
        return true;
    }
    return (dev == PAIBING_DEVICE_ID);
}

bool paibing_multi_mode_handle_payload(const uint8_t *payload, uint8_t payload_len)
{
    if (payload_len < PAIBING_MFG_PAYLOAD_LEN) {
        return false;
    }
    if (payload[0] != PAIBING_MFG_ID_LO || payload[1] != PAIBING_MFG_ID_HI) {
        return false;
    }
    if (payload[2] != PAIBING_MFG_PROTO_VER) {
        return false;
    }

    uint8_t cmd = payload[3];
    uint8_t dev = payload[4];
    uint16_t session = (uint16_t)(payload[5] | ((uint16_t)payload[6] << 8));

    if (!paibing_mfg_dev_matches(dev)) {
        return false;
    }

    if (cmd == PAIBING_MFG_CMD_START) {
        if (g_measure_active && (session == g_measure_session)) {
            return false;
        }
        g_measure_active = true;
        g_measure_session = session;
        osal_printk("%s START dev:0x%02x session:0x%04x\r\n", PAIBING_MULTI_LOG, dev, session);
        return true;
    }

    if (cmd == PAIBING_MFG_CMD_STOP) {
        if (!g_measure_active) {
            return false;
        }
        g_measure_active = false;
        g_measure_session = session;
        osal_printk("%s STOP dev:0x%02x session:0x%04x\r\n", PAIBING_MULTI_LOG, dev, session);
        return true;
    }

    return false;
}
#else
bool paibing_multi_mode_handle_payload(const uint8_t *payload, uint8_t payload_len)
{
    unused(payload);
    unused(payload_len);
    return false;
}
#endif

#if PAIBING_MULTI_MODE_ENABLE
static bool paibing_parse_adv_manufacturer(const uint8_t *data, uint8_t len)
{
    uint8_t idx = 0;

    if (data == NULL || len == 0) {
        return false;
    }

    while (idx < len) {
        uint8_t field_len = data[idx];
        if (field_len == 0) {
            break;
        }
        if ((uint16_t)idx + 1u + (uint16_t)field_len > (uint16_t)len) {
            break;
        }

        uint8_t ad_type = data[idx + 1];
        const uint8_t *ad_value = &data[idx + 2];
        uint8_t ad_value_len = (uint8_t)(field_len - 1);

        if (ad_type == PAIBING_ADV_TYPE_MANUFACTURER) {
            if (paibing_multi_mode_handle_payload(ad_value, ad_value_len)) {
                return true;
            }
        }

        idx = (uint8_t)(idx + 1u + field_len);
    }

    /* 部分协议栈上报的 data 仅为 7 字节裸载荷 */
    if (len >= PAIBING_MFG_PAYLOAD_LEN) {
        return paibing_multi_mode_handle_payload(data, PAIBING_MFG_PAYLOAD_LEN);
    }
    return false;
}
#endif

#if PAIBING_MULTI_MODE_ENABLE
void paibing_sle_multi_mode_write_cbk(uint8_t server_id, uint16_t conn_id,
    ssaps_req_write_cb_t *write_cb_para, errcode_t status)
{
    unused(server_id);
    unused(conn_id);
    unused(status);

    if (write_cb_para == NULL || write_cb_para->value == NULL ||
        write_cb_para->length < PAIBING_MFG_PAYLOAD_LEN) {
        return;
    }
    (void)paibing_multi_mode_handle_payload(write_cb_para->value, PAIBING_MFG_PAYLOAD_LEN);
    osal_printk("%s write cmd handle:0x%x len:%u\r\n", PAIBING_MULTI_LOG,
        write_cb_para->handle, write_cb_para->length);
}
#else
void paibing_sle_multi_mode_write_cbk(uint8_t server_id, uint16_t conn_id,
    ssaps_req_write_cb_t *write_cb_para, errcode_t status)
{
    unused(server_id);
    unused(conn_id);
    unused(write_cb_para);
    unused(status);
}
#endif

void paibing_multi_mode_init(void)
{
#if PAIBING_MULTI_MODE_ENABLE
    g_measure_active = false;
    g_measure_session = 0;
    osal_printk("%s enabled, device_id=0x%02x, cmd via SSAP write\r\n", PAIBING_MULTI_LOG, PAIBING_DEVICE_ID);
#else
    unused(PAIBING_MULTI_LOG);
#endif
}

bool paibing_multi_mode_report_enabled(void)
{
#if !PAIBING_MULTI_MODE_ENABLE
    return true;
#else
    return g_measure_active;
#endif
}

bool paibing_multi_mode_on_seek_result(const sle_seek_result_info_t *info)
{
#if !PAIBING_MULTI_MODE_ENABLE
    unused(info);
    return false;
#else
    if (info == NULL || info->data == NULL || info->data_length == 0) {
        return false;
    }
    return paibing_parse_adv_manufacturer(info->data, info->data_length);
#endif
}

void paibing_sle_cmd_scan_start(void)
{
#if !PAIBING_MULTI_MODE_ENABLE || !PAIBING_MULTI_SCAN_ENABLE
    return;
#else
    sle_seek_param_t param = {0};
    param.own_addr_type = 0;
    param.filter_duplicates = 0;
    param.seek_filter_policy = 0;
    param.seek_phys = 1;
    param.seek_type[0] = 1;
    param.seek_interval[0] = SLE_SEEK_INTERVAL_DEFAULT;
    param.seek_window[0] = SLE_SEEK_WINDOW_DEFAULT;

    errcode_t ret = sle_set_seek_param(&param);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s set seek param fail:0x%x\r\n", PAIBING_MULTI_LOG, ret);
        return;
    }
    ret = sle_start_seek();
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s start seek fail:0x%x\r\n", PAIBING_MULTI_LOG, ret);
    }
#endif
}
