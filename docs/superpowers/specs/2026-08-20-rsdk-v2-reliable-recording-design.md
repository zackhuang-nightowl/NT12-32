# RSDK v2 —— 高可靠多路 NVR 录像存储 设计

- 日期: 2026-08-20
- 分支: tutkagent
- 范围: `components/recorder` + `components/streaming` 写盘链
- 格式: 允许 `RSDK_FORMAT_VERSION` bump 到 2(v1 盘只读兼容,不静默改写)

---

## 1. 背景与问题

对 `components/recorder` 做线程安全 + 可靠性审计,并结合 `components/streaming` 写盘链,确认了导致"录像会断 / 数据损坏"的根因。当前 recorder 内部**零内部锁**,但其 API 实测被 ≥4 类线程并发调用:

| 线程 | 调用的 recorder API | 动的共享态 |
|---|---|---|
| Record Worker(单线程) | `rsdk_rec_write_frame`→`start_seg`/`index_write`/`invalidate_chunk` | `sb.write_ptr_chunk`、`sb.seq_epoch`、`st.index_next`、索引区、盘组 balance |
| Puller 线程(≤64) | `rsdk_rec_open_group_stream`→`start_seg`;`rsdk_rec_close`→`finalize_seg`+`dev_flush` | 同一 dev/group、SB/SysTab |
| 回放线程(多个) | `rsdk_group_query_stream`、`rsdk_group_play_*` | 读索引区、读数据区 |
| 存储 tick 线程 | `rsdk_group_smart_refresh` | `group->health_ok` |

### 已确认的竞争(会导致数据损坏)

- **R1 — chunk 双分配**:`rsdk_dev_alloc_chunk`(`rsdk_storgedev.c:321-322`)对 `sb.write_ptr_chunk` 做无锁 RMW,worker(翻段)与 puller(open)并发可分到同一 chunk → 两段写同一盘区 → 互相覆盖。单盘时 64 条流共用一个 dev,相机频繁重连时高频命中。
- **R2 — 索引丢更新**:`rsdk_index_write`(`rsdk_index.c:37-38`)对 `st.index_next` 无锁 RMW,worker 写与 puller 关 writer 的 `finalize_seg` 并发 → 段索引条目丢失 → 该段录像回放/日历消失。
- **R3 — SB/SysTab flush 撕裂**:`rsdk_dev_flush`(`rsdk_storgedev.c:284-291`)在共享 `sb/st` 上先算 CRC 再写盘,并发 flush 或算 CRC 与写盘之间被改 → 盘上 SB CRC 不符 → 开机主备都坏 → `RSDK_E_CORRUPT`,整盘录像丢失。

### 可靠性缺陷

- **Rel1** 帧只有 `hdr_crc32`(头),**载荷无 CRC**;两次 pwrite(`rsdk_rec.c:159-160`)非原子,掉电/坏块撕裂载荷无法检测。
- **Rel2** 帧载荷只在段边界 fsync → 掉电丢最多一个 chunk(8–32MB≈数十秒/路)。
- **Rel3** `rsdk_dev_info.free_chunks` 恒等于 `data_chunks`(`rsdk_storgedev.c:270`),假容量。
- **Rel4** `rsdk_rawdev_pread` 越界静默补 0 当成功(`rsdk_rawdev.c:48`)。
- **满盘周期性卡顿**:覆盖模式每翻段 `rsdk_index_invalidate_chunk`(`rsdk_index.c:85-100`)**全索引线性扫描**(多 TB 盘上百万次 pread),同步在写盘线程 → 队列爆 → 丢帧。这是"录像断"的头号底层元凶之一。

### streaming 写盘链根因

- **满即丢帧**:`stream_record_q_push`(`stream_hub.c:151`)满返回 -1,帧被 free 丢弃;puller 侧不 drain → 背压退化成丢帧。
- **单 worker 线程**:`stream_record_worker.c` 单线程 512KB/轮 round-robin 64 队列,多盘无法并行。
- **wall_time 取写盘时刻**:`stream_record_worker.c:117` `f.wall_time = time(NULL)`,worker 滞后则录像时间轴错位。

