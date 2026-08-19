# nop12-32 — 自包含 NT12-32 固件构建（脱离 ODC .frm 依赖）设计

**日期**: 2026-08-18
**目标机**: NightOwl NT12-32（Novatek NA51090 / ODC3516OWL，16 路 PoE NVR）
**状态**: 设计已审（rootfs=全保留+MANIFEST；引导链=先冻结二进制，BSP 构建作下一步）
**产物命名**: `NOP12_32_<VERSION>.bin`（带版本号）；开发阶段 **VERSION 默认 1.0.0**。
版本单一真源 `nop12-32/VERSION`（默认 `1.0.0`），`build.sh` 读取它命名产物,并与设备内固件版本
(nvr_app `NVR_DEF_FW_VERSION`,现即 1.0.0,见 getDeviceInfo/TUTK/DHCP 主机名同源)保持一致。

---

## 1. 目标与非目标

**目标**：新建 `nop12-32/` 目录,作为**完整、可复现、我们端到端拥有**的嵌入式固件工程——
一条命令从源码/受控产物构出可刷机的 `FW98633A.bin`,**不再依赖 ODC 发布的 `.frm`**。
带齐所有初始化脚本、PoE、DHCP 等内容(受源控、可编辑)。

**非目标(本期)**：
- 不重写 Novatek 媒体 HAL / 交换子板固件(无源码,作为二进制冻结)。
- 不做 rootfs 最小化裁剪(先全保留,MANIFEST 标注来源,后续再裁)。
- 引导链本期不从 BSP 源码编(先冻结已验证二进制;BSP 构建为 Phase 2)。

---

## 2. 现状（脱离前）

固件 = **11 个分区**经 `nvtpack`(FW98633A.ini)打成 `FW98633A.bin`:

| # | 分区 | 现来源 | 本工程处置 |
|---|---|---|---|
| 00 | loader.bin | ODC/BSP 二进制 | **冻结**(Phase1)→ BSP 构建(Phase2) |
| 01/02 | fdt.bin / fdtr.bin | 同上 | 同上 |
| 03 | atf.bin | 同上 | 同上 |
| 04 | uboot.bin | 同上 | 同上 |
| 05 | kernel.bin | 同上 | 同上 |
| 06 | rootfs.squash.bin.raw (槽A) | **我们 mksquashfs** rootfs_ota | **自建**(每次重打) |
| 07 | rootfs1.bin (槽B) | 同 A | 同 A |
| 08 | flash.bin | ODC 数据分区种子 | 冻结种子 |
| 09 | sys.bin | 同上 | 冻结种子 |
| 10 | user.bin | 同上 | 冻结种子 |

当前 rootfs = ODC `.frm` 的 rootfs 基底 + 我们注入(nvr_app/GUI/init/DHCP 改动)。
ODC 依赖点:rootfs 用户态里**我们没源码**的二进制(libhdal 等 Novatek 运行库、硬件 bring-up、
mac_gen_v3、面板/VIN/HDMI 初始化、CRAMFS web)+ 引导链二进制。

**关键利好**:BSP `na51090_linux_sdk` **有全源码**(linux-kernel / u-boot / busybox / root-fs /
Makefile)+ 本板配置 `configs/Linux/cfg_98633_ODC_DEVICE` → Phase2 可从源码重建引导链/内核。

---

## 3. 目录结构

```
nop12-32/
├── VERSION                       # 固件版本单一真源(开发默认 1.0.0)→ 产物 NOP12_32_<VERSION>.bin
├── build.sh                      # 一键: app → rootfs → (bootchain) → nvtpack → NOP12_32_<VERSION>.bin
├── flash.sh                      # A/B 槽刷机(串口+nandwrite;复用本会话流程)
├── README.md                     # 构建/刷机/分区说明
├── MANIFEST.md                   # 每个受控产物的来源分类(见 §5)
├── rootfs/                       # ★ 我们源控的完整根文件系统(冻结自当前能跑的 rootfs_ota)
│   │                             #   一律按 FHS/编码规范归位,分类见下:
│   ├── lib/  usr/lib/            #   ★ 所有运行库依赖(.so)统一在此:libhdal.so、libssl/libcrypto、
│   │                             #     libcurl、TUTK(libIOTCAPIs/libAVAPIs)、libjson-c… + 内核模块 .ko
│   ├── bin/ sbin/ usr/bin/ usr/sbin/  #   可执行:busybox、mac_gen_v3、dhcpd/udhcpc、nvtpack 运行件…
│   ├── etc/                      #   配置:init.d/(全部 init 脚本)、dhcpd_vlan.conf、hosts…
│   ├── usr/share/udhcpc/default.script   #   RFC5227 ACD + 上次IP复用
│   └── dvr/                      #   应用域:nvr_app(bin/)、GUI(lvgl/custom/)、config/*.json、
│                                 #     DVR 框架件(app/GUI/配置每次构建覆盖;其余冻结)
├── bootchain/                    # loader/fdt/fdtr/atf/uboot/kernel.bin —— Phase1 冻结二进制
├── partitions/                   # FW98633A.ini + flash.bin/sys.bin/user.bin 种子 + nvt-all.bin
├── sdk/                          # ★ 只放"用到的"SDK 内容,不全量复制(完整 BSP 仍外部引用)
│   ├── nvtpack/                  #   打包工具 nvtpack(+依赖)、nvtpack.dts
│   ├── configs/cfg_98633_ODC_DEVICE/  #   本板配置(分区 dtsi、内核/uboot defconfig)——供 Phase2 构建
│   └── README.md                 #   记录外部完整 BSP 路径 + 本目录抽了哪些、为何够用
├── tools/                        # 我们的构建/刷机脚本:mksquashfs 封装、flash_a/b.sh、serctl.py、capboot.py
└── docs/
    └── init_scripts_reference.md # 启动/初始化脚本参考(已产出)
```

