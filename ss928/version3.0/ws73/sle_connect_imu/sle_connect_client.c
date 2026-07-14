/**
 * 连接 paibing_imu：目标 MAC CC:92:43:00:A1:00，订阅 0x2222/0x2323 Notify 打印 IMU ASCII 行
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include "securec.h"
#include "sle_common.h"
#include "sle_device_discovery.h"
#include "sle_connection_manager.h"
#include "sle_ssap_client.h"
#include "sle_errcode.h"
#include "sle_transmition_manager.h"
#include "sle_device_manager.h"
#include "sle_connect_client.h"

#define SLE_PANEL_COMPANY_ID0       0xEB
#define SLE_PANEL_COMPANY_ID1       0x1A
#define SLE_PANEL_PROTO_VER         0x01
#define SLE_PANEL_CMD_START_MEASURE 0xA1

#define SLE_IMU_LOG                  "[SLE_IMU]"
#define SLE_SEEK_INTERVAL_DEFAULT    160
#define SLE_SEEK_WINDOW_DEFAULT      40
#define UUID_16BIT_LEN               2
#define UUID_INDEX                   14
#define UUID_15_BYTE                 15
#define SPEED_DEFAULT_CONN_INTERVAL  0x10
#define SPEED_DEFAULT_TIMEOUT_MULT   0x1f4
#define DATA_LEN                     251
#define DEFAULT_SLE_SPEED_MCS        10
#define DEFAULT_SLE_SPEED_MTU_SIZE   1500
#define BYTE_LEN_128                 128
#define SLE_AD_COMPLETE_NAME         0x0B
#define SLE_UUID_SERVICE             0x2222
#define SLE_UUID_NOTIFY              0x2323
#define TARGET_NAME                  "paibing_imu"
#define TARGET_MAC_STR               "CC:AD:C9:00:22:01"

static const uint8_t g_target_mac[SLE_ADDR_LEN] = { 0xCC, 0xAD, 0xC9, 0x00, 0x22, 0x01 };

static sle_announce_seek_callbacks_t g_seek_cbk;
static sle_connection_callbacks_t g_connect_cbk;
static ssapc_callbacks_t g_ssapc_cbk;

static sle_uuid_t g_client_app_uuid = { UUID_16BIT_LEN, { 0 } };
static uint8_t g_client_id;
static uint16_t g_connect_id;
static uint16_t g_notify_handle;
static volatile uint8_t g_connecting;
static volatile uint8_t g_connected;
static volatile uint8_t g_link_setup_started;
static sle_addr_t g_peer_addr;

static void sle_imu_start_scan(void);
static void sle_imu_clear_stale_bonds(void);
static void sle_imu_start_link_setup(uint16_t conn_id);

static int uuid16_match(const sle_uuid_t *uuid, uint16_t id)
{
    if (uuid == NULL) {
        return 0;
    }
    return uuid->uuid[UUID_INDEX] == (uint8_t)(id & 0xff) &&
        uuid->uuid[UUID_15_BYTE] == (uint8_t)((id >> 8) & 0xff);
}

static void format_mac(char *buf, size_t len, const sle_addr_t *addr)
{
    uint8_t i;

    if (buf == NULL || len == 0 || addr == NULL) {
        return;
    }
    buf[0] = '\0';
    for (i = 0; i < SLE_ADDR_LEN && (i * 3 + 1) < len; i++) {
        (void)snprintf(buf + i * 3, len - i * 3,
            i + 1 < SLE_ADDR_LEN ? "%02x:" : "%02x", addr->addr[i]);
    }
}

static void parse_adv_name(const uint8_t *data, uint8_t len, char *name, size_t name_len)
{
    uint8_t offset = 0;

    if (name == NULL || name_len == 0) {
        return;
    }
    name[0] = '\0';
    if (data == NULL || len == 0) {
        return;
    }

    while (offset + 1 < len) {
        uint8_t seg = data[offset];
        uint8_t type;
        uint8_t vlen;
        uint8_t i;

        if (seg < 2 || offset + 1 + seg > len) {
            break;
        }
        type = data[offset + 1];
        vlen = (uint8_t)(seg - 1);
        if (type == SLE_AD_COMPLETE_NAME && vlen > 0 && vlen < name_len) {
            for (i = 0; i < vlen; i++) {
                name[i] = (char)data[offset + 2 + i];
            }
            name[vlen] = '\0';
            return;
        }
        offset = (uint8_t)(offset + 1 + seg);
    }
}

static int mac_match_target(const sle_addr_t *addr)
{
    if (addr == NULL) {
        return 0;
    }
    return memcmp(addr->addr, g_target_mac, SLE_ADDR_LEN) == 0;
}

static int seek_should_connect(sle_seek_result_info_t *info)
{
    char name[64];

    if (info == NULL) {
        return 0;
    }
    parse_adv_name(info->data, info->data_length, name, sizeof(name));
    if (name[0] != '\0' && strcmp(name, TARGET_NAME) == 0) {
        return 1;
    }
    return mac_match_target(&info->addr);
}

static void print_imu_payload(const uint8_t *data, uint16_t len)
{
    uint16_t i;

    if (data == NULL || len == 0) {
        return;
    }
    printf("%s ", SLE_IMU_LOG);
    for (i = 0; i < len; i++) {
        uint8_t c = data[i];
        if (c == '\n' || (c >= 32 && c <= 126)) {
            putchar((int)c);
        } else {
            printf("\\x%02x", c);
        }
    }
    if (data[len - 1] != '\n') {
        putchar('\n');
    }
    fflush(stdout);
}

static errcode_t sle_imu_register_persistence(void)
{
    char buff[BYTE_LEN_128] = { 0 };

    if (getcwd(buff, BYTE_LEN_128) == NULL) {
        return ERRCODE_SLE_FAIL;
    }
    return sle_dev_manager_register_file_path(buff);
}

static void sle_imu_clear_stale_bonds(void)
{
    sle_addr_t target = { 0 };
    sle_addr_t zero = { 0 };
    errcode_t ret_target;
    errcode_t ret_zero;

    (void)memcpy_s(target.addr, SLE_ADDR_LEN, g_target_mac, SLE_ADDR_LEN);
    ret_target = sle_remove_paired_remote_device(&target);
    ret_zero = sle_remove_paired_remote_device(&zero);
    printf("%s 清除旧绑定 %s ret=0x%x, 全零MAC ret=0x%x\n",
        SLE_IMU_LOG, TARGET_MAC_STR, ret_target, ret_zero);
    fflush(stdout);
}

static void sle_imu_begin_ssap_discovery(uint16_t conn_id)
{
    ssap_exchange_info_t info = { 0 };

    if (g_client_id == 0) {
        printf("%s SSAP 发现跳过: client 未注册\n", SLE_IMU_LOG);
        fflush(stdout);
        return;
    }
    info.mtu_size = DEFAULT_SLE_SPEED_MTU_SIZE;
    info.version = 1;
    printf("%s 发起 SSAP 服务发现 (MTU=%u)\n", SLE_IMU_LOG, (unsigned)info.mtu_size);
    (void)ssapc_exchange_info_req(g_client_id, conn_id, &info);
    fflush(stdout);
}

static void sle_imu_start_link_setup(uint16_t conn_id)
{
    if (g_link_setup_started) {
        return;
    }
    g_link_setup_started = 1;

    sle_set_mcs(conn_id, DEFAULT_SLE_SPEED_MCS);

    sle_set_phy_t phy = {
        .tx_format = SLE_RADIO_FRAME_2,
        .rx_format = SLE_RADIO_FRAME_2,
        .tx_phy = SLE_PHY_2M,
        .rx_phy = SLE_PHY_2M,
        .tx_pilot_density = SLE_PHY_PILOT_DENSITY_16_TO_1,
        .rx_pilot_density = SLE_PHY_PILOT_DENSITY_16_TO_1,
        .g_feedback = 0,
        .t_feedback = 0,
    };
    (void)sle_set_phy_param(conn_id, &phy);
    fflush(stdout);
}

static void sle_imu_write_measure_start_cmd(uint16_t prop_handle)
{
    uint8_t payload[] = {
        SLE_PANEL_COMPANY_ID0, SLE_PANEL_COMPANY_ID1, SLE_PANEL_PROTO_VER,
        SLE_PANEL_CMD_START_MEASURE, 0x01, 0x00, 0x00
    };
    ssapc_write_param_t param = { 0 };

    param.handle = prop_handle;
    param.type = SSAP_PROPERTY_TYPE_VALUE;
    param.data_len = sizeof(payload);
    param.data = payload;
    errcode_t ret = ssapc_write_req(g_client_id, g_connect_id, &param);
    printf("%s 写开始测量命令(0xA1)到特征 hdl=0x%04x ret=0x%x\n",
        SLE_IMU_LOG, prop_handle, ret);
    fflush(stdout);
}

static void sle_imu_enable_notify(uint16_t prop_handle)
{
    uint8_t cccd[] = { 0x01, 0x00 };
    ssapc_write_param_t param = { 0 };

    param.handle = (uint16_t)(prop_handle + 1);
    param.type = SSAP_DESCRIPTOR_CLIENT_CONFIGURATION;
    param.data_len = sizeof(cccd);
    param.data = cccd;
    errcode_t ret = ssapc_write_req(g_client_id, g_connect_id, &param);
    printf("%s 写 CCCD(handle=0x%04x) ret=0x%x，等待 Notify...\n",
        SLE_IMU_LOG, param.handle, ret);
    fflush(stdout);
}

static void sle_imu_sle_enable_cbk(errcode_t status)
{
    if (status != ERRCODE_SLE_SUCCESS) {
        printf("%s 星闪开启失败: 0x%x\n", SLE_IMU_LOG, status);
        fflush(stdout);
        return;
    }
    printf("%s 星闪已开启，扫描目标 MAC %s / 名称 %s\n",
        SLE_IMU_LOG, TARGET_MAC_STR, TARGET_NAME);
    fflush(stdout);
    if (ssapc_register_client(&g_client_app_uuid, &g_client_id) != ERRCODE_SLE_SUCCESS) {
        printf("%s ssapc_register_client 失败\n", SLE_IMU_LOG);
        fflush(stdout);
        return;
    }
    sle_imu_start_scan();
}

static void sle_imu_seek_enable_cbk(errcode_t status)
{
    printf("%s 扫描已开启 status=0x%x\n", SLE_IMU_LOG, status);
    fflush(stdout);
}

static void sle_imu_seek_disable_cbk(errcode_t status)
{
    printf("%s 扫描已停止 status=0x%x\n", SLE_IMU_LOG, status);
    fflush(stdout);
}

static void sle_imu_seek_result_cbk(sle_seek_result_info_t *info)
{
    char mac[24];
    char name[64];
    errcode_t ret;

    if (info == NULL || g_connecting || g_connected) {
        return;
    }
    if (!seek_should_connect(info)) {
        return;
    }

    parse_adv_name(info->data, info->data_length, name, sizeof(name));
    format_mac(mac, sizeof(mac), &info->addr);
    printf("%s 发现目标 rssi=%d mac=%s name=%s -> 连接\n",
        SLE_IMU_LOG, info->rssi, mac, name[0] != '\0' ? name : "(无)");

    g_connecting = 1;
    (void)memcpy_s(&g_peer_addr, sizeof(g_peer_addr), &info->addr, sizeof(sle_addr_t));
    sle_stop_seek();
    ret = sle_connect_remote_device(&g_peer_addr);
    if (ret != ERRCODE_SLE_SUCCESS) {
        printf("%s sle_connect_remote_device 失败: 0x%x\n", SLE_IMU_LOG, ret);
        g_connecting = 0;
        sle_imu_start_scan();
    }
    fflush(stdout);
}

static void sle_imu_connect_state_cbk(uint16_t conn_id, const sle_addr_t *addr,
    sle_acb_state_t conn_state, sle_pair_state_t pair_state, sle_disc_reason_t disc_reason)
{
    char mac[24];

    if (addr != NULL) {
        format_mac(mac, sizeof(mac), addr);
    } else {
        mac[0] = '\0';
    }

    if (conn_state == SLE_ACB_STATE_CONNECTED) {
        g_connect_id = conn_id;
        g_connected = 1;
        g_connecting = 0;
        printf("%s 已连接 conn_id=%u mac=%s pair_state=0x%x\n",
            SLE_IMU_LOG, conn_id, mac, pair_state);
        if (pair_state == SLE_PAIR_PAIRED) {
            printf("%s 已配对(pair=0x3)，开始 SSAP 发现\n", SLE_IMU_LOG);
            sle_imu_start_link_setup(conn_id);
            sle_imu_begin_ssap_discovery(conn_id);
        } else if (pair_state == SLE_PAIR_NONE && addr != NULL) {
            printf("%s 未配对(pair=0x1)，发起配对...\n", SLE_IMU_LOG);
            sle_pair_remote_device(addr);
        } else {
            printf("%s 连接 pair_state=0x%x，等待配对完成回调\n", SLE_IMU_LOG, pair_state);
        }
    } else if (conn_state == SLE_ACB_STATE_DISCONNECTED) {
        printf("%s 已断开 reason=0x%x\n", SLE_IMU_LOG, disc_reason);
        g_connected = 0;
        g_connecting = 0;
        g_notify_handle = 0;
        g_link_setup_started = 0;
        if (disc_reason == 0x10 || disc_reason == 0x7) {
            (void)sle_remove_paired_remote_device(&g_peer_addr);
        }
        sleep(1);
        sle_imu_start_scan();
    }
    fflush(stdout);
}

static void sle_imu_pair_complete_cbk(uint16_t conn_id, const sle_addr_t *addr, errcode_t status)
{
    printf("%s 配对完成 conn_id=%u status=0x%x\n", SLE_IMU_LOG, conn_id, status);
    if (status == ERRCODE_SLE_PAIRING_REJECT || status == ERRCODE_SLE_AUTH_FAIL ||
        status == ERRCODE_SLE_AUTH_PKEY_MISS) {
        if (addr != NULL) {
            (void)sle_remove_paired_remote_device(addr);
        }
        printf("%s 配对失败，已清除绑定等待重连\n", SLE_IMU_LOG);
        fflush(stdout);
        return;
    }
    if (!g_connected) {
        fflush(stdout);
        return;
    }
    sle_imu_start_link_setup(conn_id);
    sle_imu_begin_ssap_discovery(conn_id);
    fflush(stdout);
}

static void sle_imu_update_cbk(uint16_t conn_id, errcode_t status, const sle_connection_param_update_evt_t *param)
{
    ssap_exchange_info_t info = { 0 };

    (void)param;
    printf("%s 连接参数更新 status=0x%x\n", SLE_IMU_LOG, status);
    info.mtu_size = DEFAULT_SLE_SPEED_MTU_SIZE;
    info.version = 1;
    (void)ssapc_exchange_info_req(g_client_id, conn_id, &info);
    fflush(stdout);
}

static void sle_imu_set_phy_cbk(uint16_t conn_id, errcode_t status, const sle_set_phy_t *param)
{
    sle_connection_param_update_t parame = { 0 };

    (void)param;
    (void)status;
    parame.conn_id = conn_id;
    parame.interval_min = SPEED_DEFAULT_CONN_INTERVAL;
    parame.interval_max = SPEED_DEFAULT_CONN_INTERVAL;
    parame.max_latency = 0;
    parame.supervision_timeout = SPEED_DEFAULT_TIMEOUT_MULT;
    sle_update_connect_param(&parame);
}

static void sle_imu_exchange_info_cbk(uint8_t client_id, uint16_t conn_id,
    ssap_exchange_info_t *param, errcode_t status)
{
    ssapc_find_structure_param_t find_param = { 0 };

    (void)param;
    (void)status;
    printf("%s MTU 交换完成，发现服务 0x%04x\n", SLE_IMU_LOG, SLE_UUID_SERVICE);
    sle_set_data_len(conn_id, DATA_LEN);
    find_param.type = SSAP_FIND_TYPE_PRIMARY_SERVICE;
    find_param.start_hdl = 1;
    find_param.end_hdl = 0xFFFF;
    (void)ssapc_find_structure(client_id, conn_id, &find_param);
    fflush(stdout);
}

static void sle_imu_find_service_cbk(uint8_t client_id, uint16_t conn_id,
    ssapc_find_service_result_t *service, errcode_t status)
{
    ssapc_find_structure_param_t find_param = { 0 };

    if (service == NULL) {
        return;
    }
    printf("%s 发现服务 hdl=0x%04x-0x%04x status=0x%x\n",
        SLE_IMU_LOG, service->start_hdl, service->end_hdl, status);
    if (!uuid16_match(&service->uuid, SLE_UUID_SERVICE)) {
        fflush(stdout);
        return;
    }
    printf("%s 匹配服务 0x%04x，发现特征\n", SLE_IMU_LOG, SLE_UUID_SERVICE);
    find_param.type = SSAP_FIND_TYPE_PROPERTY;
    find_param.start_hdl = service->start_hdl;
    find_param.end_hdl = service->end_hdl;
    (void)ssapc_find_structure(client_id, conn_id, &find_param);
    fflush(stdout);
}

static void sle_imu_find_property_cbk(uint8_t client_id, uint16_t conn_id,
    ssapc_find_property_result_t *property, errcode_t status)
{
    if (property == NULL) {
        return;
    }
    printf("%s 发现特征 hdl=0x%04x uuid16=0x%04x oper=0x%x status=0x%x\n",
        SLE_IMU_LOG, property->handle, property->uuid.uuid[14], property->operate_indication, status);
    if (!uuid16_match(&property->uuid, SLE_UUID_NOTIFY)) {
        fflush(stdout);
        return;
    }
    g_notify_handle = property->handle;
    printf("%s 匹配 Notify 特征 0x%04x，开启通知\n", SLE_IMU_LOG, SLE_UUID_NOTIFY);
    sle_imu_enable_notify(property->handle);
    fflush(stdout);
}

static void sle_imu_notification_cbk(uint8_t client_id, uint16_t conn_id,
    ssapc_handle_value_t *data, errcode_t status)
{
    (void)client_id;
    (void)conn_id;
    if (status != ERRCODE_SLE_SUCCESS || data == NULL || data->data == NULL || data->data_len == 0) {
        printf("%s Notify 异常 status=0x%x len=%u\n", SLE_IMU_LOG, status,
            data != NULL ? (unsigned)data->data_len : 0U);
        fflush(stdout);
        return;
    }
    print_imu_payload(data->data, data->data_len);
}

static void sle_imu_find_cmp_cbk(uint8_t client_id, uint16_t conn_id,
    ssapc_find_structure_result_t *structure_result, errcode_t status)
{
    (void)client_id;
    (void)conn_id;
    (void)structure_result;
    (void)status;
}

static void sle_imu_write_cfm_cbk(uint8_t client_id, uint16_t conn_id,
    ssapc_write_result_t *write_result, errcode_t status)
{
    (void)write_result;
    printf("%s CCCD 写确认 status=0x%x\n", SLE_IMU_LOG, status);
    if (status == ERRCODE_SLE_SUCCESS && g_notify_handle != 0) {
        sle_imu_write_measure_start_cmd(g_notify_handle);
        printf("%s Notify 已订阅，已写 0xA1 + 等待 @ 行 IMU 数据\n", SLE_IMU_LOG);
        (void)ssapc_read_req(client_id, conn_id, g_notify_handle, SSAP_PROPERTY_TYPE_VALUE);
    }
    fflush(stdout);
}

static void sle_imu_read_cfm_cbk(uint8_t client_id, uint16_t conn_id,
    ssapc_handle_value_t *read_data, errcode_t status)
{
    (void)client_id;
    (void)conn_id;
    if (status != ERRCODE_SLE_SUCCESS || read_data == NULL || read_data->data == NULL || read_data->data_len == 0) {
        printf("%s 读特征失败 status=0x%x\n", SLE_IMU_LOG, status);
        fflush(stdout);
        return;
    }
    print_imu_payload(read_data->data, read_data->data_len);
}

static void sle_imu_start_scan(void)
{
    sle_seek_param_t param = { 0 };

    param.own_addr_type = 0;
    param.filter_duplicates = 1;
    param.seek_filter_policy = SLE_SEEK_FILTER_ALLOW_ALL;
    param.seek_phys = SLE_SEEK_PHY_1M;
    param.seek_type[0] = SLE_SEEK_ACTIVE;
    param.seek_interval[0] = SLE_SEEK_INTERVAL_DEFAULT;
    param.seek_window[0] = SLE_SEEK_WINDOW_DEFAULT;
    (void)sle_set_seek_param(&param);
    (void)sle_start_seek();
}

static void register_callbacks(void)
{
    (void)memset_s(&g_seek_cbk, sizeof(g_seek_cbk), 0, sizeof(g_seek_cbk));
    g_seek_cbk.sle_enable_cb = sle_imu_sle_enable_cbk;
    g_seek_cbk.seek_enable_cb = sle_imu_seek_enable_cbk;
    g_seek_cbk.seek_disable_cb = sle_imu_seek_disable_cbk;
    g_seek_cbk.seek_result_cb = sle_imu_seek_result_cbk;
    sle_announce_seek_register_callbacks(&g_seek_cbk);

    (void)memset_s(&g_connect_cbk, sizeof(g_connect_cbk), 0, sizeof(g_connect_cbk));
    g_connect_cbk.connect_state_changed_cb = sle_imu_connect_state_cbk;
    g_connect_cbk.pair_complete_cb = sle_imu_pair_complete_cbk;
    g_connect_cbk.connect_param_update_cb = sle_imu_update_cbk;
    g_connect_cbk.set_phy_cb = sle_imu_set_phy_cbk;
    sle_connection_register_callbacks(&g_connect_cbk);

    (void)memset_s(&g_ssapc_cbk, sizeof(g_ssapc_cbk), 0, sizeof(g_ssapc_cbk));
    g_ssapc_cbk.exchange_info_cb = sle_imu_exchange_info_cbk;
    g_ssapc_cbk.find_structure_cb = sle_imu_find_service_cbk;
    g_ssapc_cbk.find_structure_cmp_cb = sle_imu_find_cmp_cbk;
    g_ssapc_cbk.ssapc_find_property_cbk = sle_imu_find_property_cbk;
    g_ssapc_cbk.write_cfm_cb = sle_imu_write_cfm_cbk;
    g_ssapc_cbk.read_cfm_cb = sle_imu_read_cfm_cbk;
    g_ssapc_cbk.notification_cb = sle_imu_notification_cbk;
    ssapc_register_callbacks(&g_ssapc_cbk);
}

void sle_connect_client_init(void)
{
    g_client_id = 0;
    g_connect_id = 0;
    g_notify_handle = 0;
    g_connecting = 0;
    g_connected = 0;
    g_link_setup_started = 0;
    (void)memset_s(&g_peer_addr, sizeof(g_peer_addr), 0, sizeof(g_peer_addr));

    printf("%s 目标 MAC: %s，或广播名 %s\n", SLE_IMU_LOG, TARGET_MAC_STR, TARGET_NAME);
    printf("%s 服务/Notify UUID: 0x%04x / 0x%04x\n", SLE_IMU_LOG, SLE_UUID_SERVICE, SLE_UUID_NOTIFY);
    fflush(stdout);

    register_callbacks();
    if (sle_imu_register_persistence() != ERRCODE_SLE_SUCCESS) {
        printf("%s 注册持久化路径失败（可忽略）\n", SLE_IMU_LOG);
    }
    enable_sle();
}

void sle_connect_client_deinit(void)
{
    sle_stop_seek();
    if (g_client_id != 0) {
        (void)ssapc_unregister_client(g_client_id);
    }
    disable_sle();
    fflush(stdout);
}
