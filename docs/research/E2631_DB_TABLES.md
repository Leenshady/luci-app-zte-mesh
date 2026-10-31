# E2631 配置库全表清单（276 张）

2026-11-01 真机只读取证的第二份原文：配置库**全部表名**。

```sh
sendcmd 1 DB all      # 输出 0..275，共 276 张表
```

> 命令名纠正：本文档初版与 `E2631_ROOTFS` §9 写的是 `sendcmd 1 DB l`，那是我猜的，
> **实际命令是 `DB all`**。`DB l` 未验证，不再使用。

这份清单的用途：**从此不再猜表名**。前两轮踩的两个坑——`IGD.P1905CFGn`（不存在）与
`P1905ComCfg` 被并进后面整段列名（实际只有 8 列）——根源都是"从字符串表推断表名"。
现在表名的全集在手里，凡不在本清单里的表名一律视为不存在。

配套：列级证据在 [E2631_DB_SNAPSHOT](E2631_DB_SNAPSHOT.md)，离线 schema 推断在
[E2631_ROOTFS](E2631_ROOTFS.md) §6。

---

## 1. 与 mesh 直接相关的 19 张（MultiAP / P1905 两族）

这是最有用的切片：这台机器上 mesh 相关的表不是我们之前跟踪的 5 张，而是 **19 张**。

| # | 表 | 已知情况 |
|---|---|---|
| 143 | **`MultiAPCfg`** | **已实测，50 列**（[SNAPSHOT](E2631_DB_SNAPSHOT.md) §7）。角色开关 / 跳数 / `MeshName` / `IFLink` / ACD 全在这 |
| 146 | `MultiAPMaster` | 已实测，55 列。`EnBtmKickoff`/`EnVbss`/`En11rSteer`/`EnActiveDeauth` |
| 144 | `MultiAPDomain` | 已实测，8 实例 × 106 列。全部阈值与状态机 |
| 148 | `MultiAPRoamCatonCfg` | 已实测，1 行 7 列。漫游帧缓存全局参数，出厂全关 |
| 142 | `MultiAPCacheMac` | 表在、0 行。per-MAC 缓存实例，运行时浮动 |
| 149 | `MultiAPStaCfg` | 表在、0 行。三地址映射，没配过绑定 |
| 147 | `MultiAPPTK` | **独立表**（此前被我误当成 `P1905ComCfg` 的列名） |
| 141 | `MultiAPALLSta` | 拓扑视图：全部 STA |
| 145 | `MultiAPDualBand` | 拓扑视图：双频 |
| 150 | `MultiAPTopo` | 拓扑视图 |
| 165 | `P1905ComCfg` | 已实测，**8 列**（§4.1 自纠的那张） |
| 164 | `P1905Cfg` | **与 `P1905ComCfg` 并列的另一张**，此前完全没注意。1905 接口名的候选落点（D5） |
| 168 | **`P1905StaBind`** | **D6 的真正落点**：`P1905StaBind` 是**表名**，不是列名。此前我把它当成 `P1905ComCfg` 的一列 |
| 162 | `P1905AcdBssCfg` | ACD 按 BSS 的配置 |
| 163 | `P1905AcdVenderCfg` | ACD 厂商配置 |
| 170 | `P1905_ACD` | ACD 主体表（`AcdPubKey*` 在 `MultiAPCfg`，密钥材料可能在这里） |
| 166 | `P1905DeviceAlias` | 1905 设备别名 |
| 167 | `P1905DeviceInfo` | 1905 设备信息 |
| 169 | `P1905ZTEMANUF` | ZTE 厂商自定义 1905 TLV |

**三条直接后果**：

1. **D6 的入口从猜测变成"有表"**：`P1905StaBind` 就是 STA 绑定表。上一轮说 `MultiAPStaCfg`
   是 D6 落点，现在看 `P1905StaBind` 更像（名字直接对应 `CmMultiApSetStaBind`）。
   两条都要 dump 才能分清职责：`MultiAPStaCfg` 管三地址映射，`P1905StaBind` 管绑定关系。
2. **D5 的 1905 接口名有了新候选**：`P1905Cfg` 与 `P1905ComCfg` 是并列的两张表，
   此前我把它们当成一张。`InterfaceName`/`EnableP1905` 很可能在 `P1905Cfg`。
