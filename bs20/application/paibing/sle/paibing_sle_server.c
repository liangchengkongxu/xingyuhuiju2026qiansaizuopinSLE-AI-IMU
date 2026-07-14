/**
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026.
 *
 * Description: paibing 星闪(SLE)服务端 —— 基于 sle_uart_server 定制.
 */

#include "common_def.h"
#include "securec.h"
#include "soc_osal.h"
#include "sle_errcode.h"
#include "sle_device_manager.h"
#include "sle_connection_manager.h"
#include "sle_transmition_manager.h"
#include "sle_device_discovery.h"
#include "paibing_sle_server.h"
#include "paibing_sle_server_adv.h"
#include "mac_config.h"
#if !PAIBING_SLE_SENSOR_BROADCAST
#include "paibing_multi_mode.h"
#endif

#define UUID_LEN_2              2
#define UUID_INDEX              14
#define OCTET_BIT_LEN           8
#define BT_INDEX_0              0
#define BT_INDEX_4              4

#define SLE_UUID_SERVER_SERVICE   0x2222
#define SLE_UUID_SERVER_NTF_REPORT 0x2323
#define SLE_UUID_TEST_PROPERTIES  (SSAP_PERMISSION_READ | SSAP_PERMISSION_WRITE)
#define SLE_UUID_TEST_OPERATION_INDICATION  (SSAP_OPERATE_INDICATION_BIT_READ | \
    SSAP_OPERATE_INDICATION_BIT_WRITE | SSAP_OPERATE_INDICATION_BIT_NOTIFY)
#define SLE_UUID_TEST_DESCRIPTOR   (SSAP_PERMISSION_READ | SSAP_PERMISSION_WRITE)

static char g_sle_uuid_app_uuid[UUID_LEN_2] = { 0x12, 0x34 };
static char g_sle_property_value[OCTET_BIT_LEN] = { 0 };

static uint16_t g_sle_conn_id;
static uint8_t g_sle_server_id;
static uint16_t g_sle_service_handle;
static uint16_t g_sle_property_handle;
static uint16_t g_sle_pair_hdl;
static sle_link_qos_state_t g_sle_link_state = SLE_QOS_IDLE;
static ssaps_write_request_callback g_paibing_user_write_cb = NULL;

