#include "sle_tp_client.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "securec.h"
#include "sle_common.h"
#include "sle_connection_manager.h"
#include "sle_device_discovery.h"
#include "sle_device_manager.h"
#include "sle_errcode.h"
#include "sle_ssap_client.h"
#include "sle_transmition_manager.h"
#include "sle_tp_common.h"

#define SLE_TP_SEND_RETRY             3
#define SLE_TP_SEND_DELAY_US          0

typedef enum {
    SLE_TP_SEND_IDLE = 0,
    SLE_TP_SEND_ACTIVE,
    SLE_TP_SEND_EXIT,
} sle_tp_send_state_t;

static sle_announce_seek_callbacks_t g_seek_cbk;
static sle_connection_callbacks_t g_connect_cbk;
static ssapc_callbacks_t g_ssapc_cbk;

static sle_uuid_t g_client_app_uuid = { SLE_TP_UUID_16BIT_LEN, { 0 } };
static uint8_t g_client_id;
static uint16_t g_conn_id;
static uint16_t g_write_handle;
static volatile sle_tp_send_state_t g_send_state = SLE_TP_SEND_IDLE;
static volatile uint8_t g_connecting;
static volatile uint8_t g_connected;
static sle_addr_t g_peer_addr;
static sle_tp_counter_t g_tx_counter;
static sle_tp_client_config_t g_cfg;
static pthread_t g_send_thread;
static uint8_t g_thread_started;
static uint64_t g_total_start_us;
static volatile int8_t g_last_rssi = 127;

static void sle_tp_connect_peer(void);

static void sle_tp_apply_max_power(void)
{
    errcode_t ret = sle_customize_max_pwr(SLE_TP_MAX_BLE_POWER_DBM, SLE_TP_MAX_TX_POWER_DBM);
    printf("%s set max TX power ble=%d sle=%d dBm ret=0x%x\n",
        SLE_TP_LOG, SLE_TP_MAX_BLE_POWER_DBM, SLE_TP_MAX_TX_POWER_DBM, ret);
    fflush(stdout);
}

static void sle_tp_tune_link(uint16_t conn_id)
{
    sle_set_phy_t phy = {
        .tx_format = SLE_RADIO_FRAME_2,
        .rx_format = SLE_RADIO_FRAME_2,
        .tx_phy = SLE_PHY_4M,
        .rx_phy = SLE_PHY_4M,
        .tx_pilot_density = SLE_PHY_PILOT_DENSITY_16_TO_1,
        .rx_pilot_density = SLE_PHY_PILOT_DENSITY_16_TO_1,
        .g_feedback = 0,
        .t_feedback = 0,
    };
    errcode_t ret_mcs;
    errcode_t ret_phy;

    ret_mcs = sle_set_mcs(conn_id, SLE_TP_MAX_MCS);
    ret_phy = sle_set_phy_param(conn_id, &phy);
    (void)sle_set_data_len(conn_id, SLE_TP_MAX_DATA_LEN);
    printf("%s link tune: mcs=%u data_len=%u mtu=%u intv=0x%04x mcs_ret=0x%x phy_ret=0x%x\n",
        SLE_TP_LOG, SLE_TP_MAX_MCS, SLE_TP_MAX_DATA_LEN, SLE_TP_MAX_MTU,
        SLE_TP_MAX_CONN_INTV, ret_mcs, ret_phy);
    fflush(stdout);
}

static errcode_t sle_tp_register_persistence(void)
{
    char buff[128] = { 0 };

    if (getcwd(buff, sizeof(buff)) == NULL) {
        return ERRCODE_SLE_FAIL;
    }
    return sle_dev_manager_register_file_path((const uint8_t *)buff);
}

static void sle_tp_connect_peer(void)
{
    char mac[24];
    errcode_t ret;

    if (g_connecting || g_connected) {
        return;
    }

    sle_tp_format_mac(mac, sizeof(mac), g_peer_addr.addr);
    g_connecting = 1;
    printf("%s direct connect to %s (no scan)\n", SLE_TP_LOG, mac);
    ret = sle_connect_remote_device(&g_peer_addr);
    if (ret != ERRCODE_SLE_SUCCESS) {
        printf("%s sle_connect_remote_device failed: 0x%x\n", SLE_TP_LOG, ret);
        g_connecting = 0;
    }
    fflush(stdout);
}