3. **`MultiAPPTK` / `P1905StaBind` 这两个"列名"其实是表名**——这是第三处同类错误，
   与前两处同因：字符串表里表名与列名交错，靠排列顺序切不出可靠边界。

## 2. 与漫游/无线相关的 11 张（D7 的新线索）

| # | 表 | 为什么值得看 |
|---|---|---|
| 270 | **`WlanRoamingCfg`** | **此前完全没在视野里**。WLAN 侧漫游配置，11k/11v/11r 的开关可能在它，而不在 `MultiAPMaster` |
| 271 | `WlanRoamingSta` | 漫游 STA 实例表 |
| 259 | `WLANCfg` | 无线总配置 |
| 261 | `WLANSSID` | SSID 实例 |
| 258 | `WLANBase` | 基础参数 |
| 260 | `WLANPSK` | 密钥 |
| 264 | `WLANWEP` | 老加密 |
| 265 | `WLANWMM` | WMM |
| 266 | `WLANWPS` | WPS（配对） |
| 262 | `WLANScheduler` | 定时开关无线 |
| 272 | `WlanTimeCfg` | 无线的时段配置 |

`WlanRoamingCfg` / `WlanRoamingSta` 是这一轮最值得新增的两条待办：
[E2631_ROOTFS](E2631_ROOTFS.md) §5 从 `autouserconf` 得到"只有 11k/11v 没有 11r"，
如果 11r 在这两张表里有开关位，那就是**配置层没开**而不是**固件不支持**——这会改写 D7 的判据。

## 3. 角色与组网相关的 3 张

| # | 表 | 说明 |
|---|---|---|
| 139 | `MapCooperatorCfg` | [SCHEMA](../milestones/SCHEMA.md) 已记录：`AutoSetup`、`EnRebootElect`、`WpsLongPress` 等，**影响角色选举触发分支** |
| 154 | `MultiapCapability` | 能力声明表：设备向控制器声明支持哪些能力（与 `SupportStrategies=251` 位图可能对应） |
| 206 | `SFUMeshCfg` | SFU（主网关侧）的 mesh 配置 |
| 207 | `SFUTopoCfg` | SFU 侧拓扑配置 |

`MultiapCapability` 与 `MultiAPDomain.SupportStrategies=251` 一起看，有机会解开那个位图。

## 4. 其余表（按字母序全量）

仅供"存在性"核对，不再逐张说明。共 276 张，编号即 `DB all` 的行号。

