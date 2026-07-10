/*
 * WS73：扫描 paibing_imu / MAC CC:92:43:00:A1:00，连接后打印 Notify IMU 文本行
 */
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <stdint.h>
#include "sle_connect_client.h"

#define SLE_IMU_SLEEP_SEC 2

static volatile uint8_t g_running = 1;

static void signal_handler(int signum)
{
    printf("\n[SLE_IMU] signal %d, exiting...\n", signum);
    g_running = 0;
}

static void register_signal(void)
{
    (void)signal(SIGINT, signal_handler);
    (void)signal(SIGTERM, signal_handler);
}

int main(void)
{
    (void)setvbuf(stdout, NULL, _IOLBF, 0);
    (void)setvbuf(stderr, NULL, _IOLBF, 0);
    printf("[SLE_IMU] sle_connect_imu start (Ctrl+C to stop)\n");
    fflush(stdout);
    register_signal();
    sle_connect_client_init();

    while (g_running) {
        sleep(SLE_IMU_SLEEP_SEC);
    }

    sle_connect_client_deinit();
    printf("[SLE_IMU] sle_connect_imu end\n");
    return 0;
}
