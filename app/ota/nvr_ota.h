/***************************************************************************************
 *  nvr_ota.h — NVR 固件自升级（下载/本地 .rom → MD5+版本校验 → A/B 烧写）
 *
 *  包格式（签名包）:
 *    文本头:
 *      nopUpgrade
 *      version=x.y.z
 *      md5=<payload 的 32 位 hex>
 *      force=0|1          # 可选；1=强制升级
 *      <空行>
 *      <squashfs payload>
 *    或以 "nopUpgrade" 起头但无 version 字段：视为强制升级（工厂包）。
 *
 *  规则:
 *    1) 必须校验 payload MD5，不符则失败。
 *    2) 包版本必须高于当前 system.fw_version，除非强制升级。
 *    3) 强制升级：头 force=1，或命令 force/forceUpgrade，或以 nopUpgrade 起头且无 version。
 *    4) 通过后交 /dvr/bin/nvr_do_update.sh 写非活动 rootfs（A/B 备份当前槽）并切槽重启。
 ***************************************************************************************/
#ifndef NVR_OTA_H
#define NVR_OTA_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NVR_OTA_MAGIC "nopUpgrade"

/* src: http(s) URL 或本地路径(FileName)。md5_hex: 32 位 hex（头里有则头优先）。
 * cur_version: 当前固件版本。pkg_version: 无包头时用命令里的 version。
 * force: 跳过「版本必须更高」。返回 0=已启动，-2=忙，<0=参数错。 */
int  nvr_ota_start(const char *src, const char *staging, const char *updater,
                   const char *md5_hex, const char *cur_version,
                   const char *pkg_version, int force);

int  nvr_ota_progress(void);
const char *nvr_ota_state(void);
void nvr_ota_deinit(void);

/* ---- OTA 服务器查询（GUI_checkServerFirmware / 通道查新）----
 * 规则 URL: <base>/<env>/<product>/<model>
 *   base    缺省 NVR_URL_OTA_BASE（app/config/nvr_urls.h）
 *   env     NightOwl_Production（stage 构建为 NightOwl）
 *   product videoRecorder | standaloneIpCamera
 * GET 200 JSON: version / description / url / file_checksum。
 * 版本比较先剥 Model_ 前缀，再按 x.y.z 数值比。 */

typedef struct nvr_ota_meta {
    char version[64];
    char description[2048];
    char url[512];          /* 固件下载 URL（JSON "url"） */
    char checksum[80];      /* file_checksum，可空 */
} nvr_ota_meta_t;

int  nvr_ota_build_check_url(char *out, size_t cap,
                             const char *base, const char *env,
                             const char *product, const char *model);
/* 0=ok；-1=网络；-2=服务器/非 200/坏 JSON。 */
int  nvr_ota_query(const char *url, nvr_ota_meta_t *out);
/* >0 a>b；0 相等；<0 a<b。只比数字段。 */
int  nvr_ota_ver_cmp(const char *a, const char *b);
/* 去掉 model_ 前缀（大小写不敏感）后写入 out。model 可空。 */
void nvr_ota_ver_norm(const char *raw, const char *model, char *out, size_t cap);
/* HTTP(S) 下载到 dst。prog 可空，pct=0..100。0=ok，-1=失败。 */
int  nvr_ota_http_download(const char *url, const char *dst,
                           void (*prog)(int pct, void *ud), void *ud);
int  nvr_ota_file_md5(const char *path, char *out, size_t cap);

#ifdef __cplusplus
}
#endif
#endif /* NVR_OTA_H */