```
0 AcIptv                       1 AccessAlias                  2 AccessDev
3 AccessFamily                 4 AccessNotifyCfg              5 AclCfg
6 Addr6Cfg                     7 Addr6Store                   8 AdevGroupDevice
9 AdevGroupInfo               10 AgentBindInfo               11 AntiNet
12 AntiNetAuth                13 ApIptv                      14 AutoReboot
15 BanPort                    16 BrGrp2ndIP                  17 BrPathCost
18 BrPortPermit               19 Bridge                      20 Brport
21 CPUCFG                     22 ChangeWanMode               23 ClearLinkCtrl
24 CloudServer                25 ComCmdConf                  26 CpeDetectCfg
27 DDNSClient                 28 DDNSHostname                29 DDNSService
30 DHCP4CInfo                 31 DHCP4SBridgeCfg             32 DHCP4sClient
33 DHCP4sIPv4Addr             34 DHCP4sPool                  35 DHCP4sPortFilter
36 DHCP4sServer               37 DHCP4sStaticAddr            38 DHCP6C
39 DHCP6CRcvOpt               40 DHCP6CRetrytime             41 DHCP6CSentOpt
42 DHCP6CSer                  43 DHCP6S                     44 DHCP6SClt
45 DHCP6SCltAddr              46 DHCP6SCltOpt                47 DHCP6SCltPfx
48 DHCP6SHostInfo             49 DHCP6SOpt                  50 DHCP6SPool
51 DHCPCComm                  52 DHCPSComm                  53 DHCPSHostInfo
54 DNSClientServer            55 DNSComm                    56 DNSDHCPHostsList
57 DNSHostsList               58 DNSRelayServer              59 DNSSettings
60 DSSCfg                     61 DSSInfo                    62 DevAuthInfo
63 DevDongType                64 DevInfo                    65 DevPOTS
66 DeviceLogUploadCfg         67 DeviceWol                  68 DiagCfg
69 DiagConnection             70 DownLoadDiag               71 DownLoadDiagIncrResult
72 DownLoadDiagPerConnResult  73 DualWanPrority             74 ETHRMON
75 EthGlobalConfProduct       76 EthLink                    77 EthPortConfProduct
78 EthRatelimitProduct        79 EthVlan                    80 FMCfg
81 FTPServerCfg               82 FTPUser                    83 FTTRCAP
84 FTTRInfo                   85 FWALG                      86 FWBase
87 FWChain                    88 FWDMZ                      89 FWDOS
90 FWIP                       91 FWLevel                    92 FWLevelCfg
93 FWPM                       94 FWProtocol                 95 FWSC
96 FWURL                      97 GIAUser                    98 GIAUserApp
99 GIAUserFilter             100 GuestIsolate              101 GuestIsolateInfo
102 HLTCtl                   103 HLTHistoryLanStatInfo     104 HLTHistoryWLanStatInfo
105 HLTInfo                  106 HLTLanStatInfo            107 HLTWLanStatInfo
108 HistoryHLTInfo           109 HostNameList              110 Htpdate
111 HttpServerCfg            112 IGMPLanSrc                113 IGMPProxy
114 IGMPWan                  115 IPGlobal                  116 IPInterface
117 IPV6PRoute               118 IPv4Address               119 IPv4ForwardingRT
120 IPv6BridgeCfg            121 IPv6CommOpts              122 IPv6ForwardingRT
123 InterfaceStatus          124 LANDNSOriginCfg           125 LEDCfg
126 LaninfoCtl               127 LaninfoDev                128 LedTimerCfg
129 Log                      130 LogSerialCfg              131 LogTraceCfg
132 MAC                      133 MACUsed                   134 MLDProxyCfg
135 MLDWan                   136 MQTTSrv                   137 MSMCfg
138 MacFilter                139 MapCooperatorCfg          140 MirrorConf
141 MultiAPALLSta            142 MultiAPCacheMac           143 MultiAPCfg
144 MultiAPDomain            145 MultiAPDualBand           146 MultiAPMaster
147 MultiAPPTK               148 MultiAPRoamCatonCfg       149 MultiAPStaCfg
150 MultiAPTopo              151 MultiCastGlobalConf       152 MultiCastPortConf
153 MultiCastSpecialGroup    154 MultiapCapability         155 ND
156 NDIFSet                  157 NSLookupDiagRes           158 NatIfSetting
159 NatPtMapping             160 NsLookup                  161 NsLookupResult
162 P1905AcdBssCfg           163 P1905AcdVenderCfg         164 P1905Cfg
165 P1905ComCfg              166 P1905DeviceAlias          167 P1905DeviceInfo
168 P1905StaBind             169 P1905ZTEMANUF             170 P1905_ACD
171 PINGDiag                 172 PPPCache                  173 PPPIF
174 PRoute                   175 ParentControlGroup        176 ParentControlProhibit
177 ParentControlTime        178 ParentControlUser        179 PassThroughIpv6
180 PhyEnhance               181 PortBindBasic             182 PortBinding
183 PortControl              184 PortIsolation            185 PortRedirect
186 PowerCfg                 187 PrefixCfg                 188 QDCfg
189 QOSBasic                 190 QoSBandwidthRule         191 QoSRemark
192 RA                       193 RAIS                     194 RAOpt
195 Relation                 196 RouteSYSRT               197 Router
198 SFU                      199 SFUAPAbility             200 SFUAction
201 SFUDevAuthInfo           202 SFUDiagCfg               203 SFUEthStatus
204 SFUIPTVCfg               205 SFULanHostInfo            206 SFUMeshCfg
207 SFUTopoCfg               208 SFUWanInfo                209 SFUWanSelectInfo
210 SFUWiFi                  211 SFUWiFiAP                 212 SFUWiFiMlo
213 SFUWiFiRd                214 SFUWiFiSec                215 SFUWiFiSsid
216 SNTP                     217 STAIF                    218 STAProfile
219 Sbus                     220 SecProtect               221 SoftwareList
222 SrmCfg                   223 SupportedDataModel        224 SwitchMirr
225 SysdiagCfg               226 SyslogCfg                227 TagParamCfg
228 TelnetCfg                229 TimePolicy               230 TimeSynInfo
231 TimeUpgrade              232 Tr069Queue               233 UADDCfg
234 UDPEchosDiag             235 UNBindUser               236 UPnPCfg
237 UPnPPortMap              238 UURESTARTTIMEOUT         239 UpLoadDiag
240 UpLoadDiagIncrResult     241 UpLoadDiagPerConnResult   242 Upgrade
243 UserIF                   244 VendorCfg                245 VendorLog
246 WANC                     247 WANCD                    248 WANCIP
249 WANCIPOpts               250 WANCIPPrvData            251 WANCPPP
252 WANCPPPComm              253 WANCPPPStatus            254 WAND
255 WANDCommCfg              256 WANSelectCfg             257 WLANAD
258 WLANBase                 259 WLANCfg                  260 WLANPSK
261 WLANSSID                 262 WLANScheduler            263 WLANSetting
264 WLANWEP                  265 WLANWMM                  266 WLANWPS
267 WanAddrStatus            268 WanDetectStatus          269 WanKeyParm
270 WlanRoamingCfg           271 WlanRoamingSta           272 WlanTimeCfg
273 WolInfo                  274 cpeserver                 275 dynQoS
```

