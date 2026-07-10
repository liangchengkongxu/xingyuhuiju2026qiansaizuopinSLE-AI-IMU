/**
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026.
 *
 * @brief  paibing 可选：本机固定 MAC / 对端白名单 / 数据上报方式。
 */

#ifndef PAIBING_SLE_MAC_CONFIG_H
#define PAIBING_SLE_MAC_CONFIG_H

#define PAIBING_MAC_LEN 6

/* 0：不写死本机 MAC，由芯片/协议栈分配；1：使用下方 PAIBING_LOCAL_MAC_* */
#define PAIBING_USE_FIXED_LOCAL_MAC 1

#define PAIBING_LOCAL_MAC_B0 0xCC
#define PAIBING_LOCAL_MAC_B1 0xAD
#define PAIBING_LOCAL_MAC_B2 0xC9
#define PAIBING_LOCAL_MAC_B3 0x00
#define PAIBING_LOCAL_MAC_B4 0x22
#define PAIBING_LOCAL_MAC_B5 0x08

/* 0：任意对端可连；1：仅允许下方对端 MAC */
#define PAIBING_PEER_WHITELIST_ENABLE 0

#define PAIBING_ALLOWED_PEER_MAC_B0 0xEE
#define PAIBING_ALLOWED_PEER_MAC_B1 0xEF
#define PAIBING_ALLOWED_PEER_MAC_B2 0x33
#define PAIBING_ALLOWED_PEER_MAC_B3 0x9B
#define PAIBING_ALLOWED_PEER_MAC_B4 0x8D
#define PAIBING_ALLOWED_PEER_MAC_B5 0x5C

/*
 * 1：非连接扫播，ADV+ScanRsp 的 0xFF 发 ASCII 行（与 BLE Notify 相同）
 * 0：连接后 Notify ASCII 行
 */
#define PAIBING_SLE_SENSOR_BROADCAST 1
#define PAIBING_BLE_SENSOR_BROADCAST 0

/* 传感器广播模式关闭时：多人模式 / 连接后 Write 测量指令 */
#define PAIBING_MULTI_MODE_ENABLE 0
#define PAIBING_MULTI_SCAN_ENABLE 0
/* 本机设备号（广播载荷 DEV 字节） */
#define PAIBING_DEVICE_ID 0x01

#endif
