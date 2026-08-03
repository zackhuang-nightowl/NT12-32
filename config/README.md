# config — 各功能模块运行期配置

统一放置六大功能模块的**运行期**配置。编译期特性开关另见 `components/recorder/rsdk_features.conf`。

## 为什么用 JSON

通道模型是**嵌套**的：`设备(device) → 视频源(source) → 通道(channel)`，一台设备可含多路视频源
（鱼眼/多目/多 URL）。扁平 INI 表达不了这种一对多，故用 JSON（nop 已内置 cJSON 解析）。

## 文件清单与加载顺序

```
config/
├── system.json      系统级：型号 / 通道容量 / 视频输出 / 时间 / 网络(eth0管理·eth1汇聚)   [最先]
├── channels.json    ⭐ 通道表：PoE 口 + 数字通道 + 单设备多视频源 → 通道映射
├── streaming.json   ③ 拉流默认：传输/超时/重连/主子码流/解码后端
├── onvif.json       ② ONVIF：发现(客户端) + 服务端(对 App 暴露)
├── nop.json         ① NOP：角色/传输(8089·TUTK)/鉴权/能力
├── cloud_tutk.json  ④ TUTK：UID/authkey/服务器/AV端口
├── storage.json     ⑥ 存储：裸盘/加密/满盘/盘组/健康(映射到 nvr_storage_cfg_t)
└── recorder.json    ⑤ 录像：运行期计划（连续/事件/预录）
```

加载：`system → storage → channels → streaming/onvif/nop/tutk`（通道依赖系统容量；拉流依赖盘组已装配）。

---

## 通道模型（本项目的核心抽象）

三个概念，一对多逐层展开：

| 概念 | 含义 | 例 |
|------|------|----|
| **device** 设备 | 一个接入端点（一个 IP / 一个 PoE 口） | PoE 口 1 上的 IPC；LAN 里一台 IP 相机 |
| **source** 视频源 | 设备提供的一路可取流的视频（一个 profile / 一个 URL） | 鱼眼原始、展开视图、Lane A/B |
| **channel** 通道 | UI/录像/预览里的逻辑槽位（0..capacity-1） | 屏幕第 3 分屏、回放第 13 路 |

映射规则：**一个 source 绑定一个 channel**；一台 device 可有多个 source → 占多个 channel（= 单设备多视频源）。

### 三类接入（同一张表统一表达）

1. **PoE 通道**（`type:"poe"`）：绑物理 PoE 口 1..16；eth1 内建 DHCP 按口号分 `198.18.<口>.100`；
   ONVIF 自动接入；默认 PoE 口 P → 通道 `P-1`。
2. **数字通道**（`type:"ip"`）：走 eth0/LAN 的独立 IP 相机（非 PoE 口），手动填 IP/URL 或 ONVIF 发现；
   通道号取 `poe_ports` 之后（如 16 PoE → 数字通道从 16 起）。
3. **单设备多视频源**：`type:"ip"` 且 `sources[]` 多项 → 一台设备占多个通道（鱼眼展开 / 多目 / 多 URL）。

### 路数可配

`system.json.channels`：`capacity`(总通道)、`poe_ports`(PoE 口数)、`ip_channels`(数字通道数)。
例：`capacity=32, poe_ports=16, ip_channels=16` = 16 PoE + 16 数字（XVR 形态）；
纯 PoE NVR 则 `capacity=16, poe_ports=16, ip_channels=0`。

---

## channels.json → 各模块结构体的展开（loader 职责）

配置 loader 把 `channels.json` 的 device/source **扁平化**成每通道运行参数，喂给各模块：

```
channels.json (device→source)
        │  flatten by source.channel
        ▼
每通道: { chn, url, user, pass, codec, stream, record, vout_win }
        ├──► nvr_stream_add_channel()   (③ streaming: nvr_stream_chan_cfg_t)
        ├──► rsdk_rec_open_group(chn)    (⑤ recorder: 录像目标)
        └──► preview 布局: channel N → 当前分屏第 N 窗口 (vout_win)
```

- `url` 为空 + `onvif.auto` → 由 ② onvif 模块 `GetStreamUri` 填充（PoE/自动发现场景）。
- `vout_win` 不在通道表里固定：由 `system.json.video_out.default_layout` + preview 运行期布局决定
  （通道 N 默认落到该布局第 N 个窗口；只录不显则 `-1`）。
- `stream=main/sub`：录像走 `record_stream`(默认 main)，分屏预览走 `preview_stream`(默认 sub)。

> loader 实现（`app/config/`）待补：读 JSON → 校验容量/通道号唯一 → 产出上述每通道结构。
> 本次先冻结**配置文件与模型**；loader 随 app 集成层一起写。
