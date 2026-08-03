# Bind_IPC — 即插即用 / LAN Add 连接出图流程

> 来源：`NT12-SDK/Bind_IPC.html` / `NT12-SDK/nop_api_doc/Bind_IPC/Bind_IPC.mmap`（MindManager 思维导图，已解出 `Document.xml` 正文）。
> 参考实现：`SDK_NEW/nop_client/nop_bind_ipc.{c,h}`（GUI 侧 LAN Add 命令的 build/parse）。
>
> **本文档随实现推进持续补充。** 相关：路由非透传名单见 [NOP_NONPASSTHROUGH_APIS.md](NOP_NONPASSTHROUGH_APIS.md)。

## 0. 角色与通道分工

- **界面/GUI 进程（LVGL，橙色）**：向 NVR 发 NOP 命令、显示与交互。
- **NVR 进程（蓝色）**：执行发现、连接、鉴权、激活、出图、绑定。
- **通道分工**：
  - PoE 即插即用 = 物理口 **1~16** → 通道 **0~15**（`198.18.<口>.100`，ONVIF 自动）。
  - **LAN Add = 通道 17~32**（数字/局域网相机，手动检索添加）。
- **Manual Add**（手输 IP 页面）：暂不实作。

## 1. 设备发现与三分类（ONVIF-Discovery 解析 `ProbeMatches.Scopes`）

分类由 Discovery 广播的 scope 决定（并参考 manufacturer=Nightowl、MAC 前缀 `54:2b:57`）：

| 类别 | 判定标识（Scopes） | 后端 kind | 连接方式 |
|---|---|---|---|
| **NOP 设备** | `onvif://www.onvif.org/nopVersion/1.0`（或 `/2.0`）<br>`onvif://www.onvif.org/A1C2B3/mac/54:2b:57:70:98:10` | NOP | NOP 私有协议（增强安全模式）|
| **nopOnvif 设备** | `onvif://www.onvif.org/nopOnvif/1.0`<br>（可伴 `onvif://www.onvif.org/nopState/active`）| NOPONVIF | ONVIF digest + 激活 + 算法密码 |
| **onvif 设备** | 无以上任何标识 | ONVIF | 通用 ONVIF digest |
| 电池机设备 | （较老机型）| — | **本期忽略**，命中标 unsupported |

辅助 scope：`onvif://www.onvif.org/bound` 表示设备已被某 NVR 绑定。
- 默认 Protocol 选择 Onvif；命中 nopOnvif 标识则走激活设备流程；命中 nopVersion/A1C2B3 则标记为 nop 协议。
- NVR 的 OnlineDevicesList **不回复** nop 类以外需过滤的设备（按机型过滤逻辑待补）。

## 2. 即插即用「3 种连接出图方案」

给定一个已发现设备，NVR 按 kind 选择方案，**逐个尝试直到 `getStream` 出图成功**。

### 方案 A — NOP 设备（增强安全模式 EnhancedSecurity）
1. `getEnhancedSecurity {channel}`：
   - `random` **非空** → 已启用增强模式：用返回的 `random` 经**算法**算出 password 连接设备。
   - `random` **为空** → 支持但未开启：发 `setEnhancedSecurity {channel, random:"<算法>"}` 让设备开启增强模式并回 `random`，再据此算出 password 连接。
   - `statusCode == 501` → 不支持增强模式，回退基础连接。
2. 连接成功后 `getStream` 出图。
> 关闭增强模式：`setEnhancedSecurity {channel, random:""}`（空串关闭）。

### 方案 B — nopOnvif 设备（ONVIF digest + 激活）
1. 开启 **digest** 的 ONVIF 连接；默认账号 `admin`，密码由**算法**算出（非 `123456`）。
2. 若 Discovery 含 `nopOnvif` 或设备未激活 → 执行 **§3 激活流程**。
3. 激活成功且 `getStream` 出图成功 → 加入 AddedList。

### 方案 C — 通用 ONVIF 设备
1. 开启 digest 的 ONVIF 连接；默认账号/密码 `admin/123456`，或使用 `GUI_setLanDevice` 传入的账号/密码。
2. `getStream` 出图。

## 3. 设备激活流程（X_NightOwl，nopOnvif / 未激活设备）

