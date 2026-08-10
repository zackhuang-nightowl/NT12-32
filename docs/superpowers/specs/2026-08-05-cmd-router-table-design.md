# NVR app 命令分派层路由表化 + 出图清理 — 设计 (spec)

日期:2026-08-05
状态:待实现

## 1. 背景与目标

`app/router/nvr_cmd_router.c` 当前用超长 `if (!strcmp(func, ...))` 链(`handle_local`)+
前缀匹配(`is_channel_cmd`)分派界面命令,`nvr_cmd_display.c` / `nvr_cmd_lan.c` 内部也是
if 链。文件杂乱、难维护、含死代码/重复 helper。

`components/nop` 里已有干净的表驱动范本:`nop/nop_router.c`(`register(func,cap,handler)`
精确表)、`onvif/mapping/onvif_map_table.c`(中央映射表 `g_onvif_map_table[]`,一行一个
`onvif_map_<func>` handler)、`onvif/mapping/CONVENTIONS.md`。乱的只是 `app/` 这层。

**目标:**
1. 把 `app/router/` 的 if 链重构成**一张集中静态路由表**(func 字符串 → 命名 handler 函数),
   对齐 nop / nopMapping 约定,便于后续"一点一点补业务"(加一行表 + 一个 handler)。
2. 清理已验证可用的出图链路里的**临时诊断打印**,统一到 `NVR_LOG*`。
3. 只保留当前真正可用的代码,去死代码/重复 helper。

**非目标(明确不做):**
- 不动 `components/nop`(已是表驱动)。
- 不动 `app/` 其它子模块(channel/config/event/ota/record_sched 的内部实现),除非被
  路由重构直接牵连(仅调整调用/包含关系)。
- 不改出图算法逻辑(NAL 分类、align64、commit 成图、clearwin 等已真机 v50 验证的实现原样保留),
  只清打印。

## 2. 决策记录(brainstorming 已确认)

| 项 | 决策 |
|----|------|
| 重构范围 | 仅命令分派层(`app/router/`)+ 出图打印清理 |
| 未实现接口 | 不进表;miss 后按规则透传 / onvifMapping / 回落。无空桩 |
| 表实现 | app 本地表,沿用 nop 命名约定;handler 用 cJSON 入出参、返回 `char*`(不套 nop_router 的 envelope) |
| 表形态 | **集中静态数组,手动加行**(每接口都特殊、无共性,不做运行时自注册) |
| 分派模型 | 本地表优先 → miss 默认透传 / 按 kind onvifMapping;黑名单 = 一小撮"禁止透传"的 func |
| 验证 | 编译通过 + 主机单测;真机出图由用户上板验证 |

## 3. 目标架构

### 3.1 分派次序(`nvr_cmd_dispatch`)

```
parse json → func, args, channel(from args)
  1. row = table_lookup(func)
        命中 → return row->fn(args, ctx)          // 本地白名单
  2. func ∈ BLACKLIST
        → return resp_status(403, "not allowed")  // 禁止盲透传(改道由具体项决定)
  3. channel >= 0:
        kind == onvif  → onvifMapping：nop_app_dispatch(nop, json_in)  // 映射前门
        else           → forward_nop(ip, ...)：透传 POST 设备 /APPJsonCmd
        (通道找不到设备 → 501)
  4. 无 channel → 回落 nop_app_dispatch(nopcore 293 handler) → 501
```

删除:`handle_local` if 链、`nvr_cmd_display_handle`/`nvr_cmd_lan_handle` 内部 if 链、
`is_channel_cmd` 前缀匹配。

### 3.2 表与 handler 约定

```c
/* nvr_cmd_internal.h */
typedef struct {
    nvr_settings_t     *settings;
    nvr_chan_mgr_t     *cm;
    nvr_storage_t      *stg;
    rsdk_group_t       *group;
    nop_app_t          *nop;
    nvr_preview_t      *pv;
    nvr_chan_persist_t *persist;
    int                 dev_nop_port;
} nvr_cmd_ctx_t;

typedef char *(*nvr_cmd_fn)(cJSON *args, const nvr_cmd_ctx_t *ctx); /* 返回 malloc'd 应答 JSON */

typedef struct { const char *func; nvr_cmd_fn fn; } nvr_cmd_route_t;
```

- handler 名 = 对应 NOP func,前缀 `cmd_`:如 `cmd_getDeviceInfo`、
  `cmd_GUI_setDeviceDisplayMode`、`cmd_X_NightOwl_getStorageInfo`。便于对照审计
  (同 mapping/CONVENTIONS §3)。
- 新增业务 = 在 `g_nvr_cmd_table[]` 加一行 + 写一个 `cmd_<func>`,dispatch 零改动。

## 4. 文件布局(app/router/)

| 文件 | 职责 |
|------|------|
| `nvr_cmd_router.c/.h` | dispatch 核心(表查/黑名单/透传/onvifMapping/回落)+ HTTP server + `forward_nop` |
| `nvr_cmd_table.c` | **唯一那张表** `g_nvr_cmd_table[]`,按域分组注释;`nvr_cmd_table_lookup()` |
| `nvr_cmd_internal.h` | `nvr_cmd_ctx_t` / `nvr_cmd_fn` / 全部 handler 声明(按域分组) |
| `nvr_cmd_util.c/.h` | 共享 `resp_status/resp_content/jstr/jint/jbool`(收敛现在两份重复) |
| `nvr_cmd_display.c` | 出图域 handler(已存在,改造) |
| `nvr_cmd_lan.c` | LAN 子设备域 handler(已存在,改造) |
| `nvr_cmd_system.c` | setName/getName/getDeviceInfo/setTimezone/reboot/owner/remoteAccess |
| `nvr_cmd_storage.c` | storageInfo/format/disksHealth/currentStorage |
| `nvr_cmd_cloud.c` | 云存开关/配置 |
| `nvr_cmd_record.c` | 录像触发/开关/推送开关 |
| `nvr_cmd_ota.c` | upgradeFirmware/checkFirmwareUpgradeStatus |
| `nvr_cmd_event.c` | queryEventList/queryEventCalendar |
| `CONVENTIONS.md` | app 路由层约定,镜像 mapping/CONVENTIONS.md |

