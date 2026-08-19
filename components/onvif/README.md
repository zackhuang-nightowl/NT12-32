# ② ONVIF 协议 (onvif)

NVR 侧 ONVIF 的**两个角色**，协议实现全部来自上游 Happytime（intact 于 `third_party/happytime_onvif_rtsp`），本目录只放 NVR 集成 glue。

## 两个角色

| 角色 | 用途 | 上游实现 |
|------|------|----------|
| **ONVIF 客户端** | NVR 发现/接入 16 路 IPC，取 `GetStreamUri` 给 ③拉流 | `third_party/happytime_onvif_rtsp/source/onvif/`（Discovery/Media/Media2） |
| **ONVIF 服务端** | NVR 对手机 App / 第三方平台暴露自身为 ONVIF 设备 | 同上（`onvif_srv.*`）+ `components/nop/src/onvif/onvif_server_adapter.cpp` |

> `components/nop` 已内置 ONVIF 适配器（`src/onvif/onvif_adapter.cpp`、`onvif_server_adapter.cpp`），
> 是 NOP 能力 ↔ Happytime ONVIF 的桥。本模块与之协作，别重复实现。

## glue 待写

```
onvif/
├── onvif_discovery.{c,h}   WS-Discovery 扫 eth1 → 输出候选 IPC 列表给 app/channel
└── onvif_probe.{c,h}       对选中 IPC 取 profiles/stream_uri/能力，交给 streaming
```

## 数据流位置

```
app/channel  ──► onvif(discovery/probe) ──► third_party/happytime onvif
                        │ stream_uri
                        ▼
                  components/streaming (③)
```

---

## ✅ glue 已实现

| 文件 | 内容 | 校验 |
|------|------|------|
| `include/nvr_onvif.h` | 取流钩子 + 发现 API | ✅ |
| `src/nvr_onvif.c` | 基于 `nop_onvif_*` 实现 | ✅ **对真实 nop_onvif.h 编译通过** |
| `include/nvr_lan34569.h` / `src/nvr_lan34569.c` | UDP 34569 扫网 + 本机应答 | ✅ |

- `nvr_onvif_get_url()` = app 里 `nvr_onvif_get_url` 弱符号的**强实现**——链接本模块后，
  **PoE 16 路 + 自动发现通道立即能取流出图**（main→profile[0], sub→profile[1] → GetStreamUri）。
- `nvr_onvif_discover()` 包 `nop_onvif_discover`（WS-Discovery），回调候选相机给 app/channel。
- `nvr_lan34569_discover()` 广播 DVRIP 探测，应答带 MAC；`nvr_lan34569_server_start()` 供 App 找回本机。
