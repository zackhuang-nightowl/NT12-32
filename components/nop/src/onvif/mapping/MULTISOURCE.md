# 多视频源（Multi-VideoSource）映射设计 — SDK 内部说明

> 对应 NOPMappingONVIF.md §10。开发向：讲清楚「一台设备多个视频源」时，
> NOP 请求如何在下发时用到**正确那一源**的 ONVIF token。P1 数据面已实现
> （见文末「代码分布」），P1.1/P2 为后续。

## 1. 一句话模型

**一个视频源 = 一个 NVR channel；channel 上钉一个 `VideoSourceToken`；
下发指令时用 `channel` 反查该源的 token 组，各接口取组里对应 token。**

- 源的唯一标识：**`VideoSourceToken`**（物理输入）。`GetVideoSources` /
  `GetProfiles` 返回几个不同的 `VSC.SourceToken` 就有几个源。**绝不写死 2 路。**
- 一台物理设备的多个源共享 `host:port`；用 `channel_entry.video_source_token`
  在设备内区分。SDK 只认设备（一条 ONVIF 连接），源靠这个字段落到 channel。
- `video_source_token` 为空 = 第一个/唯一源（单源相机、NOP 后端），行为与
  改造前完全一致（零回归）。

## 2. 下发链路（第二源同理，只是选到第二组）

```
北向  args.channel = 18            (ch18 = 源1 = 热成像)
  │  channels_get(18) → { host:port(共享) , video_source_token="VideoSource_1" }
  │  onvif_session_begin(be,18)
  │     · 取/建该 channel 的会话，连接设备、Digest 鉴权
  │     · 首次 resolve：GetProfiles 按 VSC.SourceToken 分组，
  │       锁定 bound_source 那一组 → 缓存 vsc / analytics_cfg
  │  handler 用会话访问器取「当前源」token 下发 ONVIF
  │     onvif_session_vsc(s,…)            → VSC_thm     (OSD / Mask)
  │     onvif_session_analytics_cfg(s,…)  → Cfg_2       (Line/Field/Motion/Object 规则)
  └──  onvif_session_profile(s)           → Media1 profile (PTZ；见 §5 限制)
```

**"用到第二源 token" 就发生在 `onvif_session_begin`**：它按 channel 的
`bound_source` 把会话解析到对应源的 token 组。**handler 代码不感知源**——照旧
调访问器，返回值已是本 channel 所属源的 token。

## 3. token 归组（源标识 = `VSC.SourceToken`）

`nop_onvif_resolve_source(dev, source_token, &out)`（onvif_adapter_ext.cpp）：

```
GetProfiles()：
  target = source_token 或（为空时）锁定第一个 profile 的 VSC.SourceToken
  遍历 profiles，仅保留 VSC.SourceToken == target 的：
    out.profile       = 首个匹配 profile 的 token
    out.vsc_token     = 该 profile 的 VideoSource.token        (OSD/Mask 作用域)
    out.analytics_cfg = 首个带 Analytics 的 profile 的 Analytics.token  (规则作用域，常在子码流)
```

- 归属**只认 `VSC.SourceToken`**，不靠 profile 名称/顺序。
- 事件反查用同一标识：事件 `Message/Source` 的
  `VideoSourceConfigurationToken` → 反查回同一 channel（P2）。

## 4. 每类指令用哪个 token

| 指令 | token（取自当前源组） |
|---|---|
| OSD / Mask 建/查 | `onvif_session_vsc(s)` = 该源 `VSC token` |
| 越线/入侵/移动/对象 规则 | `onvif_session_analytics_cfg(s)` = 该源 `VideoAnalyticsConfiguration token` |
| 对焦 Imaging Focus | `onvif_session_vsc(s)`（沿用原 media2_video_source_token 的取值） |
| PTZ 连续移动/预置/巡航/Home | `onvif_session_profile(s)`（Media1 profile，见 §5 限制） |

访问器约定 **0=成功**（与被替换的 `nop_onvif_media2_video_source_token` /
`nop_onvif_analytics_config_token` 一致），故 handler 全是一行 drop-in swap。

## 5. P1 范围与已知限制

**已实现（P1 数据面）**：per-source 的 OSD / Mask / 对焦 / 所有 Analytics 规则
（Line/Field/Motion/Object）与 `AI_getChannelAICapabilities`，均按 channel 绑定的源取正确 token。

**已实现（能力按源构建）**：`X_NightOwl_getDeviceCapabilities` 现在
**枚举设备的每个视频源**（`nop_onvif_list_sources`），逐源用
`nop_onvif_get_device_caps(dev, source_token, …)`（OR 该源各 profile 的
ConfigurationSet flags + PTZ node/service caps）+ 该源 `analytics_cfg` 的
`get_ai_caps`，产出 `channels:[ 每源一项 ]`。每项带 `videoSourceToken`（源的稳定键）
与 `channel`（`onvif_backend_channel_for_source` 反查该源已绑定的 NVR 通道，未注册则 -1）。
device 级聚合与 channel 号最终分配仍归上层。

