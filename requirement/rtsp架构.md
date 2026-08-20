这版已经比上一版清晰很多，而且你刚把 **PoE 也统一改成 TCP** 后，传输策略更一致了。

基于你这张更详细的图，我建议按“必须改 / 强烈建议 / 可保持”来处理。你现在真正需要动的地方，主要还是集中在 `stream_route_video`、录像路径和 live 同步策略。

* **必须改 1：`GAP` 不能直接决定后面的统一行为。** 现在还是 `NAL -> GAP -> PAR -> live/rec`。建议 `GAP` 只做“检测 + 标记”，不要在公共路径做跳帧、等 IDR、重连。输出类似 `frame.damaged / discontinuity / generation`，然后录像和 live 各自处理。
* **必须改 2：录像和 LiveView 要加独立 Queue。** 现在 `PAR -> writer`、`PAR -> VDEC` 看起来仍然是直接调用。建议 `PAR/FrameHub -> RecordQueue -> writer`，以及 `PAR/FrameHub -> LiveQueue -> VDEC`。这样 VDEC 卡住、HDMI 切换、磁盘抖动都不会反向堵住 RTSP callback。
* **必须改 3：`live_synced=false` 时不能“只喂关键帧”。** 更准确应该是：等待一个完整可解码起点，然后连续喂。也就是 `VPS/SPS/PPS + IDR AU -> live_synced=true -> 后续 P/B 连续喂`。不能一直“只喂关键帧”，否则有些编码器会表现成低帧率甚至解码异常。
* **必须改 4：关键帧缓存不要只是 `kf` 单帧。** 建议缓存完整 `bootstrap`：`VPS/SPS/PPS + 完整 IDR access unit + codec generation`。H.264 至少 SPS/PPS，H.265 要 VPS/SPS/PPS。主流和子流分开缓存。
* **必须改 5：主流和子流的 callback 上下文必须严格分离。** 现在 MAIN/SUB 都进 `stream_route_video`，所以 route 内一定要带 `stream_id = main/sub`，否则 SPS/PPS、kf、frame_num、RTP seq、live_synced 都有混流风险。
* **必须改 6：录像关键帧门控只允许发生在“录像启动/新分片启动”，不能持续作为丢帧逻辑。** 正确逻辑是：录像开始时等待首个 IDR，然后之后所有完整帧都录；新 segment 同样尽量从 IDR 开始。正常录像过程中不能碰到 P 帧就门掉。

我会把你图里的这部分：

```text
CB
 ↓
NAL
 ↓
GAP
 ↓
PAR
 ├─ live
 └─ record
```

改成：

```text
CB
 ↓
RTP/NAL parse
 ↓
Access Unit Assemble
 ↓
Continuity Inspect
 ↓
Frame Hub
 ├─────────────── Record Queue
 │                    ↓
 │              Recorder State
 │                    ↓
 │           IDR-aligned writer
 │
 └─────────────── Live Queue
                      ↓
                Live Sync State
                      ↓
               Drop/Resync Policy
                      ↓
                    VDEC
```

你的 `GAP` 现在写的是：

```text
RTP seq / H264 frame_num
```

这里也建议调整一下。既然现在 **主流、子流全部 RTSP over TCP**，`RTP seq` 仍然可以用于检测异常，但它不再主要代表“网络 UDP 丢包”。TCP 下 sequence 跳变可能意味着：

```text
camera 本身没发
RTSP reconnect
camera encoder reset
RTP session reset
depacketizer 丢数据
应用 buffer 被清
```

因此不要写成：

```text
seq gap => packet loss => reconnect
```

而应该是：

```text
seq gap
  ↓
mark discontinuity
  ↓
判断 connection generation / timestamp / frame completeness
  ↓
决定是否需要 resync
```

另外，`H264 frame_num` 可以作为辅助，但不要把它作为核心连续性标准。现实 IPC 的编码行为并不总是特别规范，而且 H.265 也完全不同。更稳的是以：

```text
RTP seq
RTP timestamp
NAL start/end
完整 Access Unit
IDR presence
RTSP connection generation
```

一起判断。

你现在的 `live_synced` 思路是对的，但建议改成明确状态机，而不是 bool：

```text
LIVE_IDLE
LIVE_WAIT_CONFIG
LIVE_WAIT_IDR
LIVE_SYNCED
LIVE_RESYNC
```

例如窗口刚显示：

```text
show_win < 0
→ LIVE_IDLE

show_win >= 0
→ LIVE_WAIT_IDR

拿到 VPS/SPS/PPS + IDR
→ flush/create decoder
→ send config
→ send IDR
→ LIVE_SYNCED

后续连续送帧
```

出现不连续：

