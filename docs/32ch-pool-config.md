# 32 路解码/显示池配置(基于 SDK mempool_calc)—— 最稳定档

> 目标:把媒体池从 NVR_16CH(16 路)扩到 **32 路同时解码上屏**(突破 `hd_videoout_open` 第 17 路 -22)。
> **设计取向:最稳定**——不追 1080p 子流、不超频、不上调解码预算、池留大余量。
>
> **最稳定参数:32 路子流 D1(704×576)@ 25fps,解码预算保持 995 Mpix/s。**
>
> | 维度 | 值 | 稳定性 |
> |------|-----|--------|
> | 解码吞吐 | 32×704×576×25 = **324 Mpix/s** | 占 995 预算仅 **33%**,余 67% → 绝不掉队/无 RESYNC/无闪 |
> | 解码时钟 | 600MHz(真机已是最大,**不动**) | 不超频 |
> | 预算 g_total | **995 保持**(不上调 1327) | 认证值、已验证,不冒险 |
> | 池:解码+显示 | ~245 MB | |
> | 总媒体 | ~431 MB / ~1GB 保留 = **42%** | 大量余量 |
> | vout 窗 | 32 / 硬件 64 | 够 |
>
> D1 子流正好匹配 32/36-div 4K 屏单格尺寸(≈640×360),不浪费解码/内存。
> (若将来要更高清,可上 720p@25=737Mpix/s=74% 预算,但余量小、不如 D1 稳;1080p 见文末"激进档"。)

---

## 1. 已做的代码级修改(SDK 例子)

文件:`na51090_linux_sdk/code/hdal/samples/media_flow/mempool_calc.c`(main() 用 `*_dvr_8ch` 表)。
把 decode 相关池的通道数 **8 → 32**;DISP0_IN(设备级)不动;enc 表不动(NVR 不编码预览)。

| 表 | 原(8 路) | 改后(32 路,最稳定 D1) | 对应池 / DDR |
|----|-----------|--------------|--------|
| `disp_dec_in_pool_dvr_8ch` | `8, 1920,1080, 25, 8192,7,0.3` | `32, 704,576, 25, 2048,7,0.3` | DISP_DEC_IN(码流)/ DDR0 |
| `share_disp_dec_out_pool_dvr_8ch` | `8, 1920,1088, H265,4.0,0.3` | `32, 704,576, H265,4.0,0.3` | DISP_DEC_OUT + RATIO / **DDR1**(大池) |
| `share_disp_cap_out_pool_dvr_8ch` | `0,8, 1920,1152, NVX3,5.0` | `0,32, 3840/3,2160/3, NVX3,4.0` | DISP0_CAP_OUT(VPE 出/格)/ DDR0 |
| `disp_in_pool_dvr_8ch` | `0, 1920,1088, YUV420,4.0` | **不变**(设备级 4K) | DISP0_IN |

> 大解码池(DEC_OUT)放 **DDR1**(整块 1GB),小池/码流/CAP 放 DDR0 → 两通道平衡,单通道不溢出。
> **`mhal_budget` 不改**(保持 995);**h26xdec 时钟不改**(真机已 600MHz 最大)。

> CAP_OUT 用**每格降采样尺寸**(1280×720),而非 32×全屏——否则会过大。实际应按目标宫格布局的
> 单格分辨率取。

---

## 2. SDK 原公式算出的 32 路池大小(主机复算,权威公式见 mempool_calc.c)

公式:`NVX3(w,h)=((((ALIGN(w,32)/32)*3+1)/2)*16)*ALIGN(h,2)*3/2`;DEC_OUT 累加
`= fout + ALIGN(fout,256)*(cnt*ch-1)`;DEC_IN `= (win*2 + bs*(bs_cnt-2)) * ch`,`bs=bitrate/8/fps`。

**最稳定档(32 路 D1 704×576 @25fps):**

| 池 | 大小 | 缩放 |
|----|------|------|
| DISP_DEC_IN(码流) | 12.7 MB | ×ch |
| DISP_DEC_OUT(解码 YUV) | 55.7 MB | ×cnt×ch |
| DISP_DEC_OUT_RATIO | 13.9 MB | ×cnt×ch |
| DISP0_CAP_OUT(VPE 出,1280×720) | 126.6 MB | ×cnt×ch |
| DISP0_IN(4K 设备级) | 35.9 MB | ×cnt |
| **解码+显示合计** | **~245 MB** | |

**DDR 校验(真机 2GB)**:Linux 实测 995MB → 保留媒体 ≈ **1GB**(2GB−Linux)。
32 路解码+显示 245MB + 其余池(user_blk/cnn/osg/fb/audio ~186MB)= **~431 MB / ~1GB → 占 42%,大量余量。** ✅
(单帧 D1 NVX3 = 0.44MB。CAP_OUT 用 1280×720 是对 32-div 单格的保守放大 = 余量。)

---

## 2.5 ⚠️ 作废:手改二进制 DTB 的做法(真机花屏,勿用)

> 曾试过反编译 `cfg_NVR_16CH.dtb`→改 `mem_pool` 4 个池 size→dtc 重编。看似成功(反编译校验 size 对),
> **但烧上真机花屏**。根因:改 DEC_OUT 114→288MB 后,内核按累积 size 排布,**后面所有池(含 DISP0_FB
> 显示帧缓冲)地址整体顺移**,显示/GUI 仍按旧地址写 → 踩显示缓冲 → 花屏。
> **禁止手改二进制 DTB。所有实现按 SDK 源码级别。** 见 [[sdk-source-level-no-shortcuts]]。