> 特殊说明：从 PoE 添加设备时，若设备未激活，可**直接**执行激活流程。

- 查询：`X_NightOwl_getDeviceActive {channel}`
  - `status:1` = active；`status:0` = inactive。
  - `statusCode:501` = 不支持激活 → 执行基础流程。
  - 已激活返回 `{statusCode:200, content:{error:"already_active_error"}}`。
- 激活：`X_NightOwl_setDeviceActive {channel, password}`
  - **`password` = 明文口令与 NightOwl 做 AES256 加密所得**（算法见 §5）。
  - 例：`{"func":"X_NightOwl_setDeviceActive","args":{"channel":1,"password":"123adsa123"}}`
- 成功判据：`getDeviceActive == active` 且 `getStream` 出图成功 → 加入 AddedList；**同步该密码到配置库**。
- 失败：`GUI_GetAddedLanDevices` 回复该 channel `status = 7`。
- 上线联通后：① longpolling 推送设备状态；② `GUI_GetAddedLanDevices` 回复 `status = 1`。
- 状态映射：`getDeviceActive.status(1/0)` ↔ `GUI_LanSearch.status(1/0)`（默认设备都是 1）。

## 4. LAN Add 命令集（NVR 侧 handler；结构见 `nop_bind_ipc.h`）

| 命令 | 说明 |
|---|---|
| `GUI_LanSearch` | 检索，5s 后回复；进入该页每 5s 自动检索一次；非 UI 线程/非阻塞 socket 避免卡 UI |
| `GUI_LanAddDevice` | 添加，最长 **60s** 超时后回复；可多选并行、按序号顺序绑定通道 |
| `GUI_GetAddedLanDevices` | 列出已绑定的 17~32 通道 Camera + 状态 |
| `GUI_LanDelDevice` | 仅本地删除通道，不控制相机状态；可多选逐个删除 |
| `GUI_getLanDevice` | config 弹窗读取当前配置 |
| `GUI_setLanDevice` | config 弹窗应用配置 |

### config 弹窗（LVGL 小螺丝图标）交互
- 点击 config → `GUI_getLanDevice` 填充页面；**Protocol=nop 时禁用 userName/password 字段**。
- 点击 apply → `GUI_setLanDevice`（可设 `enhancedSecurity=true/false`）。
- 点击 close → 关闭弹窗。
- 设 `enhancedSecurity` 后：弹窗阻塞，每 2s 查 `GUI_getLanDevice`，共 3 次；`status` 出现 1 即退出阻塞并关闭弹窗，并请求一次 `GUI_GetAddedLanDevices` 刷新列表状态。3 次都非 1 → 状态置 disConnected，改为非阻塞每 2s 轮询直到 status=1 再关弹窗。（增强模式重连可能耗时较长）
- 修改配置后新连接状态需同步到 `GUI_getLanDevice`。

## 5. 密码 / 加密算法（已实现于 [components/crypto]，密钥/盐待填）

> 私钥/盐在 `components/crypto/src/nvr_crypto.c` 顶部：`NVR_ENH_KEY_X` / `NVR_ENH_KEY_Y`（增强）、
> `NVR_ACT_AES_KEY[32]` / `NVR_ACT_AES_IV[16]`（激活 AES 盐）——**填真实值后 `nvr_pw_algo_ready()` 返回 1**。
> 自测已用文档向量校验算法本身（`test_nvr_crypto`）。

### ① 增强安全模式鉴权密码 P_enh（NOP 设备）
```
text     = key[x] + random + key[y]
P_enh    = firstSixteenAlphanumeric( SHA-256(text) )          // 取 hex 前 16 字符
例：key[x]=eT79Uo51sK, random=sfa8a9, key[y]=SzxGJDZxjJ
    SHA256 = 7f9c00a7f43d4716935ca28f3d3b711af9dbc7d00829b18dedcdf75eab48e555
    P_enh  = 7f9c00a7f43d4716
```
`P_enh` 用于 ONVIF/8089/RTSP554/7000对讲 的 **HTTP Digest** 密码（账户固定 `admin`）。
鉴权失败 401 会带 `Random:` 头 → 用它重算 P_enh 重试；402 → 退回普通模式(清 random)；501 → 设备不支持。
API：`nvr_pw_from_random(random)` / 可测核心 `nvr_pw_enhanced_calc(key_x,random,key_y)`。

