# platform — NA51090 / NT98633 硬件适配层

**唯一**允许直接调用 na51090 `hdal` 私有 API 的地方。换 SoC 只改这一层，上层模块不动。

## media_hal — 媒体硬件抽象 ✅ 已实现（对真实 hdal 头编译通过）

封装 hdal 的解码/缩放/显示，给上层稳定接口。管线（参照样例 `playback_1div_to_4div.c`）：

```
hd_videodec ──bind──► hd_videoproc(VPE 缩放) ──bind──► hd_videoout(HDMI/CVBS)
```

| 封装 | 底层 hdal | 用途 | 文件 |
|------|-----------|------|------|
| `mhal_vdec_*` | `hd_videodec` + `hd_videoproc` | 硬解 + 送分屏 | `mhal_vdec.c` |
| `mhal_vout_*` | `hd_videoout` | HDMI/CVBS + 分屏布局 | `mhal_vout.c` |
| `mhal_aout_*` | `hd_audiodec` + `hd_audioout` | 回放 AAC/G711 → 喇叭 | `mhal_aout.c` |
| `mhal_budget_*` | (纯 C) | 746 Mpix/s 解码预算 | `mhal_budget.c` |

### 数据流对接（③ streaming / 回放音频）

```
streaming: CRtspClient.video_cb(Annex-B)
   → mhal_vdec_send(d, annexb, len, ts)     // hd_videodec_send_list
   → hd_videodec 硬解 → hd_videoproc 缩放到窗口 → hd_videoout 上屏
mhal_vout_set_layout(4/9/16) 决定每通道落哪个分屏窗口。

playback audio: 盘上 AAC(stream=2)
   → mhal_aout_send(AAC, ...)               // hd_audiodec_send_list
   → hd_audiodec ──bind──► hd_audioout(喇叭/HDMI)
```

### 编译校验状态

- **本机(x86) `gcc -fsyntax-only -I<BSP>/code/hdal/include`：`mhal_vout.c` / `mhal_vdec.c` 均零错误通过。**
  → 所有 hdal API 调用、结构体字段(`HD_VIDEODEC_PATH_CONFIG.max_mem.*`/`HD_VIDEOPROC_OUT`/`HD_VIDEOOUT_WIN_ATTR`)、
    枚举(`HD_VIDEO_CODEC`)、宏(`HD_VIDEODEC_IN/OUT`/`MAKEFOURCC`/`HD_VIDEODEC_SET_COUNT`) 均匹配真实 SDK。
- **出库需 aarch64 交叉工具链 + hdal.so**（本机只能语法校验，不能链接）。

### 剩余板级 TODO（标在源码，需按 dts/面板调）

- [ ] **内存池/ddr_id**：`cfg_dec_path` 的 `max_mem.ddr_id`、`data_pool[].ddr_id` 应经
      `vendor_common_get_ddrid(HD_COMMON_MEM_DISP_DEC_*_POOL,...)` 取（见样例 `get_all_id`）。
- [ ] **输出时序**：HDMI 3840x2160@30 / CVBS NTSC 的 `hd_videoout_set(ctrl, DEVCONFIG/OUTPUT,...)`（见 `display_with_change_mode.c`）。
- [ ] **解码取图**：`mhal_vdec_recv` 的 YUV mmap 拷贝（抓拍用，见 `pull_decout_and_scale_jpg`）。
- [ ] **解码最大分辨率**：`MHAL_DEC_MAX_W/H` 默认 1920x1088，按通道主码流上限调。

## BSP 引用（不复制）

BSP 全量 1.6G（kernel 4.19 + u-boot + atf + toolchain + hdal 源码 307M）留原处，
路径见 [`bsp_ref.txt`](bsp_ref.txt)。CMake 用 `-DBSP_ROOT=` 覆盖默认路径。