## 2.6 ★ 已按 SDK 源码级实现(正确)

找到产品源:`na51090_linux_sdk/code/hdal/samples/hdal_product/NVR_16CH/cfg_NVR_16CH.dts`
(可读 mem_pool:`<ddr_id>, "pool", <blk_size>, <cnt>, "shared"`;**blk_size 单位 = 256 字节块数,不是字节**)。
池分两 DDR:大解码池全在 **DDR1**(整块 1GB 保留给媒体)。

**改法**:`cp cfg_NVR_16CH.dts cfg_NVR_32CH.dts`,把 4 个 per-channel 解码池**块数 ×2**,`dtc` 重编:

| 池(DDR) | 16 路块数 | 32 路块数(×2) | ≈MB |
|----------|-----------|----------------|-----|
| DISP_DEC_IN (DDR0) | 62976 | **125952** | 16→32 |
| DISP_DEC_OUT (DDR1) | 449896 | **899792** | 114→228 |
| DISP_DEC_OUT_RATIO (DDR1) | 101184 | **202368** | 25→50 |
| DEC_TILE (DDR1) | 14840 | **29680** | 3.6→7.3 |

**校验**:DDR1 媒体 = **394 MB / 1024 MB 保留 = 38%**(余 630MB);DDR0 媒体 140MB(+16MB)。
产物:`hdal_product/NVR_16CH/{cfg_NVR_32CH.dts, cfg_NVR_32CH.dtb}` + 部署 `fwbuild/.../cfg_NVR_32CH.dtb`。

> ★ 之前手改二进制花屏的真因 = **单位错**:在二进制里把 DEC_OUT 字段写成 288MB(**字节值**),
> 而它是**块数** → 等于 288M×256 = 73GB 池 → 溢出踩显示 → 花屏。源码级改块数(899792=228MB)才对。
> `mhal_budget` 995 / 时钟 600MHz / 准入逻辑 全部未动。

## 3. (原则)SDK 产品构建流程

改 SDK **产品/板级 media 配置**把媒体档 16CH→32CH,让 **reserved-memory 区 + 所有池 + framebuffer 一起
被 SDK 一致地重排**(不会出现手改那种"只改池 size 致 fb 地址顺移踩显示")。步骤:

1. **池大小按 SDK 权威工具算**:`code/hdal/samples/media_flow/mempool_calc.c`(已改池表为 **32 路 @1080p 上限**:
   `disp_dec_in/dec_out/cap_out` 通道数 8→32)。`make`(aarch64)→ 推设备跑 → 打印各池 required size。
2. **改 SDK 产品配置**的 media 布局(生成 `cfg_NVR_16CH.dtb` 的那份产品配置,含 mem_pool + reserved-memory + fb),
   把池/保留区按 32 路填好 → SDK 产品构建出 `cfg_NVR_32CH.dtb`(池、地址、fb 全对齐一致)。
3. **约束不动**:`mhal_budget` g_total=995、解码时钟 600MHz、"Σ≤995 就出图"准入逻辑 —— 全部保持。
4. **真机 A/B 验 + 回退**:保留 16CH 可回退;逐步 16→25→32 路,看 `hd_videoout_open` 的 -22 是否消失、无花屏。

### (参考)SDK mempool_calc 池大小校准

1. **编译 mempool_calc**(SDK aarch64 工具链 + libhdal):
   `cd na51090_linux_sdk/code/hdal/samples && make`(产物 aarch64,推设备跑)。
2. **设备上运行**:`./mempool_calc` → 打印各池 required size(SDK 权威值,校对 §2 的估算)。
3. **写入 DTB `mem_pool`**:把算出的 DEC_IN/DEC_OUT/DEC_OUT_RATIO/DISP0_CAP_OUT 大小填进产品 DTB
   源(生成 `cfg_NVR_16CH.dtb` 的那份 mem 布局配置),重编出 `cfg_NVR_32CH.dtb`。
   - `mem_pool` 是打包结构体 blob(名字[46]+size[4]+attr),**不要手改二进制**;走产品 DTB 源重生成。
4. **配套(最稳定档均不改)**:
   - **解码预算** `mhal_budget` g_total = **995 保持**(D1 档只用 33%,大余量;不上调=最稳)。
   - **解码时钟**:真机实测 `h264d/h265d_mclk = 600MHz` 已是**最大**(DTS 合法值 450/500/600,`<0>`=eFuse→本片 600);
     h264d_aclk=600、h265d_aclk=350。**不动时钟**(不超频)。
   - videoout 硬件 64 窗、`MHAL_MAX_CH=32` 都够。
5. **真机验证 + 回退预案**(VPU/上屏,主机台无法离线验):
   - 保留原 `cfg_NVR_16CH.dtb`,A/B 或可回退槽刷新 DTB;
   - 开机看 dmesg 池分配是否成功、是否 OOM/池溢出;
   - 逐步验:16 路 OK → 25 路 → 32 路;看 `hd_videoout_open` 是否还 -22。

---

## 4. 风险与边界

- **改错 DTB 池会开不了机/花屏/整机卡**——必须有回退槽,逐步验。
- CAP_OUT/DEC_OUT 是否共享池由产品 `get_pool_share_id` 决定;共享则总量更省(428MB)。
- 32×1080p 受**解码吞吐**约束只能 ~15fps(995 Mpix/s);要更高帧率见
  [[decode-capacity-32ch-budget]] / `docs/decode-display-capacity-and-limits.md`。
- 子流实际分辨率/帧率若低于 1080p@15,池可相应缩小,余量更大。
