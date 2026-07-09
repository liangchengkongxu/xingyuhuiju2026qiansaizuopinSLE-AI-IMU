/**
 * 基于原厂 sle_uuid_client，仅保留扫描 + 打印所有广播（可读输出）
 * 广播数据按 WS73 SLE 数据类型解析，见 sle_uuid_server_adv.h
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "securec.h"
#include "sle_common.h"
#include "sle_device_discovery.h"
#include "sle_errcode.h"
#include "sle_seek_print_client.h"
#include "sle_imu_adv.h"

#define SLE_SEEK_LOG              "[SLE_SEEK]"
#define SLE_IMU_LOG               "[SLE_IMU]"
#define SLE_IMU_LINE_MAX          320
#define SLE_SEEK_INTERVAL_DEFAULT 160
#define SLE_SEEK_WINDOW_DEFAULT   40
#define SLE_SEEK_INTERVAL_FAST    80
#define SLE_SEEK_WINDOW_FAST      80
/* 最大接收：100% 占空比，interval=8 → 1ms 一轮（0.125ms 单位） */
#define SLE_SEEK_INTERVAL_MAX_RX  8
#define SLE_SEEK_WINDOW_MAX_RX    8
#define SLE_NAME_MAX              64
#define SLE_CONTENT_MAX           384
#define SLE_HEX_LINE_MAX          128

#define SLE_IMU_DEDUP_SLOTS       16

/* 与原厂 sle_uuid_server_adv.h 中 sle_adv_data_type 一致 */
#define SLE_AD_DISCOVERY_LEVEL       0x01
#define SLE_AD_ACCESS_MODE           0x02
#define SLE_AD_SERVICE_DATA_16       0x03
#define SLE_AD_SERVICE_DATA_128      0x04
#define SLE_AD_UUID16_LIST           0x05
#define SLE_AD_UUID128_LIST          0x06
#define SLE_AD_UUID16_INC            0x07
#define SLE_AD_UUID128_INC           0x08
#define SLE_AD_SERVICE_HASH          0x09
#define SLE_AD_SHORT_NAME            0x0A
#define SLE_AD_COMPLETE_NAME         0x0B
#define SLE_AD_TX_POWER              0x0C
#define SLE_AD_SLB_DOMAIN            0x0D
#define SLE_AD_SLB_MAC_ID            0x0E
#define SLE_AD_EXTENDED              0xFE
#define SLE_AD_MANUFACTURER          0xFF

static sle_announce_seek_callbacks_t g_seek_cbk;
static uint32_t g_report_count;
static int g_quiet_log;

typedef struct {
    char mac[24];
    uint32_t last_uptime_ms;
} sle_imu_dedup_slot_t;

static sle_imu_dedup_slot_t g_imu_dedup[SLE_IMU_DEDUP_SLOTS];

static int mac_is_zero(const sle_addr_t *addr)
{
    uint8_t i;

    if (addr == NULL) {
        return 1;
    }
    for (i = 0; i < SLE_ADDR_LEN; i++) {
        if (addr->addr[i] != 0) {
            return 0;
        }
    }
    return 1;
}

static void format_mac(char *buf, size_t buf_len, const sle_addr_t *addr)
{
    uint8_t i;

    if (buf == NULL || buf_len == 0 || addr == NULL) {
        return;
    }
    buf[0] = '\0';
    for (i = 0; i < SLE_ADDR_LEN && (i * 3 + 1) < buf_len; i++) {
        (void)snprintf(buf + i * 3, buf_len - i * 3,
            i + 1 < SLE_ADDR_LEN ? "%02x:" : "%02x", addr->addr[i]);
    }
}

static const char *event_type_desc(uint8_t event_type)
{
    switch (event_type) {
        case 3:  return "广播报告(简)";
        case 11: return "广播报告(扩展/含名称)";
        default: return "其它";
    }
}

static void copy_printable(char *dst, size_t dst_len, const uint8_t *src, uint8_t src_len)
{
    uint8_t i;
    size_t n = 0;

    if (dst == NULL || dst_len == 0 || src == NULL || src_len == 0) {
        return;
    }
    for (i = 0; i < src_len && n + 1 < dst_len; i++) {
        char c = (char)src[i];
        if (c >= 32 && c <= 126) {
            dst[n++] = c;
        }
    }
    dst[n] = '\0';
}

static int payload_is_printable(const uint8_t *p, uint8_t n)
{
    uint8_t i;

    for (i = 0; i < n; i++) {
        if (p[i] < 32 || p[i] > 126) {
            return 0;
        }
    }
    return n > 0;
}

static void append_content(char *buf, size_t buf_len, const char *piece)
{
    size_t cur;

    if (buf == NULL || piece == NULL || buf_len == 0 || piece[0] == '\0') {
        return;
    }
    cur = strlen(buf);
    if (cur == 0) {
        (void)snprintf(buf, buf_len, "%s", piece);
    } else if (cur + 2 < buf_len) {
        (void)snprintf(buf + cur, buf_len - cur, "; %s", piece);
    }
}

static void append_hex_bytes(char *buf, size_t buf_len, const uint8_t *data, uint8_t len)
{
    uint8_t i;
    size_t cur = strlen(buf);

    for (i = 0; i < len && cur + 3 < buf_len; i++) {
        cur += (size_t)snprintf(buf + cur, buf_len - cur, "%02x%s", data[i], (i + 1 < len) ? " " : "");
    }
}