---

## 2. 目标与不变量

- **正确性**:任意并发(N puller open/close + M 回放读 + tick SMART + 写盘)下,永不双分配 chunk、永不丢索引、永不写坏 SB/SysTab。
- **可靠性**:掉电最多丢 ≤1s 录像;任何坏帧/坏块可检测、可跳过、不污染回放。
- **不丢录像**:正常负载零丢帧;磁盘物理带宽不足时按 GOP 丢 + 打 gap 标记(诚实)。
- **多盘**:单盘=退化 1 盘组;多盘 JBOD 按盘并行写、按盘串行;缺盘/坏盘降级继续录。
- **约束**:回放接口/查询语义不变;v1 盘只读兼容;不静默改写 v1 盘。

---

## 3. 架构:线程模型 B(每盘 worker + 元数据锁)

```
Puller(只入队,采集时刻打 wall_time)
   └─ per-(chn,stream) RecordQueue ──┐
                                     ▼  按"当前段所在盘"派发
              Disk0 Worker   Disk1 Worker  ...   (每盘 1 线程)
                   │              │
              拥有该盘所有 writer 的 open/close/write/rollover/reclaim
                   ▼              ▼
                 Disk0          Disk1
```

- **writer 归属盘 worker 线程**:open/close/写/翻段/回收全在该盘 worker 线程内执行 → 同盘天然串行,写路径无需加锁(消除 R1/R2)。puller 只投递"请开/关"请求 + 入队。
- **段跨盘迁移**:balance 每段选盘;段收尾后下一段若落到别的盘,writer 句柄在一次带锁短操作里从旧盘 worker 交接到目标盘 worker。
- **每盘一把元数据锁** `pthread_mutex`,只保护 `sb/st/index/badmap/free 计数` 的**内存态**;数据区 pread/pwrite(不同偏移)不加锁,热路径无锁。保护:回放查询快照、写索引、tick SMART。
- **group 锁**:保护 `health_ok/bw/chn_last/rr`。
- 单盘退化为 1 worker + 1 锁,保留归属模型,单盘同样受益。

**备选 A(未采用)**:单 worker + 盘组大锁。最简单、正确,但多盘时一个线程串行写所有盘,不能并行利用多机械盘。B 在单盘时等价于 A,多盘时更优,故选 B。

---

## 4. 并发正确性(堵死 R1/R2/R3)

- **R1**:`alloc_chunk` 仅在盘 worker 线程 + 持元数据锁做 `write_ptr` RMW。
- **R2**:`index_write`/`invalidate_chunk`/`index_next` 全在盘 worker 线程 + 持锁。
- **R3**:`dev_flush` 持锁,且**先在锁内拷贝 sb/st 快照,再算 CRC 写盘**,杜绝算 CRC 与写盘间被改。
- **回放**:`group_query`/`play` 读索引时持元数据锁做一次快照(拷出命中 slot),释放锁后读数据区。
- **tick SMART**:group 锁保护。

---

## 5. O(1) 覆盖回收 + 真实容量

- 开盘时索引全量载入内存(几 MB),建两张表:
  - `chunk → 占用它的 index slot` 反查表 → 回收 O(1),不再扫盘。
  - `free_chunks` 精确计数 + `earliest_time` → 真实容量 / 可录天数(修 Rel3)。
- 落盘位图复用 `compute_layout` 已预留的 2bit/chunk 位图区(当前只用 1bit 坏块,第 2bit 作"有效数据")→ 无需扩预留区。
- 回收改为 O(1) 反查 + 摊还,不在写热路径全扫。

---

## 6. 崩溃一致性 / 可靠性