static uint8_t g_sle_uuid_base[] = {
    0x37, 0xBE, 0xA8, 0x80, 0xFC, 0x70, 0x11, 0xEA,
    0xB7, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

#define PAIBING_SLE_LOG "[paibing sle]"

/*
 * BS20 芯片 GFSK 实际最大约 6dBm；高功率 NV/API 可至 ~8dBm（见《功率配置说明书》）。
 * announce_tx_power=20 只是参数上限，未 customize 时会被钳在默认 6dBm。
 */
#define PAIBING_SLE_GFSK_MAX_DBM  8
#define PAIBING_SLE_PSK_MAX_DBM   2

/* -------------------------------------------------------------------------- */
/* UUID 工具                                                                  */
/* -------------------------------------------------------------------------- */

static void sle_uuid_set_base(sle_uuid_t *out)
{
    if (memcpy_s(out->uuid, SLE_UUID_LEN, g_sle_uuid_base, SLE_UUID_LEN) != EOK) {
        out->len = 0;
        return;
    }
    out->len = UUID_LEN_2;
}

static void sle_uuid_setu2(uint16_t u2, sle_uuid_t *out)
{
    sle_uuid_set_base(out);
    out->len = UUID_LEN_2;
    out->uuid[UUID_INDEX] = (uint8_t)u2;
    out->uuid[UUID_INDEX + 1] = (uint8_t)(u2 >> 8);
}

/* -------------------------------------------------------------------------- */
/* SSAPS 回调                                                                   */
/* -------------------------------------------------------------------------- */

static void ssaps_mtu_changed_cbk(uint8_t server_id, uint16_t conn_id,
    ssap_exchange_info_t *mtu_size, errcode_t status)
{
    osal_printk("%s mtu changed server_id:0x%x conn_id:0x%x mtu:0x%x status:0x%x\r\n",
        PAIBING_SLE_LOG, server_id, conn_id, mtu_size->mtu_size, status);
}

static void ssaps_start_service_cbk(uint8_t server_id, uint16_t handle, errcode_t status)
{
    osal_printk("%s start service server_id:%d handle:0x%x status:0x%x\r\n",
        PAIBING_SLE_LOG, server_id, handle, status);
}

static void ssaps_add_service_cbk(uint8_t server_id, sle_uuid_t *uuid, uint16_t handle, errcode_t status)
{
    unused(uuid);
    osal_printk("%s add service server_id:0x%x handle:0x%x status:0x%x\r\n",
        PAIBING_SLE_LOG, server_id, handle, status);
}

static void ssaps_add_property_cbk(uint8_t server_id, sle_uuid_t *uuid,
    uint16_t service_handle, uint16_t handle, errcode_t status)
{
    unused(uuid);
    osal_printk("%s add property server_id:0x%x svc:0x%x handle:0x%x status:0x%x\r\n",
        PAIBING_SLE_LOG, server_id, service_handle, handle, status);
}

static void ssaps_add_descriptor_cbk(uint8_t server_id, sle_uuid_t *uuid,
    uint16_t service_handle, uint16_t property_handle, errcode_t status)
{
    unused(uuid);
    osal_printk("%s add desc server_id:0x%x svc:0x%x prop:0x%x status:0x%x\r\n",
        PAIBING_SLE_LOG, server_id, service_handle, property_handle, status);
}

static void ssaps_delete_all_service_cbk(uint8_t server_id, errcode_t status)
{
    osal_printk("%s delete all service server_id:0x%x status:0x%x\r\n",
        PAIBING_SLE_LOG, server_id, status);
}

/* -------------------------------------------------------------------------- */
/* 连接 & 传输回调                                                              */
/* -------------------------------------------------------------------------- */

static void sle_connect_state_changed_cbk(uint16_t conn_id, const sle_addr_t *addr,
    sle_acb_state_t conn_state, sle_pair_state_t pair_state, sle_disc_reason_t disc_reason)
{
    osal_printk("%s conn_state_chg conn_id:0x%x state:%d pair:%d disc:%d\r\n",
        PAIBING_SLE_LOG, conn_id, conn_state, pair_state, disc_reason);
    osal_printk("%s addr:%02x:**:**:**:%02x:%02x\r\n",
        PAIBING_SLE_LOG, addr->addr[BT_INDEX_0], addr->addr[BT_INDEX_4]);

    if (conn_state == SLE_ACB_STATE_CONNECTED) {
        g_sle_conn_id = conn_id;
        g_sle_link_state = SLE_QOS_IDLE;
    }
    if (conn_state == SLE_ACB_STATE_DISCONNECTED) {
        g_sle_conn_id = 0;
        g_sle_pair_hdl = 0;
        g_sle_link_state = SLE_QOS_IDLE;
#if !PAIBING_SLE_SENSOR_BROADCAST
        (void)paibing_sle_adv_restart();
#endif
    }
}

static void sle_pair_complete_cbk(uint16_t conn_id, const sle_addr_t *addr, errcode_t status)
{
    osal_printk("%s pair complete conn_id:0x%x status:0x%x\r\n",
        PAIBING_SLE_LOG, conn_id, status);
    osal_printk("%s pair addr:%02x:**:**:**:%02x:%02x\r\n",
        PAIBING_SLE_LOG, addr->addr[BT_INDEX_0], addr->addr[BT_INDEX_4]);
    if (status == ERRCODE_SLE_SUCCESS) {
        g_sle_pair_hdl = conn_id + 1;
    }
}

#if !PAIBING_SLE_SENSOR_BROADCAST
static void paibing_ssaps_write_request_cbk(uint8_t server_id, uint16_t conn_id,
    ssaps_req_write_cb_t *write_cb_para, errcode_t status)
{
    paibing_sle_multi_mode_write_cbk(server_id, conn_id, write_cb_para, status);
    if (g_paibing_user_write_cb != NULL) {
        g_paibing_user_write_cb(server_id, conn_id, write_cb_para, status);
    }
}
#endif

static void sle_tm_send_data_busy_cbk(uint16_t conn_id, sle_link_qos_state_t link_state)
{
    osal_printk("%s send busy conn_id:%u state:%u\r\n", PAIBING_SLE_LOG, conn_id, link_state);
    g_sle_link_state = link_state;
}

/* -------------------------------------------------------------------------- */
/* 服务添加                                                                    */
/* -------------------------------------------------------------------------- */

static errcode_t sle_uuid_server_service_add(void)
{
    sle_uuid_t service_uuid = {0};
    sle_uuid_setu2(SLE_UUID_SERVER_SERVICE, &service_uuid);
    errcode_t ret = ssaps_add_service_sync(g_sle_server_id, &service_uuid, 1, &g_sle_service_handle);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s add service fail ret:0x%x\r\n", PAIBING_SLE_LOG, ret);
        return ERRCODE_SLE_FAIL;
    }
    return ERRCODE_SLE_SUCCESS;
}

