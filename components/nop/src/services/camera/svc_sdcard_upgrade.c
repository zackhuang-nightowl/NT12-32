/**
 * @file svc_sdcard_upgrade.c
 * @brief SD-card firmware upgrade entry: find an image under the SD /upgrade
 *        directory and apply it through HAL_OTA. See nop_sdk/nop_sdcard_upgrade.h.
 */
#include "nop_sdk/nop_sdcard_upgrade.h"

#if defined(__unix__) || defined(__APPLE__) || defined(__linux__)

#include "nop_sdk/hal/hal_registry.h"
#include "nop_sdk/hal/hal_ota.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

/* Case-insensitive check that @p name ends with ".img" and has a stem (so a
 * bare ".img" is not matched). */
static int name_has_img_suffix(const char *name)
{
    size_t len = strlen(name);
    const char *ext;
    if (len <= 4)                        /* need at least one stem char + ".img" */
        return 0;
    ext = name + (len - 4);
    return (ext[0] == '.') &&
           (ext[1] == 'i' || ext[1] == 'I') &&
           (ext[2] == 'm' || ext[2] == 'M') &&
           (ext[3] == 'g' || ext[3] == 'G');
}

nop_status_t nop_sdcard_upgrade_scan(const char *upgrade_dir,
                                     char *applied_path, size_t applied_cap)
{
    const hal_ota_if *ota = (const hal_ota_if *)hal_registry_get(HAL_OTA);
    DIR              *dir;
    struct dirent    *entry;
    char              path[512];
    int               found = 0;

    if (!upgrade_dir)
        return NOP_ERR_PARAM;
    if (!ota || !ota->apply_sdcard_image)
        return NOP_ERR_NOTIMPL;

    dir = opendir(upgrade_dir);
    if (!dir)
        return NOP_ERR_NOTFOUND;      /* no /upgrade directory => nothing to do */

    while ((entry = readdir(dir)) != NULL) {
        struct stat st;
        int         written;
        if (!name_has_img_suffix(entry->d_name))
            continue;
        written = snprintf(path, sizeof(path), "%s/%s", upgrade_dir, entry->d_name);
        if (written < 0 || (size_t)written >= sizeof(path))
            continue;                 /* path too long — skip this candidate */
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode))
            continue;                 /* only a regular file is a valid image */
        found = 1;
        break;
    }
    closedir(dir);

    if (!found)
        return NOP_ERR_NOTFOUND;

    if (applied_path && applied_cap > 0) {
        snprintf(applied_path, applied_cap, "%s", path);
    }
    return ota->apply_sdcard_image(ota->ctx, path);
}

#else /* non-POSIX: no directory scanning */

nop_status_t nop_sdcard_upgrade_scan(const char *upgrade_dir,
                                     char *applied_path, size_t applied_cap)
{
    (void)upgrade_dir; (void)applied_path; (void)applied_cap;
    return NOP_ERR_NOTIMPL;
}

#endif
