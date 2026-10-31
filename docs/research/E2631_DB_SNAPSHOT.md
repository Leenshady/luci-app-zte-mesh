# E2631 配置库实测快照：七张 mesh 表的真身

2026-11-01，真机（市售 AX3000，自报 E2631，2026-08 固件）上跑**只读**命令得到的原文整理。三轮：

```sh
# 第一轮
sendcmd 1 DB p MultiAPMaster     # RowCount=1，55 列
sendcmd 1 DB p P1905ComCfg       # RowCount=1，8 列
sendcmd 1 DB p MultiAPDomain     # RowCount=8，每行 106 列
# 第二轮（§5）
sendcmd 1 DB p P1905CFGn         # table not find —— 这个表名是错的
sendcmd 1 DB p MultiAPStaCfg     # RowCount=0（表存在、无实例）
sendcmd 1 DB p MultiAPCacheMac   # RowCount=0（表存在、无实例）
sendcmd 1 DB p MultiAPRoamCatonCfg   # RowCount=1，7 列
# 第三轮（§6、§7）
sendcmd 1 DB p MultiAPCfg        # RowCount=1，50 列 —— 角色开关/跳数/MeshName/IFLink 全在这
sendcmd 1 DB all                 # 全部表名，276 张 —— 见 E2631_DB_TABLES
```

这份文件的用途只有一个：**它是 [E2631_ROOTFS](E2631_ROOTFS.md) §6 那些离线推断的对照答案**。
§6 是从 `cspd` 字符串表按排列顺序切出来的（当时标"中—强"），这里给出实测结果 —
**表归属错了三处、数值注释错了一处**，错误与成因记在 §4、§5.1、§6.2。
表名全集另见 [E2631_DB_TABLES](E2631_DB_TABLES.md)。

> 值都带出处：这一份是出厂状态，机器已组过网（domain 4/8 是自动生成的 `ZMESH-` 专用 SSID）。

---

## 1. `MultiAPMaster`（单实例，`ViewName=MULTIAPMASTER`，55 列）

顺序即 schema 顺序，值即实测值。

| 列 | 值 | 列 | 值 |
|---|---|---|---|
| `ViewName` | `MULTIAPMASTER` | `EnMultiDomain` | 1 |
| `EnRoam` | 1 | `EnBandSteer` | **0** |
| `EnDevSteer` | 1 | `ApPeriodRptIntval` | 0 |
| `TrafficStatsRptFlag` | 1 | `LinkMetricsRptFlag` | 1 |
| `StaRssiRptThr24G` | -110 | `StaRssiRptThr5G` | -110 |
| `RssiHysteresisMargin` | 0 | `ChanUtilRptThr24G` | 0 |
| `ChanUtilRptThr5G` | 0 | `RssiCompensateThr` | 0 |
| `ReTransIntval` | 3 | `ReTransCntLmt` | 1 |
| `EnActiveDeauth` | **1** | `Standalone` | 0 |
| `SupportBSMode` | `None` | `CurrentBSMode` | `RSSIThreshold` |
| `EnBhPhyRate` | 0 | `EnAgileSteer` | 0 |
| `InChannelScanRptFlag` | 0 | `UnSuccAssocRptFlag` | 0 |
| `UnSuccAssocRptRate` | 0 | `Profile1bStaAssocDis` | 0 |
| `Profile2bStaAssocDis` | 0 | `TransmitPowerLimit24G` | 27 |
| `TransmitPowerLimit5G` | 27 | `TransmitPowerLimit6G` | 0 |
| `EnApOnOflineLog` | 0 | `EnRoamLog` | 0 |
| `EnDbusRoamReport` | 1 | `En11rSteer` | **0** |
| `WiFi6StaStatusRptFlag` | 1 | `TopoSortTimer` | 15 |
| `EnBhSteerCheck` | 0 | `11rSsidSync` | 1 |
| `EnBSFilter` | 0 | `HashCap` | 128 |
| `EnBtmKickoff` | **0** | `EnNoCandidateKick` | 0 |
| `NoCandidateKickRssi` | -80 | `NoCandidateLmtEnKick` | 5 |
| `EnQuickRoam` | 0 | `EnNeighBorLearn` | 0 |
| `EnQuickRoamDecsion` | 0 | `EnSteerNo11k` | 0 |
| `EnMloTopoSort` | 0 | `EnVbss` | **0** |
| `EnMeshSR` | 0 | `ToVbssMode` | 1 |
| `EnSteerCurBand` | 0 | `EnRoamKick` | **0** |
| `EnIPhoneRoamOpt` | 0 | | |

按功能分组读（出厂状态）：

