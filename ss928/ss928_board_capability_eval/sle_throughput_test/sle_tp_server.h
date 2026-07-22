#ifndef SLE_TP_SERVER_H
#define SLE_TP_SERVER_H

#include "sle_tp_common.h"

void sle_tp_server_init(void);
void sle_tp_server_deinit(void);
void sle_tp_server_start_service(void);
void sle_tp_server_print_stats(uint64_t interval_us);
const sle_tp_counter_t *sle_tp_server_rx_counter(void);

#endif
