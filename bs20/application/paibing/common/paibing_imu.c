/**
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026.
 *
 * @file app_main.c
 * @brief  拍柄共用：调试串口、MPU-9250、AHRS、主循环（传输由 sle/ble 实现）。
 *
 * @par 硬件约定
 * - UART0 调试：TX=MGPIO19，RX=MGPIO20，115200 8N1。
 * - MPU9250 软件 I2C：SCL=MGPIO16，SDA=MGPIO15（GPIO 位模拟）。
 * - 软复位：MGPIO22 低电平有效，消抖后 hal_reboot_chip()。
 *
 * @par BLE/星闪 双模行格式（Notify，ASCII；超长时由协议栈按 MTU 分包）
 * - 例：@185230,A-13,+1,+98,G-2,-1,R+123,P-456,M102\n — @ 后为相对开机点的毫秒；A/G 为 mg/10 和 dps；R/P 为 roll/pitch 角度*10；M 为力度*100。
 *
 * History: \n
 * 2026-05-01, Create file. \n
 */
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>

/* SDK 头：芯片寄存器、OS、驱动、BLE/星闪 示例、软复位 */
#include "chip_io.h"
#include "platform_core.h"
#include "soc_osal.h"
#include "time/osal_timer.h"
#include "uart.h"
#include "gpio.h"
#include "pinctrl.h"
#include "tcxo.h"
#include "watchdog.h"
#include "hal_reboot.h"
#include "paibing_imu.h"


/* -------------------------------------------------------------------------- */
/* 应用参数：串口、主循环周期、按键                                         */
/* -------------------------------------------------------------------------- */
#define PAIBING_UART_BAUDRATE 115200
#define PAIBING_SEND_INTERVAL_MS 100
/* 陀螺零偏校准：采样数 × 间隔 ≈ 1s（请保持静止） */
#define PAIBING_CALIB_SAMPLES 20
#define PAIBING_CALIB_INTERVAL_MS 50
#define PAIBING_RESET_KEY_PIN S_MGPIO22
#define PAIBING_RESET_KEY_DEBOUNCE_MS 30

/* 软件 I2C 引脚与位时延：延时过小可能导致总线不稳，过大则降低读速率 */
#define PAIBING_I2C_SCL_PIN S_MGPIO16
#define PAIBING_I2C_SDA_PIN S_MGPIO15
#define SOFT_I2C_DELAY_US 4

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

/* MPU-9250 灵敏度 */
#define ACCEL_SENSITIVITY   16384.0f  // LSB/g @ ±2g
#define GYRO_SENSITIVITY    131.0f    // LSB/(°/s) @ ±250dps

/* Mahony AHRS 参数 */
static float q0 = 1.0f, q1 = 0.0f, q2 = 0.0f, q3 = 0.0f;
#define TWO_KP (2.0f * 0.8f)
#define TWO_KI (2.0f * 0.01f)

/* 低通滤波参数 */
#define FILTER_ALPHA 0.3f

/* 校准参数 */
static float gyro_bias_x = 0, gyro_bias_y = 0, gyro_bias_z = 0;
static bool calibrated = false;
static uint32_t sample_count = 0;

/* -------------------------------------------------------------------------- */
/* MPU-9250 寄存器与 7 位器件地址（AD0 决定 0x68 / 0x69）                     */
/* -------------------------------------------------------------------------- */
#define MPU9250_I2C_ADDR_LOW 0x68
#define MPU9250_I2C_ADDR_HIGH 0x69
#define MPU9250_WHO_AM_I_REG 0x75
#define MPU9250_WHO_AM_I_VAL 0x71
#define MPU9250_PWR_MGMT_1_REG 0x6B
#define MPU9250_PWR_MGMT_2_REG 0x6C
#define MPU9250_SMPLRT_DIV_REG 0x19
#define MPU9250_CONFIG_REG 0x1A
#define MPU9250_GYRO_CONFIG_REG 0x1B
#define MPU9250_ACCEL_CONFIG_REG 0x1C
#define MPU9250_ACCEL_CONFIG2_REG 0x1D
#define MPU9250_ACCEL_XOUT_H_REG 0x3B

