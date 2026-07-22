#include "sle_tp_server.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "securec.h"
#include "sle_common.h"
#include "sle_connection_manager.h"
#include "sle_device_manager.h"
#include "sle_errcode.h"
#include "sle_ssap_server.h"
#include "sle_tp_server_adv.h"

#define SLE_TP_SERVER_APP_UUID0       0x55
#define SLE_TP_SERVER_APP_UUID1       0x50
#define SLE_TP_BYTE_LEN_128           128

static uint8_t g_server_id;
static uint16_t g_conn_id;
static uint16_t g_service_handle;
static sle_tp_counter_t g_rx_counter;
static uint64_t g_window_start_us;
static uint64_t g_total_start_us;
static uint8_t g_connected;

static void sle_tp_reset_window(void)
{
    g_window_start_us = sle_tp_now_us();
    if (g_total_start_us == 0) {
        g_total_start_us = g_window_start_us;
    }
}

static void sle_tp_write_request_cbk(uint8_t server_id, uint16_t conn_id,
    ssaps_req_write_cb_t *write_cb_para, errcode_t status)
{
    (void)server_id;
    (void)conn_id;
    if (status != ERRCODE_SLE_SUCCESS || write_cb_para == NULL) {
        sle_tp_counter_add_error(&g_rx_counter);
        return;
    }
    if (g_window_start_us == 0) {
        sle_tp_reset_window();
    }
    sle_tp_counter_add(&g_rx_counter, write_cb_para->length);
}

static void sle_tp_connect_state_cbk(uint16_t conn_id, const sle_addr_t *addr,
    sle_acb_state_t conn_state, sle_pair_state_t pair_state, sle_disc_reason_t disc_reason)
{
    char mac[24];
    uint8_t i;

    (void)pair_state;
    mac[0] = '\0';
    if (addr != NULL) {
        for (i = 0; i < SLE_ADDR_LEN; i++) {
            (void)snprintf(mac + i * 3, sizeof(mac) - i * 3,
                i + 1 < SLE_ADDR_LEN ? "%02x:" : "%02x", addr->addr[i]);
        }
    }

    if (conn_state == SLE_ACB_STATE_CONNECTED) {
        g_conn_id = conn_id;
        g_connected = 1;
        g_total_start_us = 0;
        g_window_start_us = 0;
        printf("%s peer connected conn_id=%u mac=%s\n", SLE_TP_LOG, conn_id, mac);
        if (pair_state == SLE_PAIR_NONE && addr != NULL) {
            sle_pair_remote_device(addr);
        }
    } else if (conn_state == SLE_ACB_STATE_DISCONNECTED) {
        printf("%s peer disconnected reason=0x%x\n", SLE_TP_LOG, disc_reason);
        g_conn_id = 0;
        g_connected = 0;
        g_total_start_us = 0;
        g_window_start_us = 0;
        (void)sle_tp_server_adv_start();
    }
    fflush(stdout);
}

static errcode_t sle_tp_add_rx_property(uint8_t server_id, uint16_t service_handle)
{
    sle_uuid_t property_uuid;
    ssaps_property_info_t property = { 0 };
    uint16_t handle = 0xffff;
    uint8_t init_value[] = { 'R', 'X' };

    sle_tp_set_uuid16(&property_uuid, SLE_TP_RX_UUID);
    property.uuid = property_uuid;
    property.permissions = SSAP_PERMISSION_READ | SSAP_PERMISSION_WRITE;
    property.operate_indication = SSAP_OPERATE_INDICATION_BIT_READ |
        SSAP_OPERATE_INDICATION_BIT_WRITE_NO_RSP;
    property.value = init_value;
    property.value_len = sizeof(init_value);
    return ssaps_add_property_sync(server_id, service_handle, &property, &handle);
}

static errcode_t sle_tp_add_service(void)
{
    sle_uuid_t service_uuid;
    sle_uuid_t app_uuid = { 0 };
    uint8_t app_raw[2] = { SLE_TP_SERVER_APP_UUID0, SLE_TP_SERVER_APP_UUID1 };
    ssap_exchange_info_t mtu = { SLE_TP_DEFAULT_MTU, 1 };
    errcode_t ret;

    app_uuid.len = sizeof(app_raw);
    if (memcpy_s(app_uuid.uuid, app_uuid.len, app_raw, sizeof(app_raw)) != EOK) {
        return ERRCODE_SLE_FAIL;
    }
    ret = ssaps_register_server(&app_uuid, &g_server_id);
    if (ret != ERRCODE_SLE_SUCCESS) {
        return ret;
    }

    sle_tp_set_uuid16(&service_uuid, SLE_TP_SERVICE_UUID);
    ret = ssaps_add_service_sync(g_server_id, &service_uuid, 1, &g_service_handle);
    if (ret != ERRCODE_SLE_SUCCESS) {
        return ret;
    }

    ret = ssaps_set_info(g_server_id, &mtu);
    if (ret != ERRCODE_SLE_SUCCESS) {
        return ret;
    }

    ret = sle_tp_add_rx_property(g_server_id, g_service_handle);
    if (ret != ERRCODE_SLE_SUCCESS) {
        return ret;
    }

    return ssaps_start_service(g_server_id, g_service_handle);
}