`nvr_cmd_router.h` 对外接口不变(`nvr_cmd_router_start/stop/port`、纯函数
`nvr_cmd_dispatch(r, json_in)`),保证 `nvr_app.c` 装配与现有单测入口不破。

### 4.1 本地表初始清单(从现有可用实现搬迁,1:1)

- display(nvr_cmd_display.c):`GUI_setDeviceDisplayMode`、`GUI_getDeviceDisplayMode`、
  `GUI_setChannelMapping`、`GUI_getChannelMapping`、`GUI_setDeviceDisplayExt`、
  `GUI_getDeviceDisplayExt`、`GUI_getSysDisplay`、`GUI_setSysDisplay`、
  `X_NightOwl_getChannelStatus`、`GUI_longPolling`、`X_NightOwl_getDeviceCapabilities`、
  `X_NightOwl_setChannelZoomPan`、`X_NightOwl_getChannelZoomPan`
- lan(nvr_cmd_lan.c):现有 `nvr_cmd_lan_handle` 覆盖的 func(实现时逐个列出登记)
- system:`setName`、`getName`、`getDeviceInfo`、`X_NightOwl_setTimezone`、`reboot`、
  `X_NightOwl_setOwner`、`X_NightOwl_getOwner`、`GUI_getRemoteAccessState`、
  `GUI_setRemoteAccessState`
- cloud:`X_NightOwl_setCloudRecordSwitch`、`X_NightOwl_getCloudRecordSwitch`、
  `setCloudRecordConfigs`、`getCloudRecordConfigs`
- record:`X_NightOwl_setChannelRecordingTriggers`、`X_NightOwl_getChannelRecordingTriggers`、
  `X_NightOwl_setChannelsPushNotificationSwitch`、`X_NightOwl_getChannelsPushNotificationSwitch`、
  `X_NightOwl_setChannelRecordingSwitch`、`X_NightOwl_getChannelRecordingSwitch`
- storage:`X_NightOwl_getStorageInfo`、`getStorageInfo`、`formatStorage`、
  `getAllDisksHealth`、`getCurrentStorage`、`setCurrentStorage`
- ota:`upgradeFirmware`、`checkFirmwareUpgradeStatus`
- event:`X_NightOwl_queryEventList`、`X_NightOwl_queryEventCalendar`

> 迁移铁律:逐个 func 行为**逐字保留**当前实现(含默认值、字段名、返回码),仅换外壳。
> 迁移完成后原 `handle_local` / `nvr_cmd_display_handle` if 链整体删除,严禁双份实现漂移。

### 4.2 黑名单

`nvr_cmd_router.c` 里一个显式静态数组(初始为空或极小),带注释说明"未命中但禁止盲透传"的用途;
用户后续按需加行。

## 5. 出图 / 打印清理

范围:`platform/media_hal/`、`components/streaming/`、`app/preview/`、`app/channel/`。

1. `mhal_vout.c` / `mhal_vdec.c` 裸 `printf` → `NVR_LOG*`(`common/include/nvr_log.h`,header-only,
   可直接 include):
   - 失败/错误路径(open fail、start_list fail、get_block fail、HDMI 设置失败)→ `NVR_LOGE`
   - 常态噪声(`apply_window` 每帧行 @ mhal_vout.c:257、`commit: N 窗成图`、`HDMI 实际 WxH`、
     clearwin 提示)→ `NVR_LOGD`(默认 INFO 静默,`NVR_LOG_LEVEL=3` 可开)
2. 删 doc `出图实现_2026-08-05.md` §四.1 点名的临时诊断:happytime `[RTSP-TX/RX/CONN]` fprintf、
   puller 帧 hex dump。实现时在 `components/streaming/**/*.cpp` 及 happytime bm 树精确 grep 定位后删除。
3. **只清打印与死代码,不动出图算法**。

## 6. 验证

- `app/router/` 单独编译通过(app 目标 / 主机侧)。
- 复用并扩展 `app/router/test_nvr_cmd_display.c`;新增对 `nvr_cmd_dispatch` 纯函数的分派测试:
  - 命中本地表 → 返回对应 content
  - miss + 无 channel → 回落(mock nop_app_dispatch)
  - miss + channel(非 onvif)→ 透传路径(mock forward)
  - 黑名单 func → 403
- `ctest` 现有用例(test_nvr_cmd_display / test_nvr_cmd_lan / test_nvr_preview 等)全绿。
- 真机全量交叉编译 + 上板出图由用户验证。

## 7. 风险与缓解

- **行为漂移**:迁移逐字保留 + 删除旧 if 链,单测锁关键 func 应答。
- **平台层 include nvr_log**:`nvr_log.h` header-only 无依赖,`platform/media_hal` 可安全 include;
  若该层不便依赖 common/include,则退化为本地轻量宏包裹(实现时确认 include 路径已含 common/include)。
- **onvifMapping/透传入口**:复用现有 `nop_app_dispatch` 与 `forward_nop`,不新造机制。