static void sle_tp_enable_cbk(errcode_t status)
{
    if (status != ERRCODE_SLE_SUCCESS) {
        printf("%s enable failed: 0x%x\n", SLE_TP_LOG, status);
        return;
    }
    if (ssapc_register_client(&g_client_app_uuid, &g_client_id) != ERRCODE_SLE_SUCCESS) {
        printf("%s ssapc_register_client failed\n", SLE_TP_LOG);
        return;
    }
    sle_tp_connect_peer();
}

static void sle_tp_connect_state_cbk(uint16_t conn_id, const sle_addr_t *addr,
    sle_acb_state_t conn_state, sle_pair_state_t pair_state, sle_disc_reason_t disc_reason)
{
    char mac[24];

    if (addr != NULL) {
        sle_tp_format_mac(mac, sizeof(mac), addr->addr);
    } else {
        mac[0] = '\0';
    }

    if (conn_state == SLE_ACB_STATE_CONNECTED) {
        g_conn_id = conn_id;
        g_connected = 1;
        g_connecting = 0;
        g_total_start_us = 0;
        printf("%s connected conn_id=%u mac=%s\n", SLE_TP_LOG, conn_id, mac);
        if (pair_state == SLE_PAIR_NONE && addr != NULL) {
            sle_pair_remote_device(addr);
        }
    } else if (conn_state == SLE_ACB_STATE_DISCONNECTED) {
        printf("%s disconnected reason=0x%x, retry in %dms\n",
            SLE_TP_LOG, disc_reason, SLE_TP_CONNECT_RETRY_MS);
        g_connected = 0;
        g_connecting = 0;
        g_write_handle = 0;
        g_send_state = SLE_TP_SEND_IDLE;
        g_total_start_us = 0;
        usleep(SLE_TP_CONNECT_RETRY_MS * 1000U);
        sle_tp_connect_peer();
    }
    fflush(stdout);
}

static void sle_tp_pair_complete_cbk(uint16_t conn_id, const sle_addr_t *addr, errcode_t status)
{
    (void)addr;
    printf("%s pair complete conn_id=%u status=0x%x\n", SLE_TP_LOG, conn_id, status);
    sle_tp_tune_link(conn_id);
    fflush(stdout);
}

static void sle_tp_update_cbk(uint16_t conn_id, errcode_t status, const sle_connection_param_update_evt_t *param)
{
    ssap_exchange_info_t info = { 0 };

    (void)param;
    printf("%s conn param updated status=0x%x\n", SLE_TP_LOG, status);
    info.mtu_size = SLE_TP_MAX_MTU;
    info.version = 1;
    (void)ssapc_exchange_info_req(g_client_id, conn_id, &info);
    fflush(stdout);
}

static void sle_tp_set_phy_cbk(uint16_t conn_id, errcode_t status, const sle_set_phy_t *param)
{
    sle_connection_param_update_t par = { 0 };

    if (param != NULL) {
        printf("%s negotiated PHY conn=%u tx_phy=%u rx_phy=%u status=0x%x\n",
            SLE_TP_LOG, conn_id, param->tx_phy, param->rx_phy, status);
    }
    par.conn_id = conn_id;
    par.interval_min = SLE_TP_MAX_CONN_INTV;
    par.interval_max = SLE_TP_MAX_CONN_INTV;
    par.max_latency = 0;
    par.supervision_timeout = SLE_TP_MAX_CONN_TIMEOUT;
    sle_update_connect_param(&par);
    fflush(stdout);
}

static void sle_tp_exchange_info_cbk(uint8_t client_id, uint16_t conn_id,
    ssap_exchange_info_t *param, errcode_t status)
{
    ssapc_find_structure_param_t find_param = { 0 };

    (void)param;
    (void)status;
    sle_set_data_len(conn_id, SLE_TP_MAX_DATA_LEN);
    find_param.type = SSAP_FIND_TYPE_PRIMARY_SERVICE;
    find_param.start_hdl = 1;
    find_param.end_hdl = 0xffff;
    (void)ssapc_find_structure(client_id, conn_id, &find_param);
    fflush(stdout);
}

