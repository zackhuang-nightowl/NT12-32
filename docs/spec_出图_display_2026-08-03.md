# 设计规格 — 出图（LVGL 显示指令 → NVR 视频输出）

> 日期：2026-08-03　子项目：② 出图（三块之一，独立 spec）
> 定位：**NVR 纯服务端**。LVGL（独立进程）经 8089 `/APPJsonCmd` 下发 NOP 显示指令，
> NVR 据此驱动解码视频贴到显示层。本 spec 只覆盖"出图"，不含事件上报①、录像③。
> 验证方式：主机单测纯逻辑 + 串口真机验证贴图（mhal 依赖 BSP，不能主机跑）。

---

## 1. 目标与验收

**目标**：真机上 LVGL 进入 LiveView 主页后，能看到真实相机视频按其宫格布局/映射正确显示；
换布局/翻页/拖拽换位/双击全屏/悬浮块/离开页面 均由对应 NOP 指令驱动 NVR 正确出图。

**验收（串口真机）**：
1. 开机进主页，HDMI/显示器点亮，多宫格显示各通道**子码流**画面。
2. `GUI_setDeviceDisplayMode{4,1}` / `{9,1}` / `{16,1}` → 屏幕切 4/9/16 宫格，各格显示正确通道。
3. `GUI_setDeviceDisplayMode{mode,2}` 翻页 → 显示下一段通道。
4. `GUI_setChannelMapping[...]` 交换 → 对应格通道互换，**重启后保持**（持久化生效）。
5. 双击某格（LVGL 发 `GUI_setDeviceDisplayMode{1,page}`）→ 单格全屏且切**主码流**；退出恢复多宫格子码流。
6. `GUI_setDeviceDisplayExt{...}` → 指定千分比位置出现悬浮视频块；`channels:[]` → 消失。
7. `GUI_setDeviceDisplayMode{0,0}` → 停所有解码（离开 LiveView）。
8. `X_NightOwl_getChannelStatus{channel}` 返回真实状态，**全 0-7**（0无相机/1活动/2休眠/3断连/4鉴权失败/5分辨率超限/6固件升级中/7未激活），随实际交互更新。
9. `X_NightOwl_getDeviceCapabilities` 返回每通道能力集（**从相机取回后组装**，缓存进 channels.json）。

---

## 2. 行为模型（依据 nop_api_doc/LiveView，已核实）

**四要素合成**（屏幕第 k 格，k 从 0 起）：
```
格k矩形   = NVR 按 displayMode 均分全屏自算（GUI 不下发格坐标）
格k通道   = ChannelMapping[(displayPage-1)*displayMode + k]     // 值 1-based
格k码流   = 多宫格→子码流；单格全屏(displayMode=1)→主码流       // 无独立字段，由布局隐含
```
- `displayMode` ∈ {1,4,9,16}；**特殊值 0 = 离开 LiveView → 停所有解码**。
- `displayPage`：页偏移（1-based）。
- `ChannelMapping`：数组下标=宫格序号，值=通道号(1-based)；**NVR 持久化**，GUI 仅开机 load 一次。
- `displayExt`：独立叠加层、顶层；GUI 给千分比 x/y/w/h(0~1000) + channel + streamType(main/sub)；
  与宫格可共存，实际用法多为先 `displayMode:0` 退 LiveView 再用。
- `SysDisplay`：分辨率固定 1080P（scale up/down），返回 resolutionList/resolution/opacity/fb。

**开机指令序列**（LVGL→NVR）：`GUI_getSysDisplay` → `GUI_getChannelMapping` →
`GUI_setDeviceDisplayMode`(真正开始贴视频) → `X_NightOwl_getChannelStatus`(逐通道) →
`X_NightOwl_getDeviceCapabilities` → `GUI_longPolling` 订阅。

**坐标系**：displayExt 千分比相对全屏（x/w 相对屏宽，y/h 相对屏高）。宫格坐标 NVR 均分自算。

**通道号**：GUI 全程 **1-based**；NVR 内部（nvr_preview/nvr_channel）**0-based** → 边界统一转换。

---

## 3. 架构（三层，已获认可）

