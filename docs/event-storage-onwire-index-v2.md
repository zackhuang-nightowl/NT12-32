# 事件存储 · 盘上索引化架构 v2(定稿 · 取代 event-index-architecture.md)

> 本文取代 `event-index-architecture.md`(v1 轻量方案:复用 64B 段槽/pad 复原 event_id/云存 DONE 位/截图现生成)。
> v2 采用**重方案**:独立 128B 事件索引区 + 存截图 blob + 四态云存 + 行业 chunk + 每 chunk 关键帧索引 + 统一扫盘重建。
> 已在 `nvr_firmware_new` 全量实现并主机验证,本次迁移到 `nvr_firmware`。

## 0. 原则
- **盘(索引区 + 数据区自描述记录)= 唯一权威**;事件三样(音视频/截图/云存态)必从索引区可查且可扫盘重建。
- **meta.db = 元数据边车**(仍保留):只存富 AI 元数据,可丢、不重建。
- **所有区尺寸按算法算,无固定上限**,支持 16TB 及更大盘。
- 尺寸/预算参数**定成宏/配置**,实机调试后可调。

## 1. 盘上格式

| 区 | 元素 | 大小算法 | 说明 |
|---|---|---|---|
| SuperBlock/SysTab | 512B / ≤4096B(主备) | 固定 | SysTab 加 `evtidx_*` 字段(占 `_rsv`) |
| 位图 | 2bit/chunk 主备 | `ceil(chunk数×2/8)×2` | |
| 段索引区 | 64B 段槽 | `chunk数 × slots_per_chunk` | 默认 slots=4 |
| **事件索引区** | **128B 事件槽** | `chunk数 × evt_slots_per_chunk` | 默认 64;紧邻段索引区 |
| 数据区 | chunk | `chunk_mib` auto | 见 §2 |
| MetaRegion | 截图 blob + 文档 | `meta_ratio_pct × 盘容量` | **默认 3%**,环形 |

- **保留区对齐 1MiB**(不是整 chunk)→ 大 chunk 下不白扔近一个 chunk。
- 事件槽 128B 字段:`event_id / chn / rectype / type_mask / start_time / end_time / av(disk,chunk,off,end_off) / snap(disk,off,len,ts) / state(4态) / attempts / flags(VALID/HAS_SNAP/OPEN) / crc32`。

## 2. chunk 档位(行业量级,随盘放大)

| 盘容量 | chunk | 参照 |
|---|---|---|
| <8GiB(开发/小盘) | 8 MiB | |
| ≤4TB | 256 MiB | 海康默认 |
| ≤16TB | 512 MiB | |
| >16TB | 1024 MiB | 大盘压索引 |

- 因 chunk 随盘放大,索引/事件区恒 <0.2%,**不限制盘大小**。
- **录像帧不设大小上限**:单帧只受"放进一个 chunk"约束(256MB–1GB),对真实视频等于不设限;`payload_len` u32。

## 3. 事件三样(只读索引区,均可重建)

1. **音视频**:事件槽 av 定位 + 段索引按时间查 → 回放(关键帧 seek)。持续内事件 + 纯事件段都查得到。
2. **截图**:事件**触发时刻**那一帧 → MetaRegion blob(环形),指针在事件槽。
   - 单张按 **20MP** 估:平均 ~6MB,**最大 16MB(宏 `RSDK_PIC_MAX_BYTES`,超出拒绝)**;缩略图常二次缩放 → 实际更小、保留更多。
   - MetaRegion 环形覆盖最旧;读取**必须校验 PIC0 头 event_id==请求 id**,不匹配当无图,**绝不返回错图**。
   - 老事件的记录仍在(音视频/云存态),仅缩略图随环覆盖丢失(可接受降级)。
3. **云存态**:四态 `未上传(PENDING)/上传中(UPLOADING)/已上传(DONE)/待重试(RETRY)`;事件槽内联 + `RK_CLOUD_STATE` 标记落数据区(重建源)。去掉旧"已丢失"。

## 4. 关键帧索引(大 chunk 保拖动)
- 每段 IDR 累积 `(wall_time, chunk内偏移)`,段闭合写一条 `RK_KEYIDX` 记录进本 chunk。
- `rsdk_play_seek(wall)` 优先读表直达最近 IDR,无表则顺扫;回放跳非 FRAME 记录。
- 关键帧张数按段实际 IDR 动态(chunk 内空间为界),无固定上限。