| 组 | 列 | 出厂 |
|---|---|---|
| 多域 / 漫游总开关 | `EnMultiDomain=1`、`EnRoam=1`、`EnDevSteer=1`、`EnBandSteer=0`、`Standalone=0` | **漫游与设备引导开着，频段引导关着，非 standalone** |
| 引导方式 | `CurrentBSMode=RSSIThreshold`、`SupportBSMode=None`、`EnSteerNo11k=0`、`EnSteerCurBand=0`、`EnAgileSteer=0`、`EnQuickRoam=0`、`EnQuickRoamDecsion=0`、`EnNeighBorLearn=0`、`EnBhSteerCheck=0`、`EnBSFilter=0` | 只走 RSSI 门限这一种；快漫游、邻居学习、非 11k 终端引导全关。`SupportBSMode=None` 与 `CurrentBSMode=RSSIThreshold` 并存，读作"不限制可选模式，当前用 RSSI 门限" |
| 踢人 | `EnActiveDeauth=1`、`EnBtmKickoff=0`、`EnNoCandidateKick=0`、`NoCandidateKickRssi=-80`、`NoCandidateLmtEnKick=5`、`EnRoamKick=0` | **默认踢人是 deauth，不是 BTM** —— 见 §1.1 |
| 11r | `En11rSteer=0`、`11rSsidSync=1` | 与 `CONFIG_CSP_WLAN_SUPPORT_80211KV`（无 11r）一致：SSID 同步开着，引导不用 11r |
| VBSS | `EnVbss=0`、`ToVbssMode=1` | 功能位在，出厂关 |
| 上报 / 统计 | `ApPeriodRptIntval=0`、`TrafficStatsRptFlag=1`、`LinkMetricsRptFlag=1`、`WiFi6StaStatusRptFlag=1`、`StaRssiRptThr24G/5G=-110`、`ChanUtilRptThr24G/5G=0`、`RssiHysteresisMargin=0`、`RssiCompensateThr=0`、`ReTransIntval=3`、`ReTransCntLmt=1`、`InChannelScanRptFlag=0`、`UnSuccAssocRptFlag=0` | 链路度量/流量统计开着，**带内扫描报告关着**（D3） |
| 功率 / 日志 / 拓扑 | `TransmitPowerLimit24G/5G=27`、`6G=0`、`EnApOnOflineLog=0`、`EnRoamLog=0`、`EnDbusRoamReport=1`、`TopoSortTimer=15`、`HashCap=128` | 27 dBm 上限；拓扑排序 15 s；MAC 哈希 128 |

### 1.1 最有用的一条：默认"踢人"走的是 `EnActiveDeauth`，不是 `EnBtmKickoff`

`EnActiveDeauth=1` + `EnBtmKickoff=0` + `EnRoamKick=0`，同时域级 `EnStaRoam=1`。
也就是说出厂行为是：**按门限判断 → 主动去认证踢掉**，BTM 提示（11v 的 BSS Transition
Management Request）那条路默认是关的。做 D7 时这决定了我们该比什么：

- 我们要发的如果只有 `0x893a` steering 请求，厂商这台默认**不会**跟着踢 —— 它走 deauth；
- 反过来，我们抓厂商的线上行为时，应预期看到 deauth / disassoc，而不是 BTM Request。

`DisAssocImminent=1` + `DisAssocTimer=30` 在 `MultiAPDomain`（§3）里，是 BTM 真正启用以外的
另一套参数，别把它当成"BTM 已开"的证据。

---

## 2. `P1905ComCfg`（单实例，8 列）

| 列 | 值 | 备注 |
|---|---|---|
| `Enable` | 1 | 1905 总开关 |
| `EnableIsSync` | 1 | 使能状态在 mesh 内同步 |
| `Policy` | 1 | 策略档位 |
| `DropNum` | 0 | 丢弃计数（0 = 没丢过） |
| `MarketConfEn` | 1 | 市场配置使能 |
| `MarketSign` | `8000` | 市场标识（运营商/区域档，`8000` 应为默认） |
| `DropRoam` | 0 | 丢弃时是否触发重选 |
| `A1Enable` | 0 | A1 口（1905 拓扑里的第一个口）相关，出厂关 |

**这张表只有 8 列。** 见 §4.1 —— 我原先从字符串表切给它的四十多个列名，绝大多数不在这里。

---

## 3. `MultiAPDomain`（8 实例，每行 106 列）

### 3.1 实例与 SSID 槽

`RowCount=8`，`ViewName` 是 `MULTIAPDOMAIN1..8`，`ESSID` 是 `isSave=0` 的运行时值，
`DomainUsed` 也是 `isSave=0`（= 运行时是否生效）：

| 实例 | `ESSID` | `DomainUsed` | 判断 |
|---|---|---|---|
| `MULTIAPDOMAIN1` | `LLJ_SF` | **1** | 生效 |
| `MULTIAPDOMAIN2` | `ZTE-QCuecc_GUEST` | 0 | Guest 未开 |
| `MULTIAPDOMAIN3` | `SSID3` | 0 | **出厂占位名，从没改过** |
| `MULTIAPDOMAIN4` | `ZMESH-TRx3zR` | **1** | mesh 专用 SSID，生效 |
| `MULTIAPDOMAIN5` | `LLJ_5G` | **1** | 生效 |
| `MULTIAPDOMAIN6` | `ZTE-QCuecc_GUEST` | 0 | Guest 未开（与 2 同名，另一个频段） |
| `MULTIAPDOMAIN7` | `SSID7` | 0 | 出厂占位名 |
| `MULTIAPDOMAIN8` | `ZMESH-TRx3zR_5G` | **1** | 生效 |

