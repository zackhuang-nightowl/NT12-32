/***************************************************************************************
 *  nvr_gui_config.c — 读写 GUI_CONFIG.json。见 nvr_gui_config.h。
 ***************************************************************************************/
#include "nvr_gui_config.h"
#include "nvr_defaults.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

static char g_path[256] = NVR_DEF_GUI_CONFIG;

void nvr_gui_config_init(const char *config_dir)
{
    const char *cands[3]; int nc = 0; static char cfgpath[256];
    const char *env = getenv("NVR_GUI_CONFIG");
    if (env && env[0]) cands[nc++] = env;
    cands[nc++] = NVR_DEF_GUI_CONFIG;
    if (config_dir) { snprintf(cfgpath, sizeof(cfgpath), "%s/GUI_CONFIG.json", config_dir); cands[nc++] = cfgpath; }
    /* 选第一个已存在的作为读写路径;都不存在则用第一候选(供后续写入创建)。 */
    for (int i = 0; i < nc; i++)
        if (access(cands[i], F_OK) == 0) { snprintf(g_path, sizeof(g_path), "%s", cands[i]); return; }
    snprintf(g_path, sizeof(g_path), "%s", cands[0]);
}

static cJSON *read_root(void)
{
    FILE *f = fopen(g_path, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > 65536) { fclose(f); return NULL; }
    char *buf = malloc((size_t)n + 1); if (!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, (size_t)n, f); fclose(f); buf[rd] = 0;
    cJSON *j = cJSON_Parse(buf); free(buf); return j;
}

int nvr_gui_config_get_display(int *mode, int *page)
{
    int m = NVR_DEF_GUI_MODE, p = NVR_DEF_GUI_PAGE;
    cJSON *r = read_root();
    if (r) {
        cJSON *dm = cJSON_GetObjectItem(r, "displayMode"), *dp = cJSON_GetObjectItem(r, "displayPage");
        if (cJSON_IsNumber(dm)) m = (int)dm->valuedouble;
        if (cJSON_IsNumber(dp)) p = (int)dp->valuedouble;
        cJSON_Delete(r);
    }
    if (mode) *mode = m;
    if (page) *page = p;
    return 0;
}

int nvr_gui_config_get_channels(int *poe_n, int *lan_n)
{
    int poe = NVR_DEF_GUI_POE_N, lan = NVR_DEF_GUI_LAN_N;
    cJSON *r = read_root();
    if (r) {
        cJSON *ch = cJSON_GetObjectItem(r, "channels");
        if (cJSON_IsArray(ch) && cJSON_GetArraySize(ch) >= 2) {
            poe = (int)cJSON_GetArrayItem(ch, 0)->valuedouble;
            lan = (int)cJSON_GetArrayItem(ch, 1)->valuedouble;
        }
        cJSON_Delete(r);
    }
    if (poe_n) *poe_n = poe;
    if (lan_n) *lan_n = lan;
    return 0;
}

int nvr_gui_config_set_display(int mode, int page)
{
    if (mode == 0) return 0;   /* 0=退出 Liveview 的瞬时态,不持久化 */

    cJSON *r = read_root();
    if (!r) r = cJSON_CreateObject();   /* 文件不存在/损坏 → 新建(仅含显示字段) */

    cJSON *dm = cJSON_GetObjectItem(r, "displayMode");
    if (dm) cJSON_SetNumberValue(dm, mode); else cJSON_AddNumberToObject(r, "displayMode", mode);
    cJSON *dp = cJSON_GetObjectItem(r, "displayPage");
    if (dp) cJSON_SetNumberValue(dp, page); else cJSON_AddNumberToObject(r, "displayPage", page);

    char *txt = cJSON_Print(r);   /* pretty:与 LVGL 写入风格一致 */
    cJSON_Delete(r);
    if (!txt) return -1;

    /* 原子写:tmp → fsync → rename */
    char tmp[300]; snprintf(tmp, sizeof(tmp), "%s.tmp", g_path);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { free(txt); return -1; }
    size_t len = strlen(txt);
    int ok = (write(fd, txt, len) == (ssize_t)len);
    free(txt);
    if (ok) fsync(fd);
    close(fd);
    if (!ok) { unlink(tmp); return -1; }
    if (rename(tmp, g_path) != 0) { unlink(tmp); return -1; }
    return 0;
}
