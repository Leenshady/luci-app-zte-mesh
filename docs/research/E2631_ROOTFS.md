# E2631 rootfs：取回来、解开、里面到底有什么

2026-11-01。真机（市售 AX3000，自报 E2631）上按 [MESH_INTEROP](MESH_INTEROP.md) §4.2.3 的办法把
`/dev/mtdblock8` 取回，得到 `rootfs.jffs2`。这份文档记录**取法与解包的两个坑**，以及
**从里面挖到的、对 mesh 项目真正有用的东西**——其中最关键的是 §6：厂商 mesh 配置库的表名与列名。

| 项目 | 值 |
|---|---|
| 文件 | `rootfs.jffs2`，17 825 792 B（17 MiB，与 `/proc/mtd` 的 mtd8 尺寸相等） |
| sha256 | `9cf3dd0c2f9c9ae095567b72f1b5bda0b266497b60a10da834077b114fb2dacd` |
| 首 8 字节 | `85 19 01 e0 31 00 00 00` —— JFFS2 magic `0x1985`，**第一次节点就是 dirent** |
| 结论 | **读路径返回明文**，[MESH_INTEROP](MESH_INTEROP.md) §4.1 的判据就此结案，不需要任何密钥 |

---

## 1. 两个必须先说清的坑

### 1.1 这台机器没有 `dd`（见 [MESH_INTEROP](MESH_INTEROP.md) §4.2.3）

取的时候用 `cat /dev/mtdblock8 > /var/rootfs.jffs2`。块设备读到尾即 EOF，`cat` 与
`dd if=… of=…` 等价，只是不能 `skip`/`count`。size 必须等于 `/proc/mtd` 里 mtd8 的 size。

### 1.2 这份 JFFS2 是**变体**：inode 头 68 字节，不是 64

这是本次最有价值的技术发现，也是最容易踩的坑：**标准工具（jefferson / mtd-utils 的
`jffs2dump`）解这份镜像会得出满地垃圾**，而且不报错。

标准 `struct jffs2_raw_inode` 是 64 字节，`data` 紧跟 `data_crc`。这份镜像里
`totlen = 68 + csize`，也就是说 `data_crc` 之后还有 4 个字节才到 `data`：

| 节点内偏移 | 字段 | 说明 |
|---|---|---|
| 0x00 | magic / nodetype / totlen / hdr_crc | 12 B，与标准一致 |
| 0x0C | `ino` | ← **注意：与标准相比 ino/version 顺序是反的** |
| 0x10 | `version` | |
| 0x14 | `mode` | |
| 0x18 / 0x1A | `uid` / `gid` | u16 each |
| 0x1C | `isize` | |
| 0x20 / 0x24 / 0x28 | `atime` / `mtime` / `ctime` | |
| 0x2C | `offset` | |
| 0x30 | `csize` | |
| 0x34 | `dsize` | |
| 0x38 / 0x39 / 0x3A | `compr` / `usercompr` / `flags` | |
| 0x3C | `data_crc` | 覆盖 `data` 区，**不含 0x40 那 4 字节** |
| 0x40 | **未知 4 字节** | 每节点都不同；既不是压缩块的 CRC、也不是解压后的 CRC、也不是 CRC-32C；含义未定 |
| 0x44 | `data` | `78 5e …` 是 zlib 头，位置由此确认 |

判据不靠猜：拿 `data_crc` 当 oracle，让工具在两种布局里投票——

```
inode-layout zte68 {'std64': 0, 'zte68': 25}      # 25 个数据节点全部只认 68 字节布局
bad hdr_crc 0   bad data_crc 0                    # 全镜像 5894 个 inode 节点，无一例外
```

两个附带事实，都与标准不同、都已被逐条验证：

- **字节序是小端**：magic 常量是 `0x1985`，磁盘上是 `85 19` → 小端。ARM 设备按主机序写，
  所以这份镜像所有 u16/u32 都要按小端解（大端镜像的首两字节会是 `19 85`）。
- **JFFS2 的 CRC 不是 zlib 的**：reflected 多项式 `0xEDB88320`、`init=0`、**没有收尾异或**。
  直接用 `zlib.crc32()` 会把全部 6412 个候选节点的 `hdr_crc` 判成坏节点（这是本项目踩过的坑）。

工具在 `tools/lab/unpack_jffs2.py`（纯标准库，自动判字节序、自动选 inode 布局）：

```sh
python tools/lab/unpack_jffs2.py rootfs.jffs2 --list          # 只看清单与统计
python tools/lab/unpack_jffs2.py rootfs.jffs2 -o out/         # 解到目录
```

## 2. 解包结果

```
dirent 518   inode 5894   endian little   layout zte68
bad hdr_crc 0   bad data_crc 0   compression: none 3930 / zlib 1964
inodes 518   dirents 518   orphans 0
```

518 个 inode 里 **411 个是 `/dev` 下的设备节点**（mdev 模板，`chr`/`blk` 占绝大多数），
真正的文件/目录/软链只有 **107 个**。17 MiB 的分区里绝大部分是 `0xFF` 空闲区——JFFS2 不会回收旧节点，
所以 5894 个节点里有很多是被覆盖的历史版本。

**这个 rootfs 是个薄壳**，应用全在 `/kmodule`：

```
/bin  -> ./kmodule/bin        /lib  -> ./kmodule/lib        /usr  -> ./kmodule/usr
/home -> ./kmodule/home       /userconfig -> /defcfg        /linuxrc -> /bin/busybox
```

## 3. 分区与挂载：`/etc/init.norm` 是权威表

比 `/proc/mtd` 更可靠，因为这是内核实际执行的挂载序列（`/etc/rc` → `LOADTYPE` 判定 → `init.norm`）：

| 设备 | 挂载点 | 文件系统 |
|---|---|---|
| mtdblock3 | `/wlan` | jffs2 |
| mtdblock4 | `/usercfg` | jffs2 |
| mtdblock5 | `/defcfg` | jffs2（`/userconfig` 指向这里） |
| mtdblock8 | `/` | jffs2（来自 cmdline `root=/dev/mtdblock8`） |
| mtdblock9 | `/UUPlugin` | jffs2 |
| mtdblock10 | `/owdptsplugin/bin` | jffs2 |
| mtdblock11 | `/status` | jffs2 |
| mtdblock12 | `/DPIPlugin` | jffs2 |
| mtdblock13 | `/UnifiedStorage` | jffs2 |
| mtdblock14 | `/owjdxbplugin/bin` | jffs2 |

tmpfs 由 `/etc/rc` 建：`/var` 40 MiB、`/tmp` 20 MiB、`/tracelog` 2 MiB、`/DPITMP` 60 MiB、
`/uadd` 1 MiB、`/owjdxbplugin/tmp` 3 MiB。**这与 [MESH_INTEROP](MESH_INTEROP.md) §4.2.3 那张实测表逐项吻合**，
等于把那份"实测"补上了来源。

`/etc/rc` 还有一条对我们有用：`LOADTYPE=$(cat /proc/zte/verinfo/loadtype)`，
`3` → `init.norm`（正常），`2` → `init.debug`（调试），其它 → 直接退出。

## 4. 身份：这台机器自报什么