现状是"两头都在、中间没桥"：LVGL 显示指令落在 nop 静态 handler（不驱动视频），
app 的 `nvr_preview→mhal_vout` 能驱动视频却无人调用。方案：在 **app 路由层**把指令接到 preview，
并补 mhal HDMI 输出与任意矩形能力。nop 静态 handler 退化为兜底（不再被 8089 路由命中）。

```
LVGL ──8089 /APPJsonCmd──▶ app/router/nvr_cmd_router (L3: 拦截显示指令)
                                    │  驱动
                                    ▼
                          app/preview/nvr_preview (L2: 布局/映射/悬浮块/码流编排 + 持久化)
                                    │  调用
                                    ▼
                     platform/media_hal/mhal_vout (L1: HDMI 输出 + 宫格/任意矩形贴图)
```

### L1 — `platform/media_hal/mhal_vout.{h,c}`
1. **补 HDMI 输出时序/模式**：`mhal_vout_init` 现 TODO；用 `hd_videoout_set(ctrl_path, HD_VIDEOOUT_PARAM_DEVCONFIG, …)` + OUTPUT 配置输出制式（参照 BSP 样例 `display_with_change_mode.c`）。分辨率入参来自 `system.json`（默认 1920×1080）。**这步不做屏不亮**。
2. **新增任意矩形绑定** `int mhal_vout_bind_rect(int decoder_chn, int x, int y, int w, int h)`（像素）。把现有 `mhal_vout_bind`(宫格) 重构为"`mhal_layout_rect` 算矩形 → 调共用内部 `apply_window()`"，两者复用同一下发路径（videoproc OUT rect + videoout IN_WIN_ATTR）。
3. **新增停窗** `int mhal_vout_unbind(int win_or_chn)`：隐藏窗口（`visible=0`）供 displayMode=0 / displayExt 清除用。
4. OSD（`mhal_vout_osd`）保持桩，本 spec 不做（见 §6 后续）。

### L2 — `app/preview/nvr_preview.{h,c}`
1. **状态扩展**：新增 `int display_mode`(1/4/9/16/0)、`int display_page`(1-based)、
   `int channel_map[NVR_MAX_CH]`(格→通道,0-based,-1空)、每窗 `int stream[PV_MAX_WIN]`。
2. **重排逻辑改为按映射**：窗 k 通道 = `channel_map[(display_page-1)*win_count + k]`（替换现在 `page*win_count+k` 的顺序映射）。
3. **新 API**：
   - `int nvr_preview_set_mode(nvr_preview_t*, int display_mode, int display_page)`：mode=0 → `mhal_vout_unbind` 全部 + 停解码；否则设布局+按映射重排；单格(mode=1)自动切主码流，多格切子码流。
   - `int nvr_preview_set_mapping(nvr_preview_t*, const int *map1based, int n)`：更新映射（1-based→0-based）、**经 `nvr_chan_persist` 原子写回 channels.json**、按当前 mode/page 重排。
   - `int nvr_preview_set_ext(nvr_preview_t*, const nvr_pv_ext_t *blocks, int n)`：每块 `{chn0, x,y,w,h(千分比), stream}` → `千分比*disp/1000` 换像素 → `mhal_vout_bind_rect` + 按 stream 调 `nvr_stream_switch_stream`；`n=0` 清所有悬浮块。悬浮块用独立高位窗口（避免与宫格窗冲突；同一 decoder 单窗限制下，悬浮块通道与宫格通道不重叠——符合真实用法）。
   - getter：`nvr_preview_get_mode/page`、`nvr_preview_get_mapping`、`nvr_preview_get_ext`。
4. **千分比→像素换算**抽成纯函数 `pv_thousandths_to_px(v, span)`（主机可单测）。
5. 保留现有 `on_channel_online/offline`、`set_icons`；`fullscreen/single_zoom` 归并进 `set_mode`。

