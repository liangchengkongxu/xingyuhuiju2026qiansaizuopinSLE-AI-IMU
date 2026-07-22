#include "sle_tp_common.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "sle_ssap_stru.h"

void sle_tp_counter_init(sle_tp_counter_t *c)
{
    if (c == NULL) {
        return;
    }
    c->bytes = 0;
    c->packets = 0;
    c->errors = 0;
    pthread_mutex_init(&c->lock, NULL);
}

void sle_tp_counter_add(sle_tp_counter_t *c, uint32_t len)
{
    if (c == NULL) {
        return;
    }
    pthread_mutex_lock(&c->lock);
    c->bytes += len;
    c->packets++;
    pthread_mutex_unlock(&c->lock);
}

void sle_tp_counter_add_error(sle_tp_counter_t *c)
{
    if (c == NULL) {
        return;
    }
    pthread_mutex_lock(&c->lock);
    c->errors++;
    pthread_mutex_unlock(&c->lock);
}

void sle_tp_counter_snapshot(sle_tp_counter_t *c, uint64_t *bytes, uint64_t *packets, uint64_t *errors)
{
    if (c == NULL) {
        return;
    }
    pthread_mutex_lock(&c->lock);
    if (bytes != NULL) {
        *bytes = c->bytes;
    }
    if (packets != NULL) {
        *packets = c->packets;
    }
    if (errors != NULL) {
        *errors = c->errors;
    }
    pthread_mutex_unlock(&c->lock);
}

uint64_t sle_tp_now_us(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

void sle_tp_format_rate(char *buf, size_t len, uint64_t bytes, uint64_t interval_us)
{
    double kbps;
    double mbps;

    if (buf == NULL || len == 0) {
        return;
    }
    if (interval_us == 0) {
        snprintf(buf, len, "0.00 Kbps (0.00 Mbps)");
        return;
    }
    kbps = (double)bytes * 8.0 * 1000000.0 / (double)interval_us / 1000.0;
    mbps = kbps / 1000.0;
    snprintf(buf, len, "%.2f Kbps (%.3f Mbps)", kbps, mbps);
}

int sle_tp_uuid16_match(const void *uuid, uint16_t id)
{
    const sle_uuid_t *u = (const sle_uuid_t *)uuid;

    if (u == NULL) {
        return 0;
    }
    return u->uuid[SLE_TP_UUID_INDEX] == (uint8_t)(id & 0xff) &&
        u->uuid[SLE_TP_UUID_15_BYTE] == (uint8_t)((id >> 8) & 0xff);
}

void sle_tp_set_uuid16(void *uuid_out, uint16_t id)
{
    sle_uuid_t *u = (sle_uuid_t *)uuid_out;

    if (u == NULL) {
        return;
    }
    memset(u, 0, sizeof(*u));
    u->len = SLE_TP_UUID_16BIT_LEN;
    u->uuid[SLE_TP_UUID_INDEX] = (uint8_t)(id & 0xff);
    u->uuid[SLE_TP_UUID_15_BYTE] = (uint8_t)((id >> 8) & 0xff);
}

int sle_tp_parse_mac(const char *text, uint8_t mac_out[6])
{
    unsigned int b[6];
    int n;

    if (text == NULL || mac_out == NULL) {
        return 0;
    }
    n = sscanf(text, "%x:%x:%x:%x:%x:%x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]);
    if (n != 6) {
        return 0;
    }
    for (int i = 0; i < 6; i++) {
        if (b[i] > 0xff) {
            return 0;
        }
        mac_out[i] = (uint8_t)b[i];
    }
    return 1;
}

void sle_tp_format_mac(char *buf, size_t len, const uint8_t mac[6])
{
    if (buf == NULL || len == 0 || mac == NULL) {
        return;
    }
    snprintf(buf, len, "%02x:%02x:%02x:%02x:%02x:%02x",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}
