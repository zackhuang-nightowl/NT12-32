# 事件/云存 索引权威架构(首发重构)

> 目标:**索引区(盘上冻结 64B 槽)= 事件与录像的唯一权威表**;meta.db 仅存"富元数据";
> 删除 meta 的 CLOUD 表。云存状态进索引(可从盘重建)。以**架构稳定、可靠、可重建**为第一。

## 0. 原则
- **盘(索引+数据)= 权威源**;meta.db(sqlite,片外)= 可丢可重建的**富元数据边车**。
- 一个事件 = 索引里的一条**事件段**(rectype≠CONTINUOUS,flags 带 EVENT)。
- 事件三要素都能从索引拿到或重建:**event_id、类型/触发时刻/段址、云存"已上传"态**。

## 1. 盘上格式(冻结 64B 槽,零扩容)
`rsdk_index_slot_t` 现有 `flags(uint8)` + `_pad[3]`。本次:
- **flags 新增位**:`RSDK_SLOT_CLOUD_DONE = 0x08`(已上传)。(VALID=1/EVENT=2/OPEN=4 之外的空位)
- **_pad[3] 用作 event_id 复原**:`pad[0..1]=salt(uint16)`,`pad[2]=delta(触发时刻 − 段首时刻,秒,≤255)`。
  - `event_id = make_event_id(chn, start_time+delta, rectype, salt)`(与 rsdk_cloud_make_event_id 同布局:[chn:8][rectype:8][salt:16][starttime:32])。
  - `触发时刻 = start_time + delta`。
- crc32 覆盖整槽,写槽/改槽时重算(已有)。老盘无此内容 → 首发无兼容包袱。

数据区标记(已有类型,继续用):
- `RSDK_RK_EVENT`(rsdk_mk_event_t):事件内联标记(event_start/end/type_mask)——重建时辅助。
- `RSDK_RK_CLOUD_STATE`(rsdk_mk_cloud_t.state):**云存终态**(DONE)落盘 —— index 丢失时靠它把 `CLOUD_DONE` 位重建回来。

## 2. 写入路径
- 事件触发(nvr_event evt_sink → record_sched):**每个事件都铸造 event_id**(salt=铸造时随机),
  开事件段 writer 时把 **event_id(salt)+触发时刻** 交给 writer。
- writer `fill_slot`(事件段):写 `flags|=EVENT`、`pad=salt+delta`。→ 索引槽自带可复原 event_id。
- **不再** `rsdk_cloud_event_begin` 写 meta CLOUD(该表删除)。
- 云存上传完成:`CLOUD_DONE` 置位(重写该 seg 的索引槽)+ 写 `RK_CLOUD_STATE(DONE)` 标记(供 index 重建)。

## 3. 读取路径(全查索引)
- `queryEventList / 事件月历 / 时间轴`:`rsdk_group_foreach_stream` 按事件 rectype 遍历事件段 →
  用 `rsdk_slot_event_id/time(slot)` 得 event_id+触发时刻 → 列表项 `{chn, 触发时刻, type, event_id, thumbnailUrl}`。
- `eventSnap?eid=`:event_id→(chn,触发时刻)→索引定位事件段→seek 触发帧→**HW JPEG**(本项后续实现)。
- `getEventExtInfo`:查 **meta.db AI_EVENT**(by event_id)——**唯一**保留的 meta 用途(富元数据,开启才有)。

## 4. 云存上传器(重构:状态层从 sqlite 表 → 索引)
- **待传队列** = 索引里 `EVENT && !CLOUD_DONE && (该通道/类型云存开)` 的事件段;开机由扫索引构建内存队列。
- **上传**:用事件段 seg_ref 取片上传。
- **完成**:置 `CLOUD_DONE`(改槽)+ 写 `RK_CLOUD_STATE(DONE)`。
- **运行态**(UPLOADING/FAILED/attempts):仅内存;失败重试;掉电/重启从索引重建(DONE 位持久)。
- **删除** `rsdk_cloud.c` 的 meta-CLOUD-表实现;对上层暴露基于索引的 `list_pending/mark_done/is_done`。

## 5. 重建(rsdk_scan_rebuild2 / 开机自愈)
- 扫数据区帧头:重建索引段(已有);事件段的 event_id 用帧头 `event_id`(或 RK_EVENT 标记)→ 反推 salt/trigger 写回 pad。
- 读 `RK_CLOUD_STATE(DONE)` → 对应事件段置 `CLOUD_DONE`。
- **不再回填 meta**(AI 富元数据丢了就丢;CLOUD 表已删)。

## 6. 变更清单(按提交分类)
**SDK(components/recorder,一次提交):**
- rsdk_types.h:`RSDK_SLOT_CLOUD_DONE`;pad 布局约定。
- rsdk_index:`rsdk_slot_event_id/ time(slot)` 读助手;`rsdk_index_set_cloud_done(dev,seg_id)`(改槽置位)。
- rsdk_rec:writer 带 event_id(salt)+触发时刻;`fill_slot` 写 pad;`rsdk_rec_mark_cloud(w,event_id,state)`。
- rsdk_scan:重建读 RK_CLOUD_STATE→置 CLOUD_DONE;pad 复原;去掉 AI_EVENT 回填。
- rsdk_cloud:改为**索引态**接口(list_pending/mark_done/is_done);删 meta CLOUD 表逻辑。

**业务(app/components,一次提交):**
- record_sched:每事件铸 event_id;交 streaming 的事件段 writer;删 rsdk_cloud_event_begin。
- streaming(stream_router/record_worker):事件段 writer 收 event_id(salt)+触发时刻。
- nvr_cmd_event:queryEventList/月历/时间轴 → 索引 foreach + slot event_id;删 collect_from_meta(CLOUD)/collect_from_disk。
- cloud_uploader:用索引 list_pending + mark_done。
- getEventExtInfo:保持 meta AI_EVENT(唯一 meta 用途)。

**后续(编码成 JPEG 簇,单独):** ② mhal HW JPEG → ③ eventSnap 取触发帧 → ④ 删 rsdk_pic 相机抓拍。

## 7. 可靠性要点
- 索引槽改写(CLOUD_DONE 置位、pad 复原)走 `rsdk_index_write` 的 seg_id upsert(持组锁,与录像/回收串行,幂等)。
- 云存 DONE **双持久**:索引位(快)+ RK_CLOUD_STATE 标记(index 重建源)。
- meta.db 丢失:事件表/云存态从盘全恢复;仅 AI 富元数据丢(可接受)。