/* -------------------------------------------------------------------------- */
/* 全局状态                                                                  */
/* -------------------------------------------------------------------------- */
static uint8_t g_paibing_uart_rx_buf[64];
static uint16_t g_mpu9250_i2c_addr = MPU9250_I2C_ADDR_LOW;
static uint8_t g_uart_print_div = 0;
static uint64_t g_boot_jiffies0 = 0;
static uart_buffer_config_t g_paibing_uart_buf_cfg = {
    .rx_buffer = g_paibing_uart_rx_buf,
    .rx_buffer_size = sizeof(g_paibing_uart_rx_buf),
};

/* 滤波状态 */
static float filtered_acc_x = 0.0f, filtered_acc_y = 0.0f, filtered_acc_z = 0.0f;
static float filtered_total_acc = 1.0f;

static char g_paibing_uart_buf[128];
static uint8_t g_mpu_raw_buf[14];

/* 串口发送原始字符串（无格式化） */
void paibing_uart_send(const char *msg)
{
    (void)uapi_uart_write(UART_BUS_0, (const uint8_t *)msg, (uint32_t)strlen(msg), 0);
}

/* 串口 printf：栈上小缓冲，注意 fmt 不要过长 */
static void paibing_uart_printf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(g_paibing_uart_buf, sizeof(g_paibing_uart_buf), fmt, args);
    va_end(args);
    if (len <= 0) {
        return;
    }
    size_t out_len = (size_t)len < sizeof(g_paibing_uart_buf) ? (size_t)len : (sizeof(g_paibing_uart_buf) - 1);
    (void)uapi_uart_write(UART_BUS_0, (const uint8_t *)g_paibing_uart_buf, (uint32_t)out_len, 0);
}

/* -------------------------------------------------------------------------- */
/* 工具函数                                                                  */
/* -------------------------------------------------------------------------- */
static inline float radians_to_degrees(float rad) {
    return rad * 180.0f / M_PI;
}
static inline float degrees_to_radians(float deg) {
    return deg * M_PI / 180.0f;
}
static inline float normalize_angle(float angle_deg) {
    while (angle_deg > 180.0f) angle_deg -= 360.0f;
    while (angle_deg <= -180.0f) angle_deg += 360.0f;
    return angle_deg;
}

/* -------------------------------------------------------------------------- */
/* Mahony AHRS                                                                */
/* -------------------------------------------------------------------------- */
void MahonyUpdate(float gx, float gy, float gz, float ax, float ay, float az, float dt)
{
    float recipNorm;
    float halfvx, halfvy, halfvz;
    float halfex, halfey, halfez;
    float qa, qb, qc;

    halfvx = q1 * q3 - q0 * q2;
    halfvy = q0 * q1 + q2 * q3;
    halfvz = q0 * q0 - 0.5f + q3 * q3;

    recipNorm = 1.0f / sqrtf(ax * ax + ay * ay + az * az);
    ax *= recipNorm;
    ay *= recipNorm;
    az *= recipNorm;

    halfex = (ay * halfvz - az * halfvy);
    halfey = (az * halfvx - ax * halfvz);
    halfez = (ax * halfvy - ay * halfvx);

    static float exInt = 0.0f, eyInt = 0.0f, ezInt = 0.0f;

    if (TWO_KI > 0.0f) {
        exInt += halfex * dt;
        eyInt += halfey * dt;
        ezInt += halfez * dt;
        gx += TWO_KP * halfex + TWO_KI * exInt;
        gy += TWO_KP * halfey + TWO_KI * eyInt;
        gz += TWO_KP * halfez + TWO_KI * ezInt;
    } else {
        gx += TWO_KP * halfex;
        gy += TWO_KP * halfey;
        gz += TWO_KP * halfez;
    }

    qa = q0;
    qb = q1;
    qc = q2;
    q0 += (-qb * gx - qc * gy - q3 * gz) * (0.5f * dt);
    q1 += (qa * gx + qc * gz - q3 * gy) * (0.5f * dt);
    q2 += (qa * gy - qb * gz + q3 * gx) * (0.5f * dt);
    q3 += (qa * gz + qb * gy - qc * gx) * (0.5f * dt);

    recipNorm = 1.0f / sqrtf(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3);
    q0 *= recipNorm;
    q1 *= recipNorm;
    q2 *= recipNorm;
    q3 *= recipNorm;
}

