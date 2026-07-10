/*
 * WS73 SLE 扫描打印工具 — 主程序（参照原厂 sle_client.c）
 */
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <stdint.h>
#include "sle_seek_print_client.h"

#define SLE_SEEK_SLEEP_SEC 5

static volatile uint8_t g_running = 1;

static void signal_handler(int signum)
{
    printf("\n[SLE_SEEK] signal %d, exiting...\n", signum);
    g_running = 0;
}

static void register_signal(void)
{
    (void)signal(SIGINT, signal_handler);
    (void)signal(SIGTERM, signal_handler);
}

int main(void)
{
    printf("[SLE_SEEK] sle_seek_print_all start (Ctrl+C to stop)\n");
    register_signal();
    sle_seek_print_client_init();

    while (g_running) {
        sleep(SLE_SEEK_SLEEP_SEC);
    }

    sle_seek_print_client_deinit();
    printf("[SLE_SEEK] sle_seek_print_all end\n");
    return 0;
}
