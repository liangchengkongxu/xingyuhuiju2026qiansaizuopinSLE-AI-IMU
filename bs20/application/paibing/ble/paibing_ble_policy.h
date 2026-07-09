/**
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026.
 */

#ifndef PAIBING_BLE_POLICY_H
#define PAIBING_BLE_POLICY_H

#include <stdbool.h>
#include <stdint.h>

bool paibing_ble_peer_whitelist_enabled(void);
bool paibing_ble_is_peer_allowed(const uint8_t *peer_mac);
void paibing_ble_apply_peer_whitelist(void);

#endif