static const char *sle_ad_type_name(uint8_t type)
{
    switch (type) {
        case SLE_AD_DISCOVERY_LEVEL:  return "发现等级";
        case SLE_AD_ACCESS_MODE:      return "接入层能力";
        case SLE_AD_SERVICE_DATA_16:  return "标准服务数据";
        case SLE_AD_SERVICE_DATA_128: return "自定义服务数据";
        case SLE_AD_UUID16_LIST:      return "16bit服务UUID列表";
        case SLE_AD_UUID128_LIST:     return "128bit服务UUID列表";
        case SLE_AD_UUID16_INC:       return "16bit服务UUID(部分)";
        case SLE_AD_UUID128_INC:      return "128bit服务UUID(部分)";
        case SLE_AD_SERVICE_HASH:     return "服务结构散列";
        case SLE_AD_SHORT_NAME:       return "缩写本地名称";
        case SLE_AD_COMPLETE_NAME:    return "完整本地名称";
        case SLE_AD_TX_POWER:         return "广播发射功率";
        case SLE_AD_SLB_DOMAIN:       return "SLB通信域";
        case SLE_AD_SLB_MAC_ID:       return "SLB媒体接入层ID";
        case SLE_AD_EXTENDED:         return "扩展类型";
        case SLE_AD_MANUFACTURER:     return "厂商自定义";
        default:                      return NULL;
    }
}

static const char *discovery_level_desc(uint8_t v)
{
    switch (v) {
        case 0: return "不可见(预留)";
        case 1: return "一般可发现";
        case 2: return "优先可发现(预留)";
        case 3: return "仅曾配对设备(预留)";
        case 4: return "指定设备可发现";
        default: return "未知等级";
    }
}

static void format_field(uint8_t type, const uint8_t *val, uint8_t vlen,
    char *name, size_t name_len, char *piece, size_t piece_len)
{
    const char *tn = sle_ad_type_name(type);
    char label[48];

    if (piece == NULL || piece_len == 0) {
        return;
    }
    piece[0] = '\0';
    if (tn != NULL) {
        (void)snprintf(label, sizeof(label), "%s", tn);
    } else {
        (void)snprintf(label, sizeof(label), "类型0x%02x", type);
    }

    if (type == SLE_AD_DISCOVERY_LEVEL && vlen >= 1) {
        snprintf(piece, piece_len, "%s=%s(%u)", label, discovery_level_desc(val[0]), val[0]);
    } else if (type == SLE_AD_ACCESS_MODE && vlen >= 1) {
        snprintf(piece, piece_len, "%s=0x%02x", label, val[0]);
    } else if ((type == SLE_AD_SHORT_NAME || type == SLE_AD_COMPLETE_NAME) && vlen > 0) {
        char n[SLE_NAME_MAX];
        copy_printable(n, sizeof(n), val, vlen);
        snprintf(piece, piece_len, "%s='%s'", label, n);
        if (name != NULL && name[0] == '\0' && n[0] != '\0') {
            snprintf(name, name_len, "%s", n);
        }
    } else if (type == SLE_AD_TX_POWER && vlen >= 1) {
        snprintf(piece, piece_len, "%s=%d dBm", label, (int8_t)val[0]);
    } else if (type == SLE_AD_MANUFACTURER && vlen > 0) {
        char hx[96];
        hx[0] = '\0';
        append_hex_bytes(hx, sizeof(hx), val, vlen > 16 ? 16 : vlen);
        snprintf(piece, piece_len, "%s[%uB]=%s%s", label, vlen, hx, vlen > 16 ? "..." : "");
    } else if (vlen > 0 && payload_is_printable(val, vlen)) {
        char n[SLE_NAME_MAX];
        copy_printable(n, sizeof(n), val, vlen);
        snprintf(piece, piece_len, "%s='%s'", label, n);
    } else if (vlen > 0) {
        char hx[96];
        hx[0] = '\0';
        append_hex_bytes(hx, sizeof(hx), val, vlen > 12 ? 12 : vlen);
        snprintf(piece, piece_len, "%s[%uB]=%s%s", label, vlen, hx, vlen > 12 ? "..." : "");
    }
}

static int is_known_sle_type(uint8_t type)
{
    return sle_ad_type_name(type) != NULL;
}

/* 单字段：type(1) + length(1) + value，返回消耗字节数 */
static int consume_type_first(const uint8_t *data, uint8_t len, uint8_t offset,
    char *name, size_t name_len, char *content, size_t content_len)
{
    uint8_t type;
    uint8_t vlen;
    char piece[128];

    if (offset + 2 > len) {
        return 0;
    }
    type = data[offset];
    vlen = data[offset + 1];
    if (!is_known_sle_type(type) || vlen > 250 || offset + 2 + vlen > len) {
        return 0;
    }
    format_field(type, &data[offset + 2], vlen, name, name_len, piece, sizeof(piece));
    if (piece[0] != '\0') {
        append_content(content, content_len, piece);
    }
    return 2 + vlen;
}

/* 单段：length(1) + type(1) + value(length-1)，与 sle_set_adv_local_name 一致 */
static int consume_len_first(const uint8_t *data, uint8_t len, uint8_t offset,
    char *name, size_t name_len, char *content, size_t content_len)
{
    uint8_t seg_len;
    uint8_t type;
    uint8_t vlen;
    const uint8_t *val;
    char piece[128];

    if (offset + 1 >= len) {
        return 0;
    }
    seg_len = data[offset];
    /* 至少含 type + 1 字节 value；与原厂 sle_set_adv_local_name 一致 */
    if (seg_len < 2 || offset + 1 + seg_len > len) {
        return 0;
    }
    type = data[offset + 1];
    vlen = (uint8_t)(seg_len - 1);
    val = &data[offset + 2];
    if (!is_known_sle_type(type)) {
        return 0;
    }
    format_field(type, val, vlen, name, name_len, piece, sizeof(piece));
    if (piece[0] != '\0') {
        append_content(content, content_len, piece);
    }
    return 1 + seg_len;
}

