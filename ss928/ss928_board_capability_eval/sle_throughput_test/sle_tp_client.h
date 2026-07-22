#ifndef SLE_TP_CLIENT_H
#define SLE_TP_CLIENT_H

#include <stdint.h>

typedef struct {
    uint16_t payload_size;
    uint32_t report_interval_ms;
    uint8_t peer_mac[6];
} sle_tp_client_config_t;

void sle_tp_client_init(const sle_tp_client_config_t *cfg);
void sle_tp_client_deinit(void);
void sle_tp_client_print_stats(uint64_t interval_us);

#endif