static errcode_t sle_tp_register_callbacks(void)
{
    ssaps_callbacks_t ssaps_cbk = { 0 };
    sle_connection_callbacks_t conn_cbk = { 0 };
    errcode_t ret;

    ssaps_cbk.write_request_cb = sle_tp_write_request_cbk;
    ret = ssaps_register_callbacks(&ssaps_cbk);
    if (ret != ERRCODE_SLE_SUCCESS) {
        return ret;
    }

    conn_cbk.connect_state_changed_cb = sle_tp_connect_state_cbk;
    return sle_connection_register_callbacks(&conn_cbk);
}

static errcode_t sle_tp_register_persistence(void)
{
    char buff[SLE_TP_BYTE_LEN_128] = { 0 };

    if (getcwd(buff, SLE_TP_BYTE_LEN_128) == NULL) {
        return ERRCODE_SLE_FAIL;
    }
    return sle_dev_manager_register_file_path(buff);
}

void sle_tp_server_start_service(void)
{
    errcode_t ret = sle_tp_add_service();

    if (ret != ERRCODE_SLE_SUCCESS) {
        printf("%s add service failed: 0x%x\n", SLE_TP_LOG, ret);
        return;
    }
    ret = sle_tp_server_adv_start();
    if (ret != ERRCODE_SLE_SUCCESS) {
        printf("%s announce failed: 0x%x\n", SLE_TP_LOG, ret);
        return;
    }
    printf("%s service 0x%04x ready, advertising as \"%s\"\n",
        SLE_TP_LOG, SLE_TP_SERVICE_UUID, SLE_TP_ADV_NAME);
    fflush(stdout);
}

void sle_tp_server_init(void)
{
    g_server_id = 0;
    g_conn_id = 0;
    g_service_handle = 0;
    g_connected = 0;
    g_window_start_us = 0;
    g_total_start_us = 0;
    sle_tp_counter_init(&g_rx_counter);

    if (sle_tp_register_callbacks() != ERRCODE_SLE_SUCCESS) {
        printf("%s register callbacks failed\n", SLE_TP_LOG);
        return;
    }
    if (sle_tp_server_adv_register_cbks() != ERRCODE_SLE_SUCCESS) {
        printf("%s register adv callbacks failed\n", SLE_TP_LOG);
        return;
    }
    if (sle_tp_register_persistence() != ERRCODE_SLE_SUCCESS) {
        printf("%s persistence register failed (ignored)\n", SLE_TP_LOG);
    }
    enable_sle();
}

void sle_tp_server_deinit(void)
{
    (void)sle_tp_server_adv_stop();
    if (g_server_id != 0) {
        ssaps_delete_all_services(g_server_id);
        usleep(10000);
    }
    disable_sle();
}

const sle_tp_counter_t *sle_tp_server_rx_counter(void)
{
    return &g_rx_counter;
}

void sle_tp_server_print_stats(uint64_t interval_us)
{
    uint64_t bytes;
    uint64_t packets;
    uint64_t errors;
    char rate[64];

    if (interval_us == 0) {
        return;
    }

    sle_tp_counter_snapshot(&g_rx_counter, &bytes, &packets, &errors);
    sle_tp_format_rate(rate, sizeof(rate), bytes, interval_us);

    printf("%s RX window: %llu bytes, %llu pkts, err=%llu, rate=%s, conn=%u\n",
        SLE_TP_LOG,
        (unsigned long long)bytes,
        (unsigned long long)packets,
        (unsigned long long)errors,
        rate,
        g_connected);
    fflush(stdout);

    pthread_mutex_lock(&g_rx_counter.lock);
    g_rx_counter.bytes = 0;
    g_rx_counter.packets = 0;
    g_rx_counter.errors = 0;
    pthread_mutex_unlock(&g_rx_counter.lock);
}