- **帧载荷 CRC(修 Rel1)**:v2 帧头加 `payload_crc32`(v2 64B 布局重排保留位);回放校验,坏载荷跳过并 gap 标记。
- **周期 fsync(修 Rel2)**:盘 worker 每 ~1s(可配)`fdatasync`;段边界仍 fsync。掉电窗口 ≤1s。
- **段收尾原子化**:SEG_CLOSE 标记写 `frame_count/total_bytes/seg_crc`(结构已存在);OPEN 槽 + 段尾双保险。
- **write_ptr 持久化**:随周期 fsync 推进落盘,崩溃回滚窗口 ≤1s。
- **pread 短读(修 Rel4)**:越界/短读返回明确错误,不静默补 0。

---

## 7. 分片:时间 + IDR 对齐

- Recorder 状态机加 `REC_ROTATE_PENDING`:到达目标时长(默认 60s,可配)后不立即切,等下一个 IDR 再收旧段 / 开新段 → 每段从 IDR 起,回放段界无缝。
- chunk 写满仍强制切(兜底)。

---

## 8. 写盘链(streaming 侧)

- **采集时刻 wall_time(修根因)**:puller 在 `on_video` 用 Time Mapper 打采集时刻(首帧锚定 NVR wallclock,后续按 RTP delta 递推),经队列透传;worker 不再 `time(NULL)`。
- **不丢帧背压**:RecordQueue 增大(按码率×缓冲秒数);满时丢整个 GOP 到下一 IDR 而非丢单帧,并打 gap 标记。per-disk worker 让磁盘带宽成为唯一上限。
- **writer 生命周期**:open/close 变为投递给盘 worker 的请求,消除 puller↔worker 的 writer use-after-free。

---

## 9. 盘上格式 v2 + 兼容

- `RSDK_FORMAT_VERSION=2`;新增:帧头 `payload_crc32`、位图第 2bit 语义、SB 加 `free_chunks`/`earliest` 缓存(可选,内存重建兜底)。
- v1 盘只读挂载兼容(能回放老录像),提示"格式化以启用高可靠";不静默改 v1 盘。
- format/迁移在 `rsdk_format` + `storage_disk` 层加版本判定。

---

## 10. 测试

- 单元:并发 open/write/close/reclaim 压测(TSan)、掉电点注入(扩展现有 `rsdk_rawdev_fault_inject`)、v1→v2 兼容读、段界 IDR 对齐回放、满盘覆盖 O(1) 回收正确性。
- 集成:32 路主子 + 边录边回放 + 拔盘,跑 N 小时无断无损坏。

---

## 11. 分期落地(每期可独立验证)

1. **P0 止血**:线程模型 B 的锁 + writer 归属(消除 R1/R2/R3、UAF)—— 先让它不损坏数据。
2. **P1 回收**:O(1) 回收 + 真实容量(消除满盘周期卡顿→丢帧)。
3. **P2 可靠**:帧 CRC + 周期 fsync + 段收尾原子(format v2)。
4. **P3 分片+时间**:IDR 对齐切片 + 采集时刻 wall_time + 不丢帧背压。

---

## 12. 影响文件(预估)

- `components/recorder/src/rsdk_storgedev.c` — dev 锁、alloc_chunk、dev_flush 快照、free 计数、format v2、pread 短读
- `components/recorder/src/rsdk_rec.c` — writer 写路径、段收尾原子、payload CRC、ROTATE_PENDING
- `components/recorder/src/rsdk_index.c` — 内存索引 + chunk→slot 反查、O(1) invalidate、快照查询
- `components/recorder/src/rsdk_balance.c` — group 锁、per-disk worker 归属/迁移
- `components/recorder/include/rsdk_types.h` — v2 帧头布局、版本常量
- `components/streaming/src/stream_record_worker.c` — per-disk worker、不丢帧背压
- `components/streaming/src/stream_hub.{c,h}` — 队列增大、GOP 丢弃策略
- `components/streaming/src/stream_router.c` — writer open/close 投递、采集时刻 wall_time、Time Mapper
- `components/storage/src/storage_disk.c` — v1/v2 版本判定
