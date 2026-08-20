# 数据库出厂默认值（Seed）

> 对照实码：[nvr_settings.c](../components/config/src/nvr_settings.c) · JSON 默认：[config/system.json](../config/system.json) 等  
> GET 接口应**只读**这些表/KV，不在 C 层再填默认（见 [API_HARDCODED_DEFAULTS.md](API_HARDCODED_DEFAULTS.md)）。  
> **可视化版**：[NVR_DATA_MAP.html](NVR_DATA_MAP.html)（含裸盘索引/元数据/抓拍区 + 全部配置文件）

---

## 两个 SQLite 库

| 库 | 路径（典型） | 用途 |
|----|--------------|------|
| **设置库** `nvr_settings.db` | `<config_dir>/nvr_settings.db` | 通道配置、系统 KV、网络、账户等 |
| **元数据库** `meta.db` | `<config_dir>/meta.db` | 录像/事件元数据索引（运行时写入） |

---

## 播种时机

| 函数 | 何时执行 | 说明 |
|------|----------|------|
| `seed_chn_defaults()` | **每次** `nvr_settings_open()` / 恢复出厂 | `INSERT OR IGNORE` 补齐 chn 0..31，**不覆盖**用户已改行 |
| `seed_from_json()` | **仅首次**（`meta_kv.seeded` 不存在） | 读 `config/*.json` 写 `setting` KV + `local_link`；完成后写 `meta_kv.seeded=1` |
| 迁移 `migrate_settings()` | 固件 `SCHEMA_VERSION` 升高 | 只做 `ALTER TABLE`，不写业务默认行 |

当前 schema 版本：**9**（写入 `meta_kv.schema_version`）。

**v9 迁移**（`from < 9`）：`record_config.stream_type='main'` → `'both'`；未启用的 `cloud_channel`（`enable=0`）`main` → `'sub'`。

---

## 码流策略（DB → 设备行为）

| 表/列 | 出厂默认 | 驱动模块 | 行为 |
|-------|----------|----------|------|
| `record_config.stream_type` | **`both`** | `rec_schedule_apply` → `nvr_stream_set_record_mask` | `both`=主+子 writer；`main`/`sub`=单轨；`disable`=双轨关 |
| `cloud_channel.stream_type` | **`sub`** | `nvr_rec_trigger_event` 登记 + `cloud_uploader` 取段 | `main`/`sub`/`disable`；SET 缺省 `streamType=sub` |
| `record_config.triggers` | 10 类全开 | — | **仅存库**；本地事件过滤尚未接（排程仍看 `schedule(record_event)`） |

策略解析：`components/config/src/nvr_record_policy.c`（`nvr_record_stream_mask` / `nvr_cloud_ch_upload_stream`）。

---

## 库 A · `nvr_settings.db` — 全部 16 表

通道类表实际写入 **chn=0..31** 共 32 行；下表各举 **chn=0**（或 **id=1**）一条代表。

| # | 表 | 出厂初始化 | 示例一行 |
|---|-----|:----------:|----------|
| 1 | **`setting`** | ✅ | `key='system.capacity', ival=32, sval=NULL` |
| 2 | **`auth`** | ❌ 没有 | —（改密/登录流程才写 `id=1`） |
| 3 | **`local_user`** | ❌ 没有 | —（向导 `GUI_createUser` 首次建 Admin 才写） |
| 4 | **`nop_owner`** | ❌ 没有 | —（云绑定 `GUI_login` 成功才写 `id=1`） |
| 5 | **`camera`** | ✅ | `chn=0, enabled=0, type='single', dev_chn=1`（ip/mac 等待发现） |
| 6 | **`camera_capability`** | ✅ | `chn=0, caps_json='', signal='IPC', probed_at=0` |
| 7 | **`record_config`** | ✅ | `chn=0, record_on=1, triggers='pir,pixelChange,human,face,vehicle,animal,package,doorbellRing,lineCross,fieldIntrusion', stream_type='both'` |
| 8 | **`record_schedule`** | ✅ | `chn=0, sched_on=1, rules='[{"id":"rule-id-1","weekdays":[1,2,3,4,5,6,7],"startTime":"000000","endTime":"235959"}]'` |
| 9 | **`push_config`** | ✅ | `chn=0, switch_on=0, dnd_enable=0, dnd_start='2100', dnd_end='0700', dnd_weekdays='1,2,3,4,5,6,7', time_unit='hour', triggers='', photo_on=0, snooze_end=0` |
| 10 | **`cloud_channel`** | ✅ | `chn=0, stream_type='sub', triggers='human,face,vehicle', enable=0` |
| 11 | **`schedule`** | ✅ | `chn=0, domain='record_event', sensor='pixelChange', rule_id='rule-id-1', weekdays='1,2,3,4,5,6,7', start_hms='000000', end_hms='235959'` |
| 12 | **`local_link`** | ✅ | `id=1, network_type='DHCP', mac='', ip='192.168.9.114', subnet_mask='255.255.255.0', gateway='192.168.9.1', dns1='8.8.8.8', dns2='8.8.4.4'` |
| 13 | **`email_alert`** | ❌ 没有 | — |
| 14 | **`ftp`** | ❌ 没有 | — |
| 15 | **`ddns`** | ❌ 没有 | — |
| 16 | **`meta_kv`** | ✅ | `key='schema_version', val='9'`（首启另写 `seeded='1'`） |

