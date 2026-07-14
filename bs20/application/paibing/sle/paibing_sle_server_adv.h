/**
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026.
 *
 * Description: paibing 星闪(SLE) 广播配置接口.
 */

#ifndef PAIBING_SLE_SERVER_ADV_H
#define PAIBING_SLE_SERVER_ADV_H

#include <stdbool.h>
#include <stdint.h>
#include "sle_device_discovery.h"
#include "errcode.h"
#include "mac_config.h"

/* 广播数据结构 */
typedef struct sle_adv_common_value {
    uint8_t length;
    uint8_t type;
    uint8_t value;
} le_adv_common_t;

/* 广播信道位图 */
typedef enum sle_adv_channel {
    SLE_ADV_CHANNEL_MAP_77      = 0x01,
    SLE_ADV_CHANNEL_MAP_78      = 0x02,
    SLE_ADV_CHANNEL_MAP_79      = 0x04,
    SLE_ADV_CHANNEL_MAP_DEFAULT = 0x07
} sle_adv_channel_map_t;

/* 广播数据类型 */
typedef enum sle_adv_data {
    SLE_ADV_DATA_TYPE_DISCOVERY_LEVEL          = 0x01,
    SLE_ADV_DATA_TYPE_ACCESS_MODE              = 0x02,
    SLE_ADV_DATA_TYPE_SERVICE_DATA_16BIT_UUID  = 0x03,
    SLE_ADV_DATA_TYPE_SERVICE_DATA_128BIT_UUID = 0x04,
    SLE_ADV_DATA_TYPE_COMPLETE_LOCAL_NAME      = 0x0B,
    SLE_ADV_DATA_TYPE_TX_POWER_LEVEL           = 0x0C,
    SLE_ADV_DATA_TYPE_MANUFACTURER_SPECIFIC    = 0xFF,
} sle_adv_data_type;

/* 注册广播相关回调 */
errcode_t paibing_sle_announce_register_cbks(void);

/* 设置广播参数并开始广播 */
errcode_t paibing_sle_adv_start(void);

/* 断线后重新广播，便于对端再次扫描连接 */
errcode_t paibing_sle_adv_restart(void);

/* 设置星闪公共地址（须在广播前调用） */
errcode_t paibing_sle_set_local_addr(void);

/* 配置对端连接白名单（见 paibing_mac_config.h） */
errcode_t paibing_sle_apply_peer_whitelist(void);
bool paibing_sle_peer_whitelist_enabled(void);
bool paibing_sle_is_peer_allowed(const uint8_t *peer_mac);

errcode_t paibing_sle_adv_prepare(void);
errcode_t paibing_sle_adv_push_sensor(uint32_t uptime_ms, int32_t ax_mg, int32_t ay_mg, int32_t az_mg,
    int32_t gx_mdps, int32_t gy_mdps, float roll_deg, float pitch_deg, float magnitude);

#endif
