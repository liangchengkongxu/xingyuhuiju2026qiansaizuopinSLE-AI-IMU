/**
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026.
 *
 * @brief  paibing BLE 连接 + Notify 发送九轴数据。
 */

#ifndef PAIBING_BLE_UART_H
#define PAIBING_BLE_UART_H

#include <stdint.h>
#include "errcode.h"

errcode_t paibing_ble_uart_init(void);
errcode_t paibing_ble_uart_push_sensor(uint32_t uptime_ms, int32_t ax_mg, int32_t ay_mg, int32_t az_mg,
    int32_t gx_mdps, int32_t gy_mdps, float roll_deg, float pitch_deg, float magnitude);

#endif
