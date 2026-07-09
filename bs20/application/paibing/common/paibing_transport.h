/**
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026.
 *
 * @brief  拍柄传感器上报传输层接口（星闪 / 蓝牙各自实现）。
 */

#ifndef PAIBING_TRANSPORT_H
#define PAIBING_TRANSPORT_H

#include <stdint.h>

typedef struct {
    void (*init)(void);
    void (*push_sensor)(uint32_t uptime_ms, int32_t ax_mg, int32_t ay_mg, int32_t az_mg,
        int32_t gx_mdps, int32_t gy_mdps, float roll_deg, float pitch_deg, float magnitude);
} paibing_transport_t;

#endif
