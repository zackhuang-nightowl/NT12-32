# PoE VLAN / PVID 映射与 DHCP 分配（NT12-32）

> 2026-09-02 真机定位并验证（16 口全出图）。本文说明 PoE 口 → VLAN → 网段 → IP → 通道
> 的完整链路、三层一致性要求、交换芯片 VLAN 模式、以及 DHCP 租约机制与排障要点。

## 1. 硬件架构

- NVR SoC 只有一个物理汇聚口 **eth1**；16 个 PoE 供电口在 **PN1116AT V6.0** 交换芯片上。
- 相机 → RJ45 → PoE 板下行口 → PN1116 交换芯片(打 VLAN tag) → **内联 trunk** → SoC eth1
  → 内核 VLAN 子接口 `eth1.<vid>`。
- 相机是真实 NightOwl（OUI `54:2b:57`），走私有协议 **8012**（非 ONVIF 80）。

```
相机 → PoE口P → PN1116(tag PVID) → 内联trunk → eth1 → eth1.<PVID>
```

## 2. 口 → VLAN(PVID) 映射表 —— **非线性，相邻奇偶对调**

PN1116 规格书"端口对应 PVID 示意图" + 真机确认（硬件口1 = 图上 PVID2002）：

| 口 | PVID/VLAN | | 口 | PVID/VLAN |
|---|---|---|---|---|
| 1 | 2002 | | 2 | 2001 |
| 3 | 2004 | | 4 | 2003 |
| 5 | 2006 | | 6 | 2005 |
| 7 | 2008 | | 8 | 2007 |
| 9 | 2010 | | 10 | 2009 |
| 11 | 2012 | | 12 | 2011 |
| 13 | 2014 | | 14 | 2013 |
| 15 | 2016 | | 16 | 2015 |

**规律：`口P → VLAN = 2000 + swap(P)`**，其中 `swap` 对调相邻奇偶：`1↔2, 3↔4, …, 15↔16`
（奇数口走上排偶数 VLAN，偶数口走下排奇数 VLAN）。

> ⚠️ 这是 **PN1116 这块板的硬件固定映射**，不是线性 `口P→VLAN(2000+P)`。固件若按线性假设，
> 每个口的相机会错位到相邻口通道（GUI 上出图口整体错开一格）。

## 3. 段(IP) ↔ 通道保持"口号=段号"，靠 swap 在网络层吸收

设计目标：口 P 的相机 → 拿 `198.18.P.1` → 绑口 P 通道（NVR 业务层 `口P↔段P` 不变）。
为此，把 swap 放在**网络层**：让 `eth1.(2000+swap(P))` 服务网段 `198.18.P`。

| eth1.V | 服务段 = swap(V−2000) | NVR 侧网关 IP | 发给相机 |
|---|---|---|---|
| 2001 | 段2 | 198.18.2.100 | 198.18.2.1 |
| 2002 | 段1 | 198.18.1.100 | 198.18.1.1 |
| 2003 | 段4 | 198.18.4.100 | 198.18.4.1 |
| 2004 | 段3 | 198.18.3.100 | 198.18.3.1 |
| … | (相邻对调) | | |
| 2015 | 段16 | 198.18.16.100 | 198.18.16.1 |
| 2016 | 段15 | 198.18.15.100 | 198.18.15.1 |

## 4. 三处 base + swap 必须一致

`vlan_base = 2000`（对齐 ODC / 规格书 PVID 2001–2016），且三处都按 swap：

| 层 | 文件 | 规则 |
|---|---|---|
| NVR 建接口 | `app/netime/nvr_netime.c` | `POE_PVID_OFF(p)=(p&1)?p+1:p-1`；`vid=vlan_base+POE_PVID_OFF(p)`；`eth1.vid = 198.18.p.100` |
| 交换机 VLAN 接口 | `/etc/init.d/S30eth1vlan` | `eth1.V = 198.18.swap(V-2000).100` |
| DHCP | `/etc/dhcpd_vlan.conf` | `subnet 198.18.S → interface eth1.(2000+swap(S))` |
| DHCP 监听 | `/etc/init.d/service.dhcpd` | `eth1.2001..2016` |
| seed | `components/config/src/nvr_settings.c` + `config/system.json` | `vlan_base=2000` |

> `vlan_base` 首次从 `system.json` seed 进 `/flash/nvrcfg/nvr_settings.db`，`seeded` 护栏**不重播种**。
> 改 system.json 后必须清 `/flash/nvrcfg` 或恢复出厂，否则 netime 仍用旧值 → 与 S30 差 1 → eth1 接口并存/dup。