两条可用的结论：

1. **domain 实例 1:1 对应 SSID 槽位**（`MULTIAPDOMAINn` ↔ 第 n 个 SSID）。占位名直接就是
   `SSID3` / `SSID7`，改名后的则是 `LLJ_SF`、`LLJ_5G`、`ZMESH-TRx3zR(_5G)`。做多 SSID 时
   不要再去找"domain 和 SSID 怎么映射"——就是槽号。
2. **生效的只有 4 个**：SSID 1/4/5/8。其中 4 与 8 是同一台机器自动生成的 `ZMESH-` 专用 SSID
   的两个频段（2.4G / 5G），也就是说**组网用的专用 SSID 也占一个 domain**。踢人/漫游阈值是
   per-domain 的，回程专用 SSID 那一路同样有自己的一套 —— 做 D7 时不能只配业务的 domain。

### 3.2 列与出厂值（Row 0 原文，106 列）

8 行除 `ViewName` / `ESSID` / `DomainUsed` 外，逐列值一致（人工比对，未发现差异）。

| 列 | 值 | 列 | 值 |
|---|---|---|---|
| `ViewName` | `MULTIAPDOMAIN1` | `ESSID` | `LLJ_SF` |
| `RoamRssiLmt24G` | -60 | `RoamRssiLmt5G` | -70 |
| `RoamRssiLmt6G` | -70 | `RoamLoadLmt24G` | 80 |
| `RoamLoadLmt5G` | 80 | `RoamLoadLmt6G` | 80 |
| `BsRssiLmt24G` | -60 | `BsRssiLmt5G` | -110 |
| `BsRssiLmt5GTo6G` | -60 | `BsRssiLmt6G` | -110 |
| `BsLoadLmt24G` | 80 | `BsLoadLmt5G` | 80 |
| `BsLoadLmt6G` | 80 | `ChanInterferLmt24G` | 25 |
| `ChanInterferLmt5G` | 25 | `BsVhtChk24G` | 0 |
| `BsVhtChk5G` | 0 | `BsActiveStaChk24G` | 0 |
| `BsActiveStaChk5G` | 0 | `BsStaIdleRateLmt24G` | 10 |
| `BsStaIdleRateLmt5G` | 10 | `BsAcceptBwUtil24G` | 80 |
| `BsAcceptBwUtil5G` | 80 | `BsAcceptRssi24G` | -110 |
| `BsAcceptRssi5G` | -110 | `BsAcceptRssi6G` | -110 |
| `BsAcceptVhtCheck24G` | 0 | `BsAcceptVhtCheck5G` | 0 |
| `BsTPLimit` | 9999999 | `RssiDelta` | 2 |
| `ToPreRoamLmt` | 2 | `ToSlowLmt` | 1 |
| `ToFastLmt` | 1 | `InitLmt` | 15 |
| `PreRoamLmt` | 5 | `RoamingLmt` | 15 |
| `OfflineLmt` | 1800 | `ForbidLmt` | 36000 |
| `PreRoamForbidLmt` | 120 | `BSLazyForbidLmt` | 300 |
| `LegacyForbidLmt` | 43200 | `BtmFailedLmt` | 20 |
| `LegacyFailedLmt` | 5 | `UnknownApFailedLmt` | 5 |
| `LazyBtmFailedLmt` | 2 | `LazyLegacyFailedLmt` | 1 |
| `LegacyNoCandFailedLmt` | 5 | `BsMeasureLmt` | 15 |
| `SelStaLoadWeight` | 1 | `SelStaRssiWeight` | 1 |
| `SelApLoadWeight` | 1 | `SelApRssiWeight` | 1 |
| `StatusInitTimer` | 1 | `StatusOnlineTimer` | 1 |
| `StatusSlowTimer` | 5 | `StatusFastTimer` | 1 |
| `StatusPreRoamTimer` | 1 | `StatusForbidTimer` | 10 |
| `InBlackTimer` | 5 | `DataValidPeriod` | 5 |
| `EnOfflineRstRoam` | 1 | `EnUnknownApBlack` | 0 |
| `EnRoamModes` | 19 | `SupportStrategies` | 251 |
| `SupportRrm` | 1 | `RoamBounceCountsLmt` | 3 |
| `RoamBounceDetectTimeLmt` | 60 | `RoamBounceDwellTimeLmt` | 1200 |
| `BsBounceCountsLmt` | 3 | `BsBounceDetectTimeLmt` | 60 |
| `BsBounceDwellTimeLmt` | 1200 | `BsStaBounceLmt` | 3 |
| `BsStaBounceTimeLmt` | 3600 | `BsBounce24gTo6gLmt` | 2 |
| `24Gto5GRssiDelta` | -20 | `5Gto24GRssiDelta` | 10 |
| `To6GRssiDelta` | -20 | `EnStaRoam` | 1 |
| `EnMLOStaRoam` | 0 | `EnStaBandSteer` | 1 |
| `EnBackHaulStaRoam` | 1 | `EnLegacyStaRoam` | **0** |
| `ReqTimeout` | 1 | `Abridged` | 1 |
| `DisAssocImminent` | 1 | `DisAssocTimer` | 30 |
| `CandCurApRssiDelta` | 10 | `LoadRssiLmtDelta` | 5 |
| `DomainUsed` | 1 | `BsActive` | 0 |
| `AutoRvsAbrdgDisImnt` | 1 | `Ignore11KCap` | 1 |
| `EnBStaRssiStrategy` | 0 | `AssocStaNumStart` | 10 |
| `AssocStaNumDelta` | 4 | `AssocStaBandDelta` | 10 |
| `RssiDeltaJudgedMove` | 5 | `LoadRssiDeltaJudgedMove` | 7 |
| `LoadRefuseTimes` | 0 | `LoadRefuseAgeTime` | 300 |
| `SynRoamStaNumLmt` | 0 | `listenAgeTime` | 86400 |
| `DelayToFastTime` | 2 | `DelayBSInterval` | 0 |

