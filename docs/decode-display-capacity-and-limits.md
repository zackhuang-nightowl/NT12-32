# 解码/显示能力、限制与 32 路规划

> 平台:NA51090 / NT98633(SDK 认证纯解码 995 Mpix/s)。本文汇总解码→VPE→videoout 上屏链路的
> 硬件天花板、预算模型、当前配置限制、32/36 路可行性计算,以及一次"切宫格闪屏"回归的根因与修复。

---

## 1. 硬件天花板(硬指标)

| 资源 | 上限 | 来源 |
|------|------|------|
| 总解码吞吐 | **995 Mpix/s** = 1920×1080@480fps ≡ 3840×2160@120fps | `platform/media_hal/mhal_budget.h/.c`(SDK 认证;注明是产品档、非硅片绝对顶) |
| videoout 输入窗 | **64 窗** | SDK `hd_videoout.h: HD_VIDEOOUT_MAX_IN 64` |
| 单路解码最大分辨率 | 最高 8K(7680×4320) | h26xdec.ko 上限(见 [[8k-decode-insmod-gate]]) |
| 解码实例数 | 68 路 | h26xdec.ko `max_total_cam_ch=68` |
| 固件解码器数组 | `MHAL_MAX_CH = 32` | `platform/media_hal/mhal_internal.h` |
| 总物理 DDR | **2GB**(2 通道 × 1GB) | `na51090_linux_sdk/.../nvt-mem-tbl.dtsi` `dram reg=<0 0x80000000>` |
| 保留给媒体池 | **~1.5GB**(DDR0 0x23000000≈587MB + DDR1 整块 1GB) | 同上 `hdal-memory` 节点 |
| Linux 可见 RAM | ~1GB(`MemTotal 1018472kB`) | 真机 /proc/meminfo |

---

## 2. 解码预算模型(admission control)—— 按总量动态判断

`platform/media_hal/mhal_budget.c`:

- 每路开解码时算 `cost = 宽 × 高 × fps`(用**实际接入流**的 W×H×fps;fps 取 `cfg.fps` 或实测 `fps_est`)。
- `mhal_budget_try_reserve()`:`已用 + cost ≤ g_total(995 Mpix/s)` → 放行;超了 → 返回 `-1`,该路**被拒**。
- **关键语义**:任意分辨率/帧率**混搭**都行,只看**总和 ≤ 995 Mpix/s**。不是每路固定档。
- 超预算按**开解码先后**(先到先得),后来的被拒。**被拒通道:只录不显,录像不受影响**。
- 关闭即幂等归还预算(`mhal_budget_release`,见 commit e648f84)。
- `g_total` 可经 `mhal_budget_set_total()` 调整;默认 `MHAL_BUDGET_DEFAULT_TOTAL = 1920×1080×480 = 995 Mpix/s`。
  (旧值 746M 是 NVR_12CH 产品档,已废。)

---

## 3. 当前配置的两道限制(要 32 路必须都过)

### 3.1 软限制:解码预算(见 §2)—— 可通过 `set_total` 调
### 3.2 硬限制:vout 通路池 = 16(**当前真正卡点**)

- 真机跑的 DTB 是 **`cfg_NVR_16CH.dtb`**(`fwbuild/.../dvr/NVR_16CH/image/`)→ 媒体池按 **16 路**分配。
- 第 17 路 `hd_videoout_open(HD_VIDEOOUT_IN(0,chn))` 返回 **-22 (EINVAL),`started=16`**
  (`platform/media_hal/mhal_vdec.c` step3)→ 该路黑屏。
- 构建里**没有 32CH 配置**(只有 NVR_16CH)。
- **固件 app 不定义池大小**(只 `get_ddrid`/`get_block` 用池);池大小/路数在 **BSP/DTB 内存表 + 媒体节点**
  (`nvt-mem-tbl.dtsi` / `nvt-media.dtsi` / `nvt-display.dtsi`)。要 32 路需扩这套配置并重编 DTB。

> 结论:即使解码预算够 32,当前 vout 池只有 16 → 只能出 ~16 窗。**预算(软)+ vout 池(配置)两道坎都过才能 32 路同显。**

---

## 4. 32 路同时:分辨率 × 帧率包络(受总解码 995 Mpix/s 约束)

每帧集 = 32 × W×H;每路预算 = 995 ÷ 32 = **31.1 Mpix/s**。

| 子流分辨率 | 单帧 Mpix | 每路 fps 上限 | 32 路合计 | 32 路内存估算 |
|-----------|-----------|--------------|-----------|--------------|
| **1920×1080** | 2.07 | **15 fps** | 995(顶格) | ~930 MB |
| 1600×900 | 1.44 | 21 fps | 967 | ~700 MB |
| **1280×720** | 0.92 | **30 fps**(@30=885) | 885 | ~450 MB |
| 704×576 (D1) | 0.41 | 30(封顶) | 389 | ~250 MB |

**两个极限档:**
- 极限分辨率:**32 × 1920×1080 @ 15fps**(顶满 995,~930MB) —— 芯片基本按此设计(1080p@480 = 32×15)。
- 极限帧率:**32 × 1280×720 @ 30fps**(885 Mpix/s,~450MB)。

**混搭示例(16×1080p + 16×720p)**:单帧 47.9 Mpix。@15fps=719M ✅;@20fps=958M ✅;@25fps=1198M ❌;@30fps=1438M ❌。
→ **≤20fps 可 32 路混显**(预算侧);仍需 §3.2 的 32 路 vout 池才真出 32 窗。