`nvr_firmware`(应用源码树)保持独立;`build.sh` 交叉编译它,把产物投进 `rootfs/dvr/`。
`na51090_linux_sdk`(BSP)保持独立;Phase2 由 `build.sh` 调其 Makefile 产引导链。

---

## 4. 构建流程（build.sh）

1. **App**: `cmake --build nvr_firmware/build_arm --target nvr_app`(交叉,aarch64-ca53)→
   `cp nvr_app`、GUI 到 `rootfs/dvr/{bin,lvgl/custom}/`。
2. **Rootfs**: `mksquashfs rootfs/ rootfs.squash.bin.raw -comp xz -b 131072 -noappend -all-root`
   → 同一镜像同时作槽A(ITEM06)与槽B(ITEM07)。
3. **Bootchain**: Phase1 直接用 `bootchain/*.bin`;Phase2 用 `sdk/configs/cfg_98633_ODC_DEVICE`
   (指向外部完整 BSP)构建 atf/uboot/kernel,校验一致后替换。
4. **Pack**: 读 `nop12-32/VERSION`(默认 1.0.0)→ `sdk/nvtpack/nvtpack -fw partitions/FW98633A.ini` 产
   all-in-one 后重命名为 **`NOP12_32_<VERSION>.bin`**(如 `NOP12_32_1.0.0.bin`)。
   (FW98633A.ini 内部 nvtpack GEN 名保持原样即可,build.sh 末尾 `mv/cp` 成带版本名。)
5. **Flash**: `flash.sh` 走串口+`nandwrite` 写 A/B 槽(mtd6/7)+切 `/User/ROOTFS_MTD` 重启;
   或整包 `NOP12_32_<VERSION>.bin` 走 SD/FwUpgrade/U盘(见 [[ota-build-frm-based]])。

---

## 5. 一次性抽取 + MANIFEST（脱离 ODC 的核心）

**来源**: 当前能跑的完整 rootfs 树 = `/home/zack/fwbuild/rootfs_ota`(已验证,mksquashfs 它即现固件 rootfs)。
`cp -a rootfs_ota/* nop12-32/rootfs/`,清掉挂载占位(`m/n/o` 等空目录)与我们每次重建的件(nvr_app/GUI)。

抽取来的一切**都作为本固件的正常内容**——保持原文件名、原路径,**不加任何 ODC 前缀/标记**。
MANIFEST 只是一张"能否从源码重建"的内部台账(不改文件本身、不给文件打标),按**能否重建**分三类:
- `built`    —— 我们源码构建(nvr_app、GUI、init 脚本、dhcpd_vlan.conf、default.script、system.json…)。
- `bsp`      —— BSP 可重建(busybox、标准 lib、内核模块 .ko)。
- `prebuilt` —— 暂无源码、二进制冻结(硬件运行库 libhdal.so、mac_gen_v3、面板/VIN/HDMI init、
                CRAMFS/web、DVR.INFO 等)——照常作为固件内容,正常命名。
产出:一张"我们还不能重建的清单",即后续脱离 backlog;也让最小化裁剪有据可依。

引导链/数据分区同样登记:`bootchain/*.bin`、`partitions/{flash,sys,user}.bin` 记为 `prebuilt(frozen)`,
Phase2 逐个改成 `bsp` 并校验二进制/行为一致。

---

## 6. 分阶段（writing-plans 展开）

- **Phase 1 — 自包含可编可刷(本期落地)**:建 `nop12-32/` 骨架;冻结 rootfs_ota→rootfs/、
  pack2 引导链→bootchain/、数据分区→partitions/;写 build.sh/flash.sh/README/MANIFEST;
  **产出一版 FW98633A.bin 并真机刷验通过**(与现固件行为一致)。
- **Phase 2 — 引导链/内核从 BSP 源码**:用 `cfg_98633_ODC_DEVICE` 构 atf/uboot/kernel/modules,
  与冻结二进制逐项比对 + 真机验证(子板/面板/分区/VLAN 全 OK)后切换。
- **Phase 3 — 按 MANIFEST 逐步替换 ODC-BLOB / 最小化**:能重建的移出 ODC-BLOB;裁掉 NVR 不需要的
  (web/cramfs/IE 插件/pppoe/gsm/ddns)——每步单独真机验证。

---

## 7. 风险与对策

- **引导链/内核与硬件强耦合**(子板、面板、分区表、VLAN):Phase1 全程用冻结二进制规避;Phase2 才碰,且以"与现固件逐项一致"为准入。
- **prebuilt 二进制隐藏依赖**(某二进制悄悄依赖另一文件/路径):全保留策略下不删任何件,先保证跑通;最小化留到 Phase3 逐件验证。
- **nvtpack/mksquashfs 工具链**:nvtpack 取自 BSP `tools/`(或现 fwbuild 流程);mksquashfs 用主机 squashfs-tools(需 xz)。build.sh 开头校验工具存在。
- **数据分区种子**(flash/sys/user):含出厂默认(rc.conf/身份等);冻结现有种子,勿轻动。

---

## 8. 成功判据

1. 在干净检出的 `nop12-32/` 上 `./build.sh` 一条命令产出 `NOP12_32_<VERSION>.bin`(默认 `NOP12_32_1.0.0.bin`),无需 ODC `.frm`。
2. `./flash.sh` 刷入真机,启动、出图、8089、PoE/DHCP、GUI 全部与现固件一致。
3. `MANIFEST.md` 完整列出仍为 `prebuilt(frozen)` 的每一件——即剩余"暂不能从源码重建"清单。