## 5. 交换芯片必须工作在 **VLAN 隔离模式**（拨码/硬件）

- PN1116 系统模式三档：**默认 / AI / VLAN**（硬件拨码设定，**固件/uboot 不配置这颗芯片**，ODC 固件里也没有任何配置它的代码）。
- 规格书：VLAN 模式下"**所有供电端口数据不能互通**"（严格隔离，含广播/组播）。
- **必须拨到 VLAN 隔离模式**。否则端口互通、**广播(DHCP DISCOVER)跨 VLAN 泄漏** →
  同一相机的 DISCOVER 串到多个 `eth1.<vid>` → dhcpd 在错误段发地址 → 地址被错占 →
  多台抢同一 `x.1` → 只部分口拿到 IP、出图口飘。
- 判据：`cat /proc/net/arp` / 租约里**同一 MAC 不应出现在多个段**；若出现即为泄漏（隔离没生效）。

## 6. DHCP 分配机制（关键：按"口/VLAN"，不是按 MAC）

- dhcpd 按 **DISCOVER 从哪个 VLAN 接口进来** 决定用哪个段的 subnet 发地址 → **IP 跟口走，不跟 MAC 走**
  （相机交叉换口不出错；租约里的 MAC 只是台账，不是分配依据）。
- 每段地址池 **单地址** `range 198.18.S.1 198.18.S.1`（一段一相机一地址）。
- 我方相对 ODC 的改进：**去 NOIPC class 过滤**（给所有相机发）、**去 ping-check**、租约 300s。
- NVR 绑定：`nvr_channel.c on_discovered` 按相机 IP **第 3 段 = 口号** 匹配到口通道（不看第 4 段），
  并校验前两段 = `198.18`（防 LAN 相机误绑）。

### 僵尸租约 / 换机拿不到 IP

- 标准 dhcpd **只在租约到期(300s)才释放**，**不主动检测设备离线**。单地址池下：
  旧相机离线/换机后，其租约在 300s 内仍占着唯一地址 → **新相机拿不到 IP(no free)**。
- 处理：换机/大面积重插后，**清租约让所有相机重新 DHCP**：
  ```sh
  /etc/init.d/service.dhcpd stop
  : > /SYS/dhcpd_vlan.leases        # 清空持久租约(在 /SYS ubifs，reboot 不清)
  /etc/init.d/service.dhcpd start
  ```
  相机随后重新 DISCOVER，各自拿回本段地址。（reboot 也可，但清租约更快。）

## 7. 完整出图链路（一台相机从插入到上屏）

```
口P → PN1116 tag VLAN(2000+swap(P)) → eth1.(2000+swap(P))
   → dhcpd 发 198.18.P.1（按接口/口）
   → NVR 按第3段=P 绑口P通道(onvif_ip 198.18.P.100)
   → nop8012 登录 198.18.P.1:8012 → ACK_OK
   → 拉流 → 硬解 → 上屏
```

## 8. 排障速查

| 现象 | 定位 | 命令 |
|---|---|---|
| 出图口整体错开一格 | swap 映射未生效(线性) | `ifconfig eth1.2001` 应=198.18.2.100(swap)，非 198.18.1.100 |
| 出图口飘/只部分出图 | 广播泄漏(交换芯片没在 VLAN 隔离模式) | 租约/ARP 里同一 MAC 出现在多个段 = 泄漏 |
| 换机后某口拿不到 IP | 僵尸租约占位(单地址池) | 清 `/SYS/dhcpd_vlan.leases` + 重启 dhcpd |
| 某口 eth1.<vid> RX=0 | 该口物理没相机/没供电/网线 | `ifconfig eth1.<vid> | grep RX` |
| 全 16 口不出图/并存冲突 | DB seed vlan_base 与 S30 差 1 | 清 `/flash/nvrcfg` 重 seed；确认 `vlan_base=2000` |

## 9. 三个必要条件（16 口全出图，缺一不可）

1. **固件 swap 映射**：netime + S30 + dhcpd 三处按 `口P→VLAN(2000+swap(P))`（已编译烧录）。
2. **交换芯片 VLAN 隔离模式**：拨码到 VLAN 模式，广播不跨 VLAN。
3. **清理僵尸租约**：让所有相机重新 DHCP 拿到本段地址。

真机验证（设备 telnet:23 直连 root）：三者齐 → **16 口全出图**。
