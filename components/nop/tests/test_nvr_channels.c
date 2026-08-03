/**
 * @file test_nvr_channels.c
 * @brief NVR camera channel registry: add/assign, dedup by host+port, get,
 *        ascending list, remove, capacity, find.
 */
#include "nop_sdk/nop_nvr_channels.h"

#include <stdio.h>
#include <string.h>

static int fail(const char *m) { fprintf(stderr, "FAIL: %s\n", m); return 1; }

static void make_entry(nop_nvr_channel_entry_t *e, const char *host, int port, const char *name)
{
    memset(e, 0, sizeof(*e));
    e->channel = -1;                 /* auto-assign */
    e->enabled = 1;
    e->source  = NOP_CAMERA_SOURCE_ONVIF;
    e->port    = port;
    snprintf(e->host, sizeof(e->host), "%s", host);
    snprintf(e->name, sizeof(e->name), "%s", name);
    snprintf(e->stream_uri_main, sizeof(e->stream_uri_main), "rtsp://%s:554/main", host);
}

int main(void)
{
    nop_nvr_channels_t     *reg;
    nop_nvr_channel_entry_t e, out[8];
    int                     ch, n;

    reg = nop_nvr_channels_create(4);
    if (!reg) return fail("create");

    /* Auto-assign channels 0,1,2 for three distinct cameras. */
    make_entry(&e, "192.168.1.10", 8089, "Front"); ch = nop_nvr_channels_add(reg, &e);
    if (ch != 0) return fail("first camera should be channel 0");
    make_entry(&e, "192.168.1.11", 8089, "Back");  ch = nop_nvr_channels_add(reg, &e);
    if (ch != 1) return fail("second camera should be channel 1");
    make_entry(&e, "192.168.1.12", 8089, "Yard");  ch = nop_nvr_channels_add(reg, &e);
    if (ch != 2) return fail("third camera should be channel 2");
    if (nop_nvr_channels_count(reg) != 3) return fail("count should be 3");

    /* Dedup: same host+port returns the existing channel, no new slot. */
    make_entry(&e, "192.168.1.11", 8089, "Back-again");
    if (nop_nvr_channels_add(reg, &e) != 1) return fail("dedup should return channel 1");
    if (nop_nvr_channels_count(reg) != 3) return fail("dedup must not add a slot");

    /* find by host+port. */
    if (nop_nvr_channels_find(reg, "192.168.1.12", 8089) != 2) return fail("find yard");
    if (nop_nvr_channels_find(reg, "10.0.0.1", 8089) != -1) return fail("find absent");

    /* get returns the stored entry. */
    if (!nop_nvr_channels_get(reg, 1, &e)) return fail("get ch1");
    if (strcmp(e.name, "Back") != 0) return fail("ch1 name (original, not dup)");
    if (strcmp(e.stream_uri_main, "rtsp://192.168.1.11:554/main") != 0) return fail("ch1 stream uri");

    /* Remove channel 1, then a new add reuses the freed channel index 1. */
    if (nop_nvr_channels_remove(reg, 1) != NOP_OK) return fail("remove ch1");
    if (nop_nvr_channels_count(reg) != 2) return fail("count after remove");
    if (nop_nvr_channels_remove(reg, 1) != NOP_ERR_NOTFOUND) return fail("double remove NOTFOUND");
    make_entry(&e, "192.168.1.20", 8089, "New"); ch = nop_nvr_channels_add(reg, &e);
    if (ch != 1) return fail("new camera should reuse freed channel 1");

    /* list is ascending by channel. */
    n = nop_nvr_channels_list(reg, out, 8);
    if (n != 3) return fail("list count");
    if (out[0].channel != 0 || out[1].channel != 1 || out[2].channel != 2)
        return fail("list not ascending by channel");
    if (strcmp(out[1].host, "192.168.1.20") != 0) return fail("ch1 now the new camera");

    /* Capacity: 4-slot table already holds 3; add one more, then the 5th fails. */
    make_entry(&e, "192.168.1.30", 8089, "Five"); if (nop_nvr_channels_add(reg, &e) < 0) return fail("4th add");
    make_entry(&e, "192.168.1.31", 8089, "Six");  if (nop_nvr_channels_add(reg, &e) != -1) return fail("5th add should fail (full)");

    nop_nvr_channels_destroy(reg);
    printf("test_nvr_channels: OK (assign/dedup/get/list/remove/reuse/capacity)\n");
    return 0;
}
