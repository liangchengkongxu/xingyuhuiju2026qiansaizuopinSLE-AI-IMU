/**
 * 发送面板测量广播指令（短时 announce）
 */
#include "sle_announce_cmd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "securec.h"
#include "sle_common.h"
#include "sle_device_discovery.h"
#include "sle_errcode.h"

#define SLE_CMD_LOG           "[SLE_CMD]"
#define SLE_ADV_HANDLE        1
#define SLE_ADV_DATA_LEN_MAX  251
#define SLE_ADV_INTERVAL      0xC8
#define SLE_ADV_TX_POWER      6

#define SLE_ADV_DATA_TYPE_DISCOVERY_LEVEL  0x01
#define SLE_ADV_DATA_TYPE_ACCESS_MODE      0x02
#define SLE_ADV_DATA_TYPE_MANUFACTURER     0xFF

struct sle_adv_common_value {
    uint8_t length;
    uint8_t type;
    uint8_t value;
};

static sle_announce_seek_callbacks_t g_cbk;
static volatile uint8_t g_cmd = SLE_PANEL_CMD_START_MEASURE;
static volatile uint8_t g_dev_id = SLE_PANEL_DEV_ID_ALL;
static volatile uint16_t g_session = 0;
static volatile int g_announce_running = 0;

static uint16_t append_manufacturer_cmd(uint8_t *adv, uint16_t max_len, uint8_t cmd, uint8_t dev_id, uint16_t session)
{
    uint8_t payload[8];
    uint16_t idx = 0;

    if (max_len < 12) {
        return 0;
    }
    payload[0] = SLE_PANEL_COMPANY_ID0;
    payload[1] = SLE_PANEL_COMPANY_ID1;
    payload[2] = SLE_PANEL_PROTO_VER;
    payload[3] = cmd;
    payload[4] = dev_id;
    payload[5] = (uint8_t)(session & 0xFF);
    payload[6] = (uint8_t)((session >> 8) & 0xFF);

    adv[idx++] = (uint8_t)(1 + sizeof(payload));
    adv[idx++] = SLE_ADV_DATA_TYPE_MANUFACTURER;
    if (memcpy_s(&adv[idx], max_len - idx, payload, sizeof(payload)) != EOK) {
        return 0;
    }
    idx = (uint16_t)(idx + sizeof(payload));
    return idx;
}

static uint16_t build_announce_data(uint8_t *adv, uint16_t max_len, uint8_t cmd, uint8_t dev_id, uint16_t session)
{
    uint16_t idx = 0;
    struct sle_adv_common_value disc = {
        .length = 1,
        .type = SLE_ADV_DATA_TYPE_DISCOVERY_LEVEL,
        .value = SLE_ANNOUNCE_LEVEL_NORMAL,
    };
    struct sle_adv_common_value access = {
        .length = 1,
        .type = SLE_ADV_DATA_TYPE_ACCESS_MODE,
        .value = 0,
    };

    if (memcpy_s(&adv[idx], max_len - idx, &disc, sizeof(disc)) != EOK) {
        return 0;
    }
    idx += (uint16_t)sizeof(disc);
    if (memcpy_s(&adv[idx], max_len - idx, &access, sizeof(access)) != EOK) {
        return 0;
    }
    idx += (uint16_t)sizeof(access);
    idx += append_manufacturer_cmd(&adv[idx], (uint16_t)(max_len - idx), cmd, dev_id, session);
    return idx;
}

