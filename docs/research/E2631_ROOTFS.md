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

### 6.1 `P1905ComCfg` —— 角色、拓扑、1905 总开关

```
SetRoleEnable  RoleElect  EnMasterOnly  MeshName  MaxHop  CurHop  WANForward
TopoTimeOut  TopoDiscoverInterval  TopoSortTimer  P1905DevNum  EnReportCenterChannel
En1905Msg  EnSendM1EachBand  En11kReportMerg  EnAcdDealAuth  En11kReportMerg
AcdEnable  AcdPubKeyReset  AcdPubKeyAuth  MultiAPPTK  ACD.PTK  PubKey  PrivKey
VersionMode  MarketSign  MarketConfEn  ProductID  SteeringPolicy  EnAgentSteer  EnAgentBs
SyncBhBssEnable  SSIDSyncRadio  AcdsDelayInterval  AcdcDelayInterval  11kRssiCorrect
LargeCapNetwork  EnP1905DevSave  ExtraAPEn  ExtraAPPref  OfflineDisplay  OfflineTimeOut
RoamWebEnable  EnBsFilter  NoSteeringOUIs  TestMode  PluginLimit  STAStaBind/STAMac/TargetMac/TargetBand
```

对我们直接有用的三条：

- **`SetRoleEnable` / `RoleElect` / `EnMasterOnly`** —— 这是"设备能不能被设成 controller/agent"的开关对
  （`internet_mesh_common_lua.lua` 里 `Mode`/`Enable`/`SetRoleEnable` 走的就是这张表）。
- **`En1905Msg`** —— 1905 报文开关。配合 multiapd 的 `pktdump`，这是第二条取证路径。
- **`MaxHop` / `CurHop`** —— 拓扑深度，`TopoDiscoverInterval` 决定发现周期；做时序分析要用。

### 6.2 `MultiAPMaster` —— 漫游/踢人/VBSS/BTM 全在这张表

```
EnRoam  EnBandSteer  EnDevSteer  EnMultiDomain  Standalone  SupportBSMode  CurrentBSMode
En11rSteer  11rSsidSync  EnDbusRoamReport  WiFi6StaStatusRptFlag  EnBhSteerCheck
EnBtmKickoff  EnNoCandidateKick  NoCandidateKickRssi(-80)  EnSteerNo11k  EnRoamKick
EnVbss  ToVbssMode  EnMeshSR  EnSteerCurBand  EnIPhoneRoamOpt  EnQuickRoam
EnMloTopoSort  EnApOnOflineLog  EnRoamLog  HashCap  EnBSFilter
StaRssiRptThr24G(-110)  ChanUtilRptThr24G/5G  ReTransIntval  ReTransCntLmt
TransmitPowerLimit24G/5G/6G  Profile1bStaAssocDis  Profile2bStaAssocDis
```

**这就关掉了挂了很久的那条待办**：`EnVbss` / `ToVbssMode` 不在"别处"，就在 `MultiAPMaster`，
和 `EnBtmKickoff`、`EnRoamKick`、`En11rSteer` 是同一张表的兄弟列。也就是说
**D4（VBSS）与 D7（BTM 踢人）的入口是同一个表、同一个 web 页**
（`internet_mesh_threshold_lua.lua` 一次写三张表：`OBJ_MAP_MASTER_ID` 的 `EnBandSteer`/`EnableBtmKickoff`、
`OBJ_MULTIAP_MASTERCFG_ID` 的 `EnBtmKickoff`/`EnRoamKick`/`EnVbss`、`OBJ_MULTIAP_ROAM_ID` 的
`RoamRssiLmt24G`/`RoamRssiLmt5G`/`EnLegacyStaRoam`）。

### 6.3 `MultiAPDomain`（多实例，`MULTIAPDOMAIN%d`）—— 全部阈值

```
RoamRssiLmt24G(-60)  RoamRssiLmt5G(-70)  RoamRssiLmt6G  RoamLoadLmt24G/5G/6G
BsRssiLmt24G/5G/5GTo6G/6G  BsLoadLmt24G/5G/6G  ChanInterferLmt24G/5G
BsVhtChk24G/5G  BsActiveStaChk24G/5G  BsStaIdleRateLmt24G/5G
BsAcceptRssi24G/5G/6G  BsAcceptBwUtil24G/5G  BsTPLimit(9999999)
24Gto5GRssiDelta(-20)  5Gto24GRssiDelta  To6GRssiDelta  CandCurApRssiDelta  LoadRssiLmtDelta
ToPreRoamLmt  ToSlowLmt  ToFastLmt  InitLmt  RoamingLmt  OfflineLmt(36000)  PreRoamForbidLmt
BSLazyForbidLmt  LegacyForbidLmt(43200)  UnknownApFailedLmt  LazyBtmFailedLmt  BsMeasureLmt
EnStaRoam  EnMLOStaRoam  EnStaBandSteer  EnBackHaulStaRoam  EnLegacyStaRoam
DisAssocImminent  DisAssocTimer  Abridged  AutoRvsAbrdgDisImnt  ReqTimeout
SupportStrategies(251)  SupportRrm  EnRoamModes  EnOfflineRstRoam  EnUnknownApBlack
```

这张表解释了为什么 BTM 在真机上"看起来不生效"：**踢人是策略函数的输出，阈值全在这里**，
`EnBtmKickoff=1` 只打开开关，还要 `ToSlowLmt`/`RoamingLmt`/`StatusPreRoamTimer` 这些状态机参数配合。
做 D7 之前先 `sendcmd 1 DB p MultiAPDomain` 看实际值，比改代码快。

### 6.4 其余四张

