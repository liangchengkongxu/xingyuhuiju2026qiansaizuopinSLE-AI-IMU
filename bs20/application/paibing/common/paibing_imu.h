/**
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026.
 */

#ifndef PAIBING_IMU_H
#define PAIBING_IMU_H

#include <stdint.h>
#include "paibing_transport.h"

void paibing_board_init(void);
void paibing_uart_send(const char *msg);
uint32_t paibing_uptime_ms(void);
void paibing_imu_run(const paibing_transport_t *transport);

#endif