### 3.3 状态机的形状（从列名读出来的，**推断**）

`InitLmt / PreRoamLmt / RoamingLmt / OfflineLmt / ForbidLmt` 与
`StatusInitTimer / StatusOnlineTimer / StatusSlowTimer / StatusFastTimer / StatusPreRoamTimer / StatusForbidTimer`
是**成对**的：五个"时长上限"配六个"状态停留计时器"。这是 [E2631_ROOTFS](E2631_ROOTFS.md) §6.3
说"BTM 不只是开关"的实据 —— 状态机大致是

```
Init --(StatusInitTimer)--> Online --(StatusSlowTimer)--> Slow --(StatusPreRoamTimer)--> PreRoam --(StatusFastTimer)--> Fast
                                                                    └----> Forbid --(StatusForbidTimer)--> ...
```

配合 `ToPreRoamLmt=2 / ToSlowLmt=1 / ToFastLmt=1`（进入各状态要连续命中几次）和
`DataValidPeriod=5`（测量数据有效期）。**只把 `EnBtmKickoff` 打开、这些不配，等于打开一个不会动的状态机。**
（状态名与转移是**按列名推的**，没有反汇编 `cspd` 佐证；要坐实得看日志或 `cspd` 的 dbus 消息。）

另外三个位图字段，值得记：

| 列 | 值 | 二进制 | 读法 |
|---|---|---|---|
| `EnRoamModes` | 19 | `0b10011` | 三位有效：bit0、bit1、bit4 |
| `SupportStrategies` | 251 | `0b11111011` | 八位里只缺 bit2 |
| `SupportRrm` | 1 | — | 支持 11k（Radio Resource Measurement） |

`SupportRrm=1` 与 `Ignore11KCap=1` 合起来的意思：**发 11k 测量请求时不管终端报的能力**，
而 `EnLegacyStaRoam=0` 与 `EnSteerNo11k=0`（§1）说明非 11k/v 的老终端出厂就不引导。
两处一致：这台机器的引导只面向 11k/11v 终端。

---

## 4. 与离线推断的对照（这一节是本次实测的主要价值）

### 4.1 `P1905ComCfg` —— **切错了表，必须自纠**

| 我原来写的 | 实测 |
|---|---|
| `P1905ComCfg` 有四十多列，含 `SetRoleEnable` / `RoleElect` / `EnMasterOnly` / `MeshName` / `MaxHop` / `CurHop` / `En1905Msg` / `EnSendM1EachBand` / `ACD.*` / `VersionMode` / `SteeringPolicy` / `EnAgentSteer` / `EnAgentBs` / `STAMac` / `TargetBand` 等 | **只有 8 列**（§2），上面这些一个都不在 |

错因有两层，第二层比第一层严重：

1. `cspd` 的 schema 字符串表里列名是**连续排放**的，表名夹在中间当分隔。`P1905ComCfg` 只有 8 个
   名字，它后面的表名（`MultiAPCfg`、`MultiAPMaster`、`MultiAPDomain%d` …）被我漏切，于是把后面
   整段列名并进了这一张表。