### `schedule` 补充

每通道除上表 `pixelChange` 外，`seed_chn_defaults()` 还为 **8 种 sensor** 各插 1 行（共 9 行/通道）：

`human`, `face`, `vehicle`, `animal`, `package`, `doorbellRing`, `lineCross`, `fieldIntrusion`  
（字段与上例相同，仅 `sensor` 不同。）

### 有初始化 / 无初始化 汇总

- **有出厂种子（10 张）**：`setting`, `camera`, `camera_capability`, `record_config`, `record_schedule`, `push_config`, `cloud_channel`, `schedule`, `local_link`, `meta_kv`
- **无出厂种子（6 张）**：`auth`, `local_user`, `nop_owner`, `email_alert`, `ftp`, `ddns`

---

## `setting` 表 — 首启 JSON 播种的全部 KV

来源 `seed_from_json()` + `seed_chn_defaults()`（`record.ch.*` 每次开库 `OR IGNORE`）。

| key | 类型 | 默认值 | 来源 |
|-----|------|--------|------|
| `system.model` | sval | `NT12-32` | system.json |
| `system.device_name` | sval | `NVR-01` | system.json |
| `system.timezone` | sval | `America/New_York` | system.json |
| `system.ntp` | sval | `pool.ntp.org` | system.json |
| `network.eth0.dhcp` | ival | `1` | system.json |
| `network.eth0.ip` | sval | `192.168.9.114` | system.json |
| `network.eth0.mask` | sval | `255.255.255.0` | system.json |
| `network.eth0.gw` | sval | `192.168.9.1` | system.json |
| `network.eth1.vlan_base` | ival | `2001` | system.json |
| `system.capacity` | ival | `32` | system.json |
| `system.poe_ports` | ival | `16` | system.json |
| `system.ip_channels` | ival | `16` | system.json |
| `storage.hdd_full` | sval | `overwrite` | storage.json |
| `storage.encryption` | ival | `1` | storage.json |
| `cloud.switch` | ival | `0` | **system.json** `cloud.switch`；运行期 **`X_NightOwl_setCloudRecordSwitch`** → setting 表 |
| `cloudServer.current` | sval | `tutk` | **system.json** `cloudServer.current` → **`getCurrentClouds`** |
| `cloudServer.available` | sval | `tutk` | **system.json** `cloudServer.available`（JSON 数组 → 逗号分隔 KV） |
| `cloud.async_upload_since` | sval | `` | **system.json** `cloud.async_upload_since`；空=异步上传 HDD 全部事件；非空 UTC 秒=仅该时刻及之后 |
| `tutk.license_key` | sval | `000000` | cloud_tutk.json |
| `tutk.max_sessions` | ival | `8` | cloud_tutk.json |
| `record.ch.N.post_s` | ival | `10` | seed_chn_defaults（N=0..31） |
| `record.ch.N.pre_s` | ival | `5` | seed_chn_defaults（N=0..31） |

**故意不播种**（读 `/User` 或运行时写入）：

- `system.sn` / TUTK `uid` / `authkey` / `av_password` — 见 [nvr_identity](../components/config/src/nvr_identity.c)

---

## 库 B · `meta.db`

| 表 | 出厂初始化 | 说明 |
|----|:----------:|------|
| **`meta_doc`** | ❌ 没有 | 仅运行时事件/录像/AI 元数据写入；DDL 见 [meta_schema.sql](../components/recorder/sql/meta_schema.sql) |

---

## 恢复出厂

`nvr_settings_factory_reset()`：清空用户数据表后重新 `seed_chn_defaults()`；**保留** `meta_kv`（含 `schema_version`、`seeded`），因此**不会**再跑 `seed_from_json()`。

若需完全按 JSON 重种系统 KV，需删库或清 `meta_kv.seeded` 后重开（仅开发/调试场景）。
