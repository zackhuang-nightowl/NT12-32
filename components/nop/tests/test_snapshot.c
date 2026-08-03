/**
 * @file test_snapshot.c
 * @brief Snapshot helper: capture via the stub HAL_VIDEO and write a JPEG file.
 */
#include "nop_sdk/nop_snapshot.h"
#include "hal_stub.h"

#include <stdio.h>
#include <string.h>

static int fail(const char *m) { fprintf(stderr, "FAIL: %s\n", m); return 1; }

int main(void)
{
    const char *path = "/tmp/nop_snapshot_test.jpg";
    unsigned char buf[16];
    FILE *f;
    size_t n;

    hal_stub_register_all();   /* provides HAL_VIDEO.capture_snapshot */

    if (nop_snapshot_capture_to_file(0, 0, path) != NOP_OK)
        return fail("capture_to_file");

    f = fopen(path, "rb");
    if (!f) return fail("output file missing");
    n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    remove(path);

    /* stub emits a minimal JPEG (SOI ... EOI). */
    if (n < 2 || buf[0] != 0xFF || buf[1] != 0xD8)
        return fail("not a JPEG (missing SOI)");

    printf("test_snapshot: OK (%zu-byte JPEG written)\n", n);
    return 0;
}
