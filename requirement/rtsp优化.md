你这个现状我看明白了。现在的问题已经不是“有没有队列”，而是**RecordQueue 只是缓冲，没有真正完成执行上下文解耦**。

对于 NVR 来说，你当前方案可以作为过渡版本，但如果目标是“长期稳定、Live 不被录像 IO 拖慢、32 路甚至更高路数可扩展”，我建议继续改。

最关键的判断是：

> **不要继续让 `on_video` 线程承担 `rsdk_rec_write_frame()`。**

因为即使你现在把 drain 限制成 1/4 帧，只要某次 `rsdk_rec_write_frame()` 阻塞 100~500ms，当前这一路 puller 还是会停。TCP 数据继续在下层积压，Live 仍然可能逐渐变旧，只是比以前“整队 drain 96 帧”轻很多。

你现在可以保留这三点：

* 每个主/子流独立 `RecordQueue`
* 同一路 writer 必须严格串行
* `puller owns stream state`

但建议把“writer ownership”从 **puller 线程** 转移成 **record worker**。

推荐架构：

```text
32 Channel
每通道：
 Main Puller ──> RecordQueue(main)
 Sub  Puller ──> RecordQueue(sub)

                   ↓
          Record Worker Layer
                   ↓
                 RSDK
                   ↓
                 Disk
```

这里不建议直接做成“64 条流 = 64 个 writer thread”。

更推荐按照物理盘或固定 worker 数来分：

```text
64 RecordQueues
      ↓
4~8 Record Workers
      ↓
RSDK / Disk
```

然后保证：

```text
same writer_main
    永远只由同一个 worker 消费

same writer_sub
    永远只由同一个 worker 消费
```

最简单可以 hash：

```c
worker_id = stream_id % RECORD_WORKER_NUM;
```

这样既保证单路顺序，又避免开 64 个线程。

你当前：

```text
puller thread:
on_video
  ↓
live
  ↓
record enqueue
  ↓
record drain
  ↓
rsdk_rec_write_frame
```

建议变成：

```text
puller thread:
on_video
  ↓
parse / continuity
  ↓
live
  ↓
record_queue_push
  ↓
return
```

而另外：

```text
record worker:
wait event
  ↓
pick queue
  ↓
drain N frames
  ↓
rsdk_rec_write_frame
  ↓
next queue
```

这样 `on_video` 的耗时就基本稳定在微秒/毫秒级，不再受磁盘尾延迟影响。

你现在“不同 puller 会并发调用 `rsdk_rec_write_frame`，而 `rsdk_rec.c` 没 mutex”这一点，反而是接下来需要重点确认的。

你需要明确两件事：

```text
rsdk_rec_write_frame(writer_main_ch0)
和
rsdk_rec_write_frame(writer_main_ch1)
```

是否真的可以并发。

虽然不是同一个 writer，但如果 RSDK 内部共享：

```text
disk metadata
chunk allocator
AES context
free block map
index
global file descriptor
裸盘 allocator
```

那没有 mutex 未必安全。

所以要确认 RSDK 的线程模型到底是：

```text
A. 完全 per-writer 独立，可并行
```

还是：

```text
B. writer 独立，但底层 disk context 共享，需要串行
```

如果是 B，那么最佳方案其实更简单：

```text
每块物理盘 1 个 writer worker
```

所有对应 RecordQueue 轮询写：

```text
CH0 main: drain 4
CH0 sub : drain 4
CH1 main: drain 4
CH1 sub : drain 4
...
```

这样天然不需要底层 disk mutex，而且对 HDD 尤其合理。

如果是 SSD/NVMe，而且 RSDK 明确支持并发，再考虑：

```text
每盘 2~4 workers
```

现在还有一个点我建议你尽快改：**音频不要继续直接在 `stream_route_audio()` 同步写盘。**

你视频已经识别到：

```text
puller callback
+ synchronous disk write
= live latency risk
```

音频其实是同样的问题。

当前：

```text
stream_route_audio
   ↓
rsdk_rec_write_frame
```

如果 RSDK 卡住，主流 puller 的 callback 仍然会被堵。

建议音视频进入同一个逻辑 RecordQueue，但队列元素带类型：

```c
enum RecordFrameType {
    REC_VIDEO,
    REC_AUDIO
};
```

例如：

```text
RecordQueue(main)
   ├─ video frame
   ├─ audio frame
   ├─ video frame
   ├─ audio frame
   └─ ...
```

这样 worker 按原始时序写，音视频同步也更自然。

不要做成：

```text
VideoQueue
AudioQueue
```

然后各自消费，因为容易造成写盘顺序和 AV 时间关系复杂化。

你的 budget 机制，在有独立 worker 后仍然有用，但用途不同。

现在 budget 是为了：

```text
防止 puller 被 drain 卡死
```

以后 budget 应该变成：

```text
保证多个 channel 公平调度
```

例如 worker：

```text
CH0 drain 8
CH1 drain 8
CH2 drain 8
...
```

避免一路高码率把整个 writer worker 占满。

而且我建议从“帧 budget”逐渐改成：

```text
byte budget
+
time budget
```

因为 8 帧可能差别非常大：

```text
8 × P frame
可能 100KB

8 × IDR/高码率帧
可能 2MB+
```

更稳的是类似：

```text
每次最多：
1~2 MB
或者
2~5 ms 写盘预算
```

然后轮到下一路。

你现在这套修改里，“Live 优先 + drain 限 budget”可以继续保留，作为短期版本完全合理。但它本质上仍是：

> **降低阻塞概率，而不是消除阻塞来源。**

真正最终形态应该是：

```text
Puller Thread
  │
  ├─ Live → VDEC
  │
  └─ RecordQueue push
          │
          ▼
   Record Worker
          │
          ▼
      AES + RSDK
          │
          ▼
         Disk
```

我会给你一个明确优先级：

| 优先级 | 建议                                     |
| --- | -------------------------------------- |
| P0  | `rsdk_rec_write_frame()` 移出 `on_video` |
| P0  | 音频也进入 RecordQueue，不再同步写                |
| P0  | 明确 RSDK 是否允许不同 writer 并发               |
| P1  | 固定数量 record workers，不做一流一线程            |
| P1  | 同一路固定映射到同一 worker                      |
| P1  | worker 做公平 drain                       |
| P2  | budget 从 frame count 升级到 bytes/time    |
| P2  | queue 监控增加 `age_ms / bytes / frames`   |

如果只针对你现在的 32 路、主子都录、裸盘 AES 这个场景，我倾向于：

> **64 条独立 RecordQueue + 每块物理盘 1~2 个 Record Worker**。

这通常会比“64 个 writer thread”稳定，也比目前“64 个 puller 自己同步写”更可控。