static errcode_t setup_announce(uint8_t cmd, uint8_t dev_id, uint16_t session)
{
    sle_announce_param_t param = { 0 };
    sle_announce_data_t data = { 0 };
    uint8_t announce_data[SLE_ADV_DATA_LEN_MAX];
    uint8_t local_addr[SLE_ADDR_LEN] = { 0xEE, 0xEF, 0x33, 0x9B, 0x8D, 0x5C };
    uint16_t adv_len;
    errcode_t ret;

    adv_len = build_announce_data(announce_data, sizeof(announce_data), cmd, dev_id, session);
    if (adv_len == 0) {
        return ERRCODE_SLE_FAIL;
    }

    param.announce_mode = SLE_ANNOUNCE_MODE_NONCONN_NONSCAN;
    param.announce_handle = SLE_ADV_HANDLE;
    param.announce_gt_role = SLE_ANNOUNCE_ROLE_T_NO_NEGO;
    param.announce_level = SLE_ANNOUNCE_LEVEL_NORMAL;
    param.announce_channel_map = 0x07;
    param.announce_interval_min = SLE_ADV_INTERVAL;
    param.announce_interval_max = SLE_ADV_INTERVAL;
    param.announce_tx_power = SLE_ADV_TX_POWER;
    param.own_addr.type = SLE_ADDRESS_TYPE_PUBLIC;
    (void)memcpy_s(param.own_addr.addr, SLE_ADDR_LEN, local_addr, SLE_ADDR_LEN);

    ret = sle_set_announce_param(SLE_ADV_HANDLE, &param);
    if (ret != ERRCODE_SLE_SUCCESS) {
        printf("%s set_announce_param fail 0x%x\n", SLE_CMD_LOG, ret);
        return ret;
    }

    data.announce_data = announce_data;
    data.announce_data_len = adv_len;
    data.seek_rsp_data = NULL;
    data.seek_rsp_data_len = 0;
    ret = sle_set_announce_data(SLE_ADV_HANDLE, &data);
    if (ret != ERRCODE_SLE_SUCCESS) {
        printf("%s set_announce_data fail 0x%x\n", SLE_CMD_LOG, ret);
        return ret;
    }

    printf("%s AD=%u bytes:", SLE_CMD_LOG, adv_len);
    for (uint16_t i = 0; i < adv_len; i++) {
        printf(" %02x", announce_data[i]);
    }
    printf("\n");
    fflush(stdout);
    return ERRCODE_SLE_SUCCESS;
}

static void sle_enable_cbk(errcode_t status)
{
    errcode_t ret;

    if (status != ERRCODE_SLE_SUCCESS) {
        printf("%s enable fail 0x%x\n", SLE_CMD_LOG, status);
        fflush(stdout);
        return;
    }
    ret = setup_announce(g_cmd, g_dev_id, g_session);
    if (ret != ERRCODE_SLE_SUCCESS) {
        return;
    }
    ret = sle_start_announce(SLE_ADV_HANDLE);
    printf("%s sle_start_announce cmd=0x%02x dev=%u session=%u ret=0x%x\n",
        SLE_CMD_LOG, g_cmd, g_dev_id, g_session, ret);
    fflush(stdout);
    if (ret == ERRCODE_SLE_SUCCESS) {
        g_announce_running = 1;
    }
}

static void register_callbacks(void)
{
    (void)memset_s(&g_cbk, sizeof(g_cbk), 0, sizeof(g_cbk));
    g_cbk.sle_enable_cb = sle_enable_cbk;
    sle_announce_seek_register_callbacks(&g_cbk);
}

int sle_announce_cmd_run(const char *verb, uint8_t device_id, int duration_ms)
{
    int waited;
    int i;

    if (verb == NULL) {
        return 1;
    }
    if (strcmp(verb, "start") == 0) {
        g_cmd = SLE_PANEL_CMD_START_MEASURE;
    } else if (strcmp(verb, "stop") == 0) {
        g_cmd = SLE_PANEL_CMD_STOP_MEASURE;
    } else {
        return 1;
    }

    g_dev_id = device_id;
    g_session = (uint16_t)(rand() & 0xFFFF);
    if (duration_ms < 500) {
        duration_ms = 500;
    }
    if (duration_ms > 5000) {
        duration_ms = 5000;
    }

    register_callbacks();
    if (enable_sle() != ERRCODE_SLE_SUCCESS) {
        printf("%s enable_sle failed\n", SLE_CMD_LOG);
        return 2;
    }

    for (waited = 0; waited < 3000 && !g_announce_running; waited += 50) {
        usleep(50000);
    }
    if (!g_announce_running) {
        printf("%s announce did not start in time\n", SLE_CMD_LOG);
        disable_sle();
        return 3;
    }

    for (i = 0; i < duration_ms; i += 100) {
        usleep(100000);
    }

    (void)sle_stop_announce(SLE_ADV_HANDLE);
    usleep(200000);
    disable_sle();
    printf("%s done cmd=0x%02x dev=%u session=%u\n", SLE_CMD_LOG, g_cmd, g_dev_id, g_session);
    fflush(stdout);
    return 0;
}
