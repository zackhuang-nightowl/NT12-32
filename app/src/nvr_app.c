/***************************************************************************************
 *  nvr_app.c — 整机编排：config/settings → storage → meta → platform → streaming →
 *              record_sched → preview → event → channel_mgr，串成一台 NVR。
 *
 *  分层：本文件只创建模块、连回调、跑主循环；业务在各 app 模块内，模块间不横向调用。
 *  连续录像由 ③streaming 负责（record=1）；事件/云存由 record_sched 协调；
 *  ②ONVIF 取流以弱符号钩子接入（app/onvif 提供强符号则点亮 PoE 自动取流）。
 ***************************************************************************************/
#include "nvr_app.h"
#include "nvr_config.h"
#include "nvr_storage.h"
#include "nvr_streaming.h"
#include "mhal_vout.h"

#include "nvr_channel.h"
#include "nvr_preview.h"
#include "nvr_record_sched.h"
#include "nvr_event.h"
#include "nvr_settings.h"
#include "nvr_cloud_uploader.h"
#include "nvr_tutk.h"
#include "nvr_netime.h"
#include "nvr_ble.h"
#include "rsdk.h"
#include "nop_sdk/nop_event.h"
#include "nop_sdk/nop_app.h"
#include "nop_sdk/nop_nvr_channels.h"
#include "nvr_cmd_router.h"
#include "nvr_chan_persist.h"

/* ONVIF 映射后端（nop 内部实现，避免拉 nop 内部头，这里前向声明所需 4 个 API） */
typedef struct nop_onvif_map_backend nop_onvif_map_backend_t;
nop_onvif_map_backend_t *nop_onvif_map_backend_create(void *channels);
void nop_onvif_map_backend_destroy(nop_onvif_map_backend_t *backend);
int  nop_onvif_map_events_start(nop_onvif_map_backend_t *backend, void *event_hub);
void nop_onvif_map_events_stop(nop_onvif_map_backend_t *backend);

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct nvr_app {
    nvr_config_t      cfg;
    nvr_settings_t   *settings;
    nvr_storage_t    *stg;
    rsdk_group_t     *group;
    void             *meta;         /* rsdk_meta ctx（云存状态）；可 NULL */
    nvr_stream_mgr_t *sm;
    nop_event_hub_t  *nop_hub;
    nop_app_t        *nop;          /* NOP 分派后端（回落/翻译） */
    nop_nvr_channels_t *nop_chans;  /* 通道注册表（供 ONVIF 映射按 channel 找设备会话） */
    nop_onvif_map_backend_t *onvif_backend;  /* ONVIF 客户端映射后端（逐通道会话缓存+事件轮询）*/
    nvr_cmd_router_t *router;       /* 127.0.0.1:8089 命令路由（本地存/查 + channel→设备转发）*/
    nvr_rec_sched_t  *rs;
    nvr_preview_t    *pv;
    nvr_chan_persist_t *persist;    /* 通道映射/能力持久化(channels.json) */
    nvr_evt_hub_t    *eh;
    nvr_chan_mgr_t   *cm;
    nvr_cloud_uploader_t *up;       /* 云存上传器（有盘+meta+udid 时启动） */
    nvr_ble_t        *ble;          /* BLE 配网通路（复用命令路由；板级链路真机接入） */
    int               tutk_on;      /* TUTK P2P 已启动 */
    volatile int      running;
};

/* ② 未实现时的弱兜底：返回 -1，onvif_auto 通道保持待定 */
__attribute__((weak))
int nvr_onvif_get_url(const char *ip, int port, const char *user, const char *pass,
                      const char *stream, char *out, int out_size)
{
    (void)ip; (void)port; (void)user; (void)pass; (void)stream; (void)out; (void)out_size;
    return -1;
}

