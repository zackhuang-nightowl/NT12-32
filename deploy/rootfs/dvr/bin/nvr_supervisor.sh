#!/bin/sh
###############################################################################
# nvr_supervisor.sh — NVR 固件 + LVGL 界面进程 看护/重启 脚本
#
# 职责：
#   1) 先起 NVR 固件(nvr_app)，等 127.0.0.1:8089 就绪
#   2) 再起界面(GUI)——界面通过 http://127.0.0.1:8089/APPJsonCmd 与 NVR 交互
#   3) 循环看护两个进程：任一崩溃即按退避重启（记日志）
#   4) 收到 TERM/INT 时优雅停掉两个子进程
#
# 目标机布局（可用环境变量覆盖）：
#   NVR_BIN=/dvr/bin/nvr_app            NVR 固件
#   NVR_CFG=/dvr/config                 NVR 配置目录(含 *.json；运行期生成 nvr_settings.db/meta.db)
#   GUI_BIN=/dvr/bin/NO_xVR_GUI_V2-ca53-2.1.3_argb1555   界面(读 /mnt/custom/*)
#   注：界面配置内容须部署在 /mnt/custom/（见 deploy/README.md）
#
# 用 POSIX sh 编写，兼容 busybox；无外部依赖（nc/curl 可选）。
###############################################################################

NVR_BIN="${NVR_BIN:-/dvr/bin/nvr_app}"
NVR_CFG="${NVR_CFG:-/dvr/config}"
GUI_BIN="${GUI_BIN:-/dvr/bin/NO_xVR_GUI_V2-ca53-2.1.3_argb1555}"
NOP_HOST="${NOP_HOST:-127.0.0.1}"
NOP_PORT="${NOP_PORT:-8089}"
LOG="${LOG:-/dev/console}"          # 或 /dvr/log/nvr_supervisor.log
RESTART_MIN=2                       # 最小重启间隔秒
RESTART_MAX=30                      # 最大退避秒
CHECK_INTERVAL=2                    # 看护轮询间隔秒

NVR_PID=""
GUI_PID=""
NVR_BACKOFF=$RESTART_MIN
GUI_BACKOFF=$RESTART_MIN
RUNNING=1

log() { echo "[$(date '+%Y-%m-%d %H:%M:%S')] supervisor: $*" >> "$LOG" 2>/dev/null; }

alive() { [ -n "$1" ] && kill -0 "$1" 2>/dev/null; }

# TCP 端口就绪探测（有 nc 用 nc；否则用 /proc/net/tcp 查监听；再否则 sleep 兜底）
port_ready() {
    if command -v nc >/dev/null 2>&1; then
        nc -z "$NOP_HOST" "$NOP_PORT" 2>/dev/null && return 0
        return 1
    fi
    # /proc/net/tcp：本地端口十六进制；8089=0x1F99
    hexport=$(printf '%04X' "$NOP_PORT")
    grep -qiE ":$hexport 0" /proc/net/tcp 2>/dev/null && return 0
    return 1
}

start_nvr() {
    log "启动 NVR: $NVR_BIN $NVR_CFG"
    "$NVR_BIN" "$NVR_CFG" >> "$LOG" 2>&1 &
    NVR_PID=$!
    log "NVR pid=$NVR_PID"
}

start_gui() {
    log "启动 GUI: $GUI_BIN"
    "$GUI_BIN" >> "$LOG" 2>&1 &
    GUI_PID=$!
    log "GUI pid=$GUI_PID"
}

wait_8089() {
    i=0
    while [ $i -lt 30 ]; do
        port_ready && { log "NOP 8089 就绪"; return 0; }
        alive "$NVR_PID" || return 1        # NVR 已挂，别再等
        sleep 1; i=$((i+1))
    done
    log "警告: 等 8089 超时(30s)，仍继续起 GUI"
    return 0
}

stop_all() {
    RUNNING=0
    log "收到停止信号，关闭子进程"
    alive "$GUI_PID" && kill -TERM "$GUI_PID" 2>/dev/null
    alive "$NVR_PID" && kill -TERM "$NVR_PID" 2>/dev/null
    sleep 2
    alive "$GUI_PID" && kill -KILL "$GUI_PID" 2>/dev/null
    alive "$NVR_PID" && kill -KILL "$NVR_PID" 2>/dev/null
    exit 0
}
trap stop_all TERM INT

# ---- 启动 ----
[ -x "$NVR_BIN" ] || { log "错误: 找不到 NVR_BIN=$NVR_BIN"; exit 1; }
[ -x "$GUI_BIN" ] || log "警告: 找不到 GUI_BIN=$GUI_BIN（仅看护 NVR）"

start_nvr
wait_8089
[ -x "$GUI_BIN" ] && start_gui

# ---- 看护循环 ----
while [ "$RUNNING" = 1 ]; do
    sleep "$CHECK_INTERVAL"

    # NVR 挂 → 重启（退避）；NVR 重启后 8089 会重建，GUI 会自动重连
    if ! alive "$NVR_PID"; then
        log "NVR(pid=$NVR_PID) 已退出，${NVR_BACKOFF}s 后重启"
        sleep "$NVR_BACKOFF"
        NVR_BACKOFF=$(( NVR_BACKOFF*2 > RESTART_MAX ? RESTART_MAX : NVR_BACKOFF*2 ))
        start_nvr
        wait_8089
    else
        NVR_BACKOFF=$RESTART_MIN
    fi

    # GUI 挂 → 重启（退避）
    if [ -x "$GUI_BIN" ] && ! alive "$GUI_PID"; then
        log "GUI(pid=$GUI_PID) 已退出，${GUI_BACKOFF}s 后重启"
        sleep "$GUI_BACKOFF"
        GUI_BACKOFF=$(( GUI_BACKOFF*2 > RESTART_MAX ? RESTART_MAX : GUI_BACKOFF*2 ))
        start_gui
    else
        GUI_BACKOFF=$RESTART_MIN
    fi
done