static void sle_tp_find_service_cbk(uint8_t client_id, uint16_t conn_id,
    ssapc_find_service_result_t *service, errcode_t status)
{
    ssapc_find_structure_param_t find_param = { 0 };

    if (service == NULL) {
        return;
    }
    if (!sle_tp_uuid16_match(&service->uuid, SLE_TP_SERVICE_UUID)) {
        return;
    }
    printf("%s found service 0x%04x hdl=0x%04x-0x%04x status=0x%x\n",
        SLE_TP_LOG, SLE_TP_SERVICE_UUID, service->start_hdl, service->end_hdl, status);
    find_param.type = SSAP_FIND_TYPE_PROPERTY;
    find_param.start_hdl = service->start_hdl;
    find_param.end_hdl = service->end_hdl;
    (void)ssapc_find_structure(client_id, conn_id, &find_param);
    fflush(stdout);
}

static void sle_tp_find_property_cbk(uint8_t client_id, uint16_t conn_id,
    ssapc_find_property_result_t *property, errcode_t status)
{
    (void)client_id;
    (void)conn_id;
    if (property == NULL) {
        return;
    }
    if (!sle_tp_uuid16_match(&property->uuid, SLE_TP_RX_UUID)) {
        return;
    }
    g_write_handle = property->handle;
    g_send_state = SLE_TP_SEND_ACTIVE;
    g_total_start_us = sle_tp_now_us();
    printf("%s found write property 0x%04x hdl=0x%04x, start TX payload=%u status=0x%x\n",
        SLE_TP_LOG, SLE_TP_RX_UUID, property->handle, g_cfg.payload_size, status);
    fflush(stdout);
}

static void sle_tp_send_once(uint8_t *payload, uint16_t payload_len)
{
    ssapc_write_param_t param = { 0 };
    uint32_t i;
    errcode_t ret;

    param.handle = g_write_handle;
    param.type = SSAP_PROPERTY_TYPE_VALUE;
    param.data_len = payload_len;
    param.data = payload;
    for (i = 0; i < SLE_TP_SEND_RETRY; i++) {
        ret = ssapc_write_cmd(g_client_id, g_conn_id, &param);
        if (ret == ERRCODE_SLE_SUCCESS) {
            sle_tp_counter_add(&g_tx_counter, payload_len);
            return;
        }
        sle_tp_counter_add_error(&g_tx_counter);
        usleep(SLE_TP_SEND_DELAY_US);
    }
}

static void *sle_tp_send_thread(void *arg)
{
    uint16_t payload_len;
    uint8_t *payload;

    (void)arg;
    payload_len = g_cfg.payload_size;
    if (payload_len == 0) {
        payload_len = SLE_TP_MAX_PAYLOAD;
    }
    if (payload_len > SLE_TP_MAX_PAYLOAD) {
        payload_len = SLE_TP_MAX_PAYLOAD;
    }

    payload = (uint8_t *)malloc(payload_len);
    if (payload == NULL) {
        printf("%s alloc payload failed\n", SLE_TP_LOG);
        return NULL;
    }
    for (uint16_t i = 0; i < payload_len; i++) {
        payload[i] = (uint8_t)(i & 0xff);
    }

    while (g_send_state != SLE_TP_SEND_EXIT) {
        if (g_send_state != SLE_TP_SEND_ACTIVE || g_write_handle == 0) {
            usleep(100000);
            continue;
        }
        sle_tp_send_once(payload, payload_len);
    }

    free(payload);
    return NULL;
}

static void sle_tp_read_rssi_cbk(uint16_t conn_id, int8_t rssi, errcode_t status)
{
    (void)conn_id;
    if (status == ERRCODE_SLE_SUCCESS) {
        g_last_rssi = rssi;
    }
}

static void sle_tp_register_callbacks(void)
{
    (void)memset_s(&g_seek_cbk, sizeof(g_seek_cbk), 0, sizeof(g_seek_cbk));
    g_seek_cbk.sle_enable_cb = sle_tp_enable_cbk;
    sle_announce_seek_register_callbacks(&g_seek_cbk);

    (void)memset_s(&g_connect_cbk, sizeof(g_connect_cbk), 0, sizeof(g_connect_cbk));
    g_connect_cbk.connect_state_changed_cb = sle_tp_connect_state_cbk;
    g_connect_cbk.pair_complete_cb = sle_tp_pair_complete_cbk;
    g_connect_cbk.connect_param_update_cb = sle_tp_update_cbk;
    g_connect_cbk.set_phy_cb = sle_tp_set_phy_cbk;
    g_connect_cbk.read_rssi_cb = sle_tp_read_rssi_cbk;
    sle_connection_register_callbacks(&g_connect_cbk);

    (void)memset_s(&g_ssapc_cbk, sizeof(g_ssapc_cbk), 0, sizeof(g_ssapc_cbk));
    g_ssapc_cbk.exchange_info_cb = sle_tp_exchange_info_cbk;
    g_ssapc_cbk.find_structure_cb = sle_tp_find_service_cbk;
    g_ssapc_cbk.ssapc_find_property_cbk = sle_tp_find_property_cbk;
    ssapc_register_callbacks(&g_ssapc_cbk);
}