/* ---------- 模块间回调（app 编排，模块不横向 include） ---------- */
static void on_chan_online(void *user, int chn)
{
    nvr_app_t *a = user;
    nvr_rec_channel_up(a->rs, chn, NVR_CODEC_AUTO);
    nvr_preview_on_channel_online(a->pv, chn);
}
static void on_chan_offline(void *user, int chn)
{
    nvr_app_t *a = user;
    nvr_preview_on_channel_offline(a->pv, chn);
}
static void on_evt_icon(void *user, int chn, unsigned bits)
{
    nvr_app_t *a = user;
    nvr_preview_set_icons(a->pv, chn, bits);
}
static void on_storage_evt(nvr_stg_evt_t e, const nvr_disk_t *d, void *user)
{
    nvr_app_t *a = user;
    const char *p = d ? d->path : "(group)";
    switch (e) {
        case NVR_STG_EVT_DISK_FAILED: printf("[storage] 盘故障: %s\n", p); break;
        case NVR_STG_EVT_DISK_REMOVED:printf("[storage] 盘掉线: %s\n", p); break;
        case NVR_STG_EVT_DISK_ADDED:  printf("[storage] 新盘插入, 需重扫\n"); break;
        case NVR_STG_EVT_FULL:        printf("[storage] 盘组满(策略生效)\n"); break;
        case NVR_STG_EVT_NEED_FORMAT: printf("[storage] 空盘/外来盘, 待格式化: %s\n", p); break;
        default: break;
    }
    if (a && a->rs) nvr_rec_on_storage_evt(a->rs, e);
}

static void apply_remote_access(nvr_app_t *a);   /* 前置声明（下方定义） */

/* 设置库变更 → 云存开关/stoken + 远程访问门控（cloud.* / nop_owner / service.remote_access） */
static void on_settings_change(void *user, const char *key)
{
    nvr_app_t *a = user;
    if (!a->settings) return;
    if (a->up && strncmp(key, "cloud.", 6) == 0)
        nvr_cloud_uploader_set_switch(a->up, nvr_settings_get_int(a->settings, "cloud.switch", 0));
    if (strncmp(key, "nop_owner", 9) == 0) {
        nvr_owner_row_t ow;
        if (a->up && nvr_settings_owner_get(a->settings, &ow) == 0)
            nvr_cloud_uploader_set_stoken(a->up, ow.stoken);
        apply_remote_access(a);   /* 绑定/解绑 NOP 账户 → 重算 BLE+P2P 门控 */
    }
    if (strncmp(key, "service.remote_access", 21) == 0)
        apply_remote_access(a);   /* 本地 admin 运行时开关 */
}

/* 有盘 + meta + udid 就绪 → 启动云存上传器，初值取自设置库 */
static void maybe_start_uploader(nvr_app_t *a, const char *config_dir)
{
    (void)config_dir;
    if (!a->group || !a->meta) return;
    char udid[64]; nvr_settings_get_str(a->settings, "system.udid", udid, sizeof(udid), "");
    if (!udid[0]) { printf("[app] 云存: 未配置 UID, 上传器未启动\n"); return; }

    nvr_owner_row_t ow; memset(&ow, 0, sizeof(ow));
    if (a->settings) nvr_settings_owner_get(a->settings, &ow);

    nvr_cloud_uploader_cfg_t uc = {
        .group = a->group, .meta = a->meta, .udid = udid, .stoken = ow.stoken,
        .stage = nvr_settings_get_int(a->settings, "cloud.stage", 0),
        .worker_count = 2, .poll_interval_s = 5, .slice_ms = 15000,
    };
    if (nvr_cloud_uploader_start(&uc, &a->up) == 0) {
        nvr_cloud_uploader_set_switch(a->up, nvr_settings_get_int(a->settings, "cloud.switch", 0));
        nvr_settings_subscribe(a->settings, "cloud.", on_settings_change, a);
        /* nop_owner 由 nvr_app_start 统一订阅（同时驱动 stoken 与远程访问门控） */
        printf("[app] 云存上传器已启动(UID=%s)\n", udid);
    }
}

static pv_layout_t pv_layout_of(int n)
{
    return (n <= 1) ? PV_L1 : (n <= 4) ? PV_L4 : (n <= 6) ? PV_L6 :
           (n <= 9) ? PV_L9 : (n <= 12) ? PV_L12 : PV_L16;
}

/* ---- 远程访问(BLE+TUTK) 账户门控 ---- */
/* 有效状态：绑 NOP 账户→常开；出厂(无本地账户)→常开；仅本地 admin→运行时开关(service.remote_access)。*/
static int remote_access_effective(nvr_app_t *a)
{
    nvr_owner_row_t ow;
    if (nvr_settings_owner_get(a->settings, &ow) == 0 && ow.owner_id[0]) return 1;  /* NOP 账户 */
    nvr_auth_row_t au;
    if (nvr_settings_auth_get(a->settings, &au) < 0) return 1;                        /* 出厂无本地账户 */
    return nvr_settings_get_int(a->settings, "service.remote_access", 0);             /* 本地 admin 运行时 */
}

