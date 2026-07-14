/**
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026.
 *
 * Description: paibing 星闪(SLE) UART server 接口.
 */

#ifndef PAIBING_SLE_SERVER_H
#define PAIBING_SLE_SERVER_H

#include <stdint.h>
#include "sle_ssap_server.h"
#include "errcode.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif

/* 初始化星闪服务（注册回调、添加 service/property） */
errcode_t paibing_sle_server_init(ssaps_read_request_callback read_cb,
    ssaps_write_request_callback write_cb);

/* 通过 handle 发送数据到已连接的对端 */
errcode_t paibing_sle_send_report(const uint8_t *data, uint8_t len);

/* 返回非零表示已配对连接 */
uint16_t paibing_sle_is_connected(void);

/* 注册设备管理回调并上电 */
errcode_t paibing_sle_dev_register(void);

/* 设置广播参数并开始广播 */
errcode_t paibing_sle_adv_start(void);

/* 断线后重新广播 */
errcode_t paibing_sle_adv_restart(void);

/* 设置星闪 MAC */
errcode_t paibing_sle_set_local_addr(void);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif

#endif
