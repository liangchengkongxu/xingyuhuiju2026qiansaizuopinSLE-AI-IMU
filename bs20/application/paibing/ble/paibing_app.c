/**
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026.
 *
 * @brief  拍柄蓝牙版入口：BLE 连接 + Notify 发送九轴（ASCII 行格式与原先一致）。
 */

#include "chip_io.h"
#include "paibing_imu.h"
#include "paibing_ble_uart.h"

static void paibing_ble_transport_init(void)
{
    (void)paibing_ble_uart_init();
}

static void paibing_ble_transport_push(uint32_t uptime_ms, int32_t ax_mg, int32_t ay_mg, int32_t az_mg,
    int32_t gx_mdps, int32_t gy_mdps, float roll_deg, float pitch_deg, float magnitude)
{
    (void)paibing_ble_uart_push_sensor(uptime_ms, ax_mg, ay_mg, az_mg, gx_mdps, gy_mdps,
        roll_deg, pitch_deg, magnitude);
}

void app_main(void *unused)
{
    static const paibing_transport_t g_ble_transport = {
        .init = paibing_ble_transport_init,
        .push_sensor = paibing_ble_transport_push,
    };

    UNUSED(unused);
    paibing_board_init();
    paibing_uart_send("[paibing] transport: BLE GATT connected notify\r\n");
    paibing_imu_run(&g_ble_transport);
}