static void format_raw_hex(const uint8_t *data, uint8_t len, char *hex, size_t hex_len)
{
    uint8_t show = len;

    if (hex == NULL || hex_len == 0 || data == NULL) {
        return;
    }
    hex[0] = '\0';
    if (len > 40) {
        show = 40;
    }
    append_hex_bytes(hex, hex_len, data, show);
    if (len > show && strlen(hex) + 4 < hex_len) {
        (void)strncat(hex, " ...", hex_len - strlen(hex) - 1);
    }
}

static void parse_adv_content(const uint8_t *data, uint8_t len,
    char *name, size_t name_len, char *content, size_t content_len,
    char *raw_hex, size_t raw_hex_len)
{
    uint8_t offset = 0;
    int fields = 0;
    char tail[96];

    if (name != NULL && name_len > 0) {
        name[0] = '\0';
    }
    if (content != NULL && content_len > 0) {
        content[0] = '\0';
    }
    if (raw_hex != NULL && raw_hex_len > 0) {
        raw_hex[0] = '\0';
    }
    if (data == NULL || len == 0) {
        if (content != NULL) {
            snprintf(content, content_len, "空广播");
        }
        return;
    }

    format_raw_hex(data, len, raw_hex, raw_hex_len);

    /*
     * 扫描回调里多为 length-first（02 0c 06 / 0c 0b 名称）；
     * 少数 announce 为 type-first（01 01 01）。先 LTV 再 TLV，避免把 02 0c… 误读成 type=0x02。
     */
    while (offset < len) {
        int step = consume_len_first(data, len, offset, name, name_len, content, content_len);
        if (step == 0) {
            step = consume_type_first(data, len, offset, name, name_len, content, content_len);
        }
        if (step > 0) {
            offset = (uint8_t)(offset + step);
            fields++;
            continue;
        }
        break;
    }

    if (offset < len) {
        uint8_t rest = (uint8_t)(len - offset);
        tail[0] = '\0';
        append_hex_bytes(tail, sizeof(tail), &data[offset], rest > 12 ? 12 : rest);
        snprintf(tail + strlen(tail), sizeof(tail) - strlen(tail),
            "%s", rest > 12 ? "..." : "");
        if (fields == 0 && content != NULL) {
            snprintf(content, content_len, "未识别TLV，尾部%u字节=%s", rest, tail);
        } else if (content != NULL) {
            char piece[64];
            snprintf(piece, sizeof(piece), "未解析尾部%uB=%s", rest, tail);
            append_content(content, content_len, piece);
        }
    } else if (fields == 0 && content != NULL) {
        snprintf(content, content_len, "未能按SLE TLV解析，见原始字节");
    }
}

static int walk_adv_tlv(const uint8_t *data, uint8_t len,
    void (*on_field)(uint8_t type, const uint8_t *val, uint8_t vlen, void *ctx), void *ctx)
{
    uint8_t offset = 0;
    int fields = 0;

    if (data == NULL || len == 0) {
        return 0;
    }
    while (offset < len) {
        int step = consume_len_first(data, len, offset, NULL, 0, NULL, 0);
        if (step == 0) {
            step = consume_type_first(data, len, offset, NULL, 0, NULL, 0);
        }
        if (step <= 0) {
            break;
        }
        {
            uint8_t seg = data[offset];
            uint8_t type;
            uint8_t vlen;
            const uint8_t *val;

            if (seg >= 2 && offset + 1 + seg <= len) {
                if (data[offset + 1] <= seg - 1) {
                    type = data[offset + 1];
                    vlen = (uint8_t)(seg - 1);
                    val = &data[offset + 2];
                    if (on_field != NULL) {
                        on_field(type, val, vlen, ctx);
                    }
                    fields++;
                }
            }
        }
        offset = (uint8_t)(offset + step);
    }
    return fields;
}

static void metrics_on_field(uint8_t type, const uint8_t *val, uint8_t vlen, void *ctx)
{
    int *arr = (int *)ctx;

    if (type == SLE_AD_DISCOVERY_LEVEL && vlen >= 1) {
        arr[0] = (int)val[0];
        arr[2] = 1;
    } else if (type == SLE_AD_TX_POWER && vlen >= 1) {
        arr[1] = (int)(int8_t)val[0];
        arr[3] = 1;
    }
}

static void mac_on_field(uint8_t type, const uint8_t *val, uint8_t vlen, void *ctx)
{
    uint8_t *out = (uint8_t *)ctx;

    if (type == SLE_AD_SLB_MAC_ID && vlen >= SLE_ADDR_LEN) {
        (void)memcpy_s(out, SLE_ADDR_LEN, val, SLE_ADDR_LEN);
    }
}

