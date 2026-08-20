# ③ 拉流出图 (streaming) — ✅ glue 已实现

**整机最关键的跨模块链路**：IP 相机 RTSP → 硬件解码 → 预览上屏 + 旁路录像。

## 数据流（已落地）

```
components/onvif ── GetStreamUri ──► rtsp://198.18.N.100/main
                                          │
   CRtspClient (third_party/happytime_onvif_rtsp)   ← stream_puller.cpp
        rtsp_start(DESCRIBE) → 定 codec → rtsp_play → video_cb(裸帧)
                                          │
                          stream_route_video()  ← stream_router.c
                    ┌─────────────────────┴─────────────────────┐
                    ▼                                            ▼
        platform/media_hal                            components/recorder
          mhal_vdec_send(annexb) 硬解                   rsdk_rec_write_frame
          → 解码器 bind vout_win                         (关键帧门控起录)
          → hd_videoout / HDMI 分屏预览
```

## 落地文件（编译校验状态）

| 文件 | 语言 | 内容 | 校验 |
|------|------|------|------|
| `include/nvr_streaming.h` | C API | 16 路管理：init/add/start/stop/switch/record/state | ✅ |
| `src/stream_nal.c` | C | Annex-B NAL 分类（判 IDR/参数集 → 关键帧门控） | ✅ `-fsyntax-only` |
| `src/stream_router.c` | C | 一帧 → {mhal_vdec 上屏, rsdk_rec 录像} | ✅ 对真实 recorder+mhal 头 |
| `src/stream_puller.cpp` | C++ | CRtspClient 封装 + video/audio/notify 回调 | ✅ **对真实 CRtspClient 头零错误** |
| `src/stream_mgr.cpp` | C++ | 通道表 + 生命周期，实现 C API | ✅ |

> puller.cpp 已用 g++ 对 happytime 真实头 (`rtsp_cln.h`) 语法编译通过——
> `rtsp_start/video_codec/set_video_cb/rtsp_play/set_rtp_over_udp/超时` 等调用均为**真实 API**。

## 关键实现点

1. **每通道一个 `CRtspClient`**（16 路 → 16 实例，`stream_mgr` 用 chn 作槽位）。
2. **裸帧 → Frame Hub**：`video_cb` → CI(只 mark disc/gen) → **HUB fanout** → BS旁路 / **RecordQueue** / **LiveQueue** → VDEC。
3. **通道关联**：`set_notify_cb(cb, chan_ctx)` → 各回调 userdata 即通道上下文（已验证 happytime 回调传的就是它）。
4. **录像**：`WAIT_IDR→RECORDING` 状态机；disc/gen 打 gap 内联标记(`type_mask bit31`)；**RecordQueue(96)** 解耦磁盘抖动，满不丢帧、高水位告警。
5. **Live**：`WAIT_IDR→SYNCED→RESYNC`；LiveQueue **双阈值**(8 帧 + 500ms)；RESYNC 立即注入 bootstrap IDR。
6. **codec 自适应**：`NVR_CODEC_AUTO` 时用 `CRtspClient::video_codec()`（SDP 探测）→ 映射 rsdk codec。
7. **主/子码流**：`nvr_stream_switch_stream` 换 url 重连；预览大画面主码流、分屏子码流。
8. **强制 RTP over TCP**（TCP 重传保证主/子录像不丢包；拉流层忽略 over_tcp=0）。

```text
RTSP/TCP → AU → CI(mark disc/gen)
              → HUB ─┬─ BS(旁路: par+IDR AU)
                     ├─ RecordQueue → REC(WAIT_IDR|RECORDING) → rsdk
                     └─ LiveQueue(8帧+500ms) → LIVE SM → VDEC
```

## app 侧用法

```c
nvr_stream_mgr_cfg_t mc = { .group = storage_group, .conn_timeout=5, .rx_timeout=10 };
nvr_stream_mgr_t *sm; nvr_stream_mgr_init(&mc, &sm);

nvr_stream_chan_cfg_t cc = { .chn=0, .codec=NVR_CODEC_AUTO, .stream=NVR_STREAM_MAIN,
                             .record=1, .vout_win=0, .over_tcp=1 };
snprintf(cc.url,  sizeof cc.url,  "rtsp://198.18.1.100/main");
snprintf(cc.user, sizeof cc.user, "admin");
snprintf(cc.pass, sizeof cc.pass, "xxxxxx");
nvr_stream_add_channel(sm, &cc);
nvr_stream_start(sm, 0);                 /* 起流 → 出图 + 录像 */
```

## 剩余 TODO（接平台/联调）

- [x] **`platform/media_hal` 落地**：`mhal_vdec_*`/`mhal_vout_*` 已对接 na51090 `hd_videodec/videoproc/videoout`，对真实 hdal 头编译通过（见 `platform/README.md`）；剩板级内存池/时序 TODO。
- [ ] **自动重连**：notify 已把 `NOSIGNAL/FAIL` 写进 `c->state`；补一个 supervisor（app tick 或线程）在掉线时 `stop→start` 重连。
- [ ] **音频上屏/对讲**：`stream_route_audio` 现只做录像旁路；预览音频输出与 ONVIF backchannel 对讲另接。
- [ ] **PTS 对齐**：现用 RTP `ts` 作 pts + 墙钟 epoch 作 wall_time；跨相机时钟漂移/丢包补偿可在 router 增强。