| 项 | 值 | 出处 |
|---|---|---|
| hostname | **`E2631`** | `init.norm` 里硬编码 `hostname "E2631"` |
| 登录账号 | **`Z500_w9bZ`**（uid 0），不是 `root`/`admin` | `/etc/passwd` |
| 口令哈希 | `$5$nN.Jx1V$REfSjBW…`（SHA-256 crypt） | `/etc/shadow` |
| 产品代号 | **`E2631_QD_MESH`** | `/etc/hardcode` |
| 构建时间 | `Wed Aug 26 13:34:34 CST 2026` | `/etc/version` |
| 内核 | 4.1.25，Buildroot 2015.08.1，gcc 4.9.3 | `/etc/rc` 的 `KERNELVER` + ELF `.comment` |
| Wi-Fi | **MT7916**（`CONFIG_WLAN_MTK_7916`），11AX、160 MHz、**11k/11v** | `autouserconf` / `initwifi` |
| 产品串 | `ZXHN E2631` / `ZXHN E2631_Black` / `ZXSLC SR6101` / `ZTE E2631` | cspd 字符串表 |
| 版本读取点 | `/proc/zte/verinfo/{bootVersion,softVersion,hardVersion,loadtype}` | `initwifi`、cspd、`upgradetest` |

`ps` 里 cspd 的属主显示 `Z500_w9b`，就是这个账号——之前从 Zlion 仓库的 `ps` 里看到的那一列，
现在有了来源解释。

**`/etc/init.debug` 是废文件，别指望它给调试 shell。** 整篇是遗留的 Broadcom 模板
（`bcm_*.ko`、`/etc/cmf/cmfcfg`、`/proc/rtl_reg`、`/etc/Wireless/CL1800/host_insmod.sh`），
这套硬件一个都不存在；末尾的 `#telnetd&` 是注释掉的。**把 `loadtype` 改成 2 只会起不来**，
不会掉进调试模式。这一条推翻了"改 loadtype 换调试模式"这种想当然的做法。

## 5. 两份 `.config`：厂商把编译开关放进了 rootfs

这是本次文档里证据强度最高的一类——**编译期开关，不是运行时字符串**。

### 5.1 `/etc/autokernelconf`：完整内核 `.config`（3498 行）

```
CONFIG_ZX279128S_ASIC=y          CONFIG_ZX_DOUBLE_NAND=y
CONFIG_MTD_NAND_ZTE_DENALI=y     CONFIG_MTD_NAND_ZTE_SPIFC=y
CONFIG_JFFS2_FS=y                CONFIG_MTD_UBI=y
CONFIG_CMDLINE="mem=128M console=ttyAMA0,115200n8"
CONFIG_CMDLINE_FROM_BOOTLOADER=y
CONFIG_CRYPTO_AES=y              （但没有任何 dm-crypt / NAND 加解密驱动）
```

三条推论：

1. **NAND 加密确实不在内核里**——[MESH_INTEROP](MESH_INTEROP.md) §4.1 那条"读路径无 crypto 调用"
   从符号层面升到配置层面：连一个碰 NAND 的 crypto 消费者都没编。
2. `mem=128M` 只是 **fallback**，`CONFIG_CMDLINE_FROM_BOOTLOADER=y` 说明 Bootloader 会覆盖它，
   所以同一份 rootfs 能跑在 256 MB 的机器上（实测就是 `mem=256M`）。
3. `CONFIG_ZX_DOUBLE_NAND=y` 是 ZX SoC 的"双 NAND"选项，与 kernel1/kernel2 双槽一致。

### 5.2 `/etc/autouserconf`：cspd 的编译开关，612 个 `@define`

这份文件等于**厂商 mesh 功能矩阵**，逐条都是"编进去了没有"的硬答案：

| 开关 | 含义 |
|---|---|
| `CONFIG_CSPCORE_IEEE1905` / `_TOPOLOGY` / `_ACD` | 1905 协议栈、拓扑、ACD 都在 |
| `CONFIG_CSPCORE_WLAN_MULTIAP__V2` / `CONFIG_CSP_MULTI_AP` / `_MULTI_AP_MGR` | Multi-AP V2 + 管理面 |
| `CONFIG_CSPCORE_IEEE1905_ESSIDHIDE_SYNC` / `_MULTIAP_DHCPOPTION_60` | ESSID 隐藏同步、DHCP option 60 区分 |
| `CONFIG_CSPCORE_MESH_AUTO_SETUP` / `_MESH_MANAGER_IP_ACCESSIBLE` / `_TOPO_NOTIFY_CONTROLLER_ON` | 自动组网、管理 IP 可达、拓扑上报 |
| `CONFIG_CSPCORE_ADEV_NOTIFY_OVER_MESH` / `_GUEST_WIFI_HN_MESH` | 设备通告可以走 mesh；访客 Wi-Fi 走 HNMesh |
| `CONFIG_CSP_WLAN_SUPPORT_80211KV` | **11k + 11v，没有 11r** |
| `CONFIG_CSP_WLAN_VLAN` / `_ELINK` / `_EXTENDER_LINK_BY_DRIVER` / `_MTK_EXTENDER` | WLAN 侧 VLAN、eLink、扩展器链路走驱动 |
| `CONFIG_CSPCORE_EBTABLES_FFE_T` / `_VLAN` / `_VLANCFG_T` | 桥接走硬件转发引擎，带 VLAN |
| `CONFIG_CSPCORE_DB_ENCRY` / `_AES_ENCRY` / `_AESCBC_ENCRY` / `_ENCRY_COMKEY` | 配置库加密，含"公共密钥" |
| `CONFIG_CSPCORE_SENDCMD` + 内核 `CONFIG_CSPKERNEL_SENDCMD` | `sendcmd` 是一等公民 |
| `CONFIG_CSPCORE_OPEN_TELNET` / `_TELNET_V1` | **telnet 开关是固件内建功能** |
| `CONFIG_CSPCORE_ZLINK_VERSION v1.0.20251030` | Z-Link（中兴 mesh 品牌）版本 |

`CONFIG_CSP_WLAN_SUPPORT_80211KV` 只有 11k/11v，**没有 11r** —— 这与 [FEATURE_STATUS](../FEATURE_STATUS.md)
D7 的"固件不支持 FT"是同一件事的两面，现在有编译期证据，不再是推断。

## 6. mesh 配置库的表名与列名（本轮最重要的产出）

`/kmodule/bin/cspd`（3 MB，已 strip，但字符串表完整）里把每张表的列名**按 schema 顺序连续排放**。
于是不用解 `db_default_auto_cfg.xml`、不用上机 `sendcmd`，离线就能读出整张表结构。
区间 `0x22c580–0x22d7c0`，下面是按表名切开的整理。

> **2026-11-01 当晚已实测回填**（三条只读 `sendcmd 1 DB p`，原文整理在
> [E2631_DB_SNAPSHOT](E2631_DB_SNAPSHOT.md)）：`MultiAPMaster` 与 `MultiAPDomain` 的推断基本命中，
> **`P1905ComCfg` 切错了表**——§6.1 已改写并注明错因。本节保留离线推断的过程，
> **实测值一律以 [E2631_DB_SNAPSHOT](E2631_DB_SNAPSHOT.md) 为准。**