static int16_t read_i16_le(const uint8_t *p)
{
    return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint16_t read_u16_le(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static int copy_imu_line_from_at(const char *start, size_t max_len, char *out, size_t out_len);
static int scan_buffer_for_imu_copy(const uint8_t *data, uint8_t len, char *out, size_t out_len);

typedef struct {
    int *emitted;
    const char *mac;
} sle_imu_emit_ctx_t;

static void scan_buffer_for_imu(const uint8_t *data, uint8_t len, sle_imu_emit_ctx_t *ctx);
static int mac_whitelist_allowed(const char *mac_str);

static int imu_payload_has_data(const uint8_t *p, uint8_t vlen, uint8_t from)
{
    uint8_t i;

    if (p == NULL || from >= vlen) {
        return 0;
    }
    for (i = from; i < vlen; i++) {
        if (p[i] != 0) {
            return 1;
        }
    }
    return 0;
}

/* @ 行：A* 为 mg；M 为 centi-g；R/P 为 0.1° 整数 */
static int imu_sample_values_sane(int16_t ax_mg, int16_t ay_mg, int16_t az_mg,
    int16_t gx, int16_t gy, int16_t roll, int16_t pitch, uint16_t m_centi)
{
    (void)roll;
    (void)pitch;
    if (m_centi < 50 || m_centi > 350) {
        return 0;
    }
    if (ax_mg > 3000 || ax_mg < -3000 || ay_mg > 3000 || ay_mg < -3000 ||
        az_mg > 3000 || az_mg < -3000) {
        return 0;
    }
    if (gx > 4000 || gx < -4000 || gy > 4000 || gy < -4000) {
        return 0;
    }
    return 1;
}

/* 广播加速度 int16，单位 0.1g（×10 得 mg） */
static int imu_accel_raw_sane(int16_t ax, int16_t ay, int16_t az)
{
    if (ax > 300 || ax < -300 || ay > 300 || ay < -300 || az > 300 || az < -300) {
        return 0;
    }
    return 1;
}

static int imu_format_line(int16_t ax_mg, int16_t ay_mg, int16_t az_mg, int16_t gx, int16_t gy,
    int16_t roll, int16_t pitch, uint16_t m_centi, uint32_t t_ms, char *out, size_t out_len)
{
    if (!imu_sample_values_sane(ax_mg, ay_mg, az_mg, gx, gy, roll, pitch, m_centi)) {
        return 0;
    }
    if (t_ms > 86400000U) {
        t_ms = 0;
    }
    (void)snprintf(out, out_len,
        "@%u,A%+d,%+d,%+d,G%+d,%+d,R%+d,P%+d,M%u",
        t_ms, (int)(ax_mg / 10), (int)(ay_mg / 10), (int)(az_mg / 10), (int)gx, (int)gy,
        (int)roll, (int)pitch, (unsigned)m_centi);
    return 1;
}

/* 22 字节厂商载荷：与 paibing_build_mfg_sensor() 一致 */
static int format_imu_sensor_adv(const uint8_t *p, uint8_t vlen, char *out, size_t out_len)
{
    uint32_t t_ms;
    int16_t ax_raw;
    int16_t ay_raw;
    int16_t az_raw;
    int16_t ax_mg;
    int16_t ay_mg;
    int16_t az_mg;
    int16_t gx;
    int16_t gy;
    int16_t roll;
    int16_t pitch;
    uint16_t m_centi;

    if (vlen < SLE_IMU_ADV_MIN_LEN) {
        return 0;
    }
    if (p[0] != SLE_IMU_COMPANY_ID0 || p[1] != SLE_IMU_COMPANY_ID1 || p[2] != SLE_IMU_PROTO_SENSOR) {
        return 0;
    }

    t_ms = read_u16_le(&p[4]);
    ax_raw = read_i16_le(&p[6]);
    ay_raw = read_i16_le(&p[8]);
    az_raw = read_i16_le(&p[10]);
    if (!imu_accel_raw_sane(ax_raw, ay_raw, az_raw)) {
        return 0;
    }
    ax_mg = (int16_t)(ax_raw * 10);
    ay_mg = (int16_t)(ay_raw * 10);
    az_mg = (int16_t)(az_raw * 10);
    gx = read_i16_le(&p[12]);
    gy = read_i16_le(&p[14]);
    roll = read_i16_le(&p[16]);
    pitch = read_i16_le(&p[18]);
    m_centi = read_u16_le(&p[20]);
    return imu_format_line(ax_mg, ay_mg, az_mg, gx, gy, roll, pitch, m_centi, t_ms, out, out_len);
}

static void log_imu_raw_payload(const uint8_t *p, uint8_t vlen)
{
    uint8_t i;
    size_t pos = 0;
    char hx[160];

    if (p == NULL || vlen == 0) {
        return;
    }
    hx[0] = '\0';
    for (i = 0; i < vlen && i < 40; i++) {
        if (pos + 4 >= sizeof(hx)) {
            break;
        }
        pos += (size_t)snprintf(hx + pos, sizeof(hx) - pos, "%02x ", p[i]);
    }
    printf("%s [SLE_IMU_RAW] len=%u hex=%s%s\n", SLE_IMU_LOG, vlen, hx, vlen > 40 ? "..." : "");
    fflush(stdout);
}

static int format_imu_line_from_binary(const uint8_t *p, uint8_t vlen, char *out, size_t out_len)
{
    if (p == NULL || out == NULL || out_len < 48 || vlen < SLE_IMU_ADV_MIN_LEN) {
        return 0;
    }
    if (p[0] != SLE_IMU_COMPANY_ID0 || p[1] != SLE_IMU_COMPANY_ID1 || p[2] != SLE_IMU_PROTO_SENSOR) {
        return 0;
    }

    /* 厂商域内 ASCII @ 行（极少数固件） */
    if (vlen >= 12 && p[4] == '@' && scan_buffer_for_imu_copy(p, vlen, out, out_len)) {
        return 1;
    }

    if (format_imu_sensor_adv(p, vlen, out, out_len)) {
        return 1;
    }

    if (imu_payload_has_data(p, vlen, 4)) {
        log_imu_raw_payload(p, vlen);
    }
    return 0;
}

static int scan_buffer_for_imu_copy(const uint8_t *data, uint8_t len, char *out, size_t out_len)
{
    uint8_t i;

    if (data == NULL || out == NULL || out_len == 0) {
        return 0;
    }
    for (i = 0; i + 10 < len; i++) {
        if (data[i] != '@') {
            continue;
        }
        if (!copy_imu_line_from_at((const char *)&data[i], (size_t)(len - i), out, out_len)) {
            continue;
        }
        return 1;
    }
    return 0;
}

static void normalize_mac_key(char *mac, size_t mac_len)
{
    size_t i;

    if (mac == NULL || mac_len == 0) {
        return;
    }
    for (i = 0; i < mac_len && mac[i] != '\0'; i++) {
        if (mac[i] >= 'A' && mac[i] <= 'Z') {
            mac[i] = (char)(mac[i] - 'A' + 'a');
        }
    }
}

static int parse_line_uptime_ms(const char *line, uint32_t *out_ms)
{
    unsigned long ms;

    if (line == NULL || out_ms == NULL || line[0] != '@') {
        return 0;
    }
    if (sscanf(line + 1, "%lu", &ms) != 1) {
        return 0;
    }
    *out_ms = (uint32_t)ms;
    return 1;
}

/* 同一 MAC + @uptime_ms 只输出一次（ADV 与 Scan Response 双份去重） */
static int imu_dedup_allow(const char *mac, uint32_t uptime_ms)
{
    char key[24];
    int empty_slot = -1;
    int i;

    if (mac == NULL || mac[0] == '\0') {
        (void)snprintf(key, sizeof(key), "_unknown_");
    } else {
        (void)snprintf(key, sizeof(key), "%.23s", mac);
        normalize_mac_key(key, sizeof(key));
    }

    for (i = 0; i < SLE_IMU_DEDUP_SLOTS; i++) {
        if (g_imu_dedup[i].mac[0] == '\0') {
            if (empty_slot < 0) {
                empty_slot = i;
            }
            continue;
        }
        if (strcmp(g_imu_dedup[i].mac, key) == 0) {
            if (g_imu_dedup[i].last_uptime_ms == uptime_ms) {
                return 0;
            }
            g_imu_dedup[i].last_uptime_ms = uptime_ms;
            return 1;
        }
    }

    i = empty_slot >= 0 ? empty_slot : (int)(uptime_ms % SLE_IMU_DEDUP_SLOTS);
    (void)strncpy(g_imu_dedup[i].mac, key, sizeof(g_imu_dedup[i].mac) - 1);
    g_imu_dedup[i].mac[sizeof(g_imu_dedup[i].mac) - 1] = '\0';
    g_imu_dedup[i].last_uptime_ms = uptime_ms;
    return 1;
}

static int emit_imu_line_tagged(const char *mac, const char *line)
{
    uint32_t uptime_ms;

    if (line == NULL || line[0] != '@') {
        return 0;
    }
    if (!parse_line_uptime_ms(line, &uptime_ms)) {
        return 0;
    }
    if (!imu_dedup_allow(mac, uptime_ms)) {
        return 0;
    }
    if (mac != NULL && mac[0] != '\0') {
        printf("%s mac=%s|%s\n", SLE_IMU_LOG, mac, line);
    } else {
        printf("%s %s\n", SLE_IMU_LOG, line);
    }
    fflush(stdout);
    return 1;
}

static int try_emit_binary_imu(const uint8_t *buf, uint8_t len, sle_imu_emit_ctx_t *ctx)
{
    uint8_t i;
    char line[SLE_IMU_LINE_MAX];

    if (buf == NULL || ctx == NULL || ctx->emitted == NULL || *ctx->emitted != 0) {
        return 0;
    }
    for (i = 0; i + SLE_IMU_ADV_MIN_LEN <= len; i++) {
        if (buf[i] != SLE_IMU_COMPANY_ID0 || buf[i + 1] != SLE_IMU_COMPANY_ID1) {
            continue;
        }
        if (!format_imu_line_from_binary(&buf[i], (uint8_t)(len - i), line, sizeof(line))) {
            continue;
        }
        *ctx->emitted = 1;
        return emit_imu_line_tagged(ctx->mac, line);
    }
    return 0;
}

static int adv_has_imu_manufacturer(const uint8_t *data, uint8_t len)
{
    uint8_t i;

    if (data == NULL || len < SLE_IMU_ADV_MIN_LEN) {
        return 0;
    }
    for (i = 0; i + SLE_IMU_ADV_MIN_LEN <= len; i++) {
        if (data[i] == SLE_IMU_COMPANY_ID0 && data[i + 1] == SLE_IMU_COMPANY_ID1 &&
            data[i + 2] == SLE_IMU_PROTO_SENSOR) {
            return 1;
        }
    }
    return 0;
}

static int adv_has_imu_payload(const uint8_t *data, uint8_t len)
{
    char line[SLE_IMU_LINE_MAX];

    if (data == NULL || len < 10) {
        return 0;
    }
    if (scan_buffer_for_imu_copy(data, len, line, sizeof(line))) {
        return 1;
    }
    return adv_has_imu_manufacturer(data, len);
}

static void manufacturer_imu_on_field(uint8_t type, const uint8_t *val, uint8_t vlen, void *ctx)
{
    sle_imu_emit_ctx_t *ec = (sle_imu_emit_ctx_t *)ctx;
    char line[SLE_IMU_LINE_MAX];

    if (ec == NULL || ec->emitted == NULL || *ec->emitted != 0) {
        return;
    }
    if (type != SLE_AD_MANUFACTURER || val == NULL) {
        return;
    }
    /* 新固件：ADV / Scan Response 厂商 0xFF 内 ASCII @ 行（与 BLE Notify 一致） */
    if (vlen >= 10 && val[0] == '@') {
        if (copy_imu_line_from_at((const char *)val, (size_t)vlen, line, sizeof(line))) {
            *ec->emitted = 1;
            (void)emit_imu_line_tagged(ec->mac, line);
        }
        return;
    }
    if (scan_buffer_for_imu_copy(val, vlen, line, sizeof(line))) {
        *ec->emitted = 1;
        (void)emit_imu_line_tagged(ec->mac, line);
        return;
    }
    /* 旧二进制 EB 1A 02 回退 */
    if (vlen >= SLE_IMU_ADV_MIN_LEN && format_imu_line_from_binary(val, vlen, line, sizeof(line))) {
        *ec->emitted = 1;
        (void)emit_imu_line_tagged(ec->mac, line);
        return;
    }
    scan_buffer_for_imu(val, vlen, ec);
}

/* 与蓝牙 Notify / 新 SLE 广播 ASCII 一致：@t,A,G,R,P,M */
static int imu_ascii_line_valid(const char *line)
{
    const char *p;
    size_t n;

    if (line == NULL || line[0] != '@') {
        return 0;
    }
    if (strstr(line, ",A") == NULL || strstr(line, ",G") == NULL ||
        strstr(line, ",R") == NULL || strstr(line, ",P") == NULL) {
        return 0;
    }
    p = strrchr(line, 'M');
    if (p == NULL || p <= line || p[-1] != ',') {
        return 0;
    }
    p++;
    if (*p == '\0') {
        return 0;
    }
    for (; *p != '\0'; p++) {
        if (*p < '0' || *p > '9') {
            return 0;
        }
    }
    n = strlen(line);
    return n >= 20;
}

static int copy_imu_line_from_at(const char *start, size_t max_len, char *out, size_t out_len)
{
    size_t i;

    if (start == NULL || out == NULL || out_len == 0 || max_len < 10 || start[0] != '@') {
        return 0;
    }
    for (i = 0; i < max_len && i + 1 < out_len; i++) {
        char c = start[i];
        if (c == '\n' || c == '\r') {
            break;
        }
        if (c < 32 || c > 126) {
            break;
        }
        out[i] = c;
    }
    out[i] = '\0';
    if (i < 10 || strchr(out, ',') == NULL) {
        return 0;
    }
    return imu_ascii_line_valid(out);
}

static void scan_buffer_for_imu(const uint8_t *data, uint8_t len, sle_imu_emit_ctx_t *ctx)
{
    uint8_t i;
    char line[SLE_IMU_LINE_MAX];

    if (data == NULL || len == 0 || ctx == NULL || ctx->emitted == NULL || *ctx->emitted != 0) {
        return;
    }
    for (i = 0; i + 10 < len; i++) {
        if (data[i] != '@') {
            continue;
        }
        if (!copy_imu_line_from_at((const char *)&data[i], (size_t)(len - i), line, sizeof(line))) {
            continue;
        }
        (void)emit_imu_line_tagged(ctx->mac, line);
        *ctx->emitted = 1;
        return;
    }
}

static void resolve_report_mac(sle_seek_result_info_t *info, const char *name,
    char *mac_str, size_t mac_len);

static void try_emit_imu_from_adv(sle_seek_result_info_t *info, const char *name)
{
    char mac[24];
    int emitted = 0;
    sle_imu_emit_ctx_t ctx = { &emitted, mac };

    if (info == NULL || info->data == NULL || info->data_length == 0) {
        return;
    }
    resolve_report_mac(info, name, mac, sizeof(mac));
    if (!mac_whitelist_allowed(mac)) {
        return;
    }
    /* 1. 厂商 0xFF：ASCII @ 行（新固件 ADV + Scan Response 双份，按 uptime 去重） */
    (void)walk_adv_tlv(info->data, info->data_length, manufacturer_imu_on_field, &ctx);
    /* 2. 整包扫描 ASCII @ 行 */
    if (!emitted) {
        scan_buffer_for_imu(info->data, info->data_length, &ctx);
    }
    /* 3. 旧二进制 EB 1A 02 回退 */
    if (!emitted) {
        (void)try_emit_binary_imu(info->data, info->data_length, &ctx);
    }
}

static int extract_adv_mac_bytes(const uint8_t *data, uint8_t len, uint8_t mac_out[SLE_ADDR_LEN])
{
    uint8_t mac[SLE_ADDR_LEN];

    if (mac_out == NULL) {
        return 0;
    }
    (void)memset_s(mac, SLE_ADDR_LEN, 0, SLE_ADDR_LEN);
    (void)walk_adv_tlv(data, len, mac_on_field, mac);
    if (mac_is_zero((const sle_addr_t *)&mac)) {
        return 0;
    }
    (void)memcpy_s(mac_out, SLE_ADDR_LEN, mac, SLE_ADDR_LEN);
    return 1;
}

static void make_fallback_mac(const uint8_t *data, uint8_t len, char *mac_str, size_t mac_len)
{
    uint32_t h = 2166136261u;
    uint8_t i;

    if (mac_str == NULL || mac_len == 0) {
        return;
    }
    for (i = 0; i < len; i++) {
        h ^= (uint32_t)data[i];
        h *= 16777619u;
    }
    (void)snprintf(mac_str, mac_len, "00:00:%02x:%02x:%02x:%02x",
        (unsigned)((h >> 24) & 0xffu), (unsigned)((h >> 16) & 0xffu),
        (unsigned)((h >> 8) & 0xffu), (unsigned)(h & 0xffu));
}

static void resolve_report_mac(sle_seek_result_info_t *info, const char *name,
    char *mac_str, size_t mac_len)
{
    sle_addr_t tmp;
    uint8_t adv_mac[SLE_ADDR_LEN];

    if (mac_str == NULL || mac_len == 0) {
        return;
    }
    mac_str[0] = '\0';

    if (info != NULL && !mac_is_zero(&info->addr)) {
        format_mac(mac_str, mac_len, &info->addr);
        return;
    }
    if (info != NULL && !mac_is_zero(&info->direct_addr)) {
        format_mac(mac_str, mac_len, &info->direct_addr);
        return;
    }
    if (info != NULL && extract_adv_mac_bytes(info->data, info->data_length, adv_mac)) {
        (void)memcpy_s(tmp.addr, SLE_ADDR_LEN, adv_mac, SLE_ADDR_LEN);
        format_mac(mac_str, mac_len, &tmp);
        return;
    }
    if (info != NULL && info->data_length > 0) {
        make_fallback_mac(info->data, info->data_length, mac_str, mac_len);
        return;
    }
    (void)snprintf(mac_str, mac_len, "00:00:00:00:00:00");
}

static int mac_whitelist_allowed(const char *mac_str)
{
    static const char prefix[] = "ccad";
    int matched = 0;
    const char *p;

    if (mac_str == NULL || mac_str[0] == '\0') {
        return 0;
    }
    for (p = mac_str; *p != '\0' && matched < 4; p++) {
        char c = *p;

        if (c == ':' || c == '-') {
            continue;
        }
        if (tolower((unsigned char)c) != prefix[matched]) {
            return 0;
        }
        matched++;
    }
    return matched == 4;
}

static int report_has_identity(sle_seek_result_info_t *info, const char *name)
{
    uint8_t adv_mac[SLE_ADDR_LEN];

    if (info == NULL) {
        return 0;
    }
    if (name != NULL && name[0] != '\0') {
        return 1;
    }
    if (!mac_is_zero(&info->addr) || !mac_is_zero(&info->direct_addr)) {
        return 1;
    }
    if (extract_adv_mac_bytes(info->data, info->data_length, adv_mac)) {
        return 1;
    }
    return adv_has_imu_payload(info->data, info->data_length);
}

static void parse_adv_metrics(const uint8_t *data, uint8_t len,
    int *level, int *power_dbm, int *has_level, int *has_power)
{
    int ctx[4] = { -1, 0, 0, 0 };

    if (level != NULL) {
        *level = -1;
    }
    if (power_dbm != NULL) {
        *power_dbm = 0;
    }
    if (has_level != NULL) {
        *has_level = 0;
    }
    if (has_power != NULL) {
        *has_power = 0;
    }
    (void)walk_adv_tlv(data, len, metrics_on_field, ctx);
    if (level != NULL) {
        *level = ctx[0];
    }
    if (power_dbm != NULL) {
        *power_dbm = ctx[1];
    }
    if (has_level != NULL) {
        *has_level = ctx[2];
    }
    if (has_power != NULL) {
        *has_power = ctx[3];
    }
}

static void print_seek_readable(sle_seek_result_info_t *info)
{
    char addr_str[24];
    char direct_str[24];
    char name[SLE_NAME_MAX];
    char content[SLE_CONTENT_MAX];
    char raw_hex[SLE_HEX_LINE_MAX];
    int level = -1;
    int power_dbm = 0;
    int has_level = 0;
    int has_power = 0;

    format_mac(addr_str, sizeof(addr_str), &info->addr);
    format_mac(direct_str, sizeof(direct_str), &info->direct_addr);
    parse_adv_content(info->data, info->data_length, name, sizeof(name),
        content, sizeof(content), raw_hex, sizeof(raw_hex));
    parse_adv_metrics(info->data, info->data_length, &level, &power_dbm, &has_level, &has_power);

    g_report_count++;
    printf("%s --------------------------------------------------\n", SLE_SEEK_LOG);
    printf("%s 序号: %u  事件: %u(%s)  信号: %d dBm  数据长: %u\n",
        SLE_SEEK_LOG, g_report_count, info->event_type,
        event_type_desc(info->event_type), info->rssi, info->data_length);

    printf("%s 广播地址: %s (type=%u)%s\n", SLE_SEEK_LOG, addr_str, info->addr.type,
        mac_is_zero(&info->addr) ? "  [协议栈填0，扫描包无公开MAC]" : "");
    printf("%s 定向地址: %s (type=%u)%s\n", SLE_SEEK_LOG, direct_str, info->direct_addr.type,
        mac_is_zero(&info->direct_addr) ? "  [未使用]" : "");

    if (name[0] != '\0') {
        printf("%s 设备名: %s\n", SLE_SEEK_LOG, name);
    } else {
        printf("%s 设备名: (本包无名称)\n", SLE_SEEK_LOG);
    }

    if (content[0] != '\0') {
        printf("%s 解码字段: %s\n", SLE_SEEK_LOG, content);
    } else {
        printf("%s 解码字段: (无)\n", SLE_SEEK_LOG);
    }
    if (raw_hex[0] != '\0') {
        printf("%s 原始字节: %s\n", SLE_SEEK_LOG, raw_hex);
    }

    try_emit_imu_from_adv(info, name);

    if (report_has_identity(info, name)) {
        char out_mac[24];

        resolve_report_mac(info, name, out_mac, sizeof(out_mac));
        if (mac_whitelist_allowed(out_mac)) {
            printf("SLE_DEVICE|mac=%s|rssi=%d|name=%s|level=%d|power=%d|has_level=%d|has_power=%d\n",
                out_mac, info->rssi, name[0] != '\0' ? name : "-",
                level, power_dbm, has_level, has_power);
        }
    }
    fflush(stdout);
}

static void sle_seek_start_scan(void)
{
    sle_seek_param_t param = {0};
    int interval = SLE_SEEK_INTERVAL_FAST;
    int window = SLE_SEEK_WINDOW_FAST;
    const char *max_rx = getenv("SLE_SEEK_MAX_RX");
    const char *iv_env = getenv("SLE_SEEK_INTERVAL");
    const char *win_env = getenv("SLE_SEEK_WINDOW");

    if (max_rx != NULL && max_rx[0] != '\0' && strcmp(max_rx, "0") != 0) {
        interval = SLE_SEEK_INTERVAL_MAX_RX;
        window = SLE_SEEK_WINDOW_MAX_RX;
    }
    if (iv_env != NULL && iv_env[0] != '\0') {
        interval = (int)strtol(iv_env, NULL, 0);
    }
    if (win_env != NULL && win_env[0] != '\0') {
        window = (int)strtol(win_env, NULL, 0);
    }
    if (interval < 4) {
        interval = 4;
    }
    if (window < 4) {
        window = 4;
    }
    if (window > interval) {
        window = interval;
    }

    param.own_addr_type = 0;
    param.filter_duplicates = 0;
    param.seek_filter_policy = SLE_SEEK_FILTER_ALLOW_ALL;
    param.seek_phys = SLE_SEEK_PHY_1M;
    param.seek_type[0] = SLE_SEEK_ACTIVE;
    param.seek_interval[0] = (uint16_t)interval;
    param.seek_window[0] = (uint16_t)window;

    if (max_rx != NULL && max_rx[0] != '\0' && strcmp(max_rx, "0") != 0) {
        printf("%s 扫描参数(最大接收): interval=%d window=%d (100%%占空)\n",
            SLE_SEEK_LOG, interval, window);
        fflush(stdout);
    }

    if (sle_set_seek_param(&param) != ERRCODE_SLE_SUCCESS) {
        printf("%s 启动扫描失败: set_seek_param\n", SLE_SEEK_LOG);
        fflush(stdout);
        return;
    }
    if (sle_start_seek() != ERRCODE_SLE_SUCCESS) {
        printf("%s 启动扫描失败: start_seek\n", SLE_SEEK_LOG);
        fflush(stdout);
    }
}

static void sle_seek_enable_cbk(errcode_t status)
{
    printf("%s 星闪已开启，开始扫描…\n", SLE_SEEK_LOG);
    (void)status;
    fflush(stdout);
    sle_seek_start_scan();
}

static void sle_seek_enable_result_cbk(errcode_t status)
{
    if (status == ERRCODE_SLE_SUCCESS) {
        printf("%s 扫描已开启\n", SLE_SEEK_LOG);
    } else {
        printf("%s 扫描开启异常: 0x%02x\n", SLE_SEEK_LOG, status);
    }
    fflush(stdout);
}

static void sle_seek_disable_cbk(errcode_t status)
{
    printf("%s 扫描已停止: 0x%02x\n", SLE_SEEK_LOG, status);
    fflush(stdout);
}

static void emit_sle_device_line(sle_seek_result_info_t *info)
{
    char name[SLE_NAME_MAX];
    char out_mac[24];
    int level = -1;
    int power_dbm = 0;
    int has_level = 0;
    int has_power = 0;

    name[0] = '\0';
    parse_adv_content(info->data, info->data_length, name, sizeof(name),
        NULL, 0, NULL, 0);
    parse_adv_metrics(info->data, info->data_length, &level, &power_dbm, &has_level, &has_power);
    resolve_report_mac(info, name, out_mac, sizeof(out_mac));
    if (!mac_whitelist_allowed(out_mac)) {
        return;
    }
    try_emit_imu_from_adv(info, name);
    printf("SLE_DEVICE|mac=%s|rssi=%d|name=%s|level=%d|power=%d|has_level=%d|has_power=%d\n",
        out_mac, info->rssi, name[0] != '\0' ? name : "-",
        level, power_dbm, has_level, has_power);
    fflush(stdout);
}

static void sle_seek_result_cbk(sle_seek_result_info_t *info)
{
    if (info == NULL) {
        return;
    }
    if (g_quiet_log) {
        emit_sle_device_line(info);
        return;
    }
    print_seek_readable(info);
}

static void register_seek_callbacks(void)
{
    (void)memset_s(&g_seek_cbk, sizeof(g_seek_cbk), 0, sizeof(g_seek_cbk));
    g_seek_cbk.sle_enable_cb = sle_seek_enable_cbk;
    g_seek_cbk.seek_enable_cb = sle_seek_enable_result_cbk;
    g_seek_cbk.seek_disable_cb = sle_seek_disable_cbk;
    g_seek_cbk.seek_result_cb = sle_seek_result_cbk;
    sle_announce_seek_register_callbacks(&g_seek_cbk);
}

void sle_seek_print_client_init(void)
{
    g_report_count = 0;
    (void)memset_s(g_imu_dedup, sizeof(g_imu_dedup), 0, sizeof(g_imu_dedup));
    g_quiet_log = (getenv("SLE_SEEK_QUIET") != NULL);
    printf("%s 扫描打印工具（SLE TLV 解码）\n", SLE_SEEK_LOG);
    printf("%s 说明: 厂商0xFF 内 ASCII @ 行（默认）；EB 1A 02 二进制为旧固件回退；MAC+uptime 去重\n",
        SLE_SEEK_LOG);
    fflush(stdout);
    register_seek_callbacks();
    enable_sle();
}

void sle_seek_print_client_deinit(void)
{
    sle_stop_seek();
    disable_sle();
    printf("%s 已退出，共收到 %u 条广播\n", SLE_SEEK_LOG, g_report_count);
    fflush(stdout);
}
