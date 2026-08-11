# app — 整机集成层

把六大功能模块 + 平台层**编排**成一台 NVR。模块间不横向调用，全部经 `nvr_app.c` 编排。

> 架构总览：[docs/ARCHITECTURE.md](../docs/ARCHITECTURE.md) · 状态：[docs/STATUS.md](../docs/STATUS.md)

---

## 子模块

| 目录 | 职责 | 状态 |
|------|------|------|
| `src/` | `nvr_app.c` 启动编排 + 主循环；`main.c` 入口 | ✅ |
| `config/` | JSON 配置加载 + channels.json 扁平化 + SQLite overlay | ✅ |
| `channel/` | 通道增删改、在线状态机、PoE 绑定、发现落地 | ✅ |
| `preview/` | 分屏 1/4/9/16、翻页、主/子码流、HDMI 分辨率切换 | ✅ |
| `record_sched/` | 录像计划、满盘策略、事件时窗编排 | ✅ |
| `event/` | AI 事件中枢 → 录像/抓拍/云存联动 | ✅ |
| `playback/` | 本机回放引擎（GUI_playbackControl） | ✅ |
| `router/` | 8089 命令路由（display/lan/storage/cloud/record/ota/event/playback） | ✅ |
| `nop8012/` | NOP 8012 事件中心客户端（逐相机） | 🟡 attach 待接线 |
| `netime/` | 网络时间/NTP | ✅ |
| `ble/` | BLE 配网通路 | 🟡 板级链路待真机 |
| `ota/` | OTA 升级框架 | 🟡 |

---

## 启动时序（nvr_app_start）

```
1. nvr_config_load + nvr_settings overlay     配置 → 通道表
2. nvr_storage_init → scan → assemble          发现盘 → rsdk_group
3. mhal_vout_init(HDMI) + set_layout           显示 + 默认分屏
4. nvr_stream_mgr_init                         拉流管理器（绑盘组=录像目标）
5. 逐通道 add_channel:                          显式 URL 直接用；onvif_auto → nvr_onvif_get_url
6. nvr_stream_start_all                        起流 → 出图 + 录像
7. nop_http_server(8089) + nvr_cmd_router      协议入口
8. nvr_record_sched / nvr_event / cloud_uploader  调度与联动
主循环: storage_tick + 掉线通道重连 + 信号退出
```

---

## 8089 命令路由

LVGL 界面经 `127.0.0.1:8089/APPJsonCmd` 发 NOP JSON 命令，`nvr_cmd_router` 分派：

| 路由文件 | 处理域 |
|----------|--------|
| `nvr_cmd_display.c` | 分屏/分辨率/通道映射 |
| `nvr_cmd_lan.c` | LAN 搜索/添加/删除 |
| `nvr_cmd_storage.c` | 盘管理/格式化 |
| `nvr_cmd_cloud.c` | 云存开关/配置 |
| `nvr_cmd_record.c` | 录像计划/触发 |
| `nvr_cmd_playback.c` | 回放控制 |
| `nvr_cmd_event.c` | 事件查询 |
| `nvr_cmd_system.c` | 系统设置/NTP |
| `nvr_cmd_ota.c` | OTA 升级 |

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
