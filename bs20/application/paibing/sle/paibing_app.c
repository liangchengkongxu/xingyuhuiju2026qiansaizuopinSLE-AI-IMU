/**
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026.
 *
 * @brief  拍柄星闪版入口：非连接广播，ASCII 行格式与蓝牙相同。
 */

#include <stddef.h>
#include "chip_io.h"
#include "paibing_imu.h"
#include "paibing_sle_server.h"
#include "paibing_sle_server_adv.h"

static void paibing_sle_transport_init(void)
{
    paibing_sle_server_init(NULL, NULL);
    paibing_sle_announce_register_cbks();
    paibing_sle_dev_register();
    (void)paibing_sle_adv_prepare();
}

static void paibing_sle_transport_push(uint32_t uptime_ms, int32_t ax_mg, int32_t ay_mg, int32_t az_mg,
    int32_t gx_mdps, int32_t gy_mdps, float roll_deg, float pitch_deg, float magnitude)
{
    (void)paibing_sle_adv_push_sensor(uptime_ms, ax_mg, ay_mg, az_mg, gx_mdps, gy_mdps,
        roll_deg, pitch_deg, magnitude);
}

void app_main(void *unused)
{
    static const paibing_transport_t g_sle_transport = {
        .init = paibing_sle_transport_init,
        .push_sensor = paibing_sle_transport_push,
    };

    UNUSED(unused);
    paibing_board_init();
    paibing_uart_send("[paibing] transport: SLE binary sensor broadcast\r\n");
    paibing_imu_run(&g_sle_transport);
}
