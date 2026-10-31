# E2631 配置库实测快照：三张 mesh 表的真身

2026-11-01，真机（市售 AX3000，自报 E2631，2026-08 固件）上跑三条**只读**命令得到的原文整理：

```sh
sendcmd 1 DB p MultiAPMaster     # RowCount=1，55 列
sendcmd 1 DB p P1905ComCfg       # RowCount=1，8 列
sendcmd 1 DB p MultiAPDomain     # RowCount=8，每行 106 列
```

这份文件的用途只有一个：**它是 [E2631_ROOTFS](E2631_ROOTFS.md) §6 那些离线推断的对照答案**。
§6 是从 `cspd` 字符串表按排列顺序切出来的（当时标"中—强"），这里给出实测结果 —
有两处推断**错了**，错误与成因记在 §4。

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

错因：`cspd` 的 schema 字符串表里列名是**连续排放**的，表名夹在中间当分隔。`P1905ComCfg` 只有 8 个
名字，它后面的表名（`MultiAPMaster`、`MultiAPDomain%d` …）被我漏切，于是把后面整段列名并进了这一张表。

已经能确认归属的：`TopoSortTimer`、`EnBSFilter` 实测在 **`MultiAPMaster`**（§1），
`MarketSign`、`MarketConfEn` 确实在 `P1905ComCfg`（§2）。

还没定位的（`SetRoleEnable` / `RoleElect` / `EnMasterOnly` / `En1905Msg` / `MaxHop` / `CurHop` /
`ACD.*` / `STAMac` / `TargetBand`）：**不在本轮这三张表里**。下一个候选是 `IGD.P1905CFGn`
（D5 已经在用的实例表，见 [FEATURE_STATUS](../FEATURE_STATUS.md) D 段），验证命令：

```sh
sendcmd 1 DB p P1905CFGn      # 若报无此表，再试 sendcmd 1 DB l 2020 0 0 找表名
```

这条没跑之前，别再引用"`P1905ComCfg.SetRoleEnable`"这种写法。

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

## 5. 还没 dump 的表

| 表 | 为什么要它 | D 段哪一项 |
|---|---|---|
| `IGD.P1905CFGn` | 角色开关、1905 接口名（§4.1 那些没定位的列很可能在这里） | D5 / D6 |
| `MultiAPStaCfg` | `devMac/bssMac/staMac` 三地址绑定实例 | D6 |
| `MultiAPCacheMac` | `tBtmCacheEn` 等七个帧缓存参数 | D7 |
| `MultiAPRoamCatonCfg` | `RoamFilter` | D7 |

四条都是只读、不用停机。连同本文三张表，`sendcmd 1 DB p` 一类型的取证就齐了。
