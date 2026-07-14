/**
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026.
 *
 * @brief  paibing BLE：可连接广播 + 连接后 Notify 发送九轴 ASCII 行。
 *
 * 行格式（与原先一致）：
 * @185230,A-13,+1,+98,G-2,-1,R+123,P-456,M102\n
 */

#include <stdio.h>
#include "securec.h"
#include "soc_osal.h"
#include "bts_le_gap.h"
#include "ble_uart_server.h"
#include "ble_uart_server_adv.h"
#include "mac_config.h"
#include "paibing_ble_uart.h"
#include "paibing_ble_policy.h"

#define PAIBING_BLE_UART_LOG "[paibing ble]"

static char g_tx_line[96];
static uint8_t g_log_div;

#if PAIBING_USE_FIXED_LOCAL_MAC
static const uint8_t g_paibing_ble_addr[PAIBING_MAC_LEN] = {
    PAIBING_LOCAL_MAC_B0, PAIBING_LOCAL_MAC_B1, PAIBING_LOCAL_MAC_B2,
    PAIBING_LOCAL_MAC_B3, PAIBING_LOCAL_MAC_B4, PAIBING_LOCAL_MAC_B5
};
#endif

errcode_t paibing_ble_uart_init(void)
{
    static const uint8_t k_name[] = "paibing_imu";
    const uint8_t name_len = (uint8_t)(sizeof(k_name) - 1);

#if PAIBING_USE_FIXED_LOCAL_MAC
    ble_uart_set_local_addr(g_paibing_ble_addr, PAIBING_MAC_LEN);
#endif
    ble_uart_set_device_name_value(k_name, name_len);
    ble_uart_set_adv_local_name(k_name, name_len);
    ble_uart_server_init();
    (void)paibing_ble_apply_peer_whitelist();
    osal_printk("%s GATT server ready, name paibing_imu, connect for IMU notify\r\n", PAIBING_BLE_UART_LOG);
    return ERRCODE_SUCC;
}

errcode_t paibing_ble_uart_push_sensor(uint32_t uptime_ms, int32_t ax_mg, int32_t ay_mg, int32_t az_mg,
    int32_t gx_mdps, int32_t gy_mdps, float roll_deg, float pitch_deg, float magnitude)
{
    int len;

    if (ble_uart_get_connection_state() != GAP_BLE_STATE_CONNECTED) {
        return ERRCODE_FAIL;
    }

    len = snprintf(g_tx_line, sizeof(g_tx_line),
        "@%lu,A%+ld,%+ld,%+ld,G%+ld,%+ld,R%+d,P%+d,M%lu\n",
        (unsigned long)uptime_ms,
        (long)(ax_mg / 10), (long)(ay_mg / 10), (long)(az_mg / 10),
        (long)(gx_mdps / 1000), (long)(gy_mdps / 1000),
        (int)(roll_deg * 10), (int)(pitch_deg * 10), (unsigned long)(magnitude * 100));

    if (len <= 0 || len >= (int)sizeof(g_tx_line)) {
        return ERRCODE_FAIL;
    }

    if (ble_uart_server_send_input_report((uint8_t *)g_tx_line, (uint16_t)len) != ERRCODE_BT_SUCCESS) {
        return ERRCODE_FAIL;
    }

    if (++g_log_div >= 50) {
        g_log_div = 0;
        osal_printk("%s notify ms:%lu\r\n", PAIBING_BLE_UART_LOG, (unsigned long)uptime_ms);
    }
    return ERRCODE_SUCC;
}