### 内存估算(每路 1080p,NV12 单帧 1920×1080×1.5 = 2.97MB)
| 池 | 每路 |
|----|------|
| 解码器 DPB + 工作内存(~4帧+行缓冲) | ~14 MB |
| dec_out 输出池(counts=3) | ~9 MB |
| 显示路(dec_out_ratio + disp0_in,降采样/格) | ~2–3 MB |
| **合计** | **~25 MB/路** |

32 路 × 25MB ≈ 800MB + 录像 O_DIRECT staging ~50MB + GUI/FB/OSD ~80MB + 音频 ~10MB ≈ **~940MB** < 1.5GB。**内存够。**

> 超认证上探(硅片"非天花板"):32×1080p@20 需 1327 Mpix/s、@25 需 1659 Mpix/s —— 超认证 33%~67%,
> **不保证、必须真机压测**;25/30fps 的 32×1080p 基本超硅片能力,不建议承诺。

---

## 5. 上屏链路与"重新上图"(redisplay)

- 命令线程(8089)只置标志(`show_win`/`decode_dirty`/`vout_rebind`),**不在命令线程 open/close 解码器**;
  真正 open/close 在**喂解码器那一路的 puller 线程**(`stream_router.c` decode_dirty 兑现块),避免跨线程 UAF。
- 切回宫格(`nvr_preview_set_mode` mode>0):对 ext 块 `unbind + force_rebind`(置 vout_rebind+decode_dirty),
  关不可见路、开可见路、`request_commit`。
- **单画面用主码流会关掉其余路**(省 VPU 预算)→ 回宫格时这些路必须重开(固有代价);频繁切换=固有闪。
- 图重建走**防抖 commit 线程**(`mhal_vout_commit` stop_list/start_list 一次成图),避免逐路各自重建的黑闪。
- 锁顺序(无死锁):`disp_lock → p->lock → {CM_LOCK | mhal_lock} → commit_mtx`;`g_send_lock` 是喂帧叶子锁。

### 已知 redisplay 缺陷(待修,不改则脆弱)
- `stream_decode_open` 失败(EBUDGET / vout -22 等)后**无重试** → 该格永久黑,需再切模式才恢复。
  (注:曾加"退避重试 C2",但对**持续性**失败(vout 池满 -22)会变成每秒重开→整屏闪,**已回退**,见 §6。)
- `setDeviceDisplayExt` 曾漏 `CMD_UNBLOCK` → 持 disp_lock 阻塞 ~2s 饿死 8089(已按兄弟处理器范式修:reconfig 在锁内、wait 移锁外)。

---

## 6. 回归:切宫格"整屏刷/闪"根因与修复

**现象**:手动切显示后回 liveView,所有宫格持续刷(周期性全量重开解码器)。非崩溃(进程一直活)。

**根因链**:
1. 切宫格开路时 `hd_videoout_open` 对第 17 路起返回 **-22(vout 池满 16)**(§3.2)。
2. 我加的 **C2 退避重试**遇到这种**持续性**失败 → **每秒重开→又 -22→再重试**,把所有宫格反复 teardown+rebuild → 整屏刷。
3. 排除项:`W/router=0`(非 codec 抖动)、`wedge=0`(非看门狗)、`Mpix` 无(非预算denial日志)、非 RTSP 重连(帧号连续)。

**修复**:**撤回 C2**(`stream_router.c` 恢复 open 失败即 vdec=NULL 不重试;删 `decode_retry_at`)。
→ 切 ≤16 路宫格不再闪(开失败回到"静止黑",不闪)。

> 教训:退避重试只适合**瞬时**失败;对**持续性资源上限**失败(vout 池满)必须识别并停止重试,否则变刷屏。
> 根治仍是 §3.2 的 32 路池扩容(让 vout_open 不再 -22)。

---

## 7. 要支持 32 路,需要做的事(BSP/DTB 层,须真机验)

1. **媒体配置 16CH → 32 路**:改 `nvt-mem-tbl.dtsi` / `nvt-media.dtsi` / `nvt-display.dtsi`,把解码路数、
   videoout 路数、各池缓冲(dec_out / dec_out_ratio / disp0_in)按 32 路(1080p)分配;重编 `cfg_NVR_32CH.dtb`
   + 对应 module_init。内存已核算够(§4)。
2. **解码预算**:确认 `g_total = 995`(默认已是);若要 32×1080p@>15fps,`mhal_budget_set_total()` 上调并真机压测。
3. **redisplay 健壮性**(可选但建议):`stream_decode_open` 对**可恢复**失败做**有限次**退避重试(区分持续性 vout -22
   不死重试),避免"永久黑格"又不引入刷屏。
4. 全程**真机迭代验证**(VPU 池/上屏,主机台无法离线验)。

---

## 关联
- 本机真机验证快照:2h+ uptime、0 崩溃、0 core、MemAvailable 646MB(录像 O_DIRECT 并发修复稳,见
  [[odirect-record-concurrency-rootcause]])。
- VPD 池早期误配教训:见 `docs/vpd-sdk-conformance-deviations.md` / [[vpd-graph-info-large-pool]]。
- 8K 解码 insmod 门控:[[8k-decode-insmod-gate]]。