### L3 — `app/router/nvr_cmd_router.{h,c}`
1. router cfg 增 `nvr_preview_t *pv`、`nvr_stream_mgr_t *sm`；`nvr_app.c` 构造路由时传 `a->pv`/`a->sm`。
2. `handle_local` 拦截并接线（用已有 cJSON；响应按文档 JSON 形状）：
   | 命令 | 动作 |
   |---|---|
   | `GUI_setDeviceDisplayMode` | `nvr_preview_set_mode(pv, displayMode, displayPage)`；回 `{"result":"OK"}` |
   | `GUI_getDeviceDisplayMode` | 回 `{displayMode, displayPage}`（读 preview 当前态）|
   | `GUI_setChannelMapping` | `nvr_preview_set_mapping(...)`（含持久化）；回 `{"result":"OK"}` 或异常串 |
   | `GUI_getChannelMapping` | 回 `{"ChannelMapping":[...]}`（从 channel 文件）|
   | `GUI_setDeviceDisplayExt` | 解析 channels[] → `nvr_preview_set_ext`；成功回空 content，失败回 `{"error":...}` |
   | `GUI_getDeviceDisplayExt` | 回 `{"channels":[...]}`（当前悬浮块）|
   | `GUI_getSysDisplay` | 回 `{resolutionList, resolution, displayOpacity, fb}` |
   | `GUI_setSysDisplay` | 存 opacity；分辨率固定则仅回 `{"fb":"fb0"}` |
   | `X_NightOwl_getChannelStatus` | 读 `nvr_chan` 实时态 + 子状态标志 → 映射 **0-7** → 回 `{"status":n}` |
   | `X_NightOwl_getDeviceCapabilities` | device 能力=NVR 自身固定集；每通道能力**从相机取回后组装**（NOP 透传其能力→抽取；ONVIF 经 mapping 取回→组装），缓存进 channels.json → 回完整结构 |

---

## 4. 持久化 — 写回 channels.json + 断电保护

**背景**：`nvr_config.c` 只读 `channels.json`（首启种子），**无写回机制**。按需求，出图相关的
每通道持久数据**写回 `channels.json`**（不新建独立文件、不入 settings.db），并做**断电保护**。

**写回内容**：在 `channels.json` 顶层增/更两个键：
```json
{
  "capacity": {...}, "defaults": {...}, "devices": [...],   // 原有拓扑，保留不动
  "channelMapping": [1,2,3, ... ,32],                        // 格→通道(1-based)
  "channelCaps": [                                           // 每通道能力缓存(从相机取回)
    { "channel":1, "signal":"IPC", "hasBattery":false,
      "capabilities":["ptz","light",...], "light":[...], "ptz":[...],
      "sensors":[{"sensor":"motion","modes":["pixelChange"]}] }
  ]
}
```

**断电保护机制（原子写 + 备份回滚）**——新增 `app/config/nvr_chan_persist.{h,c}`：
1. 加载：读 `channels.json`；若 JSON 解析失败/截断 → 回滚读 `channels.json.bak`；两者皆坏 → 用内置默认（映射恒等、能力空）。
2. 保存（原子）：序列化到 `channels.json.tmp` → `fsync(fd)` → `close` → 先把当前 `channels.json` 复制为 `channels.json.bak`（若存在且有效）→ `rename(tmp, channels.json)` → `fsync(dir_fd)`。rename 是原子操作，断电只会落在"旧文件完整"或"新文件完整"，不会半写。
3. 该模块**持有 channels.json 的 cJSON 树**：提供 `get/set channelMapping`、`set channelCaps(chn,...)`，改动即触发原子保存。
4. 写入区需可写（`/dvr/config`）；若 `/dvr` 只读则退回可写区（`/config` 或 `/flash`），路径由 config_dir 决定，真机确认。

- `channelMapping`：由 `nvr_preview_set_mapping` 触发保存。
- `channelCaps`：`getDeviceCapabilities` 首次从相机取回后写入缓存；重启先返回缓存、后台再刷新。

**能力来源（§3-L3 已述）**：NOP 相机透传其能力→NVR 抽取组装；ONVIF 相机经 `nop_onvif_get_capabilities`/mapping 取回→组装成 NVR 结构。device 级能力（`displayMode`/`groupInPrimary`/`bluetooth` 等）= NVR 自身固定集。

### getChannelStatus — 全 0-7 状态（实时态，不持久化）