## 5. meta.db(保留,专存元数据)
- **存**:富 AI 元数据 `doc_type = AI_EVENT(1)/AI_FRAME(2)/LPR(3)/FACE(4)/ALARM(6)/POS(7)`,`json` 全文;主键索引 `event_id`(与事件槽对齐)、`ts`、`(chn,event_id)`、`doc_type`;车牌/人脸/类别/颜色表达式索引;WAL。
- **删**:~~SNAP(5) 截图表~~、~~CLOUD(8) 云存表~~ → 迁到事件索引区。
- **写**:AI 引擎结构化结果 → `rsdk_meta_put(event_id, json, doc_type)`(接上之前缺口)。
- **读**:`getEventExtInfo(time)` → 事件索引区按 time 找覆盖该刻的事件槽 → 取 event_id → meta.db 按 event_id 查 json → 返回;查不到→空。
- **联动**:视频 chunk 覆盖 → `rsdk_meta_purge_chunk` 清行(有界)。
- **丢失**:富详情空;事件列表/回放/截图/云存全部照常(从索引)。**不从盘重建 meta.db**。

## 6. 统一扫盘重建(动态、无上限、整合 rsdk_repair)
- `rsdk_scan_rebuild2`(nvr_firmware 已有 progress/finalize_open)单 pass:重建段槽 + 事件槽(av/类型/时间)+ 云存态(`RK_CLOUD_STATE`,**上传中→待重试**);再顺扫 MetaRegion PIC0(512 对齐)回填截图指针(校验 event_id)。
- **聚合容器动态分配**(按 `index_slot_count`/`evtidx_slot_count`),**取消 SCAN_MAX_SEG/EVT 固定 8192**;16TB 段~6万/事件~百万级不截断。
- 接进 `rsdk_repair.c` 开机分级自检:段/事件索引 CRC 或一致性异常 → 自动重建。

## 7. 参数 → 宏/配置(实机可调)

| 参数 | 宏/配置 | 默认 | 位置 |
|---|---|---|---|
| chunk 档 | `chunk_mib` | auto | rsdk_features.conf |
| 段索引密度 | `slots_per_chunk` | 4 | rsdk_features.conf |
| 事件密度 | `evt_slots_per_chunk` | 64 | rsdk_features.conf |
| MetaRegion 占比 | `meta_ratio_pct` | **3.0** | rsdk_features.conf |
| 单张截图上限 | `RSDK_PIC_MAX_BYTES` | 16 MiB(20MP) | 宏 |
| 后录/预录出厂值 | `post_record_s`/`pre_record_s` | (待定) | **nvr config 文件**(UI 可覆盖) |
| 云存切片/轮询 | `slice_ms`/`poll_interval_s` | (待定) | nvr config 文件 |
| GOP/关键帧间隔 | | (待定) | nvr config 文件 |

## 8. 迁移落点(nvr_firmware)
- **新增**:`rsdk_evtidx.h/.c`、`examples/evtidx_demo.c`、`examples/rebuild_demo.c`。
- **改 recorder**:`rsdk_types.h`(事件槽/evtidx字段/RK_KEYIDX/云存4态)、`rsdk_storgedev.c`(chunk档+事件区布局+1MiB对齐)、`rsdk_rec.c`(事件槽生命周期+关键帧+mark_cloud)、`rsdk_play.c`(seek)、`rsdk_pic.c`(截图指针+512对齐+16MB上限+读校验event_id)、`rsdk_cloud.c`(态迁事件槽)、`rsdk_scan.c`(统一重建,动态无上限)、`rsdk_repair.c`(接重建)、`CMakeLists.txt`、`rsdk_features.conf`(meta_ratio 3%)。
- **改 app**:`nvr_cmd_event.c`(查询走事件区)、`nvr_app.c`(eventSnap+自愈)、`nvr_playback.c`(rectype=-1)、`cloud_uploader/uploader.c`(云存走事件区)、`nvr_event.c`(AI元数据→meta.db)、config 文件(业务参数出厂值)。

## 9. 可靠性
- 索引槽改写走 seg_id/event_id upsert(持组锁,与录像/回收串行,幂等)。
- 云存 DONE 双持久:事件槽(快)+ RK_CLOUD_STATE(重建源)。
- meta.db 丢失:核心三样从盘全恢复;仅 AI 富元数据丢(可接受)。
