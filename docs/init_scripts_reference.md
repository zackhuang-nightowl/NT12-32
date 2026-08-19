# NT12-32 固件启动/初始化脚本参考

> 设备:NightOwl NT12-32（Novatek NA51090 / ODC3516OWL 基座，busybox init）
> 采集自真机 `/etc/init.d/`（2026-08-18，运行槽 mtd6/7）。
> **说明**:`/SYS` 分区**不含任何脚本**——它是持久数据分区（见文末）。所有初始化脚本在 **`/etc/init.d/`**。

---

## 1. 启动总流程（谁调用谁）

```
U-Boot ─▶ kernel ─▶ /init(initramfs) ─▶ 按 /User/ROOTFS_MTD 选 A/B 槽 ─▶ switch_root 到 /ROOTFS(squashfs)
      ─▶ /sbin/init 读 /etc/inittab
             │
             ├─ ::sysinit ─▶ /etc/init.d/rcS ──▶ 按字母序跑所有 S??*（见 §2）
             │                                     └─ S99run ─▶ /dvr/run ─▶ 起 nvr_app + LVGL 界面
             └─ ::respawn ─▶ -/bin/sh（串口控制台 shell）
```

- **inittab**：`::sysinit:/etc/init.d/rcS`（跑 rc 脚本）；`::respawn:-/bin/sh`（串口 shell，非 getty）；`ctrlaltdel`→reboot；shutdown 钩子 kill klogd/syslogd + umount + swapoff。
- **rcS**：遍历 `/etc/init.d/S??*` 按**文件名字母序**执行。`*.sh` 用 `. source` 加速；其余 `fork 子进程 $i start`。
- `rc.network*`、`service.*`、`ntpd.service` **不带 S 前缀**，rcS 不直接跑；由上面的 S 脚本（主要 `S40sysinit`）按需拉起。

启动槽/分区详见 [[flash-and-deploy]]、[[dhcp-spec-and-model-partition]]。

---

## 2. S## 顺序启动脚本（rcS 按序执行）

| 顺序 | 脚本 | 功能 |
|---|---|---|
| S00 | **S00devs** | `mknod` 建基础字符设备节点:`/dev/console`、`/dev/ttyAMA0/1`、`/dev/ttyS000`(串口控制台)。 |
| S02 | **S02udev** | 关闭内核 `hotplug`(`echo "" > /proc/sys/kernel/hotplug`),改用 **udevd**:`udevd -d` + `udevadm trigger` + `settle`(超时20s)动态建设备节点。 |
| S10 | **S10mount** | **核心挂载**。解析 `/proc/mtd` 找 flash/User/SYS 三个 ubifs 分区,`ubiattach`+`mount -t ubifs`:`flash→/flash`、`User→/User`、`SYS→/SYS`。首启/损坏时 `ubiformat`+`ubimkvol` 重建并跑默认脚本(补 rc.conf/resolv.conf 等)。再挂 CRAMFS(`/dev/mtdblock4`→`/CRAMFS`)并跑其 `run.sh`。 |
| S11 | **S11.load.iptable.rules.v4** | 若存在 `/etc/iptables/rules.v4/*.sh` 则逐个 `chmod +x` 后执行(加载 IPv4 防火墙规则)。本机该目录多为空。 |
| S11 | **S11.load.iptable.rules.v6** | 同上,IPv6(`/etc/iptables/rules.v6`)。 |
| S15 | **S15eth1** | **子板探测**。若 `/sys/class/net/eth1` 存在 → touch `/var/run/HAVE_DAUGHTER_BOARD`(内容 `VERSION=eth1`)。后续 `rc.network.eth1` 靠此标志判断 PoE 交换子板是否在位。 |
| S21 | **S21higmac** | 网卡/内存调优:`min_free_kbytes=8192`、`vfs_cache_pressure=200`(为网络驱动 TOE 预留连续物理内存,尽快回收 cache)。GMAC 驱动走 bypass 模式(insmod 注释掉)。 |
| S30 | **S30eth1vlan** | **PoE VLAN 汇聚**。`vconfig add eth1 2002..2017`(16 个 VLAN),`ip addr add 198.18.<口>.100/24`——口 P→VLAN(2001+P)→段 198.18.P.x,NVR 侧取 .100 网关地址。相机固定 198.18.P.1。见 [[poe-vlan-segment-mapping]]。 |
| S39 | **S39zic** | 时区数据软链:`/flash/65`→`/etc/zoneinfo/65`、`/flash/65D`→`…/65D`(zoneinfo 落持久 /flash)。 |
| S40 | **S40sysinit** | **系统主初始化**(见 §3 详解):rc.conf、hostname、localtime、syslogd、web 服务器、**MAC 生成/写网卡**、拉起 `rc.network` & `rc.network.eth1`、telnetd、crond。 |
| S99 | **S99run** | **最后一步:起主程序**。`cd /dvr; . /etc/rc.conf; [ EXTRA_SCRIPT_ENABLE=YES ] 跑 EXTRA_SCRIPT; ./run` → `/dvr/run` 拉起 **nvr_app + LVGL 界面**。 |