static errcode_t sle_uuid_server_property_add(void)
{
    ssaps_property_info_t property = {0};
    ssaps_desc_info_t descriptor = {0};
    uint8_t ntf_value[] = { 0x01, 0x00 };

    property.permissions = SLE_UUID_TEST_PROPERTIES;
    property.operate_indication = SLE_UUID_TEST_OPERATION_INDICATION;
    sle_uuid_setu2(SLE_UUID_SERVER_NTF_REPORT, &property.uuid);
    property.value = (uint8_t *)osal_vmalloc(sizeof(g_sle_property_value));
    if (property.value == NULL) return ERRCODE_SLE_FAIL;
    if (memcpy_s(property.value, sizeof(g_sle_property_value),
        g_sle_property_value, sizeof(g_sle_property_value)) != EOK) {
        osal_vfree(property.value);
        return ERRCODE_SLE_FAIL;
    }
    errcode_t ret = ssaps_add_property_sync(g_sle_server_id, g_sle_service_handle, &property, &g_sle_property_handle);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s add property fail ret:0x%x\r\n", PAIBING_SLE_LOG, ret);
        osal_vfree(property.value);
        return ERRCODE_SLE_FAIL;
    }

    descriptor.permissions = SLE_UUID_TEST_DESCRIPTOR;
    descriptor.type = SSAP_DESCRIPTOR_CLIENT_CONFIGURATION;
    descriptor.operate_indication = SSAP_OPERATE_INDICATION_BIT_READ |
        SSAP_OPERATE_INDICATION_BIT_WRITE | SSAP_OPERATE_INDICATION_BIT_DESCRIPTOR_CLIENT_CONFIGURATION_WRITE;
    descriptor.value = (uint8_t *)osal_vmalloc(sizeof(ntf_value));
    descriptor.value_len = sizeof(ntf_value);
    if (descriptor.value == NULL) {
        osal_vfree(property.value);
        return ERRCODE_SLE_FAIL;
    }
    if (memcpy_s(descriptor.value, sizeof(ntf_value), ntf_value, sizeof(ntf_value)) != EOK) {
        osal_vfree(property.value);
        osal_vfree(descriptor.value);
        return ERRCODE_SLE_FAIL;
    }
    ret = ssaps_add_descriptor_sync(g_sle_server_id, g_sle_service_handle, g_sle_property_handle, &descriptor);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s add descriptor fail ret:0x%x\r\n", PAIBING_SLE_LOG, ret);
        osal_vfree(property.value);
        osal_vfree(descriptor.value);
        return ERRCODE_SLE_FAIL;
    }
    osal_vfree(property.value);
    osal_vfree(descriptor.value);
    return ERRCODE_SLE_SUCCESS;
}

static errcode_t sle_uart_server_add(void)
{
    sle_uuid_t app_uuid = {0};
    app_uuid.len = sizeof(g_sle_uuid_app_uuid);
    if (memcpy_s(app_uuid.uuid, app_uuid.len, g_sle_uuid_app_uuid, sizeof(g_sle_uuid_app_uuid)) != EOK) {
        return ERRCODE_SLE_FAIL;
    }
    ssaps_register_server(&app_uuid, &g_sle_server_id);

    if (sle_uuid_server_service_add() != ERRCODE_SLE_SUCCESS) {
        ssaps_unregister_server(g_sle_server_id);
        return ERRCODE_SLE_FAIL;
    }
    if (sle_uuid_server_property_add() != ERRCODE_SLE_SUCCESS) {
        ssaps_unregister_server(g_sle_server_id);
        return ERRCODE_SLE_FAIL;
    }
    osal_printk("%s add service ok server_id:0x%x svc:0x%x prop:0x%x\r\n",
        PAIBING_SLE_LOG, g_sle_server_id, g_sle_service_handle, g_sle_property_handle);
    errcode_t ret = ssaps_start_service(g_sle_server_id, g_sle_service_handle);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s start service fail ret:0x%x\r\n", PAIBING_SLE_LOG, ret);
        return ERRCODE_SLE_FAIL;
    }
    return ERRCODE_SLE_SUCCESS;
}

/* -------------------------------------------------------------------------- */
/* 公开接口                                                                    */
/* -------------------------------------------------------------------------- */

static uint8_t g_sle_send_buf[96];

