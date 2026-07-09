/**
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026.
 *
 * @brief  paibing BLE 传感器厂商广播（0xFF / 22 字节，与星闪版协议一致）。
 */

#ifndef PAIBING_BLE_SENSOR_ADV_H
#define PAIBING_BLE_SENSOR_ADV_H

#include <stdint.h>
#include "errcode.h"
#include "mac_config.h"

errcode_t paibing_ble_sensor_init(void);
errcode_t paibing_ble_sensor_adv_prepare(void);
errcode_t paibing_ble_sensor_adv_push_sensor(uint32_t uptime_ms, int32_t ax_mg, int32_t ay_mg, int32_t az_mg,
    int32_t gx_mdps, int32_t gy_mdps, float roll_deg, float pitch_deg, float magnitude);

#endif