### 6.1 `P1905ComCfg` —— **实测只有 8 列**（推断在这里切错了表）

实测（`RowCount=1`）：`Enable=1`、`EnableIsSync=1`、`Policy=1`、`DropNum=0`、`MarketConfEn=1`、
`MarketSign=8000`、`DropRoam=0`、`A1Enable=0`。就这八个，是 1905 总开关、市场配置、丢弃策略。

**自纠**：本文初版把 `SetRoleEnable` / `RoleElect` / `EnMasterOnly` / `MeshName` / `MaxHop` / `CurHop` /
`En1905Msg` / `EnSendM1EachBand` / `ACD.*` / `VersionMode` / `SteeringPolicy` / `EnAgentSteer` /
`STAMac` / `TargetBand` 等四十多个列名都切给了这张表，**全部不在这张表里**。错因有两层，
第二层比第一层严重：

1. 字符串表里列名连续排列、表名夹在中间当分隔，而这八个名字后面紧跟的表名被我漏切，于是把后面整段并了进来；
2. **仓库里早有正确答案**：[SCHEMA](../milestones/SCHEMA.md) 的「补充（2026-10-12）」一节写着
   "`P1905ComCfg`：SR 上有 8 个字段：`Enable`（36）、`EnableIsSync`（68）、`Policy`（100）、`DropNum`（132）、
   `MarketConfEn`（164）、`MarketSign`（196）、`DropRoam`（292）、`A1Enable`（324），共 328 字节"，
   **与 E2631 实测逐字一致**。我重新从字符串表推断时没先查它，等于把 B.2.1 干过的活重做一遍还做错了。

已经能落地归位的两列：`TopoSortTimer`、`EnBSFilter` 实测在 **`MultiAPMaster`**（§6.2）；
`MarketSign` / `MarketConfEn` 确实在这里。

还没定位的那些（角色/跳数/1905 报文开关/ACD 密钥族/绑定目标）**不在 `P1905ComCfg` 里**，但现在都有了
实测答案，见 §6.4.1：**角色、跳数、`MeshName`、`IFLink`、`En1905Msg`、`EnReportCenterChannel` 全在
`MultiAPCfg`**（50 列，已 dump）。

