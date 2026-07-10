/*
 * 面板 -> 拍柄：星闪广播测量指令（厂商自定义数据 0xFF）
 *
 * 广播 AD 中 Manufacturer Specific 载荷（type=0xFF 之后的字节）：
 *   EB 1A        厂商 ID（ebaina）
 *   01           协议版本
 *   CMD          0xA1=开始测量  0xA2=停止测量
 *   DEV_ID       0x00=所有拍柄  0x01~0xFE=设备号（与多人扫描映射一致）
 *   SEQ_L SEQ_H  会话序号（小端）
 */
#ifndef SLE_ANNOUNCE_CMD_H
#define SLE_ANNOUNCE_CMD_H

#include <stdint.h>

#define SLE_PANEL_COMPANY_ID0       0xEB
#define SLE_PANEL_COMPANY_ID1       0x1A
#define SLE_PANEL_PROTO_VER         0x01
#define SLE_PANEL_CMD_START_MEASURE 0xA1
#define SLE_PANEL_CMD_STOP_MEASURE  0xA2
#define SLE_PANEL_DEV_ID_ALL        0x00

int sle_announce_cmd_run(const char *verb, uint8_t device_id, int duration_ms);

#endif
