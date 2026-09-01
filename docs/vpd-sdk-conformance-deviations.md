# VPD/上屏资源用法 与 SDK 样例 不符点记录

对照 SDK 样例(`code/hdal/samples/media_flow/{liveview_1div_to_4div,playback_1div_to_4div,liveview_with_clearwin}.c`、
`vendor_fisheye_warping/playback_with_fisheye_warping.c`)审计 `platform/media_hal` 的 VPD/解码/上屏资源用法。

现场症状:nvr_app 崩溃循环(49 次 SIGSEGV,每 3-6 分钟),崩前反复
`pif_send_multi_bitstreams: ioctl "VPD_PUT_COPY_MULTI_DIN" driver failed. errno(4)`;
显示驱动内核线程(gm_*/clrwin_*/lcd300vg)卡死 D 态累积 → load 52。
ch2 有活体物理设备却上不了图。

## ★ 不符点 1(根因):没有配置 videoproc(VPE)的 data_pool DDR
- **SDK 样例**:`hd_videoproc_open` 后必须
  ```c
  hd_videoproc_get(path, HD_VIDEOPROC_PARAM_DEV_CONFIG, &vpe);
  memset(&vpe,0,sizeof vpe);
  vpe.data_pool[0].mode   = HD_VIDEOPROC_POOL_ENABLE;
  vpe.data_pool[0].ddr_id = disp0_in_ddrid;   // DISP0_IN 池的 DDR
  vpe.data_pool[0].counts = HD_VIDEOPROC_SET_COUNT(3,0);
  vpe.data_pool[1..3].mode= HD_VIDEOPROC_POOL_DISABLE;
  hd_videoproc_set(path, HD_VIDEOPROC_PARAM_DEV_CONFIG, &vpe);
  ```
- **固件 `mhal_vdec_open`**:`hd_videoproc_open` 后**只** set OUT rect / IN_CROP,**从不配 VPE data_pool** →
  VPE 输出落默认池/默认 DDR。多路负载下默认池不够或落错 DDR → VPD 上屏(VPD_PUT_COPY_MULTI_DIN)
  拿不到 VPE 输出帧 → ioctl 失败 → nvr_app 在这条路踩空指针 SIGSEGV。
- **修**:新增 `cfg_proc_pool()`,按 SDK 用 `disp0_in_ddrid` 配 VPE data_pool,在 videoproc_open 后调用。

## 不符点 2:`vendor_common_get_ddrid` 返回值被忽略
- **SDK 样例**:每次都 `if (ret != HD_OK) { printf("...fail, please check dts"); return ret; }`。
- **固件 `resolve_dec_ddrid`**:3 次调用全**不检查返回**。查询失败时静默留默认 `DDR_ID0`
  (注释自陈 DISP_DEC_OUT/RATIO 在 DDR0 为"0/禁用")→ 解码输出落禁用池 → 无图/失败。
  真机日志实测 `dec_in=0 dec_out=0 dec_out_ratio=0`,与"应在 DDR1"的注释矛盾,疑查询已失败被吞。
- **修**:检查返回值,失败**大声告警**(暴露 dts 配置问题)。

## 不符点 3:`mhal_vout_clear_black` 硬编码 DDR_ID0
- **SDK 样例(clearwin)**:`vendor_common_get_ddrid(HD_COMMON_MEM_USER_BLK, ...)` 查出 USER_BLK 的 DDR 再取块。
- **固件**:硬编码 `DDR_ID0`(注释"直接用,免依赖")。若 USER_BLK 不在 DDR0 → 取块失败(干净返回,
  只是清黑跳过,不崩)。优先级低,但应改回 SDK 查询法。
- **修(已做)**:`mhal_vout_clear_black` 改为 `vendor_common_get_ddrid(HD_COMMON_MEM_USER_BLK,...)`
  查一次缓存 + 失败告警退 DDR0。

## 结论
根因是**不符点 1**(VPE data_pool 未配),直接导致 VPD 上屏失败 + 崩溃。不符点 2 是配套的健壮性缺失
(掩盖了 DDR 解析失败)。按 SDK 稳定处理方式修这三点。
