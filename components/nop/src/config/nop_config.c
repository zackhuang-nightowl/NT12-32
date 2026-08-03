/**
 * @file nop_config.c
 * @brief Device provisioning config loader (JSON via the nop_json facade) and
 *        capability-level applier. See nop_sdk/nop_config.h.
 */
#include "nop_sdk/nop_config.h"
#include "base/nop_json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void copy_str(char *dst, size_t cap, const char *src)
{
    if (!src || cap == 0) {
        if (cap)
            dst[0] = '\0';
        return;
    }
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
}

void nop_device_config_init(nop_device_config_t *config)
{
    if (!config)
        return;
    memset(config, 0, sizeof(*config));
    copy_str(config->manufacturer,     sizeof(config->manufacturer),     "NightOwl");
    copy_str(config->model,            sizeof(config->model),            "NOP-Device");
    copy_str(config->firmware_version, sizeof(config->firmware_version), "1.0");
    copy_str(config->hardware_id,      sizeof(config->hardware_id),      "1.0");
    copy_str(config->type,             sizeof(config->type),             "standaloneIpCamera");
    config->max_channel_count = 1;
    config->http_port         = 8089;
    config->ota.auto_update   = 1;

    /* Default supported detection types. */
    config->detection_types[0] = NOP_DETECT_HUMAN;
    config->detection_types[1] = NOP_DETECT_VEHICLE;
    config->detection_types[2] = NOP_DETECT_ANIMAL;
    config->detection_types[3] = NOP_DETECT_PACKAGE;
    config->detection_type_count = 4;
}

/* Read a string field from @p obj into a fixed buffer. */
static void read_str(const nop_json_t *obj, const char *key, char *dst, size_t cap)
{
    const char *v = nop_json_str(obj, key, NULL);
    if (v)
        copy_str(dst, cap, v);
}

nop_status_t nop_device_config_load_json(const char *json_text, size_t length,
                                         nop_device_config_t *config)
{
    nop_json_t *root, *device, *ota, *tutk, *caps;
    if (!json_text || !config)
        return NOP_ERR_PARAM;

    root = nop_json_parse(json_text, length);
    if (!root)
        return NOP_ERR_PARAM;

    /* device identity */
    device = nop_json_get(root, "device");
    if (device) {
        read_str(device, "manufacturer",    config->manufacturer,     sizeof(config->manufacturer));
        read_str(device, "model",           config->model,            sizeof(config->model));
        read_str(device, "firmwareVersion", config->firmware_version, sizeof(config->firmware_version));
        read_str(device, "serialNumber",    config->serial_number,    sizeof(config->serial_number));
        read_str(device, "hardwareId",      config->hardware_id,      sizeof(config->hardware_id));
        read_str(device, "type",            config->type,             sizeof(config->type));
        config->max_channel_count = (int)nop_json_num(device, "maxChannelCount",
                                                       config->max_channel_count);
    }

    config->http_port = (int)nop_json_num(root, "httpPort", config->http_port);

    /* capability level: array of lowercase capability names */
    caps = nop_json_get(root, "capabilities");
    config->capability_count = 0;
    if (caps && nop_json_is_arr(caps)) {
        int n = nop_json_arr_size(caps), i;
        for (i = 0; i < n && config->capability_count < NOP_CAP_MAX; i++) {
            const char *name = nop_json_as_str(nop_json_arr_at(caps, i), NULL);
            if (name) {
                copy_str(config->capabilities[config->capability_count],
                         sizeof(config->capabilities[0]), name);
                config->capability_count++;
            }
        }
    }

    /* supported detection types: { "detection": { "supportedTypes": [...] } } */
    {
        nop_json_t *detection = nop_json_get(root, "detection");
        nop_json_t *types = detection ? nop_json_get(detection, "supportedTypes") : NULL;
        if (types && nop_json_is_arr(types)) {
            int n = nop_json_arr_size(types), i;
            config->detection_type_count = 0;
            for (i = 0; i < n && config->detection_type_count < NOP_DETECT_TYPE_MAX; i++) {
                const char *name = nop_json_as_str(nop_json_arr_at(types, i), NULL);
                nop_detect_type_t type = nop_detect_type_from_name(name);
                if (type != NOP_DETECT_TYPE_MAX)
                    config->detection_types[config->detection_type_count++] = type;
            }
        }
    }

    /* OTA */
    ota = nop_json_get(root, "ota");
    if (ota) {
        read_str(ota, "company",  config->ota.company,  sizeof(config->ota.company));
        read_str(ota, "product",  config->ota.product,  sizeof(config->ota.product));
        read_str(ota, "model",    config->ota.model,    sizeof(config->ota.model));
        read_str(ota, "urlBase",  config->ota.url_base, sizeof(config->ota.url_base));
        config->ota.auto_update = nop_json_bool(ota, "autoUpdate",
                                                config->ota.auto_update ? true : false) ? 1 : 0;
    }

    /* TUTK */
    tutk = nop_json_get(root, "tutk");
    if (tutk) {
        config->tutk.enable = nop_json_bool(tutk, "enable", false) ? 1 : 0;
        read_str(tutk, "uid",        config->tutk.uid,         sizeof(config->tutk.uid));
        read_str(tutk, "authKey",    config->tutk.auth_key,    sizeof(config->tutk.auth_key));
        read_str(tutk, "licenseKey", config->tutk.license_key, sizeof(config->tutk.license_key));
    }

    nop_json_free(root);
    return NOP_OK;
}

nop_status_t nop_device_config_load_file(const char *path, nop_device_config_t *config)
{
    FILE         *file;
    long          size;
    char         *buffer;
    size_t        read_count;
    nop_status_t  status;

    if (!path || !config)
        return NOP_ERR_PARAM;
    file = fopen(path, "rb");
    if (!file)
        return NOP_ERR_IO;
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return NOP_ERR_IO; }
    size = ftell(file);
    if (size < 0 || size > (long)(1u << 20)) { fclose(file); return NOP_ERR_IO; }
    rewind(file);
    buffer = (char *)malloc((size_t)size + 1);
    if (!buffer) { fclose(file); return NOP_ERR_NOMEM; }
    read_count = fread(buffer, 1, (size_t)size, file);
    fclose(file);
    buffer[read_count] = '\0';

    status = nop_device_config_load_json(buffer, read_count, config);
    free(buffer);
    return status;
}

nop_status_t nop_device_config_apply_capabilities(nop_app_t *app,
                                                  const nop_device_config_t *config,
                                                  int exclusive)
{
    int i;
    if (!app || !config)
        return NOP_ERR_PARAM;

    if (exclusive) {
        /* Disable every capability first; core caps stay on by registry policy. */
        for (i = 0; i < NOP_CAP_MAX; i++)
            nop_app_set_capability(app, i, 0);
    }
    for (i = 0; i < config->capability_count; i++) {
        nop_cap_id_t cap = nop_cap_from_name(config->capabilities[i]);
        if (cap != NOP_CAP_MAX)
            nop_app_set_capability(app, (int)cap, 1);
    }
    return NOP_OK;
}