static void get_euler_from_quat(float* roll, float* pitch, float* yaw)
{
    *roll  = atan2f(2.0f * (q0 * q1 + q2 * q3), 1.0f - 2.0f * (q1 * q1 + q2 * q2));
    *pitch = asinf(2.0f * (q0 * q2 - q3 * q1));
    *yaw   = atan2f(2.0f * (q0 * q3 + q1 * q2), 1.0f - 2.0f * (q2 * q2 + q3 * q3));
}
static void soft_i2c_sda_high(void)
{
    (void)uapi_gpio_set_dir(PAIBING_I2C_SDA_PIN, GPIO_DIRECTION_INPUT);
}

static void soft_i2c_sda_low(void)
{
    (void)uapi_gpio_set_dir(PAIBING_I2C_SDA_PIN, GPIO_DIRECTION_OUTPUT);
    (void)uapi_gpio_set_val(PAIBING_I2C_SDA_PIN, GPIO_LEVEL_LOW);
}

static void soft_i2c_scl_high(void)
{
    (void)uapi_gpio_set_dir(PAIBING_I2C_SCL_PIN, GPIO_DIRECTION_INPUT);
}

static void soft_i2c_scl_low(void)
{
    (void)uapi_gpio_set_dir(PAIBING_I2C_SCL_PIN, GPIO_DIRECTION_OUTPUT);
    (void)uapi_gpio_set_val(PAIBING_I2C_SCL_PIN, GPIO_LEVEL_LOW);
}

/* 位时延：TCXO 微秒延时，满足 I2C 边沿时间 */
static void soft_i2c_delay(void)
{
    (void)uapi_tcxo_delay_us(SOFT_I2C_DELAY_US);
}

/* I2C START：SCL 高时 SDA 由高拉低 */
static void soft_i2c_start(void)
{
    soft_i2c_sda_high();
    soft_i2c_scl_high();
    soft_i2c_delay();
    soft_i2c_sda_low();
    soft_i2c_delay();
    soft_i2c_scl_low();
}

/* I2C STOP：SCL 高时 SDA 由低释放为高 */
static void soft_i2c_stop(void)
{
    soft_i2c_sda_low();
    soft_i2c_delay();
    soft_i2c_scl_high();
    soft_i2c_delay();
    soft_i2c_sda_high();
    soft_i2c_delay();
}

/* 写 8 位 + 读 ACK：ACK 时从机把 SDA 拉低 */
static bool soft_i2c_write_byte(uint8_t val)
{
    for (uint8_t i = 0; i < 8; i++) {
        if ((val & 0x80) != 0) {
            soft_i2c_sda_high();
        } else {
            soft_i2c_sda_low();
        }
        soft_i2c_delay();
        soft_i2c_scl_high();
        soft_i2c_delay();
        soft_i2c_scl_low();
        val <<= 1;
    }

    soft_i2c_sda_high();
    soft_i2c_delay();
    soft_i2c_scl_high();
    soft_i2c_delay();
    bool ack = (uapi_gpio_get_val(PAIBING_I2C_SDA_PIN) == GPIO_LEVEL_LOW);
    soft_i2c_scl_low();
    return ack;
}

