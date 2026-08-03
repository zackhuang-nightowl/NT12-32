# 部署包 — NVR 固件 + LVGL 界面 + 看护脚本

界面进程(`NO_xVR_GUI_V2-ca53-2.1.3_argb1555`)与 NVR 固件(`nvr_app`)通过
**`http://127.0.0.1:8089/APPJsonCmd`**（NOP JSON over HTTP）交互。`nvr_app` 启动时监听
8089（`nvr_app.c` 已接 `nop_http_server_start`）。看护脚本先起 NVR、等 8089 就绪、再起界面，
并循环监控、任一崩溃即重启。

## 目标机目录布局（`rootfs/` 即按此镜像放置）
```
/dvr/bin/nvr_app                                 NVR 固件（aarch64）
/dvr/bin/NO_xVR_GUI_V2-ca53-2.1.3_argb1555        界面（aarch64，读 /mnt/custom/*）
/dvr/bin/nvr_supervisor.sh                        看护/重启脚本
/dvr/config/*.json                                NVR 配置（首启种子；运行期生成 nvr_settings.db/meta.db）
/mnt/custom/                                      ⭐ 界面配置内容（GUI_CONFIG.json / gui_cfg.ini / _http_debug.json / data/Fonts）
/etc/init.d/S99nvr                                开机自启（调用看护脚本）
```
> **界面的 `mnt` 内容放在固件 Linux 的 `/mnt/custom/`** —— 界面固定从此读字体/配置，见其
> strings：`/mnt/custom/data/Fonts/`、`/mnt/custom/gui_cfg.ini`、`/mnt/custom/GUI_CONFIG.json`。

## 部署（示例）
把本目录 `rootfs/` 的内容合并进设备根文件系统（保持路径），或运行期 rsync/adb push：
```
# 界面配置内容 → /mnt/custom
cp -r rootfs/mnt/custom/*  /mnt/custom/
# 二进制 + 配置 + 看护
cp -r rootfs/dvr           /dvr/
chmod +x /dvr/bin/nvr_app /dvr/bin/nvr_supervisor.sh /dvr/bin/NO_xVR_*
cp rootfs/etc/init.d/S99nvr /etc/init.d/ && chmod +x /etc/init.d/S99nvr
```
运行期依赖（设备 rootfs 需具备）：`libssl.so.3 libcrypto.so.3 libcurl.so.4 libhdal.so` + 标准 C/C++ 运行库（原厂固件均带）。

## 手动启动 / 看护
```
/dvr/bin/nvr_supervisor.sh          # 前台看护（起 NVR→等8089→起GUI→监控重启）
# 或开机自启：/etc/init.d/S99nvr start
```
看护脚本可用环境变量覆盖路径/端口：`NVR_BIN NVR_CFG GUI_BIN NOP_PORT LOG`。
日志默认写 `/dev/console`，可设 `LOG=/dvr/log/nvr_supervisor.log`。

## 看护逻辑（`nvr_supervisor.sh`）
1. 起 `nvr_app <config>`，探测 `127.0.0.1:8089` 就绪（nc / `/proc/net/tcp` 兜底）。
2. 起界面二进制。
3. 每 2s 轮询两个 PID；任一退出→退避(2→30s)重启。NVR 重启后 8089 重建，界面自动重连。
4. `S99nvr stop` / TERM → 优雅关闭两个子进程。