**已实现（事件按源路由）**：`nop_onvif_event_msg_t` 增 `source_vsct`（Source 的
`VideoSourceConfigurationToken`）与 `class_types`（Data 的 ClassTypes）；adapter
`pull_msgs` 解析二者。poller 每 channel 订阅时用 `resolve_source` 解出本源 VSC 存
`poll_vsct[ch]`，收到事件仅当 `source_vsct == poll_vsct[ch]` 才投递（两端都已知且不等则跳过）
→ 多源设备每 channel 只上报本源事件，无串源、无重复。
**支持的事件**：CellMotion→motion、LineDetector→lineCross、FieldDetector→fieldIntrusion、
ObjectDetection→按 `Data.ClassTypes` 映 human/vehicle/face/animal（**修复**：ObjectDetection
topic 不含类别，之前被整体丢弃）。未映射（忽略）：Loitering / LineCounting / OccupancyCounting 等
（NOP 无对应 detect type）。

**已实现（PTZ 按源）**：`onvif_session_profile` 现返回本源 Media2 profile（`resolve_source`
的 `profile`；未解析则回退 Media1 profile[0]）→ ptz.c/ptz_ext.c 全部 PTZ（移动/预置/巡航/Home）
自动按源，无需改 handler。

**已实现（码流按源，keyed by VideoEncoderToken）**：`resolve_source` 额外按分辨率排出该源的
`main_venc`/`sub_venc`（VideoEncoderConfiguration token）；session 存并经
`onvif_session_main_venc`/`_sub_venc` 暴露。`GUI_get/setChannelMediaProfiles` 用 token 过滤到本源
的编码器、按 token 定名 main/sub（get 输出带 `VideoEncoderToken`；set 可按 `VideoEncoderToken` 或
main/sub 定位），编码器能力（分辨率/码率/帧率/GOP/质量 options）随每个本源编码器返回。未解析时回退
设备全序（单源零回归）。

**已实现（OSD/Mask 按源过滤）**：OSD set 的 match/create、Privacy get 的枚举、Privacy set 的
删除，都按 `config_token == 本源 VSC` 过滤——不再误改/误删/漏报其他源的 OSD/Mask。

**已实现（每设备单订阅 + 事件按源分发）**：poller 现在每个设备只由「owner 通道」
（同 `host:port` 最低索引的启用 ONVIF 通道）订阅并 PullMessages 一次；owner 的
`poll_dev_ensure` 一次性解析该设备**所有**通道的本源 VSC 存 `poll_vsct[]`；每条事件按
`source_vsct` 经 `poll_route_channel` 分发到绑定该源的通道（未知则归 owner）。避免了此前
「每通道一订阅」的重复连接/重复拉取。`poll_device_owner`/`poll_route_channel` 见 session.c。

**已实现（请求路径每设备一连接）**：backend 加 `devpool`（按 `host:port` 一个共享
`nop_onvif_device_t`）；`device_acquire` 复用/创建；`sessions[ch].dev` 改为**借用**该共享句柄
（per-channel 仍各自解析本源 token）。`backend_destroy` 释放 devpool（不再释放 sessions 借用句柄）。
单个 backend 锁串行化访问，句柄非重入也安全。→ 同设备多通道请求共用一条连接（事件侧亦每设备一份，二者句柄分离以免阻塞拉取与请求互相竞争）。

**未做（后续）**：
- **添加设备落 N channel**：`nop_onvif_list_sources` 已备好，注册流程由上层接。
- **richer event data**（ClassTypes/Direction 之外，如 UtcTime）需扩 `nop_event_t`。
- **P2 添加设备**：`nop_onvif_list_sources()` 已提供枚举；「一台设备落 N 个 channel、
  各写 `video_source_token`」的注册流程由上层接入（SDK 只做映射）。
- **事件富数据**：ClassTypes/Direction 已透传（poller 组 `extra_json` =
  `{"classType":"human","direction":"BA"}`，复用 `nop_event_t.extra_json`，无需扩结构；
  ONVIF Left/Right→NOP BA/AB）。UtcTime 暂未透传（如需可再加）。下游 8012 序列化读 extra_json 由上层接。
- **事件订阅优化**：现每 channel 一订阅（多源设备 N 订阅，各自过滤本源，正确但多占连接）；
  可改每设备一订阅 + 按 VSCT 分发到各 channel。

## 6. 代码分布

| 位置 | 职责 |
|---|---|
| `include/nop_sdk/nop_nvr_channels.h` | `channel_entry.video_source_token`（源绑定字段） |
| `include/nop_sdk/nop_onvif_ext.h` | `nop_onvif_source_tokens_t` + `nop_onvif_resolve_source` / `nop_onvif_list_sources` |
| `src/onvif/onvif_adapter_ext.cpp` | 上述两 ABI 实现（GetProfiles 按 `VSC.SourceToken` 分组） |
| `src/onvif/mapping/onvif_map_session.c` | 会话按 `bound_source` 解析并缓存 `vsc`/`analytics_cfg`；访问器 `onvif_session_vsc` / `onvif_session_analytics_cfg` |
| `src/onvif/mapping/onvif_map_internal.h` | 访问器声明 |
| `onvif_map_{osd,privacy,ptz_ext,ai,motion}.c` | 由「首个源」ABI 调用改为会话访问器（drop-in） |

> 完整分期与图示见 `nvr_firmware/docs/ONVIF双视频源方案_2026-08-10.{html,md}`。
