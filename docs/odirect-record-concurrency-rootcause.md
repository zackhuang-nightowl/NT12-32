# O_DIRECT 录像写崩溃根因与修复(rsdk_rec 聚合缓冲撕裂)

## 症状
真机反复崩溃, core 落在 O_DIRECT 聚合写代码:
- `stage_flush` @ `components/recorder/src/rsdk_rec.c`(memmove/memset 尾扇区处)
- `fill_slot`(段槽初始化处)
两处参数常为垃圾值(部分因 core 截断不可全信),但 **PC 一致落在 rsdk_rec 聚合写路径**。

## 根因(已在主机测试台复现证实)
`rsdk_rec` 模块不变式:**唯一线程(record worker)写 writer**。O_DIRECT 引入了每路可变聚合状态
(`w->stg / stg_len / stg_off / stg_cap`),一旦被**外来线程**并发写同一 writer,聚合缓冲指针/长度
撕裂 → 堆越界 / 野指针 memcpy。

违反点:`components/streaming/src/stream_record_worker.c` 的 `stream_record_worker_flush_sync()`
在**等待循环里调用 `worker_round(g_w.mgr)`** —— 而 `worker_round → rsdk_rec_write_frame` 会在
**调用者线程**(puller/mgmt,经 `stream_close_writer` / `stream_router_close`)执行,与 record worker
线程(持 `g_rec_wmtx`)**并发操作同一 writer**。`flush_sync` 那条路径**不持** `g_rec_wmtx` → 两线程
同时进 `stage_flush` 对 `w->stg` 做 memmove/扩容 → 撕裂。

> 事件片段收尾 / 录像开关 / 重配都会走 `stream_close_writer*` → `flush_sync`,故高频触发。
> 这也同时解释了两个不同崩点(stage_flush 与 fill_slot):并发下任一 writer 操作都是受害者。

## 测试台证据(components/recorder/tests/)
`odirect_concurrency_test.c` + `run.sh`,主机 + ASAN,`RSDK_DIO_FORCE_FILE=1` 走真 O_DIRECT:

| 模式 | 访问方式 | 结果 |
|------|----------|------|
| A       | 单线程(合法) | **PASS 逐字节一致**(400 帧, 含 >512KB 超大帧, 23 段) |
| Bfixed  | 两线程 + 互斥串行 | **PASS**, ASAN 干净 |
| Brace   | 两线程无串行 | **ASAN heap-buffer-overflow / SEGV** @ rsdk_rec.c stage_flush(:74 memmove)/ chunk_put(:108 memcpy) |

结论:**O_DIRECT 聚合写本身正确**(A 逐字节一致);崩溃 100% 来自**并发写同一 writer**(Brace);
**串行化即安全**(Bfixed)。

## 修复
`stream_record_worker_flush_sync()`:**删除等待循环内的 `worker_round(g_w.mgr)`**。改为只
`flush_req=1 + signal`,由 **record worker 线程独占**在其 `worker_round` 内排空队列并置 `flush_done`;
本函数仅轮询等待到完成或超时。超时后调用方照常 `close`(`close` 持 `g_rec_wmtx` 与 worker 串行,安全)。

修复后,唯一写 writer 的线程 = record worker(持 `g_rec_wmtx`);其余 writer 操作
(`close`/`end_event`)均在 `g_rec_wmtx` 内;`open` 由构造后一次性发布指针,安全。即回到 Test A/Bfixed
证明过的安全模型。

## 进一步:一盘一写(per-disk sharding,多盘写带宽)
原为**单**全局 worker 线程串行驱动所有盘的写 → 盘组(多 HDD)时磁头无法并行,聚合吞吐≈单盘。
改为 **N 个 worker 线程,N = 盘组盘数**(启动时定,上限 `REC_MAX_SHARDS=4`):
- 通道 `chn` 静态归属 shard = `chn % N` —— 与 `rsdk_balance` 的 home 盘(`chn % ndisks`)对齐,故 shard k
  的写基本落物理盘 k,多盘磁头**并行**吃满写带宽。
- 每个 writer 仍**只被其归属 shard 线程写**(保持"单线程写单 writer"不变式,热路径零锁);shard 间用各自
  `g_rec_wmtx[k]` 与 close 串行,互不阻塞。`close`/`end_event` 按通道取对应 shard 锁(`stream_rec_wlock(chn)`)。
- `N==1`(单盘/未格式化)完全退化为原单线程行为,**单盘无回归**。
- `flush_sync` 唤醒所有 shard 各自排空,只轮询全局队列空;不在调用者线程碰 writer(即根因修复)。
- 局限:shard 数在 worker 启动时按 group 盘数定;开机盘未格式化(group=NULL)→ 1 shard,之后 `set_group`
  补盘不重分片(需重启才多盘并行,罕见)。

**测试台验证(Shard 模式)**:2 盘组 + 2 线程各写不相交的 8 通道×2 流,并发触发同组 `balance_pick`(组锁)/
不同盘 index 写/切段/事件标记 → **ASAN 干净 PASS**,证明"不相交 writer 并发 + 共享盘组"安全。

## 附二:crypto 左移 UB(已随本次一并修)
`rsdk_crypto.c` subword/key 展开里 `uint8 << 24` 触碰 int 符号位(UB)。已显式转 `uint32_t`;
以 `-fsanitize=undefined -fno-sanitize-recover` 跑 FIPS-197 AES-256 向量 **PASS 无 UB**。

## 回归防护
- `components/recorder/tests/run.sh` 随时复跑四模式(CI 建议纳入:A/Bfixed/Shard 必 PASS,Brace 必 ASAN 报错)。
- 不在 `rsdk_rec` 内加锁(会给热路径引入争用),保持"单线程写单 writer"设计,靠上层分片不违反不变式。

## 附:顺带发现(未在本次改动内,单独跟进)
`components/recorder/src/rsdk_crypto.c:29,35` 存在 `left shift of <uint8> by 24 places cannot be
represented in int` 的 UB(uint8 提升为 int 后左移 24 触碰符号位)。本平台 2's complement 下行为符合
预期、非本次崩溃根因;建议将相关移位显式转 `uint32_t` 消除 UB。
