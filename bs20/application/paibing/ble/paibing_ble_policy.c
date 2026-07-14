/**
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026.
 *
 * @brief  paibing BLE 连接白名单（可选）。
 */

#include <string.h>
#include "common_def.h"
#include "securec.h"
#include "bts_def.h"
#include "bts_le_gap.h"
#include "mac_config.h"
#include "paibing_ble_policy.h"

#if PAIBING_PEER_WHITELIST_ENABLE
static const uint8_t g_paibing_ble_allowed_peer[PAIBING_MAC_LEN] = {
    PAIBING_ALLOWED_PEER_MAC_B0, PAIBING_ALLOWED_PEER_MAC_B1, PAIBING_ALLOWED_PEER_MAC_B2,
    PAIBING_ALLOWED_PEER_MAC_B3, PAIBING_ALLOWED_PEER_MAC_B4, PAIBING_ALLOWED_PEER_MAC_B5
};
#endif

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

bool paibing_ble_peer_whitelist_enabled(void)
{
#if PAIBING_PEER_WHITELIST_ENABLE
    return !paibing_mac_all_zero(g_paibing_ble_allowed_peer, PAIBING_MAC_LEN);
#else
    return false;
#endif
}

bool paibing_ble_is_peer_allowed(const uint8_t *peer_mac)
{
#if PAIBING_PEER_WHITELIST_ENABLE
    if (!paibing_ble_peer_whitelist_enabled() || (peer_mac == NULL)) {
        return true;
    }
    return memcmp(peer_mac, g_paibing_ble_allowed_peer, PAIBING_MAC_LEN) == 0;
#else
    unused(peer_mac);
    return true;
#endif
}

void paibing_ble_apply_peer_whitelist(void)
{
#if PAIBING_PEER_WHITELIST_ENABLE
    if (!paibing_ble_peer_whitelist_enabled()) {
        return;
    }
    bd_addr_t peer = {0};
    peer.type = BT_ADDRESS_TYPE_PUBLIC_DEVICE_ADDRESS;
    if (memcpy_s(peer.addr, BD_ADDR_LEN, g_paibing_ble_allowed_peer, PAIBING_MAC_LEN) != EOK) {
        return;
    }
    (void)gap_ble_add_white_list(&peer);
#endif
}
