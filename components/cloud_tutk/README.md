# ④ TUTK P2P (cloud_tutk)

手机 App 经 TUTK P2P 远程访问 NVR（穿透）。SDK intact 于 `third_party/tutk_sdk`，本目录放 glue。

## 依赖

- 库：`third_party/tutk_sdk/Lib/Linux/ArmCortexA53_NT98633_8.4.0/`（本机 SoC 精确匹配）。
- 头：`third_party/tutk_sdk/Include/`（IOTCAPIs / AVAPIs）。
- NOP 侧已有 `components/nop/src/business/caps/cap_cloud.c`（云能力入口）+ `third_party/tutk`(原 glue)。

## 角色

```
手机 App ──TUTK P2P(UID/40633)──► tutk_cloud_agent(本模块)
                                      │  IOTC 会话 + AV 通道
                                      ├─ 控制信令 → components/nop (NOP over TUTK transport)
                                      └─ 音视频 → 复用 streaming/recorder 的码流
```
原机对应进程：`tutk_cloud_agent` / `AVAPIs_Server`（端口 40633，`getIotcAuthKey`/IOTC 登录）。

## glue 待写

```
cloud_tutk/
├── tutk_agent.{c,h}     IOTC 初始化 / 登录 / 会话管理
├── tutk_av.{c,h}        AV 通道：拉 streaming 码流推给 App
└── tutk_transport.{c,h} 把 NOP 协议挂到 TUTK 通道（对接 nop_transport_if）
```

---

## ✅ glue 已实现

| 文件 | 内容 | 校验 |
|------|------|------|
| `include/nvr_tutk.h` | 设备端 P2P API | ✅ |
| `src/nvr_tutk.c` | IOTC 登录→监听→AV server→推流 | ✅ **对真实 TUTK 头编译通过** |

流程：`IOTC_Initialize2 → IOTC_Device_LoginEx(uid,auth_key) → 监听线程{IOTC_Listen→avServStart2} →
nvr_tutk_send_video 对每在线会话 avSendFrameData`。

### 集成点 / TODO
- **码流来源**：由 ③ streaming 旁路调 `nvr_tutk_send_video(chn,data,len,codec,is_key,ts)`（app 注册 remote sink）。
- **鉴权**：`avServStart2` 的 `authFn` 现传 NULL（免鉴权占位），需接 App 账号校验。
- `avServStart2` 已废弃提示 → 生产可换 `avServStart3`；会话断开清理、多 AV 通道映射待补。
- **云存**：TUTK 另有 **VSaaS**（`third_party/tutk_sdk/Include/VSaaS.h`）云录像服务，见根 README 云存说明。
