/*
 * 拍柄星闪广播 IMU（厂商 AD type=0xFF）
 *
 * 星闪当前固件（2026-07）：
 *   - ADV **与** Scan Response 均携带 ASCII @ 行（与 BLE Notify 相同格式）
 *   - 主控按 MAC + @uptime_ms 去重，有效采样约 10Hz
 *
 * 旧固件回退：
 *   - 22 字节二进制 EB 1A 02（小端）
 */
#ifndef SLE_IMU_ADV_H
#define SLE_IMU_ADV_H

#include <stdint.h>

#define SLE_IMU_COMPANY_ID0       0xEB
#define SLE_IMU_COMPANY_ID1       0x1A
#define SLE_IMU_PROTO_SENSOR      0x02
#define SLE_IMU_ADV_MIN_LEN       22

#endif
