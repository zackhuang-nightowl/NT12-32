# app — 整机集成层

把六大功能模块 + 平台层**编排**成一台 NVR。模块间不横向调用，全部经 `nvr_app.c` 编排。

> 架构总览：[docs/ARCHITECTURE.md](../docs/ARCHITECTURE.md) · 状态：[docs/STATUS.md](../docs/STATUS.md)

---

## 子模块

| 目录 | 职责 | 状态 |
|------|------|------|
| `src/` | `nvr_app.c` 启动编排 + 主循环；`main.c` 入口 | ✅ |
| `config/` | JSON 种子 + SQLite overlay + `channels.json` 持久化 | ✅ |
| `channel/` | 通道增删改、在线状态机、PoE 绑定、NOP 注册同步 | ✅ |
| `preview/` | 分屏 1/4/6/9/12/16、翻页、主/子显示、HDMI 热切 | ✅ |
| `record_sched/` | 事件时窗、满盘策略编排 | ✅ |
| `event/` | AI 事件中枢 → 录像/图标/抓拍 | ✅ |
| `talk/` | 双向对讲（本机 7000） | ✅ |
| `playback/` | 本机 HDMI 回放（墙钟 + AAC 出声） | ✅ |
| `router/` | 8089 中央表 `g_nvr_cmd_table` | ✅ |
| `nop8012/` | 逐 NOP 相机 8012 事件客户端 | ✅ 已在 `nvr_app_start` 启动 |
| `netime/` | eth0/eth1 + 时区/NTP（须在发现前落地） | ✅ |
| `ble/` | BLE 配网命令桥（复用 router） | 🟡 板级 GATT 待真机 |
| `ota/` | OTA 升级框架 | 🟡 |

---

## 启动时序（nvr_app_start）

```
 1. nvr_config_load                         JSON 种子
 2. nvr_settings_open(/flash/nvrcfg)        可写库 + overlay
 3. nvr_net_apply + nvr_time_apply          eth0/eth1、NTP（发现前）
 4. storage scan → assemble                 盘组；失败仅预览
 5. rsdk_meta_open                          /flash/nvrcfg/meta.db
 6. mhal_vout_init(HDMI)                    分辨率可降级回写
 7. nvr_stream_mgr_init                     拉流（主+子常拉）
 8. nop_hub + nop_app                       协议核
 9. rec_sched / preview / persist / event
10. chan_mgr + load + start_all             MANUAL_ONLY 则跳过配置通道
11. GUI_CONFIG.json → set_mode              启动宫格（解码门控）
12. ONVIF 映射后端 + 事件轮询
13. nvr_nop8012_start
14. nvr_playback_create
15. nvr_cmd_router + nop_http_server(8089)
16. cloud_uploader（有盘+UID）
17. apply_remote_access（门控 BLE+TUTK）
主循环: storage/chan/rec tick + 排程(5s) + preview/evt + NTP(60s)
```

---

## 8089 命令路由

LVGL / TUTK 隧道 / BLE 都进 `nvr_cmd_dispatch`。权威表：`router/nvr_cmd_table.c`。
新增本地接口 = **表加一行 + `cmd_<func>`**。

| 路由文件 | 处理域 |
|----------|--------|
| `nvr_cmd_display.c` | 分屏/映射/分辨率/longPolling/playbackControl |
| `nvr_cmd_lan.c` | LAN 搜索/添加/删除 |
| `nvr_cmd_system.c` | 设备/时间/owner/远程访问/TUTK key |
| `nvr_cmd_account.c` | 登录/用户 |
| `nvr_cmd_network.c` | 网口/NTP/DDNS/PoE/端口 |
| `nvr_cmd_storage.c` | 盘管理/格式化 |
| `nvr_cmd_cloud.c` | 云存开关/配置 |
| `nvr_cmd_record.c` | 录像计划/触发 |
| `nvr_cmd_playback.c` | 回放音频/文件列表/USB 备份 |
| `nvr_cmd_event.c` | 事件查询/日历/缩略图 |
| `nvr_cmd_p2p.c` | live / speaker / tunnel |
| `nvr_cmd_ota.c` | OTA |
| `nvr_cmd_misc.c` | 通道聚合/增强安全/云统计 |

未命中本地路由 → NOP 透传（8089→相机）或 ONVIF 翻译。

---

## 弱符号钩子

- `nvr_onvif_get_url(...)`：② ONVIF 实现；未链接时 PoE 自动通道不可用，显式 URL 通道照常工作

---

## 构建

| 目标 | 条件 | 产出 |
|------|------|------|
| `nvr_app_core` | 默认（主机） | 静态库，CI 语法校验 |
| `nvr_app` | `-DNVR_WITH_ONBOARD=ON` | aarch64 整机可执行 |

```bash
# 主机校验
cmake -S .. -B build && cmake --build build -j

# 目标机
cmake -S .. -B build_arm -DNVR_WITH_ONBOARD=ON -DBSP_ROOT=<sdk> \
  -DCMAKE_TOOLCHAIN_FILE=../cmake/toolchain-aarch64-ca53.cmake
cmake --build build_arm -j
```
