#ifndef SLE_TP_COMMON_H
#define SLE_TP_COMMON_H

#include <stdint.h>
#include <pthread.h>

#define SLE_TP_LOG                  "[SLE_TP]"

/* 测速对端 MAC（写死；改后需重新 deploy） */
#define SLE_TP_PEER_MAC             { 0x20, 0x25, 0x05, 0x29, 0x15, 0x30 }
#define SLE_TP_SERVICE_UUID         0x2222
#define SLE_TP_RX_UUID              0x2323

/* 链路拉满：参考 sle_uuid sample，73U acb 最大约 254 字节 */
#define SLE_TP_MAX_MTU              1500
#define SLE_TP_MAX_DATA_LEN         254
#define SLE_TP_MAX_PAYLOAD          251
#define SLE_TP_MAX_MCS              12   /* SLE_MCS_12 */
#define SLE_TP_MAX_CONN_INTV        0x04  /* 最小连接间隔 slot */
#define SLE_TP_MAX_CONN_TIMEOUT     0x1f4
#define SLE_TP_MAX_TX_POWER_DBM     20
#define SLE_TP_MAX_BLE_POWER_DBM    20

#define SLE_TP_DEFAULT_PAYLOAD      SLE_TP_MAX_PAYLOAD
#define SLE_TP_DEFAULT_MTU          SLE_TP_MAX_MTU
#define SLE_TP_DEFAULT_DATA_LEN     SLE_TP_MAX_DATA_LEN
#define SLE_TP_DEFAULT_MCS          SLE_TP_MAX_MCS
#define SLE_TP_DEFAULT_CONN_INTV    SLE_TP_MAX_CONN_INTV
#define SLE_TP_DEFAULT_TIMEOUT      SLE_TP_MAX_CONN_TIMEOUT
#define SLE_TP_CONNECT_RETRY_MS     2000

#define SLE_TP_UUID_INDEX           14
#define SLE_TP_UUID_15_BYTE         15
#define SLE_TP_UUID_16BIT_LEN       2

typedef struct {
    uint64_t bytes;
    uint64_t packets;
    uint64_t errors;
    pthread_mutex_t lock;
} sle_tp_counter_t;

void sle_tp_counter_init(sle_tp_counter_t *c);
void sle_tp_counter_add(sle_tp_counter_t *c, uint32_t len);
void sle_tp_counter_add_error(sle_tp_counter_t *c);
void sle_tp_counter_snapshot(sle_tp_counter_t *c, uint64_t *bytes, uint64_t *packets, uint64_t *errors);

uint64_t sle_tp_now_us(void);
void sle_tp_format_rate(char *buf, size_t len, uint64_t bytes, uint64_t interval_us);
int sle_tp_uuid16_match(const void *uuid, uint16_t id);
void sle_tp_set_uuid16(void *uuid_out, uint16_t id);
int sle_tp_parse_mac(const char *text, uint8_t mac_out[6]);
void sle_tp_format_mac(char *buf, size_t len, const uint8_t mac[6]);

#endif