| 表 | 列 | 用途 |
|---|---|---|
| `MultiAPRoamCatonCfg` | `RoamFilter` | 漫游过滤（对应内部总线 `0xa971`/`0xa972`） |
| `MultiAPCacheMac` | `OfflineRetrieveEn, TxDataAfterBaEn, TxBaDelayMs, ZeroDropCfgEn, tBtmCacheEn, tTxConDelayMs, tTxDropCnt` | 离线 STA 的帧缓存（**`tBtmCacheEn` 又一次指向 BTM**） |
| `MultiAPStaCfg` | `devMac, bssMac, staMac` | 三地址映射 |
| `MultiAPALLSta` / `MultiAPTopo` / `MultiAPDualBand` | `bSTA, localmac, uplinkDev, uplinkBSS` | 拓扑视图 |

另有视图别名：`MESH`→`Netsphere`→`EASYMESH`（`dbMAPPersonBHBSSIndex` 一类的 person 层函数）。

### 6.5 方法说明与强度

- 取证方式是**字符串表连续排列**：schema 列名在 cspd 里是连续的 ASCII 串，表名（`P1905ComCfg`、
  `MULTIAPMASTER`、`MULTIAPDOMAIN%d`、`MultiAPRoamCatonCfg`…）夹在各段之间，切分点清楚。
- 强度：**中—强**。列名与表名的**存在**是硬证据；"哪一列属于哪张表"是按排列顺序切的，有
  `httpd`（`webd_adaptor_*.c` 的字段表）与 web lua 的写入顺序两处交叉印证，但**没有逐列
  `sendcmd 1 DB p` 实测**。要变强只需上机一条命令（见 §9）。
- 局限：cspd 已 strip，拿不到函数名；列名与代码的对应关系仍只能靠名字。

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
60 字节头 + 单块 `{dec,enc,next}` + 数据。用 §10 那五个机型种子（`E263xKey0238002E` 等）
按 AES-256-CBC/ECB 各试一遍，**全部解不出 XML**——这符合 `CONFIG_CSPCORE_DB_ENCRY_COMKEY`
的存在：出厂默认值用的是**编译期公共密钥**，和"每机型一把"的 USB 备份密钥不是一套。

**这件事的价值是负结果**：不要指望拿 config.bin 的种子去开 DB 默认库。想要 schema 就走 §6
（已经拿到了），或者上机 `sendcmd 1 DB p`。

## 9. 对 mesh 项目的影响（可以直接执行的部分）

| 待办 | 之前 | 现在 |
|---|---|---|
| `EnVbss` / `ToVbssMode` 在哪个视图 | 未知，要上机翻 | **`MultiAPMaster`**，与 `EnBtmKickoff`/`EnRoamKick` 同表 |
| D3（VLAN）入口 | 靠 `0xa94e` 反推 | `CONFIG_CSP_WLAN_VLAN` + `CONFIG_CSPCORE_EBTABLES_{VLAN,FFE_T}` + `OBJ_PORTVLAN_ID` |
| D4（VBSS）入口 | 猜测 | `MultiAPMaster.EnVbss` / `ToVbssMode`（web 页已定位） |
| D5（绑定）入口 | 猜测 | `MultiAPStaCfg{devMac,bssMac,staMac}` + `P1905ComCfg.STAMac/TargetBand/P1905StaBind` |
| D6（agent 落配置） | 只有 `libzte_agent_mm.so` 三个导出 | 加 `P1905ComCfg.SetRoleEnable/RoleElect/EnMasterOnly` 三个开关 |
| D7（BTM 踢人） | 只知字符串在 | `MultiAPMaster.EnBtmKickoff` + `MultiAPDomain.ToSlowLmt/RoamingLmt` 等阈值 + `MultiAPCacheMac.tBtmCacheEn` |
| 11r/FT 支持 | "固件不支持"（推断） | **`CONFIG_CSP_WLAN_SUPPORT_80211KV`，无 11r**（编译期证据） |
| 1905 报文取证 | 只有 multiapd 的 `pktdump` | 再加 `P1905ComCfg.En1905Msg` 一路 |

三条零成本上机命令（只读，不需要停机窗口）：

```sh
sendcmd 1 DB p MultiAPMaster        # 一次把 §6.2 的列全打出来，验证本文的切分
sendcmd 1 DB p P1905ComCfg          # 看 SetRoleEnable / RoleElect / En1905Msg 实际值
sendcmd 1 DB p MultiAPDomain        # 多个实例，看阈值
```

## 10. 局限

- **没有逐列实测**：§6 的表/列归属是按 cspd 字符串的排列顺序切的，靠 httpd 字段表与 lua
  写入顺序交叉印证，但没跑 `sendcmd 1 DB p`。上机一条命令即可把强度从中—强抬到强。
- **`/etc/init.debug` 判定为废文件**，依据是它引用的驱动/脚本在本机全不存在；**没有实机验证**
  （也不建议验证，改 `loadtype` 有变砖风险）。
- **未知 4 字节**（§1.2 的 0x40）含义未定。已排除：压缩块 CRC、解压后 CRC、CRC-32C、
  per-inode 常量。它不参与任何校验，所以不影响解包正确性。
- **`db_default_auto_cfg.xml` 未解**（§8），所以 DB 的完整 schema（含非 mesh 表、列类型、
  默认值）仍然只有 §6 这一路的"名字级"信息。
- 这份 rootfs 与 Zlion 仓库里那份 `kmodule.img`（12.6 MiB）**不是同一版**：本次 13.2 MiB、
  `/etc/version` 2026-08-26；差异未逐字节比对。
