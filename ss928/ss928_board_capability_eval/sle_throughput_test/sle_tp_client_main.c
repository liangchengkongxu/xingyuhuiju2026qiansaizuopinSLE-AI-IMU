#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "sle_tp_client.h"
#include "sle_tp_common.h"

static volatile uint8_t g_running = 1;
static uint32_t g_report_ms = 1000;
static uint16_t g_payload = SLE_TP_DEFAULT_PAYLOAD;
static const uint8_t g_peer_mac[6] = SLE_TP_PEER_MAC;

static void on_signal(int signum)
{
    printf("\n%s signal %d, stopping client...\n", SLE_TP_LOG, signum);
    g_running = 0;
}

static void usage(const char *prog)
{
    printf("usage: %s [-s payload_bytes] [-i report_ms]\n", prog);
    printf("  peer MAC fixed: 20:25:05:29:15:30\n");
    printf("  -s  per-packet payload size (default %d, max %d)\n",
        SLE_TP_DEFAULT_PAYLOAD, SLE_TP_MAX_PAYLOAD);
    printf("  -i  stats print interval in ms (default 1000)\n");
}

int main(int argc, char **argv)
{
    sle_tp_client_config_t cfg;
    char mac_text[24];
    int opt;
    uint64_t last_us = 0;

    while ((opt = getopt(argc, argv, "s:i:h")) != -1) {
        switch (opt) {
        case 's':
            g_payload = (uint16_t)atoi(optarg);
            break;
        case 'i':
            g_report_ms = (uint32_t)atoi(optarg);
            break;
        case 'h':
        default:
            usage(argv[0]);
            return opt == 'h' ? 0 : 1;
        }
    }
    if (g_payload == 0) {
        g_payload = SLE_TP_DEFAULT_PAYLOAD;
    }
    if (g_report_ms == 0) {
        g_report_ms = 1000;
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    cfg.payload_size = g_payload;
    cfg.report_interval_ms = g_report_ms;
    (void)memcpy(cfg.peer_mac, g_peer_mac, sizeof(g_peer_mac));

    sle_tp_format_mac(mac_text, sizeof(mac_text), g_peer_mac);
    printf("%s throughput TX client start, peer=%s (Ctrl+C to stop)\n", SLE_TP_LOG, mac_text);
    sle_tp_client_init(&cfg);
    last_us = sle_tp_now_us();

    while (g_running) {
        usleep(g_report_ms * 1000U);
        sle_tp_client_print_stats(sle_tp_now_us() - last_us);
        last_us = sle_tp_now_us();
    }

    sle_tp_client_deinit();
    printf("%s throughput TX client end\n", SLE_TP_LOG);
    return 0;
}