void sle_tp_client_init(const sle_tp_client_config_t *cfg)
{
    static const uint8_t default_mac[] = SLE_TP_PEER_MAC;
    char mac_text[24];

    g_client_id = 0;
    g_conn_id = 0;
    g_write_handle = 0;
    g_connecting = 0;
    g_connected = 0;
    g_send_state = SLE_TP_SEND_IDLE;
    g_thread_started = 0;
    g_total_start_us = 0;
    sle_tp_counter_init(&g_tx_counter);

    g_cfg.payload_size = SLE_TP_MAX_PAYLOAD;
    g_cfg.report_interval_ms = 1000;
    (void)memcpy_s(g_cfg.peer_mac, sizeof(g_cfg.peer_mac), default_mac, sizeof(default_mac));
    if (cfg != NULL) {
        g_cfg.payload_size = cfg->payload_size ? cfg->payload_size : SLE_TP_MAX_PAYLOAD;
        g_cfg.report_interval_ms = cfg->report_interval_ms ? cfg->report_interval_ms : 1000;
        (void)memcpy_s(g_cfg.peer_mac, sizeof(g_cfg.peer_mac), cfg->peer_mac, sizeof(cfg->peer_mac));
    }

    (void)memset_s(&g_peer_addr, sizeof(g_peer_addr), 0, sizeof(g_peer_addr));
    g_peer_addr.type = 0;
    (void)memcpy_s(g_peer_addr.addr, SLE_ADDR_LEN, g_cfg.peer_mac, SLE_ADDR_LEN);

    sle_tp_format_mac(mac_text, sizeof(mac_text), g_cfg.peer_mac);
    printf("%s target MAC %s, service/char 0x%04x/0x%04x, max: mcs=%u phy=4M mtu=%u payload=%u pwr=%ddBm\n",
        SLE_TP_LOG, mac_text, SLE_TP_SERVICE_UUID, SLE_TP_RX_UUID,
        SLE_TP_MAX_MCS, SLE_TP_MAX_MTU, SLE_TP_MAX_PAYLOAD, SLE_TP_MAX_TX_POWER_DBM);

    sle_tp_register_callbacks();
    sle_tp_apply_max_power();
    if (sle_tp_register_persistence() != ERRCODE_SLE_SUCCESS) {
        printf("%s persistence register failed (ignored)\n", SLE_TP_LOG);
    }
    if (pthread_create(&g_send_thread, NULL, sle_tp_send_thread, NULL) == 0) {
        g_thread_started = 1;
    } else {
        printf("%s failed to create send thread\n", SLE_TP_LOG);
    }
    enable_sle();
}

void sle_tp_client_deinit(void)
{
    g_send_state = SLE_TP_SEND_EXIT;
    if (g_thread_started) {
        pthread_join(g_send_thread, NULL);
    }
    if (g_client_id != 0) {
        (void)ssapc_unregister_client(g_client_id);
    }
    disable_sle();
}

void sle_tp_client_print_stats(uint64_t interval_us)
{
    uint64_t bytes;
    uint64_t packets;
    uint64_t errors;
    char rate[64];

    if (interval_us == 0) {
        return;
    }

    if (g_connected && g_conn_id != 0) {
        (void)sle_read_remote_device_rssi(g_conn_id);
    }
    sle_tp_counter_snapshot(&g_tx_counter, &bytes, &packets, &errors);
    sle_tp_format_rate(rate, sizeof(rate), bytes, interval_us);
    printf("%s TX window: %llu bytes, %llu pkts, err=%llu, rate=%s, payload=%u, conn=%u, rssi=%d\n",
        SLE_TP_LOG,
        (unsigned long long)bytes,
        (unsigned long long)packets,
        (unsigned long long)errors,
        rate,
        g_cfg.payload_size,
        g_connected,
        (int)g_last_rssi);
    fflush(stdout);

    pthread_mutex_lock(&g_tx_counter.lock);
    g_tx_counter.bytes = 0;
    g_tx_counter.packets = 0;
    g_tx_counter.errors = 0;
    pthread_mutex_unlock(&g_tx_counter.lock);
}