/* 读 8 位；ack=true 在第 9 周期拉低 SDA 发 ACK，最后一字节用 NACK */
static uint8_t soft_i2c_read_byte(bool ack)
{
    uint8_t val = 0;
    soft_i2c_sda_high();
    for (uint8_t i = 0; i < 8; i++) {
        val <<= 1;
        soft_i2c_delay();
        soft_i2c_scl_high();
        soft_i2c_delay();
        if (uapi_gpio_get_val(PAIBING_I2C_SDA_PIN) == GPIO_LEVEL_HIGH) {
            val |= 0x01;
        }
        soft_i2c_scl_low();
    }

    if (ack) {
        soft_i2c_sda_low();
    } else {
        soft_i2c_sda_high();
    }
    soft_i2c_delay();
    soft_i2c_scl_high();
    soft_i2c_delay();
    soft_i2c_scl_low();
    soft_i2c_sda_high();
    return val;
}

/* -------------------------------------------------------------------------- */
/* MPU-9250：单寄存器写 / 连续读（使用当前 g_mpu9250_i2c_addr）               */
/* -------------------------------------------------------------------------- */
static errcode_t mpu9250_write_reg(uint8_t reg, uint8_t value)
{
    soft_i2c_start();
    if (!soft_i2c_write_byte((uint8_t)((g_mpu9250_i2c_addr << 1) | 0))) {
        soft_i2c_stop();
        return ERRCODE_FAIL;
    }
    if (!soft_i2c_write_byte(reg)) {
        soft_i2c_stop();
        return ERRCODE_FAIL;
    }
    if (!soft_i2c_write_byte(value)) {
        soft_i2c_stop();
        return ERRCODE_FAIL;
    }
    soft_i2c_stop();
    return ERRCODE_SUCC;
}

/* 典型读序列：写寄存器地址 + Repeated START + 读数据 */
static errcode_t mpu9250_read_regs(uint8_t reg, uint8_t *buf, uint32_t len)
{
    soft_i2c_start();
    if (!soft_i2c_write_byte((uint8_t)((g_mpu9250_i2c_addr << 1) | 0))) {
        soft_i2c_stop();
        return ERRCODE_FAIL;
    }
    if (!soft_i2c_write_byte(reg)) {
        soft_i2c_stop();
        return ERRCODE_FAIL;
    }

    soft_i2c_start();
    if (!soft_i2c_write_byte((uint8_t)((g_mpu9250_i2c_addr << 1) | 1))) {
        soft_i2c_stop();
        return ERRCODE_FAIL;
    }
    for (uint32_t i = 0; i < len; i++) {
        buf[i] = soft_i2c_read_byte(i < (len - 1));
    }
    soft_i2c_stop();
    return ERRCODE_SUCC;
}

/* 传感器寄存器为大端 16 位有符号 */
static int16_t be16_to_i16(uint8_t msb, uint8_t lsb)
{
    return (int16_t)(((uint16_t)msb << 8) | (uint16_t)lsb);
}

/* 探测 WHO_AM_I：引脚必须为 GPIO 模式；在 0x68/0x69 上尝试 */
static bool mpu9250_try_init(void)
{
    uint8_t whoami = 0;
    uint16_t addr_list[2] = { MPU9250_I2C_ADDR_LOW, MPU9250_I2C_ADDR_HIGH };
    bool found = false;

    /* Soft-I2C must run on GPIO mode, not peripheral mux mode. */
    uapi_pin_set_mode(PAIBING_I2C_SCL_PIN, 0);
    uapi_pin_set_mode(PAIBING_I2C_SDA_PIN, 0);
    (void)uapi_pin_set_pull(PAIBING_I2C_SCL_PIN, PIN_PULL_UP);
    (void)uapi_pin_set_pull(PAIBING_I2C_SDA_PIN, PIN_PULL_UP);
    soft_i2c_sda_high();
    soft_i2c_scl_high();

    for (uint8_t i = 0; i < 2; i++) {
        g_mpu9250_i2c_addr = addr_list[i];
        if (mpu9250_read_regs(MPU9250_WHO_AM_I_REG, &whoami, 1) != ERRCODE_SUCC) {
            paibing_uart_printf("whoami read fail addr=0x%02X\r\n", g_mpu9250_i2c_addr);
            continue;
        }
        paibing_uart_printf("whoami=0x%02X addr=0x%02X\r\n", whoami, g_mpu9250_i2c_addr);
        if (whoami == MPU9250_WHO_AM_I_VAL) {
            found = true;
            break;
        }
    }
    if (!found) {
        return false;
    }
    return true;
}