2. **仓库里早有正确答案**：[SCHEMA](../milestones/SCHEMA.md) 的「补充（2026-10-12）」一节写着
   "`P1905ComCfg`：SR 上有 8 个字段：`Enable`（36）、`EnableIsSync`（68）、`Policy`（100）、
   `DropNum`（132）、`MarketConfEn`（164）、`MarketSign`（196，字符串）、`DropRoam`（292）、
   `A1Enable`（324），共 328 字节"——**与 E2631 实测逐字一致**。我重新从字符串表推断时没先查它，
   等于把 B.2.1 已经干过的活重做了一遍还做错了。

已经能确认归属的：`TopoSortTimer`、`EnBSFilter` 实测在 **`MultiAPMaster`**（§1），
`MarketSign`、`MarketConfEn` 确实在 `P1905ComCfg`（§2）。

还没定位的那些（`SetRoleEnable` / `RoleElect` / `EnMasterOnly` / `En1905Msg` / `MaxHop` / `CurHop` /
`ACD.*` / `STAMac` / `TargetBand`）**不在本轮 dump 的表里**，但角色开关这一组现在已经有了确定答案：
**它们在 `MultiAPCfg`**，不是任何 `P1905*` 表。依据是本地 B.2.1 的还原（`MultiAPCfg` 50 字段，
`Mode` 偏移 36、`SetRoleEnable`、`RoleElect`、`WANForward`、`MaxHop` 偏移 228、`CurHop` 偏移 260、
`VersionMode` 偏移 356、`P1905DevNum` 偏移 388、`EnMasterOnly` 偏移 804、`MeshName` 偏移 868、
`IFLink` 偏移 1444、`NoSteeringOUIs` 偏移 1540），以及 `multiapd` 里 `MapGetMultiAPCfgDB` 的调用链。

**`P1905CFGn` 作为表名不存在**——那个表名是我上一轮凭空造的（§6）。这一条后来跑了，结果分两步：

```sh
sendcmd 1 DB p MultiAPCfg    # 50 列；角色开关 / 跳数 / MeshName / IFLink / En1905Msg 全在这（§6）
sendcmd 1 DB all             # 276 张表名全集（见 E2631_DB_TABLES）
```

> 命令名纠正：上一版这里写的是 `sendcmd 1 DB l`，那是我猜的，**实际是 `DB all`**。

`IGD.P1905CFGn` 要分两层看，不能笼统说"不存在"：

- **作为表名不存在**：276 张表里既没有 `P1905CFGn`，也没有 `IGD`。
- **作为行名可能是合法的**：仓库里已有硬证据——`TimePolicy` 表的行名就叫 `IGD.TpCfg0`…`IGD.TpCfg2`
  （[FEATURE_MAP](FEATURE_MAP.md) §15 的采集记录）。所以中兴这套 DB 里 **`IGD.` 是行名前缀**，
  `IGD.P1905CFGn` 更可能是"某张表里的第 n 行"，而不是"某张表"。

