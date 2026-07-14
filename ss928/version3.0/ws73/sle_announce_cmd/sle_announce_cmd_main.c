#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "sle_announce_cmd.h"

static void usage(const char *prog)
{
    printf("usage: %s start [device_id] [duration_ms]\n", prog);
    printf("       %s stop [device_id] [duration_ms]\n", prog);
    printf("  device_id 0=broadcast to all rackets, 1-254=specific device number\n");
}

int main(int argc, char **argv)
{
    uint8_t dev_id = SLE_PANEL_DEV_ID_ALL;
    int duration_ms = 1800;

    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }
    if (argc >= 3) {
        int v = atoi(argv[2]);
        if (v >= 0 && v <= 254) {
            dev_id = (uint8_t)v;
        }
    }
    if (argc >= 4) {
        duration_ms = atoi(argv[3]);
    }

    return sle_announce_cmd_run(argv[1], dev_id, duration_ms);
}
