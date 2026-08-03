# app — 整机集成层（相当于原厂 LocalHMI）— ✅ 最小切片已实现

把六大功能模块 + 平台层**编排**成一台 NVR。模块间不横向调用，全部经 app 编排。

## 已实现

| 文件 | 内容 | 校验 |
|------|------|------|
| `config/nvr_config.{h,c}` | JSON 配置加载 + **channels.json 扁平化**(device→source→channel) | ✅ **对真实 cJSON 编译并跑通 config/**，产出 22 通道 |
| `src/nvr_app.{h,c}` | 启动编排 + 主循环(维护/重连) + 优雅停机 | ✅ 对真实模块头零错误 |
| `src/main.c` | 入口 + SIGINT/SIGTERM | ✅ |

### 配置扁平化实测（`nvr_config_load("config")`）

覆盖全部通道形态：**PoE 16 路**(含 `expand_ports` 批量 + `198.18.<口>.100` + ONVIF 自动)、
**数字通道**、**单设备多视频源**(鱼眼 1 台→3 通道 / 一 IP 多 URL→2 通道)、**路数可配**(cap/poe/ip)。

### 启动时序（`nvr_app_start`）

```
1. nvr_config_load(config/)            配置 → sys/storage/streaming/通道表
2. nvr_storage_init→scan→assemble      发现盘→装配 rsdk_group（无盘则仅预览）
3. mhal_vout_init(HDMI)+set_layout     显示+默认分屏
4. nvr_stream_mgr_init                  拉流管理器(绑盘组=录像目标)
5. 逐通道 add_channel:                  显式URL直接用; onvif_auto→nvr_onvif_get_url(钩子)
6. nvr_stream_start_all                 起流 → 出图 + 录像
主循环 nvr_app_run: storage_tick + 掉线通道重连, 收到信号退出
```

## 子模块（待补充）

| 目录 | 职责 | 状态 |
|------|------|------|
| `channel/` | 通道增删改、在线状态机、绑定 | 🚧 现由 config 静态加载；动态增删待补 |
| `preview/` | 分屏布局切换、主/子码流、OSD | 🚧 布局已可设，交互待补 |
| `record_sched/` | 录像计划/满盘编排 | 🚧 现常录；计划表待接 recorder.json |
| `event/` | AI 事件→录像/抓拍/推送联动 | 🚧 待接 NPU + nop/tutk |
| `config/` | 配置加载 | ✅ 已实现 |

## 依赖钩子（弱符号，② 接入即生效）

- `nvr_onvif_get_url(ip,port,user,pass,stream,out,n)`：② ONVIF 实现；未实现时弱兜底返回 -1，
  显式 URL 的通道(鱼眼/多URL/数字)照常工作，PoE/自动发现通道待 ② 接入。

## 构建

完整链接需目标机(aarch64)：mhal(hdal) + streaming(happytime)。
本机可单独编译验证 `config/nvr_config.c`（纯 C + cJSON）与各 .c 的语法。