`nvr_channel` 扩展：在现有 FSM（EMPTY/BOUND/CONNECTING/ONLINE/NOSIGNAL/FAIL/DISABLED）之上，
加子状态标志 `auth_fail / out_of_res / inactive / sleeping / fw_updating`，
`nvr_chan_status_code(chn)` 按下表解析出 0-7，各标志在**真实信号点**更新：

| 码 | 含义 | 来源信号（更新点） |
|---|---|---|
| 0 | no Camera | FSM=EMPTY/DISABLED（未绑定/停用）|
| 1 | active | FSM=ONLINE（PLAYING）|
| 2 | sleeping | 电池相机上报休眠（NOP 状态/事件）|
| 3 | disconnected | FSM=CONNECTING/NOSIGNAL/FAIL（重试中/掉线）|
| 4 | auth fail | 拉流/ONVIF 返回 401 鉴权失败 → 置 `auth_fail` |
| 5 | Out of Resolution | 解码/码流能力超显示预算（mhal `EBUDGET`/`decode_denied`）|
| 6 | firmware updating | 相机上报固件升级中（NOP 状态/事件）|
| 7 | inactive(未激活) | 发现/绑定判定相机需激活（口令未设）→ 置 `inactive` |

优先级（多标志并存时）：7 未激活 > 4 鉴权失败 > 6 固件升级 > 2 休眠 > 5 分辨率 > 3 断连 > 1 活动 > 0 无相机。
2/6 依赖相机上报，NOP 通道可透传相机状态取得；对应 setter 由通道/事件路径在信号到达时调用（信号未到则不置，绝不假填）。

---

## 5. 测试策略

**主机单测（纯逻辑，不依赖 BSP/mhal）**：
- `pv_thousandths_to_px`：边界 0/500/1000 × 屏宽高。
- 宫格→通道合成：`ChannelMapping[(page-1)*mode+k]` 各 mode/page/映射组合、越界、空格(-1)。
- 通道号 1↔0 转换。
- channel_state JSON 读写：save→load 往返一致；文件缺失→默认；损坏→回落默认。
- `nvr_cmd_dispatch`（纯函数）：喂各显示命令 JSON → 校验产生的 preview 调用序列（注入 mhal mock 记录调用）+ 响应 JSON 形状。
- getChannelStatus 状态映射：各 nvr_chan 态 → 期望 0-5。

**真机验证（串口）**：§1 验收 1-9 全项。

**mhal 桩**：主机构建下提供 `mhal_vout` 记录型 mock（记录 bind/bind_rect/set_layout/unbind 调用），供 L2/L3 单测；真实 hdal 实现仅交叉构建启用。

---

## 6. 预留 / 不做

- **`X_NightOwl_get/setChannelZoomPan` — 预留**：注册 handler，接收/存储/回显 Center/Focus/Zoom 值并返回 `{"result":"OK",...}`，但**暂不驱动实际 mhal 裁剪**（真实 ROI 需新增 videoproc crop API，后续接）。即接口在、语义占位，不阻断 GUI。
- **OSD — 不做**：`mhal_vout_osd` 保持桩，本项目不需要（通道名/时间/事件图标叠加移除出范围）。
- 事件上报①、录像③（各自独立 spec）。

---

## 7. 影响文件清单

**改**：`platform/media_hal/mhal_vout.{h,c}`、`app/preview/nvr_preview.{h,c}`、
`app/router/nvr_cmd_router.{h,c}`（拦截显示指令 + getChannelStatus/getDeviceCapabilities 真实组装 + ZoomPan 预留）、
`app/channel/nvr_channel.{h,c}`（子状态标志 + `nvr_chan_status_code` 0-7）、
`app/config/nvr_config.c`（channels.json 加载对接持久化模块）、`app/src/nvr_app.c`（路由 cfg 传 pv/sm）。
**增**：`app/config/nvr_chan_persist.{h,c}`（channels.json 原子写回 + .bak 断电回滚 + mapping/caps 存取）、对应主机单测。
**退化为兜底（不改）**：`components/nop/.../cap_gui_system.c`/`cap_gui_lan.c`/`cap_misc.c`/`cap_device.c`
的对应静态 handler（8089 路由不再命中；保留供非 NVR 场景）。
