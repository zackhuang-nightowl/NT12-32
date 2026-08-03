/**
 * @file test_sdcard_upgrade.c
 * @brief SD-card upgrade entry: scan a directory for a *.img and apply it via
 *        the stub HAL_OTA. Covers found/accepted, empty-dir NOTFOUND, and the
 *        case-insensitive suffix match.
 */
#include "nop_sdk/nop_sdcard_upgrade.h"
#include "hal_stub.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define UP_DIR "/tmp/nop_sdcard_upgrade_test"

static int fail(const char *m) { fprintf(stderr, "FAIL: %s\n", m); return 1; }

static void write_file(const char *path, const char *bytes)
{
    FILE *f = fopen(path, "wb");
    if (f) { fputs(bytes, f); fclose(f); }
}

int main(void)
{
    char applied[512];
    nop_status_t status;

    hal_stub_register_all();   /* HAL_OTA.apply_sdcard_image */

    mkdir(UP_DIR, 0777);

    /* Empty directory (remove any stragglers first) → NOTFOUND. */
    remove(UP_DIR "/firmware.IMG");
    remove(UP_DIR "/notes.txt");
    status = nop_sdcard_upgrade_scan(UP_DIR, NULL, 0);
    if (status != NOP_ERR_NOTFOUND)
        return fail("empty dir should be NOTFOUND");

    /* A non-image file must be ignored. */
    write_file(UP_DIR "/notes.txt", "not firmware");
    status = nop_sdcard_upgrade_scan(UP_DIR, NULL, 0);
    if (status != NOP_ERR_NOTFOUND)
        return fail("non-image file should be ignored");

    /* Case-insensitive ".IMG" is found and applied. */
    write_file(UP_DIR "/firmware.IMG", "\x00\x01IMGDATA");
    applied[0] = '\0';
    status = nop_sdcard_upgrade_scan(UP_DIR, applied, sizeof(applied));
    if (status != NOP_OK)
        return fail("image should be found and accepted");
    if (strstr(applied, "firmware.IMG") == NULL)
        return fail("applied path not reported");

    remove(UP_DIR "/firmware.IMG");
    remove(UP_DIR "/notes.txt");

    printf("test_sdcard_upgrade: OK (applied %s)\n", applied);
    return 0;
}