**关于我自己那个 `IGD.P1905CFGn`——它是对的，只是层级少写了一层。** 2026-11-01 第四轮 dump
`P1905Cfg`(#164) 后确认：该表的 `ViewName` 逐字就是 `IGD.P1905CFG1`…`IGD.P1905CFG6`。
所以这个名字的正确读法是「表 `P1905Cfg` ／ 行 `IGD.P1905CFG<n>` ／ 列
`P1905InterfaceName`、`EnableP1905`」——**行名与两个字段名全对，错在把它压缩成了"表名"**。
（`DB p` 的参数只收表名，所以 `table(P1905CFGn) not find` 只证明它不是表，不证明它不存在。）
细节见 §6.4.2 与 [E2631_DB_SNAPSHOT](E2631_DB_SNAPSHOT.md) §8.1。

**仍然作废的写法只有一种：`P1905ComCfg.SetRoleEnable`** ——那张表实测只有 8 列，没有这个字段。

### 6.2 `MultiAPMaster` —— 漫游/踢人/VBSS/BTM 全在这张表（实测 55 列）

推断切对了表，也切对了绝大多数列；实测补上 12 个原先漏掉的（`EnMultiDomain`、`ApPeriodRptIntval`、
`TrafficStatsRptFlag`、`LinkMetricsRptFlag`、`RssiHysteresisMargin`、`RssiCompensateThr`、
**`EnActiveDeauth`**、`EnBhPhyRate`、`EnAgileSteer`、`UnSuccAssocRptFlag`、`UnSuccAssocRptRate`、
`NoCandidateLmtEnKick`）。完整 55 列与出厂值见 [E2631_DB_SNAPSHOT](E2631_DB_SNAPSHOT.md) §1。

关掉了挂很久的那条待办：**`EnVbss` / `ToVbssMode` 就在这张表**，与 `EnBtmKickoff`、`EnRoamKick`、
`En11rSteer` 是同一张表的兄弟列，实测值分别是 `EnVbss=0`、`ToVbssMode=1`。也就是说
**D4（VBSS）与 D7（BTM 踢人）的入口是同一个表、同一个 web 页**
（`internet_mesh_threshold_lua.lua` 一次写三张表：`OBJ_MAP_MASTER_ID` 的 `EnBandSteer`/`EnableBtmKickoff`、
`OBJ_MULTIAP_MASTERCFG_ID` 的 `EnBtmKickoff`/`EnRoamKick`/`EnVbss`、`OBJ_MULTIAP_ROAM_ID` 的
`RoamRssiLmt24G`/`RoamRssiLmt5G`/`EnLegacyStaRoam`）。

实测还纠正了一个方向性判断：**出厂踢人走的是 `EnActiveDeauth=1`，不是 BTM**
（`EnBtmKickoff=0`、`EnRoamKick=0`、`En11rSteer=0`）。做 D7 与抓包比对时，预期看到的是
deauth/disassoc，而不是 11v 的 BTM Request。详见快照 §1.1。

### 6.3 `MultiAPDomain`（8 实例，`MULTIAPDOMAIN%d`，每行 106 列）—— 全部阈值

推断切对了，列名全部命中（唯一错处是我给 `OfflineLmt` 注的参考值：实测 `OfflineLmt=1800`、
`ForbidLmt=36000`，我把 36000 记到了 `OfflineLmt` 头上）。完整 106 列与出厂值见
[E2631_DB_SNAPSHOT](E2631_DB_SNAPSHOT.md) §3。

实测带来两条推断时代没有的结论：

- **domain 实例与 SSID 槽位 1:1**（`MULTIAPDOMAINn` ↔ 第 n 个 SSID）：`ESSID` 与 `DomainUsed`
  都是 `isSave=0` 的运行时值，实测 8 个实例里生效 4 个（1/4/5/8），占位名直接是 `SSID3`/`SSID7`。
- **组网专用 SSID 也占一个 domain**：`ZMESH-TRx3zR`（DOMAIN4）与 `ZMESH-TRx3zR_5G`（DOMAIN8）
  是自动生成的两个槽。踢人阈值是 per-domain 的，回程那一路同样要配。

这张表解释了为什么 BTM 在真机上"看起来不生效"：**踢人是策略函数的输出，阈值全在这里**，
`EnBtmKickoff=1` 只打开开关，还要 `ToSlowLmt`/`RoamingLmt`/`StatusPreRoamTimer` 这一整套状态机
参数配合（`InitLmt/PreRoamLmt/RoamingLmt/OfflineLmt/ForbidLmt` 配 `Status*Timer`）。

### 6.4 其余几张：另有两处归属被实测推翻

| 表 | 实测 | 用途 |
|---|---|---|
| `MultiAPRoamCatonCfg` | `RowCount=1`，7 列：`OfflineRetrieveEn=0`、`TxDataAfterBaEn=0`、`TxBaDelayMs=10`、`ZeroDropCfgEn=0`、**`BtmCacheEn=0`**、`TxConDelayMs=100`、`TxDropCnt=0` | 我原先只给了它一个 `RoamFilter`——**错**，实测没有这一列 |
| `MultiAPCacheMac` | **`RowCount=0`**（表在、无实例） | 我原先把上面那 7 列记在它名下——**错**，而且列名没有 `t` 前缀 |
| `MultiAPStaCfg` | **`RowCount=0`**（表在、无实例） | 三地址映射：入口存在，只是这台机器没配过绑定 |
| `P1905StaBind` | **`RowCount=0`**（#168，表在、无实例） | **D6 绑定的更可能落点**——它是**表名**，我原先当成 `P1905ComCfg` 的列名 |
| `P1905Cfg` | **`RowCount=12`**，3 列（#164） | **1905 接口使能表**：`eth1`–`eth4`、`wlan0`、`wlan1` 六行全 `EnableP1905=1`，后 6 行空壳。`ViewName` = `IGD.P1905CFG1..6` |
| `WlanRoamingCfg` | **`RowCount=1`**，6 列（#270） | **不是 11r**：`RoamingEnable=0`、`Interval=60`、`TimeStart=120`、`RssiThresh=-50`、`RssiStart=-30`，是客户端弱信号重关联参数 |
| `WLANBase` | **`RowCount=2`**，68 列（#258） | 两个射频：`DEV.WIFI.RD1`（`Band=0`、`Channel=4`）与 `RD2`（`Band=1`、`Channel=40`）⇒ **RD1 = 2.4G、RD2 = 5G**。两行同报 Mediatek **MT7916**（一颗双频芯片，2×2） |
| `WLANCfg` | **`RowCount=8`**，49 列（#259） | **真正的 SSID 配置**：`DEV.WIFI.AP1..8`，`IfName` = `wlan0`–`wlan3` / `wlan5g0`–`wlan5g3`，含 `Enable`/隐藏/`ACLPolicy`/`BeaconType`/客访标记，以及 **7 个 per-VAP VLAN 列**（D5 的 VAP 侧落点）。`ESSID` 被遮蔽成 `***` |
| `WLANSetting` | **`RowCount=1`**，9 列（#263） | 无线总设置：`SSIDSync=1`、`DualBandSync=0`、**`ACLIsFrom1905Slave=0`**（不收 1905 侧 ACL，D6 同向证据） |
| `WlanRoamingSta` | **`RowCount=0`**（#271） | per-STA 漫游实例，出厂未填（第四张 per-STA 空表） |
| `WLANPSK` | **`RowCount=8`**，4 列（#260） | 行名 `DEV.WIFI.APn.PSK1`（与 FEATURE_MAP 的 `OBJ_WLANPSK_ID` 逐字对上）。`PreSharedKey`/`KeyPassphrase` **被遮蔽成 `***`**——密钥读不出来；但 `MultiAPDomain.ESSID` 是明文，**SSID 名可还原、密钥不行**（遮蔽不一致） |
| `AclCfg` | **`RowCount=0`**（#5） | MAC ACL 表（`FINDINGS` §9 给过结构，268 B × 256 行容量），出厂一条没有。准入/绑定数据面第五张空表 |
| `MultiAPALLSta` / `MultiAPTopo` / `MultiAPDualBand` | 未 dump | 拓扑视图（`bSTA, localmac, uplinkDev, uplinkBSS`） |

`RowCount=0` 与 `table not find` 是两件事：前者说明**表存在、入口是真的**，只是没有行
（四张 per-STA 表出厂全空，`MultiAPCacheMac` 的帧缓存行更是运行时按需填的）；
后者才说明**参数不是表名**——`IGD.P1905CFGn` 就是后者，它是 `P1905Cfg` 的行名（§6.4.2）。

> **视图别名那条是错的，已撤**。初版写"另有视图别名 `MESH`→`Netsphere`→`EASYMESH`（`dbMAPPersonBHBSSIndex`
> 一类 person 层函数）"。2026-11-01 的 `sendcmd 1 DB all` 给出全部 **276 张**表名，**这三个名字一个都不在**。
> 它们更可能是 `httpd`/web 层的名称映射，与配置库无关。全表清单见 [E2631_DB_TABLES](E2631_DB_TABLES.md)。

### 6.4.1 `MultiAPCfg` 实测：角色开关与 D3 的入口都在这里（50 列）

`sendcmd 1 DB p MultiAPCfg`，`RowCount=1`，50 列，完整分组与出厂值见
[E2631_DB_SNAPSHOT](E2631_DB_SNAPSHOT.md) §6。对项目最有用的六条：

| 实测 | 意味着什么 |
|---|---|
| `Mode=1`、`SetRoleEnable=0`、`RoleElect=0`、`EnMasterOnly=0` | 按 [SCHEMA](../milestones/SCHEMA.md) 语义 `Mode` 1=Slave ⇒ **这台设备当前是子节点**，角色固定、不参与选举 |
| `CurHop=2`、`MaxHop=2`、`EnMasterOnly=0` | **B16 待办结案**。SCHEMA 说 `EnMasterOnly=1` 时 `slave_init` 跳过、`CurHop` 不会被设成 `MaxHop`；实测是反面情形且两值相等，三者自洽 |
| **`EnReportCenterChannel=1`** | **D3 的入口在 `MultiAPCfg`，而且出厂就开着**。D3 要做的不是"打开上报"，是"接收侧怎么用" |
| **`En1905Msg=1`** | 1905 报文开关**是开的** ⇒ 上一轮"`En1905Msg` 没找到、这条取证路径落空"的说法作废 |
| `EnAgentSteer=1`、`EnAgentBs=0` | 作为 agent 收 steering 请求，但不做 band steering 决策；与 `MultiAPMaster.EnBandSteer=0` 方向一致 |
| `MeshName=""`、`IFLink="br0"` | 1905 的 Mesh 名没设（`/etc/hardcode` 的 `E2631_QD_MESH` 是另一回事）；1905 走 `br0` |

**附带的独立收获**：实测列序与反汇编还原的序位**逐字段一致**，`36 + 32n` 在序号 0–26 上六个锚点全中
（`MaxHop`228 / `CurHop`260 / `VersionMode`356 / `P1905DevNum`388 / `EnMasterOnly`804 / `MeshName`868），
只有 `IFLink`(1444)、`NoSteeringOUIs`(1540) 不符 ⇒ 序号 27 起进入变长区。
这是本仓库第一次"离线还原的 schema"与"真机 dump"完全对上，可以反过来把 SCHEMA 那句"前 25 个字段是 32 字节"
精确成"前 27 个"。

### 6.4.2 表名全集：276 张，`sendcmd 1 DB all`

这一轮同时拿到了配置库的**全部表名**（0–275，276 张），存在
[E2631_DB_TABLES](E2631_DB_TABLES.md)。它的价值不是"多了一份清单"，而是**堵住了本节所有错误的来源**：

- 我校正命令名：列全部表是 **`sendcmd 1 DB all`**，不是 `DB l`（后者是我猜的）。
- 三个被我当成列名的名字其实是表：`MultiAPPTK`(#147)、`P1905StaBind`(#168)、`P1905Cfg`(#164)。
- `IGD` 这个**表**不存在；但 `IGD.` 是**行名前缀**，`IGD.P1905CFGn` 是行名而非表名。
  `DB p` 只收表名，`table not find` 只证明它不是表。
- **第四轮已证实**：dump `P1905Cfg`(#164) 后，`ViewName` 逐字就是 `IGD.P1905CFG1`…`IGD.P1905CFG6`。
  也就是说这个名字本该保留，我上一轮"一律作废"的处置属于**过度纠正**。

`ViewName` 实测出现三种命名风格，指向 **TR-069 数据模型的根对象前缀**：

| 风格 | 例子 | 出现在 |
|---|---|---|
| 大写无点 | `MULTIAPMASTER`、`MULTIAPDOMAIN1` | `MultiAPMaster`、`MultiAPDomain` |
| `IGD.` | `IGD.P1905CFG1`、`IGD.TpCfg0` | `P1905Cfg`、`TimePolicy` |
| `DEV.` 点分 | `DEV.WIFI.ROAMING`、`DEV.BRIDGING.BR1.BRPORT2` | `WlanRoamingCfg`、`Brport` |
| `Device.` 全名 | `Device.WiFi.Radio.1` | `WLANSSID.LowerLayers`（**第四种，且与缩写同表共存**） |

`Device.` 这一种值得单记：`WLANSSID` 同一行里 `Name` 写 `DEV.WIFI.SSID1`、`LowerLayers` 写
`Device.WiFi.Radio.1`——**别名与全名并存**。所以顺 `LowerLayers` 去库里找父对象时，
**很可能找不到同名行**（DB 行名多半是 `DEV.` 风格或别的形状）。

因为 web 页写库走的是 `OBJ_*_ID` → 表 → **行名** → 列（§7），**做 web 层自动化时行名必须带前缀**，
只给 `P1905CFG1` 很可能匹配不上。

mesh 相关的表不是我们之前跟踪的 5 张，而是 **19 张**（10 张 `MultiAP*` + 9 张 `P1905*`），
另有 `MapCooperatorCfg`(#139，**已 dump：1 行 5 列，与 SCHEMA 还原逐字同序**)、
`MultiapCapability`(#154，**已 dump：1 行 2 列，只有 `SupportBSSync`/`SupportPackageProduct`**)、
`SFUMeshCfg`(#206)、`SFUTopoCfg`(#207)、`WlanRoamingCfg`(#270，**已 dump，确认与 11r 无关**)。
网络层另有 `EthVlan`(#79，**已 dump，0 行**)、`Brport`(#20，**已 dump，16 行 × 23 列**)、
`PortBinding`(#182，**已 dump，0 行**)、`WLANSSID`(#261，**已 dump，8 行 × 8 列，全列 `isSave=0`**)。

一个已知的"看起来相关、其实不然"：**`MultiapCapability` 与 `SupportStrategies=251` 没有对照关系**，
它只有两个能力位；位图的语义不在这张表（快照 §9.1）。

### 6.5 方法说明与强度

- **三段法**：① 离线切（字符串表连续排列 → 列名清单，`httpd` 字段表与 web lua 写入顺序交叉印证）；
  ② 上机 `sendcmd 1 DB p` 逐表核对；③ `sendcmd 1 DB all` 拿表名全集兜底。
  这一轮证明**①单用会切错表边界**（错了三处），所以②③不能省；
  而且**上机前应先查 [SCHEMA](../milestones/SCHEMA.md) / [POLICIES](../milestones/POLICIES.md) 里已有的还原**。
- 强度（七轮实测后）：
  - `MultiAPMaster`(55)、`MultiAPDomain`(8×106)、`P1905ComCfg`(8)、`MultiAPCfg`(50)、
    `MultiAPRoamCatonCfg`(7)、`P1905Cfg`(12×3)、`WlanRoamingCfg`(1×6)、`MultiapCapability`(1×2)、
    `Brport`(16×23)、`MapCooperatorCfg`(1×5)、`WLANSSID`(8×8)、`WLANBase`(2×68)、`WLANCfg`(8×49)、
    `WLANSetting`(1×9)
    的**列名与列归属 = 强**（真机 dump）；
  - `MultiAPStaCfg` / `MultiAPCacheMac` / `P1905StaBind` / `EthVlan` / `PortBinding` / `WlanRoamingSta` / `AclCfg`
    的**表存在 = 强**，但列名与列语义 = 中（七张都 0 行，只能靠名字）；
  - **表名全集 = 强**（276 张，`DB all`）；
  - 所有列的**含义**仍是弱—中：只有列名、出厂值和命名规律，没有反汇编或协议字段级的对应。
  - 第五轮补一条**方法论**：`MultiapCapability` 那次错不是选错表，是**把"缺语义"当成"缺数据"**
    ——`SupportStrategies=251` 的值早在 `MultiAPDomain` 里，位含义只能从反汇编或规范正文来。
    以后遇到"某个位图/枚举的含义不明"，**不要再排 dump**。
  - 第六轮补一条：**同一张表里 `DEV.WIFI.SSID1` 与 `Device.WiFi.Radio.1` 两种标识并存**，
    前者是 ZTE 缩写的别名、后者是标准 TR-181 全名。顺着 `LowerLayers` 去库里找后者**很可能找不到**
    （第七轮在 `WLANBase` 里找到对应行是 `DEV.WIFI.RD1`，见 §6.4）。
  - 第七轮补一条**方法论**：**`isSave=0` 的列只能读状态，不能当配置比对**。
    第六轮拿 `WLANSSID.Enable`（全表 `isSave=0`）去反证 `Brport.PortState`，结论正好是反的；
    换成 `WLANCfg.Enable` 后两表逐字吻合。同一个 `isSave` 判据，正着用是"别写它"，反着用是"别信它当配置"。
- 一条通用判据：**`isSave=0` 的列是运行时/派生值，写 DB 不落盘**（`MultiAPDomain.ESSID`、
  `WlanRoamingCfg.ViewName`、`WLANSSID` 整表、`WLANCfg.IfName` 都是）。改配置前先看这一列，
  否则"改了不生效"会被误判成"固件不认"；反过来拿它当配置依据也会判错（见上）。
- 局限：`cspd` 已 strip，拿不到函数名；列名与代码的对应关系只能靠名字。

## 7. `/kmodule` 里有什么：squashfs 解开后的地图

`/kmoduletmp/kmodule.img`（13 234 176 B）是 **squashfs 4.0**，`hsqs`、`comp=xz`、块 128 KiB、
1003 inode、85 个 fragment、`bytes_used` 13 233 339。工具在 `tools/lab/unpack_squashfs.py`：

```sh
python tools/lab/unpack_squashfs.py kmodule.img -o kmodule/
# dir 57  file 831  symlink 115
```

| 路径 | 内容 |
|---|---|
| `/bin` | `cspd`、`multiapd`、`httpd`、`sendcmd`、`setmac`、`devmem2`…（[MULTIAPD](MULTIAPD.md) 的 22 个成员都在） |
| `/lib` | `libcmapi`/`libcfapi`/`lib{multiap,ieee1905}{cm,cf}api`、`libdb`、`libenv`… |
| `/home/httpd/public` | Web 前端（含 `jquery/static/js/*.js` 打包产物） |
| `/home/httpd/webmodules/modules` | **257 个 `.lua`**，每个页面一个，写 DB 的入口就在这 |
| `/home/httpd/.../webd_adaptor_mesh_dev.c`（字符串） | httpd 的字段表来源，含 `QueryMeshDevtable`、`OBJ_MESH_DEV_ID` |

两个要点：

1. **这些 `.lua` 是字节码（LuaQ 头），不是源码**，但常量表完整，所以
   `OBJ_*_ID` 宏名、DB 列名、页面名、`require` 关系全都能读出来——§6 的交叉印证就是这么来的。
2. **构建路径泄露了厂商源码树**：`/zte/chip_zx279128s/product/E2631_QD_MESH/scripts/../basefilesystem/web/html/…`。
   与 `/etc/hardcode` 的 `E2631_QD_MESH`、`CONFIG_CSPCORE_CHIP_PATH chip_zx279128s` 三处一致。

### 7.1 Web 侧的 OBJ 宏 → 字段对照（从 httpd 与 lua 抽出）

| OBJ 宏 | 字段 | 页面 |
|---|---|---|
| `OBJ_NETSPHERE_MAP_ID` | `Mode, Enable, SetRoleEnable` | `Localnet_NetSphere_Mode_lua.lua`（**mesh 模式/角色页**） |
| `OBJ_MAP_MASTER_ID` | `EnBandSteer, EnableBtmKickoff, EnIPhoneRoamOpt, EnRoam, EnDevSteer` | 同上 + `internet_mesh_threshold` |
| `OBJ_MAP_DOMAIN_ID` | `BsRssiLmt24G, BsRssiLmt5G` | 同上 |
| `OBJ_MULTIAP_MASTERCFG_ID` | `EnBtmKickoff, EnRoamKick, EnVbss` | `internet_mesh_threshold_lua.lua` |
| `OBJ_MULTIAP_ROAM_ID` | `RoamRssiLmt24G, RoamRssiLmt5G, EnLegacyStaRoam` | 同上 |
| `OBJ_P1905_COM_CFG_ID` | `PEnable` | `internet_mesh_common_lua.lua` |
| `OBJ_WLAN_STAPROFILE_ID` | （回传 STA profile） | 同上 |
| `OBJ_WLAN_STA_STATUS_PROFILE_ID` | `Mlo, MLOStatus, BACKHAUL` | `vue_meshstatus_json.lua`（拓扑状态 JSON） |
| `OBJ_MESH_DEV_ID` | `t1905DevNum, Non1905DevNum, AccessType` | `QueryMeshDevtable`（httpd） |

写库调用的形状：`applyOrNewOrDelInst` / `transToPostTab` / `getSpecificInstXML`，实例 ID 形如
`MULTIAPDOMAIN1`。**这些就是 D3–D6 的入口命名**，比从 `0xa932`/`0xa94e` 反推省事得多。

## 8. 一个没解开的密文文件

`/etc/db_default_auto_cfg.xml`（5320 B）是**配置库的出厂默认值**，也是密文：

```
00000000  01 02 03 04 00 00 00 03   ← magic 0x01020304，type = 3
0000003C  00 00 14 7F 00 00 14 80 00 00 00 00   ← chunk{dec=5247, enc=5248, next=0}
00000048  98 2d 46 1d …            ← AES 密文，5248 = 328×16
```

即**与 `config.bin` 同一族信封、type 3**（不是 type 1 的 boot、也不是 type 4 的 config）：
60 字节头 + 单块 `{dec,enc,next}` + 数据。用 [E2633_TELNET_BOOT](E2633_TELNET_BOOT.md) §10 那五个机型种子（`E263xKey0238002E` 等）
按 AES-256-CBC/ECB 各试一遍，**全部解不出 XML**——这符合 `CONFIG_CSPCORE_DB_ENCRY_COMKEY`
的存在：出厂默认值用的是**编译期公共密钥**，和"每机型一把"的 USB 备份密钥不是一套。

**这件事的价值是负结果**：不要指望拿 config.bin 的种子去开 DB 默认库。想要 schema 就走 §6
（已经拿到了），或者上机 `sendcmd 1 DB p`。

## 9. 对 mesh 项目的影响（可以直接执行的部分）

编号与 [FEATURE_STATUS](../FEATURE_STATUS.md) D 段一致。**"实测"= 本条已在 2026-11-01 的真机 dump 里
看到过这个列名**（见 [E2631_DB_SNAPSHOT](E2631_DB_SNAPSHOT.md)），"待取证"= 还没看到。

| 待办 | 之前 | 现在 |
|---|---|---|
| `EnVbss` / `ToVbssMode` 在哪个视图 | 未知，要上机翻 | **`MultiAPMaster`**，与 `EnBtmKickoff`/`EnRoamKick` 同表（实测，`0`/`1`） |
| D3 信道扫描 / CAC | 只有 `SendCAC` 反汇编推断 | `MultiAPMaster.InChannelScanRptFlag=0` + **`MultiAPCfg.EnReportCenterChannel=1`** + **`P1905Cfg` 六接口全 `EnableP1905=1`**（都实测）。**发送侧全通**，D3 的缺口在接收侧。注意 `P1905Cfg` 只证"允许跑"，不证"在跑"（见下面"1905 接口使能"行） |
| D4 VBSS | 猜测 | `MultiAPMaster.EnVbss=0` / `ToVbssMode=1`（实测，出厂关） |
| D5 VLAN / 流量分离 | 靠 `0xa94e` 反推 | 编译期 `CONFIG_CSP_WLAN_VLAN` + `EBTABLES_{VLAN,FFE_T}` + `OBJ_PORTVLAN_ID` + 下发入口都在；**配置层实测为零**，三处落点全关：`EthVlan`(#79) **0 行**、`Brport`(#20) 16 口的 `PVID`/`IngressFiltering`/`AcceptableFrameTypes`/`PriorityTagging`/`WanPortVlan*` **八列全默认**、**`WLANCfg` 每行 7 个 per-VAP VLAN 列全 0**（`Vlan`/`VlanPriority`/`UpTagEnable`/`DownTagStripEnable`/`McastVlan`/`McastVlanPriority`/`McastDownTagStripEnable`）。**入口已定位到两侧：VAP 打/剥 tag 在 `WLANCfg`，端口默认 VLAN 在 `Brport`**；厂商启用时到底写哪侧，仍未解 |
| D6 STA 绑定 | 猜测 | 五张准入/绑定表**全部 0 行**：`P1905StaBind`(#168)、`MultiAPStaCfg`、`MultiAPCacheMac`、`WlanRoamingSta`(#271)、`AclCfg`(#5)。**入口存在、出厂未配**——要看到行得先配一条绑定（改配置，不是取证）。`P1905ComCfg.STAMac/TargetBand` 不存在，旧写法作废。另：`WLANSetting.ACLIsFrom1905Slave=0` ⇒ 本机**不收** 1905 侧同步的 ACL，同向证据 |
| D7 BTM 踢人 | 只知字符串在 | `MultiAPMaster.{EnBtmKickoff=0,EnRoamKick=0,En11rSteer=0,11rSsidSync=1,EnActiveDeauth=1}` + `MultiAPDomain` 五组状态机参数 + `MultiAPRoamCatonCfg.BtmCacheEn=0`（全部实测）+ `WlanRoamingCfg`（实测，**与 11r 无关**） |
| 角色开关（不在 D 段） | 只有 `libzte_agent_mm.so` 三个导出 | **结案：静态 Slave，六开关同向**。`MultiAPCfg.{Mode=1, SetRoleEnable=0, RoleElect=0, EnMasterOnly=0, MaxHop=2, CurHop=2}` + `MapCooperatorCfg.{AutoSetup=0, EnRebootElect=0}`（全部实测）⇒ 不选举、不自动开局、重启也不改主意 |
| SSID 与 band 分组 | 未知 | **两份实测交叉**：`WLANSSID`（8 行 × 8 列）给出 `SSID1`–`SSID4`→`Device.WiFi.Radio.1`、`SSID5`–`SSID8`→`Radio.2`（**4+4**）；`WLANBase`（2 行 × 68 列）给出 `RD1.Band=0/Channel=4`、`RD2.Band=1/Channel=40` ⇒ **`Radio.1` = 2.4G、`Radio.2` = 5G，且 `Radio.n` ↔ `RDn` 同号**。`WLANSSID` 全列 `isSave=0`（运行时视图，写不进去），真配置在 `WLANCfg` |
| B16 `CurHop` | 要真机确认 | **结案**：`CurHop=2=MaxHop`，且 `EnMasterOnly=0`，与 SCHEMA 的条件句三者自洽 |
| 11r/FT 支持 | "固件不支持"（推断） | **结案，两条独立证据**：编译期 `CONFIG_CSP_WLAN_SUPPORT_80211KV`（只有 11k/11v）+ 配置层 `WlanRoamingCfg` 六列里**没有任何 FT 开关**（实测）。`En11rSteer=0`、`SupportRrm=1` 与之吻合。**不必再找 11r 的配置入口** |
| 1905 接口使能 | 未知 | **`P1905Cfg` 实测**：`eth1`–`eth4`、`wlan0`、`wlan1` 六个接口全 `EnableP1905=1`，行名 `IGD.P1905CFG1..6`，12 行容量 6 用。**但只能证"允许"，证不了"在跑"**：`wlan0`=`AP1`(主 2.4G, `Enable=1`)、`wlan1`=`AP2`(访客 2.4G, **`Enable=0`**)，两个都在 2.4G、5G 一个没列；而 `REPLAY_RESULTS` §5 在 Z506 上抓到 ACD 实际走 `wlan0`+`wlan5g0`（两频段各一）。见快照 §11.6 |
| 桥端口拓扑 | 未知 | **`Brport` 实测（16 行 × 23 列）**：`BRPORT1` 是桥自身（`LowerLayers` 列 `BRPORT2`–`BRPORT12`）；`LAN1/2/3` = `DEV.ETH.IF2/IF3/IF4`；`SSID1`–`SSID8` = 8 个 BSS；`APCLI1`/`APCLI5G1` = 回程 STA；WAN 侧两条 = `IF1`(`WanName=Bridge`) 与 `IF99`(`Alias=ETH_WAN`)。**以太口没有 `IF0`** |
| BSS 启用状态 | 未知 | **两份交叉，逐个吻合**：`WLANCfg.Enable=1` 的是 `AP1/AP3/AP4/AP5/AP8`（= `SSID1/3/4/5/8`），与 `Brport.PortState=4` 的五个 BSS **完全相同** ⇒ **`PortState=4` = 在跑**。`WLANCfg.IfName` 给出 AP↔接口：2.4G `wlan0`–`wlan3`、5G `wlan5g0`–`wlan5g3`（与 [FINDINGS](../FINDINGS.md) §9 的还原逐字一致）。`DomainUsed=0` 的 `SSID3` 虽然 `Enable=1`，但 mesh 域未接管——**两个值量的是不同的事**，不矛盾（快照 §11.4） |
| 能力声明 | 未知 | **`MultiapCapability` 实测（1 行 2 列）**：`SupportBSSync=1`、`SupportPackageProduct=0`。**不含任何策略/漫游位图** |
| 1905 报文取证 | 只有 multiapd 的 `pktdump` | **`MultiAPCfg.En1905Msg=1`（实测，开着）** ⇒ 配置层这一路通。上一轮"取证路径落空"的说法**作废** |

**已执行的八轮只读命令**（2026-11-01，结果见快照与全表清单）：

```sh
sendcmd 1 DB p MultiAPMaster        # 55 列，EnVbss/EnBtmKickoff/En11rSteer 落地
sendcmd 1 DB p P1905ComCfg          # 只有 8 列 —— 切错表的证据
sendcmd 1 DB p MultiAPDomain        # 8 实例 × 106 列，DomainUsed 指认生效的 4 个
sendcmd 1 DB p P1905CFGn            # table not find —— 它不是表名，是 P1905Cfg 的行名
sendcmd 1 DB p MultiAPStaCfg        # RowCount=0，表在、无实例
sendcmd 1 DB p MultiAPCacheMac      # RowCount=0，表在、无实例
sendcmd 1 DB p MultiAPRoamCatonCfg  # 7 列，BtmCacheEn=0 在这里
sendcmd 1 DB p MultiAPCfg           # 50 列 —— 角色/跳数/MeshName/IFLink/En1905Msg/EnReportCenterChannel
sendcmd 1 DB all                    # 276 张表名全集
sendcmd 1 DB p P1905StaBind         # RowCount=0，表在、无实例
sendcmd 1 DB p P1905Cfg             # 12 行 × 3 列 —— ViewName 就是 IGD.P1905CFG1..6
sendcmd 1 DB p WlanRoamingCfg       # 1 行 × 6 列 —— 不是 11r
sendcmd 1 DB p MultiapCapability    # 1 行 × 2 列 —— 不是位图对照表
sendcmd 1 DB p EthVlan              # RowCount=0，出厂无以太 VLAN
sendcmd 1 DB p Brport               # 16 行 × 23 列 —— 端口拓扑 + VLAN 八列全默认
sendcmd 1 DB p MapCooperatorCfg     # 1 行 × 5 列 —— 与 SCHEMA 还原逐字同序
sendcmd 1 DB p WLANSSID             # 8 行 × 8 列 —— SSID1–4→Radio.1、5–8→Radio.2，全列 isSave=0
sendcmd 1 DB p PortBinding          # RowCount=0
sendcmd 1 DB p WLANBase             # 2 行 × 68 列 —— Band=0/1、Channel=4/40，band 归属结案
sendcmd 1 DB p WLANCfg              # 8 行 × 49 列 —— 真 SSID 配置 + 7 个 per-VAP VLAN 列
sendcmd 1 DB p WLANSetting          # 1 行 × 9 列 —— ACLIsFrom1905Slave=0
sendcmd 1 DB p WlanRoamingSta       # RowCount=0
sendcmd 1 DB p WLANPSK              # 8 行 × 4 列 —— 密钥被遮蔽，层级 DEV.WIFI.APn.PSK1
sendcmd 1 DB p AclCfg               # RowCount=0（准入层第五张空表）
```

**没有"还没跑的"了**——候选表全部 dump 完毕（22 张入档）。现场状态另确认一条：
**LAN 口只有 LAN2 插着网线**，`PortState` 的物理侧验证命中（快照 §12.3）。

## 10. 局限

- **实测只覆盖"存在与出厂值"**：表名、列名、列归属、行名现在是硬的（八轮真机 dump + 276 张表名全集），
  但每一列的**语义**仍只有命名和出厂值可依，没有协议字段级或反汇编级对应。特别是
  `EnRoamModes=19`、`SupportStrategies=251` 这两个位图，位的含义没解——**且不可能靠 dump 解**
  （值早就在库里，缺的是语义；`MultiapCapability` 已经证伪过一次，见快照 §9.1）。
  同类的还有 `BandWidth`(0/4)、`Standard`(7/14)、`MapAccessAttr`(32/64)、`CardMode`(8/9) 的枚举值。
- **D5 的"厂商启用时写哪张表"未解**：落点已定位到**两侧**——VAP 侧是 `WLANCfg` 每行的 7 个 VLAN 列
  （`Vlan`/`VlanPriority`/`UpTagEnable`/`DownTagStripEnable`/`McastVlan`/`McastVlanPriority`/
  `McastDownTagStripEnable`），端口侧是 `Brport` 的八个 VLAN 列
  （`PVID`/`IngressFiltering`/`AcceptableFrameTypes`/`PriorityTagging`/`WanPortVlan{Enable,Mode,ID,Pri}`），
  出厂全部为 0/默认；`EthVlan` 与 `PortBinding` 是空表。但**空表不等于不承载**（per-STA 几张表也是按需建行），
  厂商在网页上打开"VLAN/流量分离"时到底往哪写，只读取证看不到。
  两条出路：在网页上真配一次（改配置），或读 web VLAN 页面的 `.lua` 字节码看 `OBJ_*_ID` 指向哪张表（只读）。
- **`DEV.ETH.IFn ↔ ethn` 的同号关系未证**：`Brport` 给出 `IF1` 在 WAN 侧、`IF2/IF3/IF4` = `LAN1/LAN2/LAN3`、
  `IF99` = `ETH_WAN`，以太口**没有 `IF0`**，与"1 WAN + 3 LAN"的口数吻合；
  `P1905Cfg` 里的 `eth1`–`eth4` 若与 `IF1`–`IF4` 同号，就是"WAN + 3×LAN 全跑 1905"（mesh 里合理）。
  但**这是间接证据**：TR-069 实例号与内核接口名没有必然同号关系，要定得靠 `ip link`。
- **`P1905Cfg` 与"实际在跑"对不上**：`wlan0`/`wlan1` 已确认为 `AP1`/`AP2`（都在 **2.4G**，
  且 `wlan1` 对应的访客 VAP `Enable=0`），5G 侧一个都没列；而 `REPLAY_RESULTS` §5 在 Z506 上抓到
  ACD 实际走 `wlan0`+`wlan5g0`（两频段各一）。⇒ `EnableP1905=1` 只能读作"允许跑"，
  **不能读作"在跑"**。判据：`pktdump 1` 抓 1905 帧看源接口，或 `/proc/net/dev` 的计数。
- **密钥读不出来，但遮蔽不一致**：`WLANPSK.PreSharedKey`/`KeyPassphrase` 与 `WLANCfg.ESSID`/`WPAEAPSecret`
  都被 `sendcmd` 打码成 `***`（第八轮实测，8 行全如此）——密钥本体无泄露路径；
  但 `MultiAPDomain.ESSID` 是明文，**SSID 名可还原、密钥不行**。遮蔽是按表/列做的，不是全局策略。
  `WLANCfg` 是可写的真配置表（`Enable`/隐藏/`ACLPolicy`/`BeaconType` 都可落盘），
  `WLANSSID` 只是它的运行时视图（8 列全 `isSave=0`）——**两者别混，改配置改前者**。
- **`MultiAPCfg.IFLink=br0`，而 `Brport` 的 16 行全在 `DEV.BRIDGING.BR1` 下——两个桥**。
  若成立，则 1905 控制面与用户数据面本来就分属不同桥，**不需要 VLAN 就已经隔离**，
  这也顺带解释了 D5 配置层为什么是空的。但 `Brport` 里没有 `BR0` 的实例，
  可能 `br0` 只是内核桥、没有 TR-069 实例。**现象成立、解释未证**，要定靠 `ip link` 看桥成员。
- **~~`PortState` 的 0/4 语义未定~~ —— 第七轮配置侧、第八轮物理侧双证结案**：`WLANCfg.Enable=1` 的五个 BSS
  （`AP1/AP3/AP4/AP5/AP8`）与 `Brport.PortState=4` 的五个**逐个相同**；用户确认 LAN 口**只有 LAN2 插着网线**，
  与 `BRPORT11=4`/`BRPORT2=0`/`BRPORT12=0` 逐口吻合（§9.6 的预测命中）⇒ 4 = 在跑。
  上一轮那条"8 个 `Enable=1` vs 5 个 `PortState=4`"的反证不成立，根因是拿 `WLANSSID.Enable`
  （全表 `isSave=0` 的运行时视图）当成了配置开关。`DomainUsed=0` 的 `SSID3` 与它不矛盾：
  **BSS 在广播 ≠ 被 mesh 域接管**，两个值量的是不同的事（快照 §11.4、§12.3）。
  唯一没跟着闭合的：WAN 侧 `BRPORT15=4` 而 `ETH_WAN=0`，取决于 WAN 口当时插没插线（未确认）。
- **七张表全空拿不到列语义**：`MultiAPStaCfg` / `MultiAPCacheMac` / `P1905StaBind` / `WlanRoamingSta` /
  `EthVlan` / `PortBinding` / `AclCfg` 表存在但行是空的，列语义只能靠名字推——要看到行得先配一条（改配置，不是取证）。
- **`MultiAPCfg` 的偏移只在序号 0–26 上验证过**：`36+32n` 六个锚点全中，但 `IFLink`(1444)、
  `NoSteeringOUIs`(1540) 不符，序号 27 起进入变长区，宽度要读 `dbDefMultiAPCfg` 初始化才算得出。
- **`/etc/init.debug` 判定为废文件**，依据是它引用的驱动/脚本在本机全不存在；**没有实机验证**
  （也不建议验证，改 `loadtype` 有变砖风险）。
- **未知 4 字节**（§1.2 的 0x40）含义未定。已排除：压缩块 CRC、解压后 CRC、CRC-32C、
  per-inode 常量。它不参与任何校验，所以不影响解包正确性。
- **`db_default_auto_cfg.xml` 未解**（§8），所以 DB 的**列类型、默认值、非 mesh 表内容**
  仍然只有名字级信息——但**表名全集已经有了**（276 张，`DB all`），"表存不存在"这类问题不必再猜。
- 这份 rootfs 与 Zlion 仓库里那份 `kmodule.img`（12.6 MiB）**不是同一版**：本次 13.2 MiB、
  `/etc/version` 2026-08-26；差异未逐字节比对。
- **本次 dump 的机器是 Slave 角色**（`MultiAPCfg.Mode=1`）：所有出厂值反映的是**子节点**的配置。
  Master 侧的 `CurHop`/`WANForward` 等运行时值会不同，不能把这批值当成"控制器默认值"。
