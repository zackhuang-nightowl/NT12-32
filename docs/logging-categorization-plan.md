# 日志分类(category)整改方案

## 背景
当前固件日志有两个问题:
1. 只有一个全局级别 `NVR_LOG_LEVEL`(E/W/I/D),tag 是自由字符串,**无法按子系统单独开关**。排障某模块要么全开 DEBUG(信息淹没、日志暴涨,曾致 /tmp 占满 OOM),要么什么都看不到。
2. 大量 `printf("[app] ...")` 直接裸打,不走 nvr_log,无级别/无分类。
3. SDK(OnvifClientLibrary)用 `log_print`(HT_LOG_*),本平台 `log_init` 会崩故未初始化 → SDK 日志根本不输出;需要看时要经 app 层回调转 `NVR_LOG*`。

## 机制(已落地于 common/include/nvr_log.h)
- **级别**:E/W/I 一律按 `NVR_LOG_LEVEL`(默认 I)打,重要、恒可见。
- **分类门控(新)**:`NVR_LOGD(tag, ...)` 除全局级别外,再按「类别 == tag」门控:
  - 环境变量 `NVR_LOG_CATS` = 逗号分隔白名单(带边界匹配,`rtsp` 不会命中 `rtspx`);`*`/`all` 全开。
  - 某条 DEBUG 当 `(NVR_LOG_LEVEL>=D)` 或 `(tag ∈ NVR_LOG_CATS)` 时才输出。
  - 默认(级别 I、无 CATS):不打任何 DEBUG。
- **「启动方式」**:在对应启动脚本 `/dvr/fwbuild rootfs_ota/dvr/run`(或临时 shell)按需 `export NVR_LOG_CATS=rtsp,playback`,即单独打开该子系统详细日志。可做几种启动 profile(如正常/排障),排障 profile 预置一组 CATS。

## 分类命名约定(tag == category)
每个子系统用**固定、稳定**的 tag 作为类别。本次先定 RTSP/隧道相关,其余后续统一:

| 类别(tag)   | 覆盖 | 典型详细打印(应为 DEBUG) |
|-------------|------|--------------------------|
| `rtsp`      | 隧道 RTSP 服务端(nvr_rtsp_live / nvr_rtsp_srv / SDK 回调) | addStream 细节、seek/pb ctrl、SET_PARAMETER body 诊断、每会话 SDP |
| `playback`  | 回放读盘线程(pb_reader) | 段查询/定位、按帧节拍、跨段、hold |
| `puller`    | 上游拉流(stream_puller) | 每 N 帧统计、rtp_gap/fn_gap、事件 |
| `router`    | 路由/解码(stream_router) | 每 IDR、GOP、SPS 维度、rebind |
| `onvif`     | ONVIF 映射/事件轮询 | 每域请求、poller |
| `tutk`      | TUTK agent/隧道 | agent 启停、profile、cgi |
| `record`    | 录像/rsdk | 段开合、落库 |
| `net`/`dhcp`/`poe` | 网络/PoE 发现 | ARP/租约/发现细节 |

## 归类规则(每个模块照此改)
1. **生命周期/里程碑事件**(启动就绪、连接成功、seek 执行、addStream 成功、错误)→ 保留 `NVR_LOGI`/`NVR_LOGW`/`NVR_LOGE`(低频,恒可见)。
2. **高频/诊断细节**(每帧、每 N 帧、每 IDR、每请求 body、拥塞细节、维度探测)→ 改 `NVR_LOGD(<该模块类别>, ...)`,默认静默,`NVR_LOG_CATS` 按需开。
3. **裸 printf** → 逐步替换为对应 `NVR_LOG*`(启动阶段的关键就绪信息可留 INFO)。
4. **SDK 日志**:不改 SDK 的 log_print;需要看的 SDK 侧信息经 app 回调(如 pb ctrl / diag 钩子)转 `NVR_LOGD(<类别>)`。

## 迁移顺序(本次只做 rtsp,其余后续)
- [x] **rtsp / playback**:nvr_rtsp_live.c、nvr_rtsp_srv.cpp、SET_PARAMETER 诊断回调 → 高频转 DEBUG,里程碑留 INFO。(本次)
- [ ] puller / router:stream_puller.cpp、stream_router.c 每 N 帧/每 IDR → DEBUG。
- [ ] onvif:components/nop onvif 映射与 poller。
- [ ] tutk:agent 启停/cgi。
- [ ] record:rsdk 段/落库。
- [ ] net/dhcp/poe:发现/租约/ARP。
- [ ] app 裸 printf(app/src/nvr_app.c 等)→ NVR_LOG*,启动就绪信息留 INFO。

## 验证
`export NVR_LOG_CATS=rtsp` 后只应多出 rtsp 类 DEBUG;不设时无 DEBUG;`NVR_LOG_LEVEL=3` 仍全开(兼容旧行为)。