/* BLE 命令桥：组包完成的一整条 NOP JSON → 复用 8089 命令路由 → 应答(malloc)。
 * enc 由路由/envelope 层按 HTTP 同样方式处理，这里透传原文。 */
static char *ble_dispatch_bridge(void *ud, const char *json, int enc)
{
    (void)enc;
    nvr_app_t *a = (nvr_app_t *)ud;
    if (!a || !a->router) return NULL;
    return nvr_cmd_dispatch(a->router, json);
}

static void start_tutk(nvr_app_t *a)
{
    if (a->tutk_on) return;
    char uid[64], authkey[64];
    nvr_settings_get_str(a->settings, "tutk.uid", uid, sizeof(uid), "");
    nvr_settings_get_str(a->settings, "tutk.authkey", authkey, sizeof(authkey), "");
    if (!uid[0]) return;                                   /* 无 UID：无法启 P2P */
    if (nvr_tutk_init(uid, authkey[0] ? authkey : NULL) == 0 && nvr_tutk_start() == 0) {
        a->tutk_on = 1;
        printf("[app] TUTK P2P 已启动(UID=%s)\n", uid);
    } else {
        printf("[app] 警告: TUTK P2P 启动失败\n");
    }
}
static void stop_tutk(nvr_app_t *a)
{
    if (!a->tutk_on) return;
    nvr_tutk_stop(); nvr_tutk_deinit(); a->tutk_on = 0;
    printf("[app] TUTK P2P 已停止\n");
}

static void start_ble(nvr_app_t *a)
{
    if (a->ble) return;
    nvr_ble_cfg_t bc; memset(&bc, 0, sizeof(bc));
    bc.dispatch = ble_dispatch_bridge; bc.dispatch_ud = a; bc.mtu = 40;
    nvr_settings_get_str(a->settings, "system.model", bc.model,  sizeof(bc.model),  a->cfg.sys.model);
    nvr_settings_get_str(a->settings, "system.mac",   bc.mac,    sizeof(bc.mac),    "");
    nvr_settings_get_str(a->settings, "system.sn",    bc.serial, sizeof(bc.serial), "");
    if (nvr_ble_create(&bc, &a->ble) == 0) {
        nvr_owner_row_t ow; int bound = (nvr_settings_owner_get(a->settings, &ow) == 0 && ow.owner_id[0]);
        nvr_ble_update_adv(a->ble, bound, 0 /*启动即锁定*/);
        /* TODO(板级): 注册 BlueZ GATT Service 0xFFF0 / RX 0xFFF1(write→nvr_ble_on_rx) /
         *   TX 0xFFF2(notify)；把 link.ud/notify/set_adv 指到板级 BLE 栈。 */
    }
}
static void stop_ble(nvr_app_t *a)
{
    if (!a->ble) return;
    nvr_ble_destroy(a->ble); a->ble = NULL;
    printf("[app] BLE 通路已停止\n");
}

/* 依据账户门控启停 BLE+TUTK（幂等）。设置库变更或绑定态变化时调用。 */
static void apply_remote_access(nvr_app_t *a)
{
    int eff = remote_access_effective(a);
    if (eff) { start_tutk(a); start_ble(a); }
    else     { stop_tutk(a);  stop_ble(a);  }
    printf("[app] 远程访问(BLE+P2P) = %s\n", eff ? "开" : "关");
}

