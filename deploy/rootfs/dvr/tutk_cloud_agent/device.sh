#!/bin/sh
# ODC TUTK command agent 看护。对照 APP_client_Agent.md / command_agent device_ps.sh。
#
# 启动（文档包说明 + 相机串口样例，三参数）：
#   ./device.sh <UID> <CGI_PATH> <PROFILE_PATH>
# 仅 UID（开发包默认）：cgi=/system/bin/cgi  profile=/system/tutk/profile.txt
# 本机不在默认路径，未传参时用本目录 nvr_tutk_cgi + /tmp/tutk_profile.txt。
# 两参数：./device.sh <UID> <LOG_FOLDER>
#
# AVAPIs_Server_CLI 必须是 NightOwl 用本机 NA51090 toolchain 编出的版本
# (MC6630 开发包只作集成参考,不可上本机)。
#
# --cgipath / --profilepath 必须与 interactive_server --start 在同一进程
# （相机日志：先打一次 help，再 Set CGI path / Set profile path / Set UID）。

if [ $# -lt 1 ] || [ $# -gt 3 ]; then
    echo "usage: $0 <UID> [CGI_PATH] [PROFILE_PATH]"
    echo "       $0 <UID> [LOG_FOLDER]"
    exit 1
fi

TUTK_UID=$1
AGENT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
CGI_PATH="$AGENT_DIR/nvr_tutk_cgi"
PROFILE=/tmp/tutk_profile.txt
[ -f "$PROFILE" ] || PROFILE="$AGENT_DIR/profile.txt"
LOG_FOLDER=

if [ $# -eq 3 ]; then
    CGI_PATH=$2
    PROFILE=$3
elif [ $# -eq 2 ]; then
    if [ -d "$2" ]; then
        LOG_FOLDER=$2
    else
        CGI_PATH=$2
    fi
fi

CLI="$AGENT_DIR/AVAPIs_Server_CLI"

if [ -d "$AGENT_DIR/lib" ]; then
    LD_LIBRARY_PATH="$AGENT_DIR/lib:${LD_LIBRARY_PATH:-}"
    export LD_LIBRARY_PATH
elif [ -d "$AGENT_DIR/lib-command" ]; then
    LD_LIBRARY_PATH="$AGENT_DIR/lib-command:${LD_LIBRARY_PATH:-}"
    export LD_LIBRARY_PATH
fi

if [ ! -x "$CLI" ] && [ ! -f "$CLI" ]; then
    echo "AVAPIs_Server_CLI not found: $CLI"
    echo "Drop the NT98633-built agent binary here (see README.txt)."
    exit 1
fi

StartCLI() {
    # 对照 device_ps.sh：cwd = 脚本目录，用 ./AVAPIs_Server_CLI
    cd "$AGENT_DIR" || exit 1
    if [ -n "$LOG_FOLDER" ]; then
        ./AVAPIs_Server_CLI log_setting --level VERBOSE --path "$LOG_FOLDER"
    fi
    # 文档图1/图2：cgi/profile 不在默认路径时在此指定。
    # 同一进程：--cgipath + --profilepath + interactive_server --uid --start
    ./AVAPIs_Server_CLI --cgipath "$CGI_PATH" --profilepath "$PROFILE" \
        interactive_server --uid "$TUTK_UID" --start
}

###start cli###
StartCLI

###start watching###
CLINotExist=0
while true; do
    if [ "$(ps | grep -v grep | grep -c 'AVAPIs_Server')" = "0" ]; then
        CLINotExist=$((CLINotExist + 1))
        echo "AVAPIs_Server is not exist,CLINotExist=$CLINotExist"
        if [ "$CLINotExist" -gt 3 ]; then
            echo "AVAPIs_Server is not exist more than 3 times, Restart!"
            CLINotExist=0
            StartCLI
        fi
    else
        CLINotExist=0
    fi
    sleep 5
done
