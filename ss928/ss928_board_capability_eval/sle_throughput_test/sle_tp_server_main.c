#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "sle_tp_server.h"

static volatile uint8_t g_running = 1;
static uint32_t g_report_ms = 1000;

static void on_signal(int signum)
{
    printf("\n%s signal %d, stopping server...\n", SLE_TP_LOG, signum);
    g_running = 0;
}

static void usage(const char *prog)
{
    printf("usage: %s [-i report_ms]\n", prog);
    printf("  -i  stats print interval in ms (default 1000)\n");
}

int main(int argc, char **argv)
{
    int opt;
    uint64_t last_us = 0;

    while ((opt = getopt(argc, argv, "i:h")) != -1) {
        switch (opt) {
        case 'i':
            g_report_ms = (uint32_t)atoi(optarg);
            break;
        case 'h':
        default:
            usage(argv[0]);
            return opt == 'h' ? 0 : 1;
        }
    }
    if (g_report_ms == 0) {
        g_report_ms = 1000;
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    printf("%s throughput server start (Ctrl+C to stop)\n", SLE_TP_LOG);
    sle_tp_server_init();
    last_us = sle_tp_now_us();

    while (g_running) {
        usleep(g_report_ms * 1000U);
        sle_tp_server_print_stats(sle_tp_now_us() - last_us);
        last_us = sle_tp_now_us();
    }

    sle_tp_server_deinit();
    printf("%s throughput server end\n", SLE_TP_LOG);
    return 0;
}