int nvr_app_start(const char *config_dir, nvr_app_t **out)
{
    if (!config_dir || !out) return -1;
    nvr_app_t *a = calloc(1, sizeof(*a));
    if (!a) return -1;

    /* 1) 只读 JSON 配置 */
    if (nvr_config_load(config_dir, &a->cfg) != 0) {
        printf("[app] 配置加载失败(%s)\n", config_dir); free(a); return -1;
    }

    /* 2) 运行期设置库（首启从 JSON 播种）+ overlay 覆盖 */
    char dbpath[512];
    snprintf(dbpath, sizeof(dbpath), "%s/nvr_settings.db", config_dir);
    if (nvr_settings_open(dbpath, config_dir, &a->settings) == 0)
        nvr_config_overlay_from_settings(&a->cfg, a->settings);
    else
        printf("[app] 警告: 设置库打开失败(%s), 用只读 JSON\n", dbpath);

    printf("[app] %s: %d 通道(cap=%d, PoE=%d, IP=%d)\n", a->cfg.sys.model, a->cfg.nch,
           a->cfg.sys.capacity, a->cfg.sys.poe_ports, a->cfg.sys.ip_channels);

    /* 2.5) 网络与时间落地：eth0 DHCP(默认)/静态、eth1 PoE VLAN+DHCP 服务、时区+NTP(UTC)
     *      —— 必须在发现/取流之前，保证 eth1 各 PoE 网段可达。 */
    if (a->settings) {
        nvr_net_apply(a->settings);
        nvr_time_apply(a->settings);
    }

    /* 3) 存储：init → scan → assemble 盘组 */
    a->cfg.storage.cb = on_storage_evt;
    a->cfg.storage.cb_user = a;
    if (nvr_storage_init(&a->cfg.storage, &a->stg) != RSDK_OK) { printf("[app] storage init 失败\n"); goto fail; }
    printf("[app] 发现 %d 块盘\n", nvr_storage_scan(a->stg));
    if (nvr_storage_assemble(a->stg, &a->group) != RSDK_OK) {
        printf("[app] 警告: 盘组装配失败, 录像禁用, 仅预览\n"); a->group = NULL;
    }

    /* 4) 元数据库（云存状态/事件/抓拍）：<config_dir>/meta.db */
#if RSDK_CFG_METADATA
    {
        char metapath[512];
        snprintf(metapath, sizeof(metapath), "%s/meta.db", config_dir);
        if (rsdk_meta_open(metapath, &a->meta) != RSDK_OK) {
            printf("[app] 警告: meta 打开失败(%s), 云存状态禁用\n", metapath); a->meta = NULL;
        }
    }
#endif

    /* 5) 平台显示 */
    if (mhal_vout_init(MHAL_OUT_HDMI, a->cfg.sys.hdmi_w, a->cfg.sys.hdmi_h) != 0)
        printf("[app] 警告: 显示初始化失败(非目标机?)\n");

    /* 6) 拉流管理器 */
    a->cfg.stream.group = a->group;
    if (nvr_stream_mgr_init(&a->cfg.stream, &a->sm) != RSDK_OK) { printf("[app] streaming init 失败\n"); goto fail; }

    /* 7) 事件脊柱 + NOP 服务端(8089，界面交互) + 录像调度 + 预览 + 事件中枢 */
    a->nop_hub = nop_event_hub_create();
    {
        nop_app_config_t nc; memset(&nc, 0, sizeof(nc)); nc.role = NOP_ROLE_NVR;
        a->nop = nop_app_create(&nc);
        if (a->nop) nop_app_set_event_hub(a->nop, a->nop_hub);
        /* 8089 命令路由在通道管理器就绪后启动（见 §8 后）——它需要 cm 做 channel→设备 解析 */
    }

    nvr_rec_sched_cfg_t rc = { .group = a->group, .meta = a->meta,
                               .hdd_full_policy = a->cfg.storage.hdd_full, .post_record_s = 10 };
    nvr_rec_sched_init(&rc, &a->rs);

    nvr_preview_cfg_t pc = { .sm = a->sm, .osd_name = 1, .osd_datetime = 1,
                             .hdmi_w = a->cfg.sys.hdmi_w, .hdmi_h = a->cfg.sys.hdmi_h };
    nvr_preview_init(&pc, &a->pv);
    nvr_preview_set_layout(a->pv, pv_layout_of(a->cfg.sys.default_layout));

    /* 通道映射/能力持久化：打开 channels.json，载入映射到 preview（断电重启后沿用上次映射） */
    a->persist = nvr_chan_persist_open(config_dir);
    if (a->persist) {
        int map[NVR_PERSIST_MAX_CH];
        int n = nvr_chan_persist_get_mapping(a->persist, map, NVR_PERSIST_MAX_CH);
        if (n > 0) nvr_preview_set_mapping(a->pv, map, n);
    }

    nvr_evt_cfg_t ec = { .nop_hub = a->nop_hub, .rs = a->rs, .user = a, .on_icon = on_evt_icon };
    nvr_evt_init(&ec, &a->eh);

    /* 8) 通道管理器：载入配置 → 起流（回调驱动 rec/preview） */
    nvr_chan_mgr_cfg_t cc = { .sm = a->sm, .reconnect_base_s = 5, .reconnect_max_s = 30,
                              .user = a, .on_online = on_chan_online, .on_offline = on_chan_offline };
    nvr_chan_mgr_init(&cc, &a->cm);
    nvr_chan_load_config(a->cm, &a->cfg);
    nvr_chan_start_all(a->cm);

    /* 8a) PoE/LAN 自动发现绑定（初次）：ONVIF WS-Discovery → 分类 → 绑定 → 取流出图 */
    {
        char dip[64]; nvr_settings_get_str(a->settings, "network.discovery_ip", dip, sizeof(dip), "");
        int nb = nvr_chan_run_discovery(a->cm, dip[0] ? dip : NULL, 2);
        printf("[app] 初次发现绑定 %d 台相机\n", nb);
    }

    /* 8b) ONVIF 映射后端：把每通道 host/凭据/backend 注册进 nop_nvr_channels，
     *     建映射后端并挂到 nop_app；通用 ONVIF 通道的控制命令经此翻译成 ONVIF SOAP 发相机。
     *     并启动 ONVIF 事件轮询 → nop_event_hub（AI 事件进事件中枢）。 */
    if (a->nop) {
        a->nop_chans = nop_nvr_channels_create(NVR_MAX_CH);
        nvr_channel_t clist[NVR_MAX_CH]; int cn = nvr_chan_list(a->cm, clist, NVR_MAX_CH);
        for (int i = 0; i < cn; i++) {
            nop_nvr_channel_entry_t e; memset(&e, 0, sizeof(e));
            e.channel = clist[i].chn; e.enabled = clist[i].enabled ? 1 : (clist[i].chn >= 0);
            snprintf(e.host, sizeof(e.host), "%s", clist[i].onvif_ip);
            e.port = clist[i].onvif_port > 0 ? clist[i].onvif_port : 80;
            snprintf(e.username, sizeof(e.username), "%s", clist[i].user[0] ? clist[i].user : "admin");
            snprintf(e.password, sizeof(e.password), "%s", clist[i].pass);
            snprintf(e.name, sizeof(e.name), "%s", clist[i].name);
            e.backend = (clist[i].kind == 2 /*ONVIF*/) ? NOP_BACKEND_ONVIF : NOP_BACKEND_NOP;
            if (e.host[0]) nop_nvr_channels_add(a->nop_chans, &e);
        }
        nop_app_set_nvr_channels(a->nop, a->nop_chans);
        a->onvif_backend = nop_onvif_map_backend_create(a->nop_chans);
        if (a->onvif_backend) {
            nop_app_set_onvif_backend(a->nop, a->onvif_backend);
            nop_onvif_map_events_start(a->onvif_backend, a->nop_hub);  /* ONVIF 事件→事件中枢 */
            printf("[app] ONVIF 映射后端就绪(%d 通道注册, 事件轮询启动)\n", cn);
        }
    }

    /* 8b) 8089 命令路由（本地存/查设置库 + channel→真实设备 转发/翻译；回落 nop_app） */
    {
        nvr_cmd_router_cfg_t rc = { .settings = a->settings, .cm = a->cm,
                                    .stg = a->stg, .group = a->group, .meta = a->meta, .nop = a->nop,
                                    .port = nvr_settings_get_int(a->settings, "system.nop_port", 8089),
                                    .dev_nop_port = 8089,
                                    .pv = a->pv, .persist = a->persist };
        if (nvr_cmd_router_start(&rc, &a->router) != 0)
            printf("[app] 警告: 命令路由(8089) 启动失败\n");
    }

    /* 9) 云存上传器（有盘+meta+UID 时） */
    maybe_start_uploader(a, config_dir);

    /* 10) 远程访问(BLE 配网 + TUTK P2P) —— **账户门控**，不是无条件常开：
     *      · 出厂(本地账户系统为空) → 常开，供 APP/向导 BLE 发现绑定
     *      · 已绑 NOP 账户(owner) → 常开
     *      · 仅本地 admin(无 NOP 账户) → 默认关，经 GUI_setRemoteAccessState 运行时开关控制
     *      运行时开关断电丢失：本地 admin 态启动即复位为关。 */
    {
        nvr_owner_row_t ow2; nvr_auth_row_t au2;
        int bound = (nvr_settings_owner_get(a->settings, &ow2) == 0 && ow2.owner_id[0]);
        int has_admin = (nvr_settings_auth_get(a->settings, &au2) >= 0);
        if (!bound && has_admin)
            nvr_settings_set_int(a->settings, "service.remote_access", 0);  /* 复位运行时开关 */
    }
    nvr_settings_subscribe(a->settings, "nop_owner",             on_settings_change, a);
    nvr_settings_subscribe(a->settings, "service.remote_access", on_settings_change, a);
    apply_remote_access(a);

    a->running = 1;
    *out = a;
    printf("[app] 启动完成\n");
    return 0;

fail:
    nvr_app_stop(a);
    return -1;
}