## 5. 负结果：清单里**没有**的名字

这几个名字此前在文档里出现过，现在可以一律判死：

| 名字 | 此前出自 | 结论 |
|---|---|---|
| `P1905CFGn` | 上一轮凭空造的表名 | **作为表名不存在**（`table not find` 已是第一手证据）。但它可能是**行名**，见下 |
| `IGD` | 我猜的 `IGD.P1905CFGn` | 库里**没有 `IGD` 这张表**。但 `IGD.` 是**行名前缀**——`TimePolicy` 的行名就叫 `IGD.TpCfg0`…`IGD.TpCfg2`（[FEATURE_MAP](FEATURE_MAP.md) §15 的采集记录） |
| `MESH` / `Netsphere` / `EASYMESH` | [E2631_ROOTFS](E2631_ROOTFS.md) §6.4 说是"视图别名" | **不在 276 张 DB 表内**。它们更可能是 `httpd`/web 层的视图映射名，不是配置库的表 |

**关于 `IGD.P1905CFGn` 的再判断**（这一条比"不存在"更有用）：

`sendcmd 1 DB p` 的**参数只收表名**，所以 `DB p P1905CFGn` 报 not find 只能证明它不是表名，
不能证明这个标识在系统里不存在。仓库里已有硬证据表明中兴这套 DB 的**行名可以带 `IGD.` 前缀**
（`TimePolicy` 的 `IGD.TpCfg0..2`）。所以 `IGD.P1905CFGn` 的正确读法更可能是
**"`IGD` 视图下的第 n 行"**，而不是"一张表"。

验证办法：dump `P1905Cfg`(#164) 时看它的 `ViewName` 是不是形如 `IGD.P1905CFG1`。
若是，1905 接口配置就在这张表里，而"每实例一行"的结构与 `MultiAPDomain`(`MULTIAPDOMAIN%d`) 完全同构。

最后一条要改文档：`MESH→Netsphere→EASYMESH` 不该再写成"DB 视图别名"，
应降级为"web 层名称映射，与配置库无关"。

## 6. 下一步 dump 的优先级（全部只读）

| 顺序 | 命令 | 期望 |
|---|---|---|
| 1 | `sendcmd 1 DB p P1905StaBind` | D6 的真身；若 0 行，与 `MultiAPStaCfg` 一起视为"存在未配" |
| 2 | `sendcmd 1 DB p P1905Cfg` | D5 的 1905 接口名 / `EnableP1905`；**同时看 `ViewName` 是不是 `IGD.P1905CFG1`**（§5） |
| 3 | `sendcmd 1 DB p WlanRoamingCfg` | **11r 是否只是配置层没开**（可能改写 D7 判据） |
| 4 | `sendcmd 1 DB p MultiapCapability` | 与 `SupportStrategies=251` 位图对照 |
| 5 | `sendcmd 1 DB p MapCooperatorCfg` | 角色选举触发分支 |

`DB all` 已经跑过，不必再跑；但若要表名与行数一起看，`sendcmd 1 DB all` 之外还可以
逐表 `DB p <名>` 喂给 `wc -l`。