### ② 8012 消息中心登录（账户/密码 结构）
| 模式 | username | password |
|---|---|---|
| 默认 | `admin` | `admin` |
| 从机(有 ownerId) | `admin` | `ownerId`（NVR 登录 NOP 帐户时服务器给，存设置库 `nop_owner.owner_id`） |
| 增强模式 | unix 时间戳(如 `1758163048`) | `firstSixteenAlphanumeric(MD5(username:8012:P_enh))` |
```
例：username=1758163048, P_enh=7f9c00a7f43d4716
    MD5(1758163048:8012:7f9c00a7f43d4716)=e51cd5478b87be52c5934d16f9578513
    password = e51cd5478b87be52
```
API：`nvr_pw_8012_digest(username_ts, P_enh)`。8012 增强鉴权失败回 `eMSG_CMD_ACK_FAIL`，random 埋在 `reserved[3]`。

### ③ nopOnvif 激活密码 P_act（明文）
```
P_act = UPPER( first16( MD5(NVR_sn + 设备_sn) ) )            // 统一大写 HEX
```
- `NVR_sn` 取自工厂区/设置库 `system.sn`；`设备_sn` 取自 discovery scopes 的 `/serial/`(或 `/sn/`)。
- 激活成功后 P_act 即成为设备密码，NVR 之后用 `admin/P_act` 连接。
- API：`nvr_pw_activate(nvr_sn, dev_sn)`。

### ④ setDeviceActive 密码字段（AES256 密文）
```
password字段 = hex( AES-256-CBC( P_act, NVR_ACT_AES_KEY, NVR_ACT_AES_IV ) )
```
NVR 发送此密文，**IPC 解密得 P_act 并设为自身设备密码**。API：`nvr_pw_act_encrypt(nvr_sn, dev_sn)`。
（编码默认 hex；如 DG 定为 base64 可改 `nvr_pw_act_encrypt`。）

### 连接凭据选择（NVR→相机）
| kind | 连接密码 |
|---|---|
| **NOP**(增强) | `admin` + `P_enh`（Digest）；未启用增强时空/`admin/123456` |
| **nopOnvif** | 仅两种：**空密码**(未激活) 或 `admin/P_act`(已激活)；未激活则先 `setDeviceActive`(发 ④ 密文) |
| **onvif**(通用) | 空 或 `admin/123456`（或 setLanDevice 传入账密） |

## 6. 设备找回

- **34569 LocalLAN 发现**：局域网备用发现协议（UDP 端口 34569），用于 WS-Discovery 不可达/隔离场景，返回 IP/MAC/机型 → 走同一分类与绑定流程。
- **MAC 地址找回**：按已知 MAC 在 WS-Discovery/34569 结果中匹配定位（换网段/DHCP 变更后找回）→ 得当前 IP → 重绑/续用通道。
- NVR 期望：**所有已添加设备只要网络可达且密码正确，上线后立即联通并出图。**

## 7. 已知待检讨问题（备注，暂不处理）

1. WIFI Camera KIT2 模式经 APP 绑定；onvif 能检索到会显示在 Search 列表，但其 8012 消息中心登录密码为 `admin/OwnerId`，NVR 固定用 `admin` 登录会失败（"Failed for username or password not valid"）。
2. 同一 PoE Camera 即使已加入 NVR1，在 NVR2 仍能被 onvif 检索/添加/出图。
3. 即插即用口 1~16 与 LAN 口 17~32 如何让 1~16 也可用于 LAN —— **不做**。
4. 是否需要区分 KIT2 模式添加还是 LAN 模式添加？（待定）
5. 是否需要 NVR 按 model 过滤只显示 POE IPC、不显示 WIFI IPC —— **不做**。

## ReleaseHistory（导图记录）
- 2026-01-09 新增设备配置流程 `GUI_setLanDevice`/`GUI_getLanDevice`
- 2026-01-16 onvif 配置弹窗 disconnect 时非阻塞轮询更新状态，connected 关闭弹窗并刷新 AddedDevicesList
- 2026-03-19 补充 EnhancedSecurity=false 时关闭 IPC 增强模式流程
- 2026-05-07 新增 NVR 激活流程，新增 nopOnvif 设备添加