void nvr_app_run(nvr_app_t *app)
{
    if (!app) return;
    unsigned tick = 0;
    while (app->running) {
        if (app->stg) nvr_storage_tick(app->stg);   /* 盘健康/满盘/热插拔 */
        nvr_chan_tick(app->cm);                      /* 在线状态机 + 重连 + 解析待定 URL */
        nvr_rec_tick(app->rs);                       /* 结束到期事件时窗 */
        nvr_preview_tick(app->pv);                   /* 时间 OSD */
        nvr_evt_tick(app->eh);                        /* 图标衰减 */
        if (tick % 60 == 0 && app->settings)          /* 每 60s：NTP 未成功则重试(UTC) */
            nvr_time_tick(app->settings);
        if (++tick % 30 == 0)                         /* 每 30s 重扫，捕获新插入的 PoE/LAN 相机 */
            nvr_chan_run_discovery(app->cm, NULL, 2);
        sleep(1);
    }
}

void nvr_app_request_exit(nvr_app_t *app) { if (app) app->running = 0; }

void nvr_app_stop(nvr_app_t *app)
{
    if (!app) return;
    if (app->ble) { nvr_ble_destroy(app->ble); app->ble = NULL; }
    if (app->tutk_on) { nvr_tutk_stop(); nvr_tutk_deinit(); app->tutk_on = 0; }
    if (app->up) { nvr_cloud_uploader_stop(app->up); app->up = NULL; }
    if (app->cm) { nvr_chan_stop_all(app->cm); nvr_chan_mgr_deinit(app->cm); app->cm = NULL; }
    if (app->eh) { nvr_evt_deinit(app->eh); app->eh = NULL; }
    if (app->pv) { nvr_preview_deinit(app->pv); app->pv = NULL; }
    if (app->persist) { nvr_chan_persist_close(app->persist); app->persist = NULL; }
    if (app->rs) { nvr_rec_sched_deinit(app->rs); app->rs = NULL; }
    if (app->nop_hub) { nop_event_hub_destroy(app->nop_hub); app->nop_hub = NULL; }
    if (app->router) { nvr_cmd_router_stop(app->router); app->router = NULL; }
    if (app->onvif_backend) { nop_onvif_map_events_stop(app->onvif_backend);
                              nop_onvif_map_backend_destroy(app->onvif_backend); app->onvif_backend = NULL; }
    if (app->nop)    { nop_app_destroy(app->nop); app->nop = NULL; }
    if (app->nop_chans) { nop_nvr_channels_destroy(app->nop_chans); app->nop_chans = NULL; }
    if (app->sm) { nvr_stream_stop_all(app->sm); nvr_stream_mgr_deinit(app->sm); app->sm = NULL; }
    mhal_vout_deinit(MHAL_OUT_HDMI);
#if RSDK_CFG_METADATA
    if (app->meta) { rsdk_meta_close(app->meta); app->meta = NULL; }
#endif
    if (app->stg) { nvr_storage_deinit(app->stg); app->stg = NULL; }
    if (app->settings) { nvr_settings_close(app->settings); app->settings = NULL; }
    free(app);
}