---

## 3. S40sysinit 详解（系统主初始化）

1. **rc.conf**:缺失则从 `/etc/rc.conf.FACTORY` 恢复到 `/flash/rc.conf`;`rc.conf.sync` 保护;`source` 之(得 HOSTNAME/IPOPTION_ETH0/TIMEZONE/NTPD_ENABLE… 等)。
2. **GSM/hostname/时区**:补 `/flash/ppp/gsmmodem.conf`;`/etc/hostname`→`/flash/hostname` 软链;`/etc/localtime`→`/etc/zoneinfo/$TIMEZONE`(缺省 50/台湾,本机 rc.conf=65D)。
3. **/ini、syslogd**:`/ini`→`/flash/ini`;`syslogd -s 16`。
4. **Web 服务器**:恢复 boa.conf;**若无 `/dvr/SKIP.WEBSERVER.BOOTUP` 才起** `service.webserver`(否则跳过)。⚠️ 本固件用 SKIP 文件禁掉基座 web,避免占 8089,见 [[issue-8089-apache-conflict]]。
5. **MAC 生成**:`mac_gen_v3`(读 `/dvr/DVR.INFO` 的 `MAC_VENDOR`)为 eth0/eth1 生成并 `ifconfig hw ether` 写入(权威落 `/User/mac_addr_v2[.eth1]`)。
6. **拉起网络**:`rc.network &`(eth0) + `rc.network.eth1 &`(eth1/PoE)。
7. **telnetd**(rc.conf `TELNETD_ENABLE=YES` 才起)、**crond**(有 `/etc/crontab`)。

---

## 4. 网络脚本（rc.network / rc.network.eth1）

| 脚本 | 功能 |
|---|---|
| **rc.network** | **eth0 上行网络**。设 hostname/hosts、`ifconfig lo`、`ifconfig eth0 0.0.0.0`、删默认路由、`resolv.conf`→`/flash/resolv.conf`。按 rc.conf `IPOPTION_ETH0` 分支:**FIXED**(静态 IP+网关+DDNS+telnetd)/ **DHCPC**(`service.udhcpc start` + 轮询取到 IP)/ **PPPOE**/ **GSMMODEM**。末尾起 `service.network.fallover`、按 `NTPD_ENABLE` 起 `ntpd.service`。**注意**:本固件 eth0 实际由 nvr_app 的 `nvr_netime.c` 直接 `udhcpc`(带 `-r 上次IP -x hostname:model`)驱动,见 [[dhcp-spec-and-model-partition]];此脚本为基座通用路径。 |
| **rc.network.eth1** | **eth1 子板网络**。先查 `/var/run/HAVE_DAUGHTER_BOARD`(无则直接退出)。含 `write_dhcpd_conf`(据 dnsmasq.conf 网段生成 `/flash/dhcpd.conf`)。为 PoE 汇聚口做地址/DHCP 关联。 |
| **rc.network.stop** / **.eth1** | 停网:依次 stop ntpd、ddns、telnetd、udhcpc、pppoe(.eth1 停 eth1 侧)。 |
| **rc.reboot** | `umount -a -r; sync; wdt_reboot`(看门狗复位重启)。 |

---

## 5. service.* 后台服务（由 S40sysinit/rc.network 按需拉起）