```text
LIVE_SYNCED
   ↓
damaged frame / generation changed / backlog过大
   ↓
LIVE_RESYNC
   ↓
flush decoder
   ↓
drop旧帧
   ↓
找最新完整 IDR
   ↓
LIVE_SYNCED
```

这样会比当前：

```text
live_synced?
否 → 只喂关键帧
是 → 连续喂
```

稳定很多。

录像部分我特别建议再加一个 `Recorder State`：

```text
REC_WAIT_IDR
REC_RECORDING
REC_ROTATE_PENDING
REC_DISCONTINUITY
```

录像启动：

```text
开始录像
 ↓
等待完整 IDR
 ↓
写 SPS/PPS + IDR
 ↓
REC_RECORDING
```

之后：

```text
P/P/B/IDR
全部正常写
```

切片：

```text
到达目标时间
 ↓
REC_ROTATE_PENDING
 ↓
继续录
 ↓
等下一个 IDR
 ↓
新文件从 IDR 开始
```

这样不会因为机械切片造成新文件从 P 帧起步。

你当前还有一个比较重要的缺项：**没有明确的 backpressure / queue overflow 策略。**

建议：

```text
RecordQueue
容量：较大
策略：不主动 drop
high watermark：报警
持续满：认为系统异常
```

而：

```text
LiveQueue
容量：非常小
策略：允许 drop oldest
目标：永远追最新画面
```

例如 LiveView 最好不要积压几十帧：

```text
25fps

LiveQueue:
3~10 frames
```

一旦明显落后：

```text
drop until latest IDR
decoder flush
重新同步
```

因为 LiveView 的目标不是完整，而是实时。

你现在 `PB 回放与 live 互斥` 这个设计可以保留，特别是硬件 decoder 资源有限时非常合理。但建议不要只在 UI 层做：

```text
mode=0 关 live
```

还应该实际执行：

```text
LiveQueue stop
VDEC drain/flush
unbind VPE/VOUT
release decoder channel
```

回放结束：

```text
release playback decoder
重新创建/绑定 live decoder
LIVE_WAIT_IDR
拿 bootstrap + 最新 IDR
恢复显示
```

这样回放切 live 时不会把旧 decoder 的 reference frame 带回来。

另外你现在既然 PoE/LAN 全部 TCP，我建议图里直接去掉：

```text
TCP{"over_tcp?"}
```

如果产品策略已经确定，就不要再让这里表现成条件分支。

直接：

```text
RTSP_TRANSPORT_TCP
```

或者：

```text
TRANS[RTSP interleaved over TCP]
```

否则以后维护的人看到图，还会以为存在 UDP 逻辑。

我建议你最终把核心结构收敛到下面这样：

```text
IPC
 │
 ├─ Main RTSP/TCP
 └─ Sub  RTSP/TCP
        │
        ▼
  NopRtspClient
        │
   RTP Depacketize
        │
 Access Unit Assemble
        │
 Continuity Inspector
        │
      Frame Hub
       /     \
      /       \
RecordQueue   LiveQueue
    │             │
Recorder       Live State
State          Machine
    │             │
WAIT_IDR       WAIT_IDR
RECORDING      SYNCED
ROTATE         RESYNC
    │             │
writer          VDEC
    │             │
RSDK           VPE/VOUT
```

旁路再加 4 个模块：

```text
Bootstrap Cache
Stream State Machine
Time Mapper
Metrics
```

其中 `Time Mapper` 是你现在图里最值得补上的模块，因为你已经明确要求“按 NVR 本机时间显示”。

至少每个 stream 维护：

```text
rtp_timestamp
recv_monotonic
nvr_wallclock
generation
```

录像绝对时间来自 NVR，帧连续性来自 RTP timestamp，而不是每帧直接拿当前 `time()`。

如果只看这版图，我会给你一个改动优先级：

| 优先级 | 修改                                                                  |
| --- | ------------------------------------------------------------------- |
| P0  | `PAR -> writer/VDEC` 改成独立 RecordQueue / LiveQueue                   |
| P0  | `live_synced bool` 改成 Live 状态机                                      |
| P0  | “未同步只喂关键帧”改成“等待完整 IDR 起点后连续喂”                                       |
| P0  | GAP 只检测，不在公共路径执行跳帧                                                  |
| P1  | SPS/PPS/kf 改成 per-main/sub bootstrap cache                          |
| P1  | 增加 connection `generation`                                          |
| P1  | Recorder 增加 WAIT_IDR / RECORDING / ROTATE                           |
| P1  | 增加 NVR Time Mapper                                                  |
| P2  | Playback/live 切换时显式 flush/release VDEC                              |
| P2  | 增加 queue depth / reconnect / timestamp jump / decoder error metrics |

你这套现在已经不需要大改方向了，主要是把 **“共享 callback 直接处理”升级成“Frame Hub + 两条独立状态机”**。这一步做完以后，稳定性会提升非常明显。