/* 上电复位 + 时钟源 + 量程/滤波等基础配置（与 ±2g、±250dps 等对应） */
static bool mpu9250_init(void)
{
    if (!mpu9250_try_init()) {
        paibing_uart_send("mpu9250 whoami not found on 0x68/0x69\r\n");
        return false;
    }

    (void)mpu9250_write_reg(MPU9250_PWR_MGMT_1_REG, 0x80);
    osal_msleep(100);
    (void)mpu9250_write_reg(MPU9250_PWR_MGMT_1_REG, 0x01);
    (void)mpu9250_write_reg(MPU9250_PWR_MGMT_2_REG, 0x00);
    (void)mpu9250_write_reg(MPU9250_SMPLRT_DIV_REG, 0x07);
    (void)mpu9250_write_reg(MPU9250_CONFIG_REG, 0x03);
    (void)mpu9250_write_reg(MPU9250_GYRO_CONFIG_REG, 0x00);
    (void)mpu9250_write_reg(MPU9250_ACCEL_CONFIG_REG, 0x00);
    (void)mpu9250_write_reg(MPU9250_ACCEL_CONFIG2_REG, 0x03);
    osal_msleep(20);
    paibing_uart_printf("mpu9250 init ok soft-i2c gpio scl=%d sda=%d addr=0x%02X\r\n",
        PAIBING_I2C_SCL_PIN, PAIBING_I2C_SDA_PIN, g_mpu9250_i2c_addr);
    return true;
}

/* 复位键引脚：上拉输入，按下为低 */
static void paibing_reset_key_init(void)
{
    uapi_pin_set_mode(PAIBING_RESET_KEY_PIN, 0);
    (void)uapi_pin_set_pull(PAIBING_RESET_KEY_PIN, PIN_PULL_UP);
    (void)uapi_gpio_set_dir(PAIBING_RESET_KEY_PIN, GPIO_DIRECTION_INPUT);
}

/* 消抖后仍保持低电平则软复位整个应用核 */
static void paibing_check_soft_reset_key(void)
{
    if (uapi_gpio_get_val(PAIBING_RESET_KEY_PIN) != GPIO_LEVEL_LOW) {
        return;
    }
    (void)osal_msleep(PAIBING_RESET_KEY_DEBOUNCE_MS);
    if (uapi_gpio_get_val(PAIBING_RESET_KEY_PIN) != GPIO_LEVEL_LOW) {
        return;
    }
    paibing_uart_send("reset key pressed, soft reboot...\r\n");
    (void)osal_msleep(5);
    hal_reboot_chip();
}

uint32_t paibing_uptime_ms(void)
{
    uint64_t j = osal_get_jiffies();
    uint64_t d = j - g_boot_jiffies0;
    if (d > 0xFFFFFFFFULL) {
        d = 0xFFFFFFFFULL;
    }
    return osal_jiffies_to_msecs((unsigned int)d);
}

void paibing_board_init(void)
{
    paibing_uart_send("\r\n=== Paibing IMU Application Started ===\r\n");
    paibing_uart_printf("Version: %s\r\n", CONFIG_USER_FIRMWARE_VERSION);
    paibing_uart_printf("Build Date: %s %s\r\n", __DATE__, __TIME__);
    paibing_uart_send("Initializing UART...\r\n");
    uart_pin_config_t pins = {
        .tx_pin = S_MGPIO19,
        .rx_pin = S_MGPIO20,
        .cts_pin = PIN_NONE,
        .rts_pin = PIN_NONE,
    };

    uart_attr_t attr = {
        .baud_rate = PAIBING_UART_BAUDRATE,
        .data_bits = UART_DATA_BIT_8,
        .stop_bits = UART_STOP_BIT_1,
        .parity = UART_PARITY_NONE,
    };

    (void)uapi_uart_deinit(UART_BUS_0);
    (void)uapi_uart_init(UART_BUS_0, &pins, &attr, NULL, &g_paibing_uart_buf_cfg);

    paibing_reset_key_init();
    g_boot_jiffies0 = osal_get_jiffies();
}