这解释了为什么 `DB p P1905CFGn` 报 not find：`DB p` 的**参数只收表名**，不收行名。
下一步 dump `P1905Cfg`(#164) 时要顺便看它的行名是不是形如 `IGD.P1905CFG1`。

### 4.2 `MultiAPMaster` —— 切对了，但少了 12 列

实测 55 列，与推断的差异：

| 情况 | 列 |
|---|---|
| 推断有、实测确认 | `EnRoam`、`EnBandSteer`、`EnDevSteer`、`Standalone`、`SupportBSMode`、`CurrentBSMode`、`En11rSteer`、`11rSsidSync`、`EnBtmKickoff`、`EnNoCandidateKick`、`NoCandidateKickRssi`、`EnSteerNo11k`、`EnRoamKick`、`EnVbss`、`ToVbssMode`、`EnMeshSR`、`EnSteerCurBand`、`EnIPhoneRoamOpt`、`EnQuickRoam`、`EnMloTopoSort`、`EnApOnOflineLog`、`EnRoamLog`、`HashCap`、`StaRssiRptThr24G/5G`、`ChanUtilRptThr24G/5G`、`ReTransIntval`、`ReTransCntLmt`、`TransmitPowerLimit24G/5G/6G`、`Profile1bStaAssocDis`、`Profile2bStaAssocDis`、`InChannelScanRptFlag`、`EnDbusRoamReport`、`WiFi6StaStatusRptFlag`、`EnBhSteerCheck` |
| 推断漏了，实测新增 12 列 | `EnMultiDomain`、`ApPeriodRptIntval`、`TrafficStatsRptFlag`、`LinkMetricsRptFlag`、`RssiHysteresisMargin`、`RssiCompensateThr`、`EnActiveDeauth`、`EnBhPhyRate`、`EnAgileSteer`、`UnSuccAssocRptFlag`、`UnSuccAssocRptRate`、`NoCandidateLmtEnKick` |
| 推断错放（实测在别处） | `TopoSortTimer`、`EnBSFilter` 原先算给了 `P1905ComCfg`，实际在这里 |

### 4.3 `MultiAPDomain` —— 切对了，数值注释有一处错

列名全部命中。错的是我给 `OfflineLmt` 标的参考值：实测 `OfflineLmt=1800`、
`ForbidLmt=36000`（我把 36000 记到了 `OfflineLmt` 头上）。`LegacyForbidLmt=43200` 是对的。

---

## 5. 第二轮 dump：一个不存在的表名 + 第二处表归属错

第二轮跑了 `sendcmd 1 DB p P1905CFGn`，返回 **`table(P1905CFGn) not find`**，随后列出几张相近的表。
逐张 dump 的结果：

| 表 | `RowCount` | 说明 |
|---|---|---|
| `MultiAPStaCfg` | **0** | 表**存在**，但当前没有实例 |
| `MultiAPCacheMac` | **0** | 表**存在**，但当前没有实例 |
| `MultiAPRoamCatonCfg` | 1 | 7 列，见下 |

`MultiAPRoamCatonCfg`（`RowCount=1`）实测 7 列：

| 列 | 值 |
|---|---|
| `OfflineRetrieveEn` | 0 |
| `TxDataAfterBaEn` | 0 |
| `TxBaDelayMs` | 10 |
| `ZeroDropCfgEn` | 0 |
| `BtmCacheEn` | **0** |
| `TxConDelayMs` | 100 |
| `TxDropCnt` | 0 |

### 5.1 第二处表归属错：这 7 列原先被我记在 `MultiAPCacheMac` 名下

| 我原来写的 | 实测 |
|---|---|
| `MultiAPCacheMac`：`OfflineRetrieveEn, TxDataAfterBaEn, TxBaDelayMs, ZeroDropCfgEn, tBtmCacheEn, tTxConDelayMs, tTxDropCnt` | 这 7 列在 **`MultiAPRoamCatonCfg`**；`MultiAPCacheMac` 是**空表**。而且列名**没有 `t` 前缀**：`BtmCacheEn` 不是 `tBtmCacheEn`，`TxConDelayMs` 不是 `tTxConDelayMs` |
| `MultiAPRoamCatonCfg`：只有 `RoamFilter` 一列 | 实测没有 `RoamFilter`，是上面 7 列 |

同一个错因（字符串表里切错表边界）第二次犯。§4.1 那两层原因同样适用，第二层——**不先查本地
已还原的 schema**——是主因。

### 5.2 `RowCount=0` 说明什么（不是"表不存在"）

`MultiAPStaCfg` 和 `MultiAPCacheMac` 都是**表在、行空**，和 `table not find` 是两件不同的事：

- `MultiAPStaCfg` 空 ⇒ 这台机器**没配过** STA 绑定。对 D6 是**正面证据**：入口存在，
  只要有人配一条绑定就会有行；我们不是在对着一张不存在的表做规划。
- `MultiAPCacheMac` 空 ⇒ 离线 STA 的帧缓存表是**运行时按需填充**的，出厂没有静态行。

所以 D6 / D7 的库侧入口从"候选"升级为"确认存在"，剩下的是**列语义**没实测（因为拿不到行）。

### 5.3 两张表的关系：一张是全局参数，一张是 per-MAC 实例

把 `MultiAPRoamCatonCfg`（1 行 7 列）与 `MultiAPCacheMac`（0 行）放在一起看，再加上
`cspd` 的内部命令 [CONTROLLER_CAPABILITIES](../CONTROLLER_CAPABILITIES.md) 里那组
`AddCacheMac` / `DelCacheMac` / `AddRoamFilter` / `DelRoamFilter`，结构就清楚了：

| 表 | 角色 | 行数 |
|---|---|---|
| `MultiAPRoamCatonCfg` | 漫游期间帧缓存的**全局参数**（是否缓存、延时多少毫秒、丢几个包） | 恒 1 行 |
| `MultiAPCacheMac` | **每个待缓存 STA 一行**的实例表，由 `AddCacheMac` 在漫游开始/结束时增删 | 出厂 0，运行时浮动 |

这解释了 `RowCount=0` 为什么不是"表坏了"：它本来就该是空的，只有在某个 STA 正在漫游的瞬间才会有行。

出厂这 7 个参数**全是关的**（`OfflineRetrieveEn=0`、`TxDataAfterBaEn=0`、`ZeroDropCfgEn=0`、
**`BtmCacheEn=0`**、`TxDropCnt=0`，只有两个延时 `TxBaDelayMs=10`、`TxConDelayMs=100` 非零）：
**厂商默认不做漫游缓存**，漫游期间丢帧就丢了。`BtmCacheEn=0` 具体含义应是"BTM 触发的漫游是否走
这套缓存"，与 §1.1 那个"出厂不走 BTM"是一致的——两条独立证据指向同一个出厂行为。

---

## 6. 第三轮：`MultiAPCfg` —— 角色开关的真身，50 列

`sendcmd 1 DB p MultiAPCfg`，`RowCount=1`，**50 列**。顺序即 schema 顺序。

### 6.1 分五组读

**角色与总开关**

| 列 | 值 | 备注 |
|---|---|---|
| `Mode` | **1** | 按 [SCHEMA](../milestones/SCHEMA.md) 语义：0 自适应 / **1 Slave** / 2 Master / 3 两者 ⇒ **这台设备当前是子节点角色** |
| `Enable` | 1 | MESH 总开关 |
| `SyncEnable` | 1 | |
| `SetRoleEnable` | **0** | 角色固定，不参与自适应 |
| `RoleElect` | **0** | |
| `WANForward` | 1 | `isSave=0`，运行时值 |
| `EnMasterOnly` | **0** | |

**跳数与拓扑**

| 列 | 值 | 备注 |
|---|---|---|
| `MaxHop` | 2 | |
| `CurHop` | **2** | **= `MaxHop`**，见 §6.3 |
| `RetryTimes` | 4 | |
| `TopoTimeOut` | 0 | |
| `TopoDiscoverInterval` | 60 | 秒 |
| `P1905DevNum` | 3 | `isSave=0`；网内 3 台 1905 设备 |
| `EnP1905DevSave` | 1 | |

**1905 报文与 Steering**

| 列 | 值 | 备注 |
|---|---|---|
| `En1905Msg` | **1** | **1905 报文开关是开的** |
| `EnSendM1EachBand` | 0 | |
| `EnReportCenterChannel` | **1** | **D3 的入口在这里，且出厂就开着** |
| `En11kReportMerg` | 0 | |
| `SteeringPolicy` | 1 | |
| `EnAgentSteer` | **1** | agent 侧接受 steering 请求 |
| `EnAgentBs` | 0 | |
| `NoSteeringOUIs` | `""` | 空 |
| `RoamWithBS` | 1 | |
| `RoamWebEnable` | 0 | |

**ACD 密钥族**

| 列 | 值 |
|---|---|
| `AcdEnable` | 1 |
| `AcdPubKeyReset` | 1 |
| `AcdPubKeyAuth` | **0** |
| `EnAcdDealAuth` | 1 |
| `AcdsDelayInterval` | 10 |
| `AcdcDelayInterval` | 6 |

**身份、容量与杂项**

| 列 | 值 | 备注 |
|---|---|---|
| `MeshName` | `""` | **空**。`/etc/hardcode` 是 `E2631_QD_MESH`，但 1905 的 Mesh 名没设 |
| `IFLink` | `br0` | 1905 走的接口是 `br0`（不是某个 vlan 子接口） |
| `ProductID` | 0 | |
| `VersionMode` | 0 | 功能位掩码，全 0 |
| `SyncBhBssEnable` | 0 | |
| `SSIDSyncRadio` | 0 | |
| `StaSyncInterval` | 0 | |
| `ExtraAPEn` / `ExtraAPPref` | 0 / 1 | |
| `OfflineDisplay` / `OfflineTimeOut` | 0 / 604800 | 7 天 |
| `OffStaCleanInterval` | 3600 | |
| `MaxCfgingBandNum` / `MaxCfgingWaitTime` | 5 / 10 | |
| `NotifyWlanStaNum` | 16 | |
| `LargeCapNetwork` | 0 | |
| `PluginLimit` | 0 | |
| `TestMode` | 0 | |
| `EnBsFilter` | 0 | |
| `11kRssiCorrect` | 20 | |

### 6.2 偏移验证：前 27 个字段严格符合 `36 + 32n`，六个锚点全中

[SCHEMA](../milestones/SCHEMA.md) 的还原是「偏移 = 36 + 32 × 字段序号」。拿实测列序去套：

| 字段 | 序号 | `36+32n` | SCHEMA 记的 | |
|---|---|---|---|---|
| `MaxHop` | 6 | 228 | 228 | ✅ |
| `CurHop` | 7 | 260 | 260 | ✅ |
| `VersionMode` | 10 | 356 | 356 | ✅ |
| `P1905DevNum` | 11 | 388 | 388 | ✅ |
| `EnMasterOnly` | 24 | 804 | 804 | ✅ |
| `MeshName` | 26 | 868 | 868 | ✅ |
| `IFLink` | 36 | 1188 | 1444 | ❌ |
| `NoSteeringOUIs` | 37 | 1220 | 1540 | ❌ |

**这是本仓库第一次「反汇编还原的 schema」与「真机 dump 的列序」逐字段对上**——
在 50 个字段里，实测列序与还原序完全一致，六个锚点无一偏差。可以顺带把 SCHEMA 那句话精确化：
原文说"前 25 个字段（序号 0–24）都是 32 字节"，实测证明是**前 27 个（序号 0–26）都是 32 字节**，
`PluginLimit`(25)=836、`MeshName`(26)=868 都在这个 32 字节区里。

`IFLink`(1444) 与 `NoSteeringOUIs`(1540) 不符合公式 ⇒ **从序号 27 起进入变长区**。
`1540 - 1444 = 96`，暗示 `IFLink` 占 96 字节（一条路径字符串槽）。这一段要算宽度得读
`dbDefMultiAPCfg` 的初始化，本轮不定。

### 6.3 `CurHop` 待办（B16）结案

实测 `CurHop=2`、`MaxHop=2`、`EnMasterOnly=0`。SCHEMA 记的是「`CurHop` 默认 -2；`EnMasterOnly`
为 1 时 `multi_ap_slave_main` 跳过 `slave_init`，所以 `CurHop` 不会被设成 `MaxHop`」。

实测正好是**反面情形**：`EnMasterOnly=0` ⇒ `slave_init` 会跑 ⇒ `CurHop` 被设成 `MaxHop`，
而实测两者都等于 2。**这条待办可以结了**，而且不是"读到一个值"式的结案——是
「SCHEMA 的条件句 + 实测的开关状态 + 实测的两个值」三者自洽。

同类自洽还有两条：

- `Mode=1`（Slave）+ `SetRoleEnable=0` + `RoleElect=0` ⇒ 角色固定为子节点，不参与选举；
- `EnAgentSteer=1` + `EnAgentBs=0` ⇒ 作为 agent 收 steering 请求，但不做 band steering 决策。
  这与 `MultiAPMaster` 里 `EnBandSteer=0`（§1.3）方向一致。

### 6.4 两处必须纠正的结论

**(1) D3 的入口找到了，而且出厂是开的。**

`EnReportCenterChannel=1` 在 **`MultiAPCfg`**，不在 `P1905ComCfg`（那张表只有 8 列，§4.1）。
它的值是 1，意思是**设备本来就在上报中心信道**——D3 要做的不是"打开"，而是"接收侧怎么用"。

**(2) 1905 报文取证路径没有落空。**

上一轮我写「`En1905Msg` 不在 `P1905ComCfg`，这条取证路径暂时落空」。实测 `En1905Msg=1`
在 `MultiAPCfg`，**开关是开的**。所以取证路径成立：`multiapd` 的 `pktdump` 之外，
配置层这一路也是通的，不需要先改配置。

---

## 7. 本轮的第三处自纠：三个"列名"其实是表名

`DB all` 给出的 276 张表名里，有三个名字此前被我当成 `P1905ComCfg` 的**列**：

| 名字 | 我原来的写法 | 实际 |
|---|---|---|
| `MultiAPPTK` | `P1905ComCfg.MultiAPPTK` | **独立表**（#147），1905 的 PTK 缓存 |
| `P1905StaBind` | `P1905ComCfg.P1905StaBind` | **独立表**（#168），**D6 STA 绑定的真正落点** |
| `P1905Cfg` | 不存在（我根本没注意） | **独立表**（#164），与 `P1905ComCfg` 并列 |

同一错因第三次出现：字符串表里**表名与列名交错排列、表名夹在列名序列中间当分隔**。
这三次错误的共同结局是"把表名当列名"或"把后面整段列名并进前一张表"。
`DB all` 之后这条路彻底堵上——**凡不在 276 张清单里的名字，一律不是表**。

具体影响：

- **D6**：上一轮说落点是 `MultiAPStaCfg`（0 行）。现在看 `P1905StaBind` 更像正主——
  名字直接对应 `CmMultiApSetStaBind` 那个函数。两张表都要 dump 才能分清职责。
- **D5**：1905 接口名的候选从"不存在的 `IGD.P1905CFGn`"变成 **`P1905Cfg`**（存在，未 dump）。

另外一条负结果：`MESH` / `Netsphere` / `EASYMESH` 这三个名字**不在 276 张表里**。
[E2631_ROOTFS](E2631_ROOTFS.md) §6.4 把它们写成"DB 视图别名"是没有依据的，
它们更可能是 `httpd`/web 层的名称映射。已在那一节降级。

---

## 8. 还没 dump 的表（按价值排序）

| 顺序 | 表 | 为什么要它 | D 段哪一项 |
|---|---|---|---|
| 1 | `P1905StaBind` | STA 绑定的真身（§7） | D6 |
| 2 | `P1905Cfg` | 1905 接口名 / `EnableP1905`；**同时看 `ViewName` 是否形如 `IGD.P1905CFG1`**（§4.1） | D5 |
| 3 | `WlanRoamingCfg` | **11k/11v/11r 的 WLAN 侧开关**；若 11r 在这里，D7 的判据要改写 | D7 |
| 4 | `MultiapCapability` | 与 `MultiAPDomain.SupportStrategies=251` 位图对照 | D7 |
| 5 | `MapCooperatorCfg` | 角色选举触发分支（`AutoSetup`/`EnRebootElect`） | 角色 |

全部只读、不用停机。表名清单见 [E2631_DB_TABLES](E2631_DB_TABLES.md)。

`MultiAPCacheMac` / `MultiAPStaCfg` 已 dump 完但行空（§5），要看到行得先在网页上配一条绑定，
属于**改配置**，不再是只读取证。
