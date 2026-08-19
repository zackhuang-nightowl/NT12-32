TUTK command agent (ODC) — 放到设备 /dvr/tutk_cloud_agent/

本目录由 nvr_app 按 APP_client_Agent.md 相机样例拉起:
  device.sh <UID> <nvr_tutk_cgi> <profile.txt>
AVAPIs_Server_CLI 同一进程: --cgipath + --profilepath + interactive_server --uid --start。

必填(本机构建产物,不在本仓):
  AVAPIs_Server_CLI     NightOwl 用 NA51090/aarch64 toolchain 编出的 agent 主程序
                        (MC6630 开发包里的二进制不能上本机)
  lib/ 或 lib-command/  若 agent 为动态链接,把 so 放这里;device.sh 会 export LD_LIBRARY_PATH

本仓提供:
  device.sh             看护: device.sh UID cgi profile；同一进程 --start
  nvr_tutk_cgi          交叉编译产物:agent popen 本程序; -s 读 /User, -f POST :6061
  profile.txt           NVR 模板(type=videoRecorder)。运行期 nvr_app 生成 /tmp/tutk_profile.txt

出厂凭据(/User/OWL/tutkdata.json,OTA 不覆盖):
  IotcAuthKey = 00000000
  AvPassword  = 888888

隧道端口(本机,外网只走 agent):
  6061 命令(cgi → nvr_cmd_dispatch)
  8554 live RTSP
  7000 对讲
  8089 GUI + GET /eventSnap