void paibing_imu_run(const paibing_transport_t *transport)
{
    const paibing_transport_t *tp = transport;
    bool mpu_ok;

    if ((tp == NULL) || (tp->init == NULL) || (tp->push_sensor == NULL)) {
        return;
    }

    tp->init();

    mpu_ok = mpu9250_init();
    /* 从 0x3B 起连续 14 字节：加速度三轴 + 温度两字节 + 陀螺三轴（本版 BLE 不上报温度） */
    (void)memset_s(g_mpu_raw_buf, sizeof(g_mpu_raw_buf), 0, sizeof(g_mpu_raw_buf));
    while (1) {  //lint !e716 Main Loop
        paibing_check_soft_reset_key();
        if (mpu_ok && (mpu9250_read_regs(MPU9250_ACCEL_XOUT_H_REG, g_mpu_raw_buf, sizeof(g_mpu_raw_buf)) == ERRCODE_SUCC)) {
            int16_t ax = be16_to_i16(g_mpu_raw_buf[0], g_mpu_raw_buf[1]);
            int16_t ay = be16_to_i16(g_mpu_raw_buf[2], g_mpu_raw_buf[3]);
            int16_t az = be16_to_i16(g_mpu_raw_buf[4], g_mpu_raw_buf[5]);
            int16_t gx = be16_to_i16(g_mpu_raw_buf[8], g_mpu_raw_buf[9]);
            int16_t gy = be16_to_i16(g_mpu_raw_buf[10], g_mpu_raw_buf[11]);
            int16_t gz = be16_to_i16(g_mpu_raw_buf[12], g_mpu_raw_buf[13]);

            /* 转换为物理单位 */
            float raw_ax = (float)ax / ACCEL_SENSITIVITY;
            float raw_ay = (float)ay / ACCEL_SENSITIVITY;
            float raw_az = (float)az / ACCEL_SENSITIVITY;
            float raw_gx_dps = (float)gx / GYRO_SENSITIVITY;
            float raw_gy_dps = (float)gy / GYRO_SENSITIVITY;
            float raw_gz_dps = (float)gz / GYRO_SENSITIVITY;

            sample_count++;

            /* 零偏校准 */
            if (!calibrated && sample_count <= PAIBING_CALIB_SAMPLES) {
                static float sum_gx = 0, sum_gy = 0, sum_gz = 0;
                sum_gx += raw_gx_dps;
                sum_gy += raw_gy_dps;
                sum_gz += raw_gz_dps;

                if (sample_count % 10 == 0) {
                    paibing_uart_printf("Calibrating... %u/%u samples\r\n",
                        sample_count, PAIBING_CALIB_SAMPLES);
                }

                if (sample_count == PAIBING_CALIB_SAMPLES) {
                    const float n = (float)PAIBING_CALIB_SAMPLES;
                    gyro_bias_x = sum_gx / n;
                    gyro_bias_y = sum_gy / n;
                    gyro_bias_z = sum_gz / n;
                    calibrated = true;
                    filtered_acc_x = raw_ax;
                    filtered_acc_y = raw_ay;
                    filtered_acc_z = raw_az;
                    filtered_total_acc = sqrtf(raw_ax * raw_ax + raw_ay * raw_ay + raw_az * raw_az);
                    paibing_uart_printf("Calibration complete! Bias: gx=%.2f, gy=%.2f, gz=%.2f\r\n", 
                        gyro_bias_x, gyro_bias_y, gyro_bias_z);
                    tp->push_sensor(paibing_uptime_ms(),
                        (int32_t)(raw_ax * 1000), (int32_t)(raw_ay * 1000), (int32_t)(raw_az * 1000),
                        0, 0, 0.0f, 0.0f, filtered_total_acc);
                }
                /* 主循环必须周期性喂狗，否则复位 */
                (void)uapi_watchdog_kick();
                (void)osal_msleep(PAIBING_CALIB_INTERVAL_MS);
                continue;
            }

            /* 低通滤波 */
            filtered_acc_x = FILTER_ALPHA * raw_ax + (1.0f - FILTER_ALPHA) * filtered_acc_x;
            filtered_acc_y = FILTER_ALPHA * raw_ay + (1.0f - FILTER_ALPHA) * filtered_acc_y;
            filtered_acc_z = FILTER_ALPHA * raw_az + (1.0f - FILTER_ALPHA) * filtered_acc_z;
            
            float acc_x = filtered_acc_x;
            float acc_y = filtered_acc_y;
            float acc_z = filtered_acc_z;

            float gyro_x_dps = raw_gx_dps - gyro_bias_x;
            float gyro_y_dps = raw_gy_dps - gyro_bias_y;
            float gyro_z_dps = raw_gz_dps - gyro_bias_z;

            float raw_total_acc = sqrtf(acc_x * acc_x + acc_y * acc_y + acc_z * acc_z);
            filtered_total_acc = FILTER_ALPHA * raw_total_acc + (1.0f - FILTER_ALPHA) * filtered_total_acc;
            float total_acc = filtered_total_acc;

            /* AHRS */
            const float dt = PAIBING_SEND_INTERVAL_MS / 1000.0f;
            float gyro_x_rad = degrees_to_radians(gyro_x_dps);
            float gyro_y_rad = degrees_to_radians(gyro_y_dps);
            float gyro_z_rad = degrees_to_radians(gyro_z_dps);
            MahonyUpdate(gyro_x_rad, gyro_y_rad, gyro_z_rad, acc_x, acc_y, acc_z, dt);

            float roll_rad, pitch_rad, dummy_yaw;
            get_euler_from_quat(&roll_rad, &pitch_rad, &dummy_yaw);
            float roll_deg = normalize_angle(radians_to_degrees(roll_rad));
            float pitch_deg = normalize_angle(radians_to_degrees(pitch_rad));

            /* ±2g：16384 LSB/g → mg；±250dps：131 LSB/dps → mdps（与串口打印一致） */
            int32_t ax_mg = (int32_t)(acc_x * 1000);
            int32_t ay_mg = (int32_t)(acc_y * 1000);
            int32_t az_mg = (int32_t)(acc_z * 1000);
            int32_t gx_mdps = (int32_t)(gyro_x_dps * 1000);
            int32_t gy_mdps = (int32_t)(gyro_y_dps * 1000);
            int32_t gz_mdps = (int32_t)(gyro_z_dps * 1000);

            /* 串口降频打印，避免与 BLE 同时刷屏；末尾 @ 为运行毫秒 */
            if (++g_uart_print_div >= 5) {
                g_uart_print_div = 0;
                paibing_uart_printf("acc(mg):%ld,%ld,%ld gyro(mdps):%ld,%ld,%ld roll:%.1f pitch:%.1f mag:%.2f @%lums\r\n",
                    ax_mg, ay_mg, az_mg, gx_mdps, gy_mdps, gz_mdps, roll_deg, pitch_deg, total_acc, (unsigned long)paibing_uptime_ms());
            }

            tp->push_sensor(paibing_uptime_ms(), ax_mg, ay_mg, az_mg,
                gx_mdps, gy_mdps, roll_deg, pitch_deg, total_acc);
        } else {
            paibing_uart_send("mpu9250 read failed\r\n");
        }
        /* 主循环必须周期性喂狗，否则复位 */
        (void)uapi_watchdog_kick();
        (void)osal_msleep(PAIBING_SEND_INTERVAL_MS);
    }
}