errcode_t paibing_sle_send_report(const uint8_t *data, uint8_t len)
{
    ssaps_ntf_ind_t param = {0};

    if ((data == NULL) || (len == 0) || (len > sizeof(g_sle_send_buf))) {
        return ERRCODE_SLE_FAIL;
    }

    param.handle = g_sle_property_handle;
    param.type = SSAP_PROPERTY_TYPE_VALUE;
    param.value = g_sle_send_buf;
    param.value_len = len;
    if (memcpy_s(g_sle_send_buf, sizeof(g_sle_send_buf), data, len) != EOK) {
        return ERRCODE_SLE_FAIL;
    }

    if (g_sle_link_state == SLE_QOS_FLOWCTRL) {
        osal_msleep(35);
    } else if (g_sle_link_state == SLE_QOS_BUSY) {
        return ERRCODE_SLE_BUSY;
    }
    return ssaps_notify_indicate(g_sle_server_id, g_sle_conn_id, &param);
}

uint16_t paibing_sle_is_connected(void)
{
    return (g_sle_conn_id != 0) ? 1 : 0;
}

/* 使能 SLE 后，由回调触发添加服务 */
static errcode_t paibing_sle_add_service(void)
{
    return sle_uart_server_add();
}

errcode_t paibing_sle_server_init(ssaps_read_request_callback read_cb,
    ssaps_write_request_callback write_cb)
{
    g_paibing_user_write_cb = write_cb;

    /* 传输回调 */
    sle_transmission_callbacks_t tm_cbks = {0};
    tm_cbks.send_data_cb = sle_tm_send_data_busy_cbk;
    errcode_t ret = sle_transmission_register_callbacks(&tm_cbks);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s tm register fail:0x%x\r\n", PAIBING_SLE_LOG, ret);
        return ret;
    }

    /* 连接回调 */
    sle_connection_callbacks_t conn_cbks = {0};
    conn_cbks.connect_state_changed_cb = sle_connect_state_changed_cbk;
    conn_cbks.pair_complete_cb = sle_pair_complete_cbk;
    ret = sle_connection_register_callbacks(&conn_cbks);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s conn register fail:0x%x\r\n", PAIBING_SLE_LOG, ret);
        return ret;
    }

    /* SSAPS 回调 */
    ssaps_callbacks_t ssaps_cbk = {0};
    ssaps_cbk.add_service_cb = ssaps_add_service_cbk;
    ssaps_cbk.add_property_cb = ssaps_add_property_cbk;
    ssaps_cbk.add_descriptor_cb = ssaps_add_descriptor_cbk;
    ssaps_cbk.start_service_cb = ssaps_start_service_cbk;
    ssaps_cbk.delete_all_service_cb = ssaps_delete_all_service_cbk;
    ssaps_cbk.mtu_changed_cb = ssaps_mtu_changed_cbk;
    ssaps_cbk.read_request_cb = read_cb;
#if !PAIBING_SLE_SENSOR_BROADCAST
    ssaps_cbk.write_request_cb = paibing_ssaps_write_request_cbk;
#else
    ssaps_cbk.write_request_cb = write_cb;
#endif
    ret = ssaps_register_callbacks(&ssaps_cbk);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s ssaps register fail:0x%x\r\n", PAIBING_SLE_LOG, ret);
        return ret;
    }

    (void)paibing_sle_set_local_addr();

    osal_printk("%s init ok\r\n", PAIBING_SLE_LOG);
    return ERRCODE_SLE_SUCCESS;
}

/* 设备上电时触发 */
static void paibing_sle_power_on_cb(uint8_t status)
{
    osal_printk("%s power on: %d\r\n", PAIBING_SLE_LOG, status);
    (void)enable_sle();
}

/* SLE 协议栈使能后触发 —— 添加服务 */
static void paibing_sle_enable_cb(uint8_t status)
{
    osal_printk("%s enable: %d\r\n", PAIBING_SLE_LOG, status);
    (void)paibing_sle_add_service();
}

errcode_t paibing_sle_dev_register(void)
{
    errcode_t pwr_ret = sle_customize_max_pwr(PAIBING_SLE_GFSK_MAX_DBM, PAIBING_SLE_PSK_MAX_DBM);
    if (pwr_ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s customize max pwr fail:0x%x\r\n", PAIBING_SLE_LOG, pwr_ret);
    } else {
        osal_printk("%s gfsk max pwr=%ddBm\r\n", PAIBING_SLE_LOG, PAIBING_SLE_GFSK_MAX_DBM);
    }

    sle_dev_manager_callbacks_t dev_cbks = {0};
    dev_cbks.sle_power_on_cb = paibing_sle_power_on_cb;
    dev_cbks.sle_enable_cb = paibing_sle_enable_cb;
    errcode_t ret = sle_dev_manager_register_callbacks(&dev_cbks);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s dev register fail:0x%x\r\n", PAIBING_SLE_LOG, ret);
        return ret;
    }
#if (CORE_NUMS < 2)
    (void)enable_sle();
#endif
    return ERRCODE_SLE_SUCCESS;
}
