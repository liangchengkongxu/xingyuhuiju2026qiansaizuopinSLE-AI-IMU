/**
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026.
 *
 * @brief  多人模式：扫描面板广播中的厂商测量指令，控制 IMU 上报。
 */

#ifndef PAIBING_MULTI_MODE_H
#define PAIBING_MULTI_MODE_H

#include <stdbool.h>
#include <stdint.h>
#include "sle_device_discovery.h"
#include "sle_ssap_server.h"

#define PAIBING_MFG_ID_LO           0xEB
#define PAIBING_MFG_ID_HI           0x1A
#define PAIBING_MFG_PROTO_VER       0x01
#define PAIBING_MFG_CMD_START       0xA1
#define PAIBING_MFG_CMD_STOP        0xA2
#define PAIBING_MFG_DEV_ALL         0x00
#define PAIBING_MFG_PAYLOAD_LEN     7
#define PAIBING_ADV_TYPE_MANUFACTURER 0xFF

void paibing_multi_mode_init(void);

/* 多人模式关闭时：已连接即上报；开启时：仅收到 A1 且 DEV 匹配后上报 */
bool paibing_multi_mode_report_enabled(void);

/* 解析 7 字节命令载荷（连接后 SSAP Write 或裸数据） */
bool paibing_multi_mode_handle_payload(const uint8_t *payload, uint8_t payload_len);

/* 解析扫描结果中的 AD，返回 true 表示状态有变化 */
bool paibing_multi_mode_on_seek_result(const sle_seek_result_info_t *info);

/* 连接后 SSAP Write 备用路径（与面板 Write EB 1A... 一致） */
void paibing_sle_multi_mode_write_cbk(uint8_t server_id, uint16_t conn_id,
    ssaps_req_write_cb_t *write_cb_para, errcode_t status);

void paibing_sle_cmd_scan_start(void);

#endif