| 服务 | 功能 |
|---|---|
| **service.dhcpd** | **PoE VLAN 的 DHCP 服务端(ISC dhcpd)**。对 `eth1.2002..2017` 16 段起 `dhcpd -cf /etc/dhcpd_vlan.conf`,租约落 **`/SYS/dhcpd_vlan.leases`**(持久)。本固件权威 VLAN DHCP,见 [[dhcp-spec-and-model-partition]]。`start/stop/restart`。 |
| **service.udhcpc** | **eth0 DHCP 客户端**。`get_model` 读 `/User/OWLModel`(缺省 NOP12-32)→ `hostname $MODEL` + `udhcpc -x hostname:$MODEL`。⚠️ 本固件 eth0 实由 nvr_netime 驱动,此脚本多为基座备用。 |
| **service.udhcpc.eth1** | eth1 侧 udhcpc 客户端(用 `default.script.eth1`)。本固件 eth1 是 DHCP 服务端,通常不用。 |
| **service.webserver** | 基座 Apache/httpd(overlayfs 挂 /webroot、cgi-bin、IE_Plugin)。**本固件默认跳过**(SKIP.WEBSERVER.BOOTUP),防 8089 冲突。 |
| **service.ddns** | 动态 DNS 客户端(`inadyn-mt`,`_inadyn.sh`),flock 单例。 |
| **service.telnetd** | telnetd + 键盘控制(`_telnetd.kbctrl`),rc.conf 开关。 |
| **service.network.fallover** | 网络主备切换守护(`/usr/sbin/network.fallover.sh`)。本机该脚本缺失,启动会报 not found(无害)。 |
| **service.pppoe** | PPPoE 拨号(`adsl-start/stop`)。 |
| **service.gsmmodem** | 4G/GSM 拨号(`gsmmodem-start/stop`)。 |
| **ntpd.service** | NTP 时间同步守护(`ntpd -Ag`),rc.conf `NTPD_ENABLE` 控制(本机=NO)。 |

---

## 6. /SYS 分区内容（持久数据，非脚本）

`/SYS`(mtd 中的 "SYS" ubifs,S10mount 挂载)存**跨重启/OTA 保留的运行期数据**:

| 文件/目录 | 用途 |
|---|---|
| `dhcpd_vlan.leases` (+`~`) | ISC dhcpd 的 PoE VLAN 租约表(service.dhcpd 写,持久) |
| `dhcpd.leases` | eth0/基座 dhcpd 租约(本固件多空) |
| `dhcp_eth0.ip` | **eth0 上次成功 DHCP 的 IP**;开机 nvr_netime 用 `-r` 请求复用(default.script bound 时写),见 [[dhcp-spec-and-model-partition]] |
| `IPCamMedia.ini` | IPC 媒体参数缓存 |
| `VinAttr.ini` / `tp_info.ini` | 视频输入/面板(TP)属性 |
| `HddStatus.ini` / `flagUmount` / `just_fmt` | 硬盘状态/卸载标志/格式化标志 |
| `UpgradeInfo.ini` | 升级信息 |
| `time_code` | 时间码 |
| `usb_modeswitch/` | USB 4G modeswitch 规则 |
| `UBIFS` | 分区就绪标志(S10mount 建/校验) |

> 身份权威源在 **`/User`**(mtd10 最后数据分区):`/User/OWLModel`(机型)、`/User/mac_addr_v2[.eth1]`、UID/SN 等——OTA 不覆盖。

---

## 7. 本固件相对基座的关键改动定位

- **8089 命令口**:由 nvr_app 内建 worker 池服务(非基座 Apache);web 服务器被 SKIP。见 [[http-8089-pool-rebuild]]、[[issue-8089-apache-conflict]]。
- **PoE VLAN DHCP**:统一到基座 **ISC dhcpd**(service.dhcpd + dhcpd_vlan.conf,租约落 /SYS);nvr_app 不再另起 busybox udhcpd。见 [[dhcp-spec-and-model-partition]]。
- **eth0 客户端**:nvr_netime 驱动 udhcpc,`-r 上次IP`(默认复用)+ `-x hostname:model` + default.script RFC5227 ACD(占用才换)。
