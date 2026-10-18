# 原厂 Mesh 功能映射：界面控件 → 数据库 → 线上报文 → 日志

方法：在原厂 SR1010 的 Web 界面逐项改设置，`sendcmd 1 DB p` 每 7 秒整库快照差分，eth4 抓 1905 报文（Z506 子节点接在 eth4），
`multiapd` 日志级别 8。每项记录：库里哪些表.字段变了、是否落盘（isSave）、线上发了什么、子节点读到了什么、有没有 cspd 事件。
证据文件在同目录；时间为原厂 UTC 时钟，原厂日志时钟 = 原厂 UTC − 11:02:46。

约定：RCPI = 2 × (RSSI dBm + 110)，用于线上的 RSSI 阈值编码。

> **更正（实验确认，2026-10-16 13:45）：第 1 到 3 节"纯写库驱动、无事件"的说法是错的。**
> 用原厂命令行 `sendcmd 1 DB set MultiAPDomain 0 RoamRssiLmt24G -60` 直接写库，**线上没有任何 `PolicyConfigReq`**；
> 同一个值通过网页（webd → libcmapi）提交，立即发出 `PolicyConfigReq`（`4a` 首字节 `0x64`）。
> 所以触发推送的是 **CM 接口发给 `multiapd` 的通知**，不是数据库本身；光改库 `multiapd` 不会重读。
> 这与前期文档"CM 接口 → 数据库 → 事件（`0x10205`/`0x10206`）"的描述一致。shim 的配置桥必须在写库之后补发等价的 CM 通知。
> 下文第 1 到 3 节里的"纯写库"、"无 cspd 事件"请按此理解为"网页路径里没有看到 cspd 的 `a9xx` 异步事件，但有 CM 通知"。
> **（2026-10-17 补）这条通知已经查清并在 shim 里实现**：CM 设置函数写完库后发同步 `SSEND(0x2401, {u32 消息号, char name[32], u32 0}, "multiapd.map.master")`，
> 漫游阈值是消息号 `0x10206`、主控策略是 `0x10205`（第 3 节的两个 `PolicyConfigReq` 就是先 `0x10206` 后 `0x10205`）。见 REPLAY_RESULTS 第 11 节。

## 1. MESH 高级配置 → 2.4GHz 漫游阈值（-60 → -65）

- **界面：** 网络 → MESH 高级配置，2.4GHz MESH 漫游阈值，范围 -110 ~ -40，默认 -60。后端 Lua：`internet_mesh_threshold_lua`，对象 `OBJ_MULTIAP_ROAM_ID`（`RoamRssiLmt24G`、`RoamRssiLmt5G`、`EnLegacyStaRoam`）。
- **库：** `MultiAPDomain[0..7].RoamRssiLmt24G: -60 → -65`，**8 行全改**（不是只改第 0 行）。持久字段。同一次提交日志里还有 `web set OBJ_MAP_MASTER_ID`、`OBJ_MULTIAP_MASTERCFG_ID`（`iRet:-9`，值没变），随后 `dbAPISave`（落盘）。
- **线上：** 一个 `PolicyConfigReq`（0x8003）主控 → 子节点，内容 `MetricRptPolicy`（0x8a）+ 厂商 TLV（OUI 00:19:c6，头 `02 00 07`）：
  - 子 TLV `4a`（12 字节）= `5a 50 50 50 50 50 0a 00 00 00 01 2c`，布局（第 2 节与本机 slave 解码日志交叉确认）：
    `RoamRssiLmt24G/5G/6G`（3 字节，RCPI）、`RoamLoadLmt24G/5G/6G`（3 字节，`50`=80）、`AssocStaNumStart`（1 字节，`0a`=10）、`LoadRefuseTimes`（1 字节，0）、`LoadRefuseAgeTime`（4 字节大端，`0000012c`=300）。
  - 子 TLV `4d[1]=01`、`60[2]=0101`。
  - 入网时的同类消息还带 `4c[2]=0001`、`5a[1]=00`，改阈值的增量消息里没有。
- **本机 slave 解码（更正）：** 日志 `[slave] AGENT RECV srcdev 7c:7d:21:b9:c1:3a msgtype[8003]` 与 `_SlaveRoamThres iStaRssiRptThr -65 uStaLoadRptThr 80 uAssocStaNumLmt 10 uLoadRefuseTimes 0 uLoadRefuseAgeTime 300 index 0/1`
  是 **SR1010 自己的 slave 模块**（`uMode=3` 主从合一，收自己主控的包）打印的，不是 Z506。它证明原厂 agent 侧的解析路径和字段名，但**没有证明 Z506 读到了**；
  前期文档已确认 Z506 固件不解析这个厂商 TLV。Z506 的日志本次没抓。
- **事件：** 日志里没有 cspd 的 `a9xx` 异步事件；变更由网页路径的 CM 通知送到 `multiapd`，它随后发 0x8003（**直接写库不会触发**，见文首更正），另见 `master` 对 8 个 Domain 重新做 `msater_deal_bs_` 检查。
- **移植要点：** 写这 8 行之后**还要补发 CM 通知**（直接写库不会推送，见文首更正）。验证项：同样的库喂 shim，比较 0x8003 的厂商 TLV 是否逐字节一致。

## 2. MESH 高级配置 → 5GHz 漫游阈值（-70 → -75）

- **库：** `MultiAPDomain[0..7].RoamRssiLmt5G: -70 → -75`，8 行全改，只此一个字段变。
- **线上：** `PolicyConfigReq` 一个，厂商子 TLV `4a` = `5a 46 50 50 50 50 0a 00 00 00 01 2c`：**第 2 字节** `50 → 46`（70 = RCPI(-75)），其余不变。证实 `4a` 前三字节依次是 2.4G / 5G / 6G 阈值。
- **日志：** 本次日志采集已满（Debug 级别 4 MB 上限在 12:40 用完），缺；已改为原厂侧过滤噪声 + 25 MB 上限（`/DPITMP/mw/logf.sh`）。

## 3. MESH 高级配置 → 弱信号强制切换（关 → 开）

- **库：** 一个开关改三个字段：`MultiAPDomain[0..7].EnLegacyStaRoam 0→1`（8 行全改）、`MultiAPMaster[0].EnBtmKickoff 0→1`、`MultiAPMaster[0].EnRoamKick 0→1`。都是持久字段，随后 `dbAPISave`。
  Web 后端：`OBJ_MULTIAP_ROAM_ID.EnLegacyStaRoam`、`OBJ_MULTIAP_MASTERCFG_ID.EnBtmKickoff/EnRoamKick`；日志里只见 `web set OBJ_MAP_MASTER_ID`。
- **线上：** 两个 `PolicyConfigReq`（0x8003）：
  1. 83 字节：`MetricRptPolicy` + 厂商 `4a[12]`（阈值完整重发，同第 1、2 节）`4d[1]=01` `60[2]=0101`。
  2. 52 字节，纯厂商 TLV：`4c[2]=0001` `5a[1]=01` `4d[1]=01` `60[2]=0101`。入网时 `5a[1]=00`，所以 `5a` 是本开关对应字段（推测 BTM 踢人使能 = `EnBtmKickoff`）。
  推测（待验证）：`4d`=兼容版本，`60`=`P1905ComCfg` 同步（Enable、MarketConfEn），`4c[2]`=802.11r 相关两位。
- **要点：** `EnLegacyStaRoam` 与 `EnRoamKick` 变了但线上没有对应字段，疑为主控本地决策参数，不下发；下发面比库字段少。
- **事件：** 网页路径无 cspd `a9xx` 事件，但有 CM 通知（直接写库不触发，见文首更正）；本机 slave 解析路径正常（非 Z506）。

## 4. Wi-Fi 设置 → Wi-Fi 双频合一（开 → 关）

这一项不再是"只写库"：它触发 cspd 侧的 WLAN 变更处理，并自动对子节点发起一次完整的 **Renew 重新配置**。

- **库：** `WLANSetting[0].DualBandSync 1→0`（持久）、`MultiAPMaster[0].EnBandSteer 1→0`（持久）、`MultiAPDomain[4].ESSID 'cc'→'cc-5G'`（运行时，5G 域名称不再与 2.4G 同名）。
  另有瞬时的 `P1905DevNum 2→1→2`、`MultiAPDomain[0].BsActive/DomainUsed`、`[3].DomainUsed`（Renew 期间子节点短暂掉出拓扑又回来）。
- **日志里的事件链（原厂 UTC 12:49:34 起）：**
  1. `webd` 依次 set：`OBJ_MLO_ID(IGD)`、`OBJ_WLANAP_ID(DEV.WIFI.AP1)`、`OBJ_WLANPSK_ID(DEV.WIFI.AP1.PSK1)`、`OBJ_WLANSETTING_ID(DEV.WIFI.RD1 / RD2)`，随后 `dbAPISave`。
  2. cspd 侧 `multiapd_mgr`：`MapHandleWlanRenew bFreqBand:1, RenewID:1` → 日志标 `Map Renew delay 10 s`，但实测约 2 秒后就 `WlanRenewToCtr`，再约 3 秒 `acd` 发出 Renew（`RENEW_MSG_FIRST delay 3s`），所谓 10 秒的含义待查，不要当作去抖时间。（这是 WLAN 变化到 ACD Renew 的桥，shim 里没有，需用 `zte-meshctl renew-acd` 之类自己触发。）
  3. `cooperator_mgr` 向拓扑设备发 `CfIeee1905SendMsg 37025(0x90a1)`（上报灯状态），`acd` 起 `RENEW_MSG_FIRST delay 3s` 定时器。
  4. `acd` 对 0 号频段（2.4G）、1 号频段（5G）各发一个 `AP-autoconfiguration renew`；子节点回 WSC M1；原厂回 M2（日志 `Change Auth Type to [20]`、`MultiAPVendorExtAttr.bVendorID is 00372a`）。
  5. `wlan_mgr`：`Receive topo change notify … Direct AP DEV ON, need to set AP rule`、`wlan_handle_set=DEV.WIFI.AP5`。
- **线上（顺序）：** `TopoNotification`（多播，厂商 TLV `0x40` 设备信息 195 字节，含序列号、设备名、版本） → `APAutoconfRenew` ×2（每频段一个，厂商 TLV `0x40` 166 字节） →
  子节点 `APAutoconfWSC` M1 ×2 → 主控重走一遍入网时的查询序列：`TopoQuery/HigherLayerQuery`、`VendorSpecific`、`PolicyConfigReq`（`4d 4f 60`，随后完整的 `4a 4c 5a 4d 60`）、`APCapQuery/ChanPrefQuery/ClientCapQuery`、`CombinedInfraMetrics`，之后再来一次 Renew。
- **新消息类型：** `VendorSpecific`，厂商头 `02 00 fe` = **跨设备模块消息**：负载 `00 24` + 36 字节目标模块名（`cspd.wlan_manage.wlan_mgr`，零填充）+ 消息号/长度/值（`… 00 00 00 04 00 00 00 01`，4 字节值 = 1），即主控的 wlan_mgr 通知子节点的 wlan_mgr。值的含义见第 7 节：= `WLANSetting.MLOEnable`。
- **移植要点：** shim 只写库不够，必须补上 "WLAN 设置变化 → 去抖 → 触发 Renew（按频段）"，以及 `0200fe` 这类跨设备模块消息（若要和原厂子节点完全一致）。

### 4.1 补充：5G 名称是自动派生的

界面上没有单独设置 5G SSID 的地方。关掉双频合一后，5G 名称 = 2.4G 名称 + `WLANSetting.ESSIDSuffix`（当前 `-5G`），所以 `MultiAPDomain[4].ESSID` 从 `cc` 变成 `cc-5G`。
同表还有 `SSIDSync=1`、`MLOEnable=1`、`MLOSSID=1`。shim 若要复现，名称派生规则要和原厂一致（后缀固定在 `WLANSetting.ESSIDSuffix`）。

### 方法补充：被 `DB p` 打码的字段

`sendcmd 1 DB p` 把 `WLANCfg.ESSID`、`WLANPSK.KeyPassphrase`、`WLANWEP`、`STAProfile` 等字段显示为 `***`，改名字、改密码在差分里看不到。
`sendcmd 1 DB pf <表>` 写 `/var/tmp/dbtable`，**是未打码的真实值**。监视脚本因此对 `WLANCfg/WLANPSK/WLANWEP/WLANWPS/STAProfile` 每行只记一个 md5 前缀（表名 `SECRET_<表>`），
只作"变了没有"的指示，值本身不存盘、不显示。名称变化另可从未打码的 `MultiAPDomain.ESSID` 看到。**不要把 `pf` 的输出贴进文档或对话。**

## 5. Wi-Fi 设置 → Wi-Fi 名称（`cc` → `cc2`）

- **库：** 可见字段无变化。`SECRET_WLANCfg[0]`（AP1，2.4G 主 SSID）与 `[4]`（AP5，5G 主 SSID）的 md5 变化，即两个频段名称同时被改（5G = 新名称 + `-5G` 后缀）。`MultiAPDomain.ESSID` 本次未更新（运行时字段，不是权威来源）。
- **事件链：** 同第 4 节：`webd` set `OBJ_MLO_ID(IGD)`、`OBJ_WLANPSK_ID(AP1.PSK1)`、`OBJ_WLANSETTING_ID(RD1/RD2)` → `dbAPISave` → `multiapd_mgr MapHandleWlanRenew`（2.4G 与 5G 各一次，`RenewID` 递增）→ ACD Renew。**所有 WLAN 类设置都走这一条触发路径。**
- **线上（约 3 秒）：** `APAutoconfRenew` ×2（每频段） → 子节点 `APAutoconfWSC` M1 ×2 → 主控 M2 ×2，**每个 M2 三个分片**（1324 + 1383 + 209 字节，与入网时一致，最后一片是厂商 TLV） →
  子节点主动发起 **12 对厂商消息查询**，主控逐条回答：
  - `hdr=010100`（频段配置）：`07[4]=00000064`（BeaconInterval=100）、`19[4]=00000002`（MaxHop=2）、`55[1]=55`；每频段一对。
  - `hdr=010101`（每个 SSID 槽位，index 0 到 3，请求 `0b[1]=index 1d[1]=04`）：回复 `0b=index 0c 46[4] 11[4] 0f 1e 0d`。观察到：index 0 `0c=1 11=1 0f=0 1e=0 0d=0`；index 1 `0c=0 11=1 0f=1 1e=1 0d=0`；index 2 `0c=0 11=1 0f=0 1e=0 0d=0`；index 3 `0c=1 11=0 0f=0 1e=0 0d=1`。
    `0c` 与 `WLANCfg.Enable`（1,0,0,1）一致，即 SSID 使能。其余字段的含义待用"隐藏 SSID"、"访客开关"等逐项确认。
  - `hdr=010102`（ACL 列表，请求 `1b[4]=00000040 1c[4]=00000002 1d[1]=04`）：库里 `AclCfg` 为 0 行，主控回空。
  - 另有每频段的 `0100` 对应 band 0、`0101` 对应 band 1（头的前两字节是频段）。
- **日志：** `acdsGetBandConf`（频段使能、TxPower、BeaconInterval）与 `acdsGetSSIDConf`（SSID 名、使能、隐藏标志）逐条打印读到的值，说明这些厂商消息的内容来自 `multiapd` 读 WLAN 表，不需要 cspd 参与，shim 的 `CmWLANGetAPConfig` 替身给对数据即可。
- 子节点是否真的采用了新名称，本次没有读回确认（Z506 的库未读）。

## 6. Wi-Fi 设置 → 隐藏 SSID（我的 Wi-Fi，关 → 开）

- **库：** `WLANCfg[0].ESSIDHideEnable 0→1` 与 `WLANCfg[4].ESSIDHideEnable 0→1`（2.4G 与 5G 主 SSID 一起，明文字段，持久）；`SECRET_WLANCfg[0]/[4]` 哈希随之变化。
- **事件链与线上：** 同第 4、5 节（`webd` → `multiapd_mgr` → ACD Renew → M1×2 → M2×2（各 3 分片）→ 12 对厂商查询，约 3 秒）。`acdsGetSSIDConf … ESSIDHideEnable[1]`。
- **`hdr=010101` SSID 槽位配置字段对应**（请求 `0b[1]=槽位 1d[1]=04`，回复 `0b 0c 46 11 0f 1e 0d`）：

  | 字段 | 对应库字段 | 依据 |
  |---|---|---|
  | `0b[1]` | 槽位序号（0 到 3，对应 AP1 到 AP4） | |
  | `0c[1]` | `WLANCfg.Enable` | 模式 (1,0,0,1) |
  | `0d[1]` | `WLANCfg.ESSIDHideEnable` | **本次实验确认**：index 0 的 `0d` 0→1 |
  | `11[4]` | `WLANCfg.ACLPolicy` | 模式 (1,1,1,0)，4 字节 |
  | `0f[1]`、`1e[1]` | `WLANCfg.VapIsolationEnable` 与 `WLANCfg.GuestSsid` 二者之一 | 两个库字段模式同为 (0,1,0,0)，待用访客或 AP 隔离实验区分 |
  | `46[4]` | 未知 | 4 槽位均为 0 |

- 附：第 4 槽（AP4）`MapAccessAttr=64`，其余 32，`BeaconType` 为 12,12,6,6；该槽位疑为回程 BSS（隐藏标志、ACL 策略与前三槽不同）。ACL 查询 `hdr=010102` 的 `1b[4]=0x40`、`1c[4]=2` 可能与此相关，待确认。

## 7. 我的 Wi-Fi → Wi-Fi 频段聚合（MLO，开 → 关）

- **库：** `WLANSetting[0].MLOEnable 1→0`（持久）；`SECRET_WLANCfg[4]`（5G 主 SSID）哈希变化，2.4G 行不变。用户观察：关闭 MLO 后 2.4G 与 5G 才能分别设置（对应 `WLANSetting.MLOSSID=1` 时两频段 SSID/密码联动）。
- **事件链：** `webd` set `OBJ_WLANAP_ID(AP1)`、`OBJ_WLANPSK_ID(AP1.PSK1)`、**`OBJ_WLANPSK_ID(AP5.PSK1)`**（联动时只写 AP1，独立后 AP5 的 PSK 对象也被写）、`OBJ_WLANSETTING_ID(RD1/RD2)` → `dbAPISave` → `multiapd_mgr` 对 2.4G 与 5G 各一次 Renew → ACD Renew。
- **线上：** Renew ×2、M1 ×2、M2 ×2、随后厂商查询，与第 4 到 6 节相同；**M2 变短**：第 2 分片 1383 → **1353** 字节（少 30 字节，推测为 MLO/MLD 相关属性），两频段 M2 长度也不再相同（1324/1353 与 1340/1353）。
- **`0200fe` 跨设备模块消息的值 = `WLANSetting.MLOEnable`：** 目标 `cspd.wlan_manage.wlan_mgr`，4 字节值，第 4 节（MLOEnable=1 时）为 1，本次（MLOEnable=0）为 0；第 4 节中 `DualBandSync` 1→0 而本次不变，故不是 `DualBandSync`。置信度高（两个数据点）。含义：主控 wlan_mgr 通知子节点 wlan_mgr 当前 MLO 开关。

## 8. 我的 Wi-Fi → 只改 5G 名称（MLO 关闭后可独立设置）

- **库：** 只有 `SECRET_WLANCfg[4]`（5G）哈希变化，2.4G 行不变。
- **Renew 按频段精确触发：** 日志只有一次 `MapHandleWlanRenew bFreqBand:1`（5G），只发 1 个 `APAutoconfRenew`、1 个 M1、1 个 M2（1340 + 1353 + 209），厂商查询只有 5G 的 6 对（频段配置 1 对、SSID 槽位 4 对、ACL 1 对），没有 2.4G 的 6 对；`0200fe` 不出现（MLO 未变）。
- **触发判据：** `webd` 每次都写 `OBJ_WLANAP_ID(AP1)`、`OBJ_WLANPSK_ID(AP1.PSK1)`、`OBJ_WLANSETTING_ID(RD1/RD2)`，即使只改了 AP5；说明触发频段不取决于被写的对象名，而是 cspd 侧（`multiapd_mgr`）比较出的实际变化频段（推断，日志未直接给出判据）。
- **移植要点：** shim 必须按频段判断变化后各自触发 Renew，不能"收到 WLAN 写库就两个频段都 Renew"，否则线上流量与原厂不同。
- M2 长度对名称长度不敏感（WSC 加密设置按 16 字节分组）；不要用 M2 长度判断名称是否变化。

## 9. MESH 配置 → MESH 兼容（关 → 开）

- **界面与后端：** 前端提交 `{IF_ACTION=Apply, _InstID=IGD, Mode, VersionMode, Enable, PEnable, old_Enable, CurrentMode, CurrentEnable}`，**"MESH 兼容"开 = `PEnable=0`**（`PEnableReverse`），
  即 `P1905ComCfg.Enable=0`（关闭 ZTE 自组网 V2 的准入检查，接受任何对端）。`Mode` 取值是字符串 `Master`/`Slave`/自适应；`Mode=Slave` 且已启用时页面等 10 秒再回读（切副路由会改上网方式，不要试）。
- **库：** `P1905ComCfg[0].Enable 1→0`（持久）；随后运行时的 `P1905DevNum 2→1→2`、`MultiAPDomain.DomainUsed` 抖动、`MultiAPDomain[0]/[4].ESSID` 刷新为当前名称（说明 Domain.ESSID 是拓扑刷新时才更新的镜像）。
- **事件链：** `webd` 只 set `OBJ_NETSPHERE_MAP_ID` 并 `dbAPISave`；约 2 秒后 `cooperator_mgr: Recv topo up! Report light status`（`CfIeee1905SendMsg 37025`），`acd: RENEW_MSG_FIRST delay 3s` → **对两个频段都发 Renew** → M1 ×2 → M2 ×2 → 厂商查询。
  即 mesh 级配置提交会触发整网重新配置，且没有经过 `multiapd_mgr` 的按频段路径（那是 WLAN 类对象的路径）。
- **线上：**
  1. 先多播 `TopoNotification`（厂商 TLV `0x40` 设备信息），子节点 `TopoQuery/HigherLayerQuery`。
  2. 约 40 秒内主控的 `TopoResponse` **不含 `1905Nbr` TLV**（只有 `DevInfo Non1905Nbr Non1905Nbr …`），子节点每 3 秒重复 `TopoQuery`；`P1905DevNum` 暂为 1。这是瞬态：邻居关系在配置变化后被重建。
  3. 13:13:38 子节点重新作为 1905 邻居出现，主控重走完整入网序列（`VendorSpecific`、`PolicyConfigReq`、`APCapQuery/ChanPrefQuery`…），`P1905DevNum` 回到 2。
  4. 中间穿插 Renew ×2 与 12 对厂商查询（与第 5 节相同）。
- **`PolicyConfigReq` 厂商子 TLV `60[2]` = [`P1905ComCfg.Enable`, `P1905ComCfg.MarketConfEn`]：** 由 `0101` 变成 **`0001`**，实测确认（前期文档推测）。同步给子节点依赖 `P1905ComCfg.EnableIsSync=1`。
- **移植要点：** shim 的 `mesh.v2=0|1` 对应 `P1905ComCfg.Enable`，改它会让子节点短暂掉出 1905 拓扑约 40 秒再重新入网；mesh 级配置提交的 Renew 路径是"两个频段都发"，与 WLAN 类的按频段 Renew 不同。

> **更正（2026-10-17，逆向 + 日志核对）：这一节的"mesh 级 Renew"不是一条 Renew 路径，而是 `multiapd` 进程被重启。**
> - `libieee1905cmapi.so` 的 `CmMultiApTopoSetP1905ComCfg`（0x595c）写完 `P1905ComCfg` 视图后**无条件** `ASEND(0xa93d, 空负载, "cspd.cspd.multiapd_mgr")`
>   （0x5b1c；没有新旧值比较）。cspd 的 `multiapd_mgr` 模块（`MultiapMgrModuleInit` 0x18e134，事件表在 `.data` 0x2b6e34）把 `0xa93d` 派给
>   `MultiapMgrRestart`（0x18eb40）→ `MultiapRestart`（0x18ea08）：`PcStopProgram` 停掉 `multiapd`，进程退出后 `MultiapStart` 再起一个。
> - 厂商日志逐条对得上（`session/vendor-log-filtered-final.txt`，日志时钟 = UTC − 11:02:46）：第 2252 行 `02:10:09 multiapd 24955 will restart...`、
>   第 2257 行 `MultiapStart 9843 start success, start count = [3]`、第 2273 行 `cooperator_mgr Recv topo up`、第 2437 行 `acd RENEW_MSG_FIRST delay 3s`；
>   进程退出行 `exit_code = 9`（SIGKILL）。同一文件里共 6 次这样的重启（start count 2 到 7）。
> - 所以：**两个频段都 Renew** = 新守护进程的 ACD 在第一次物理链路 up 时发的第一轮 Renew（`RENEW_MSG_FIRST` 字符串在 `multiapd` 的 `_handlePhyLinkUp`，
>   0x6e7cc），不是 cspd 另外发的；**6 到 40 秒拓扑抖动** = 守护进程内的拓扑/邻居表丢失后重建（子节点每 3 秒 `TopoQuery`，控制器的 `TopoResponse` 没有 `1905Nbr`，
>   直到子节点自己的邻居老化后重新 `TopoDiscovery`）。上面"事件链"里 `cooperator_mgr: Recv topo up` 也只是新进程起来后的广播。
> - 数据库和订阅池在 cspd 里，不随守护进程重启丢失（日志里 `SubScribPublish srv 10022 … already exist!` 就是新进程重新注册时撞上旧条目）。

## 10. MESH 配置 → 设备角色（主路由 → 自适应）

- **库：** `MultiAPCfg[0].SetRoleEnable 0→1`（持久）；`Mode` 不变（2）；`CurHop` 瞬时 `2→-2→2`（持久字段，选举期间被重置）。
- **日志（角色选举原文）：** `acdGetRole: SetRoleEnable TRUE` → `cooperator_mgr MapCooperatorAC: Route mode. Set role to master` → `roleplay.c _autoSetRole: Role change to 2(1:SLAVE 2:MASTER)` → `cooperator_mgr dbAPISave`。
  结论：**`MultiAPCfg.Mode`：1=Slave，2=Master**；**"自适应"=`SetRoleEnable=1`，不改 `Mode`**，启用后由 `cooperator_mgr` 按路由/桥接状态自动设角色（路由模式→Master）。
  `sendcmd multiapd.map.master dev` 里 Dev[0] 的 `uMode:3` 是运行时"主从合一"，不是 `MultiAPCfg.Mode`。
- **线上：** 与第 9 节同类：多播 `TopoNotification` → 约 6 秒内 `TopoResponse` 无 `1905Nbr` → 两个频段都 Renew（M2 ×2 各 3 分片）→ 12 对厂商查询 → 子节点重新作为 1905 邻居出现，主控重走 `PolicyConfigReq`（`60[2]=0001`）、`0200fe`（值 0）、`APCapQuery/ChanPrefQuery`、`CombinedInfraMetrics`。
- **日志标注疑点：** 本次日志写 `web set OBJ_P1905_COM_CFG_ID`，第 9 节写 `OBJ_NETSPHERE_MAP_ID`，与 Lua 源码里字段归属相反（改 `P1905ComCfg.Enable` 的是第 9 节），疑为日志标注错位；不要据日志里的 `web set` 对象名判断落了哪张表，以库差分为准。
- **重启链（2026-10-17 补，同一份日志第 2577 到 2789 行）：** 02:12:40 `MULTIAP Notify event[2] to 0 clients`、`event[8] to 1 clients`（`MultiapMgrEnableModeChange`，事件 `0xa93c`，
  由 `CmMultiAPSetMultiAPCfg` 发出）→ 同一秒 `MultiapRestart multiapd 9843 will restart...`（start count 4）；02:12:42 新进程里 ACD 读到 `SetRoleEnable TRUE`，
  `cooperator_mgr` 的 `MapCooperatorAC` 判定路由模式、角色设为 Master，`_autoSetRole: Role change to 2` 后 `cooperator_mgr` 自己 `dbAPISave`，
  02:12:43 **第二次** `MultiapRestart`（start count 5，仍是 `0xa93c`/`0xa93d` 一类入口，具体发送者没有追到）。即一次"设备角色"提交 = 约 3 秒内两次守护进程重启。
  `CmMultiAPSetMultiAPCfg`（`libmultiapcmapi.so`）逐字段比较新旧值生成变化位（`Mode` 用 `strheq/strhne`，相同写 0、不同写 1）；
  cspd 的 `MultiapMgrEnableModeChange`（0x18eb9c）只在 `Mode`（+0x0a）、`Enable`（+0x2a）、`VersionMode`（+0x14a）或偏移 +0x46a 处的另一个字段变化时走到 `MultiapRestart`；
  `SetRoleEnable` 的变化位（+0x6a）本身只发 `event[2]` 通知。字段对应按 `multiap_cfg.def` 的顺序（每个字段 0x20 字节，变化位在 +0x0a）算出，
  +0x46a 之后含字符串字段、偏移不再等距，所以那一个字段没有确认是哪个。
  （推断）这与 10.1 一致：1→0 时 `Mode` 没有变化，只有 `event[2]` 通知而没有重启，所以线上没有动作；0→1 时网页同时提交了 `Mode`，变化位触发重启。
- **移植要点：** 角色是判断逻辑而非静态值：`SetRoleEnable=1` 时路由模式→Master、桥接模式→Slave；shim 固定 `Mode=2`+`EnMasterOnly=1` 合理，但不覆盖自适应。

**角色语义与选举流程（2026-10-17，multiapd / cspd 反汇编，P1 第 7、8 项）：**
- `MultiAPCfg.Mode`：**0 = 自动（选举中）**、1 = Slave、2 = Master、3 = 主从合一（cspd 日志 `Receive role change notify[0:AUTO; 1:SLAVE; 2:MASTER; 3:BOTH]`；`acdGetRole` 断言 `Mode <= 3`）。
  "自适应" 的网页请求 = `Mode 0` + `SetRoleEnable 1`（推断依据：第一次重启的 `event[8]`（Mode 变化位）和新守护进程日志里的 `acdGetRole: SetRoleEnable TRUE`——这行只有 `SetRoleEnable != 0 && Mode == 0` 才会打印）。7 秒一次的库快照没抓到 `Mode 0`（只持续约 3 秒）。
- `acdGetRole`（multiapd 0x7bb78）：`SetRoleEnable != 0 && Mode == 0` → ACD 角色直接取 Master；否则按 `Mode`（≤ 3）查表，再按 `CfMapGetPluginMeshMode` 通知角色变化。
- 选举结果由 cspd 的 `cooperator_mgr`（`MapCooperatorAC`：`Route mode. Set role to master.`，或 `Listen 1905 timeout. Set role to master.`）经 ACD 的 `_autoDoAutoRole`（`ACD auto do set controller`）送到 `_autoSetRole(role, reason)`（0x886c4）：
  `role == Mode && SetRoleEnable` 则什么也不做；否则（`IsAirtiesEnable()` 为假时）用 `dbAPISetView` 写 `MultiAPCfg` 的 `Mode = role` 和 `SetRoleEnable = 1`（变化位 +0x0a、+0x6a），`AcdNotifyRoleChange`，日志 `Role change to %d(1:SLAVE 2:MASTER)`，
  再 `ASEND(0xa93d, …, "cspd.cspd.multiapd_mgr")`——这就是第二次守护进程重启（第 9 节的更正已讲 0xa93d）。路由模式下结果是 Master，所以终态 `Mode 2` + `SetRoleEnable 1`。
- ACD 另有自己的 1905 选举协议（`acdAutoAsynMsg`、`_autoRoleElect`、`_autoSendSearch`、3000 ms 循环定时器、`Acd auto elect start`：设备之间交换 AP-autoconfig Search/Response 找控制器），桥接模式要靠它；没有触发过。
- `CurHop`：守护进程启动时 `slave_init`（0x60070）把它设成 -2（局部变量初值），仅当 `slave_get_global_data()` 里偏移 8 的字节非零且偏移 4 的值大于 1 时改用偏移 0xc 的值（含义没有核实），随后 `ASEND(0xa932, CurHop, "cspd.wlan_manage.wlan_mgr")`；
  从设备在 `I5TlvHigherLayerVendorTypeExtract`（0x98684）里按父节点的 `HigherLayerResponse` 设置自己的 `CurHop`。`HigherLayerResponse` 的厂商子 TLV `0x43`（4 字节大端）= 发送者的 `CfMultiapGetCurHop`（`I5TlvHigherLayerVendorTypeInsert` 0x98178 起）。
  原厂 4 帧 `HigherLayerResponse`（13:12:58、13:13:39、13:15:32、13:15:39）里 `43[4]` 全是 `00000002`；其中 13:15:32.08 那帧在 `CurHop = -2` 快照（13:15:31）之后 1 秒，仍是 2。-2 的瞬态有没有到过线上，这几帧没有证明；能说的是：观测到的每一帧都是 2。

### 10.1 还原方向（自适应 → 主路由）：无动作

- **库：** 只有 `MultiAPCfg.SetRoleEnable 1→0`。**线上与日志均无任何 mesh 相关动作**：无 Renew、无拓扑抖动、无重新入网。
- **不对称：** 打开自适应触发重新选举 → 拓扑重置 + 两个频段都 Renew（约 6 秒抖动）；关闭自适应只是保存标志。shim 要区分两个方向，不能"写库就 Renew"通吃。

## 11. 我的 Wi-Fi → 5GHz Wi-Fi 开关（开 → 关）

- **库：** 只有 `WLANCfg[4].Enable 1→0`（AP5，5G 主 SSID，持久）；`SECRET_WLANCfg[4]` 哈希随之变化；运行时 `MultiAPDomain[4].DomainUsed 1→0`。**`WLANBase.RadioStatus` 不变**：界面开关是 SSID 级，不是射频级。
- **线上：** 只对 5G 一个频段 Renew（`RenewID:9`，`bFreqBand:1`）；M2 照发，大小不变（1340 + 1353 + 209），关闭由 M2 内该 BSS 的类型表达（与前期文档"Tear Down"一致，M2 加密未解）；
  厂商 `hdr=010101` index 0 回复 **`0c` 1→0**，确认 **`0c` = `WLANCfg.Enable`**。频段配置回复仍 `07=100、19=2、55=55`；日志 `acdsGetBandConf … band[1] enable[1]`（射频未关）。
- **事后：** 约 8 秒后子节点主动发 `TopoNotification`（其 BSS 变化），主控随后重发 `0200fe`（值 0）、`PolicyConfigReq`（`4a 4c 5a 4d 60[2]=0001`）、`CombinedInfraMetrics`，并 `TopoQuery` 子节点。即子节点应用完新配置后会触发主控再刷新一轮。
- **安全模式对照（用户提供）：** 当前界面显示 **WPA3/WPA2 ↔ `WLANCfg.BeaconType=12`**（AP1、AP5）；回程槽 AP4 `BeaconType=6`，待逐个模式试完反推位含义。
- 同表字段：`WPAEncryptType/WPAAuthMode`、`11iEncryptType/11iAuthMode`、`WPA3EncryptType/WPA3AuthMode`、`WEPAuthMode/WEPLevel/WEPKeyIndex`、`BasicEncryptionModes`。

### 11.1 5G 密码（只改 5G）

- **库：** 只有 `SECRET_WLANPSK[4]` 哈希变化（`02af1b63 → 5309b577`），`WLANCfg` 不变。
- **线上：** 与改名字相同：单频段（5G）Renew ×1 → M1 → M2（1340+1353+209）→ 子节点 6 对厂商查询（频段配置 1、SSID 槽位 4、ACL 1）。厂商查询里**没有密码**，密码只在 M2 的加密段里。
- **还原核对：** 原值哈希 `02af1b63`，改回原密码后应回到该值（`SECRET_WLANPSK[4]`）。

## 12. 我的 Wi-Fi → 5G 安全模式（逐个切换）

用户按 WPA2 → WPA3 → WPA2/WPA → 不加密 → WPA3/WPA2（原值）的顺序对 5G 操作，每次间隔 10 到 20 秒。每次都是单频段（5G）Renew，子节点全程没有掉出拓扑（`P1905DevNum` 无变化）。

- **库：** 整个安全模式只编码在 **`WLANCfg[4].BeaconType`** 一个字段里，`WPAAuthMode`、`11iAuthMode`、`WPA3AuthMode`、`WEP*` 等全部不变。
- **`BeaconType` ↔ 界面选项 ↔ 日志里 M2 的认证类型（`auth.len[X:2]`，按十六进制读，即 WPS 认证类型位掩码）：**

  | 界面选项 | `BeaconType` | 配置的认证类型 | 说明 |
  |---|---|---|---|
  | 不加密 | 0 | `0x01` Open | |
  | WPA2 | 3 | `0x20` WPA2-PSK | |
  | WPA2/WPA（"wpa2 or wpa"） | 6 | `0x22` WPA2-PSK + WPA-PSK | 回程槽 AP4 现为此值 |
  | WPA3 | 10 | `0x40` SAE | |
  | WPA3/WPA2（"wpa3 or wpa2"，原值） | 12 | `0x60` WPA2-PSK + SAE | AP1、AP5 原值 |

  `BeaconType` 不是简单的位或（WPA2=3、WPA2/WPA=6、WPA3=10、WPA3/WPA2=12），按枚举表处理。"WPA"单独一项界面没有。
- **M2 认证类型 = 配置值 & 子节点能力：** 子节点在 M1 里报告自己支持的认证类型，日志 `slave_auth_type[23]`（Z506 = `0x23` = Open + WPA-PSK + WPA2-PSK，**不含 SAE**）。
  主控取交集，**交集与配置不同时才打印 `Change Auth Type to [Y]!`**：
  - WPA2 `0x20&0x23=0x20`、WPA2/WPA `0x22&0x23=0x22`、Open `0x01&0x23=0x01`：不变，无改动日志；
  - WPA3/WPA2 `0x60&0x23=0x20`：**降级为 WPA2-PSK**，`Change Auth Type to [20]!`；
  - **WPA3（纯）`0x40&0x23=0`：`Change Auth Type to [0]!`，M2 里认证类型为 0。** Z506 如何处置**未验证**（读不到子节点的库）。
  其它 BSS 槽位（默认 `0x60`）每次也都被同样降为 `[20]`。
- **M2 大小：** 5 次中只有不加密一次第一分片短 16 字节（1324，其余 1340），疑与 Open 不带密钥属性有关，M2 内有 AES 分组填充，仅作旁证。
- **移植要点：** shim 必须保存"子节点在 M1 里报告的认证类型能力"并在生成 M2 时按位与；原厂 `multiapd` 自己做这个交集，shim 只需让 M1 解析与数据库字段正确（`WLANCfg.BeaconType` 需映射为上表的认证类型位掩码，由原厂 CM 读函数完成，`CmWLANGetAPConfig`）。需验证 shim 对 `BeaconType=3/6/10/12/0` 五个值都给出一致的认证类型。

## 13. 访客 Wi-Fi（开启）

- **库：** `WLANCfg[1].Enable`、`WLANCfg[5].Enable`（AP2、AP6，2.4G 与 5G 访客）0→1；`SECRET_WLANCfg[1]/[5]` 哈希变化，密码 `WLANPSK[1]/[5]` 不变。
- **线上（13:40:51 起，约 3 秒）：** 两个频段各一次 Renew（`RenewID` 5 与 17）→ M1 ×2 → M2 ×2（各 3 分片）→ 12 对厂商查询，访客槽位（index 1，`ZTE-tsEG65_GUEST`）回复 `0c=1`，其余与第 5 节相同；约 14 秒后子节点 `TopoNotification`，主控刷新 `PolicyConfigReq`、`CombinedInfraMetrics`。无新类型消息。
- **槽位总览（日志 `acdsGetSSIDConf`）：** index 0 主 SSID、1 访客、2 Wi-Fi 5 兼容网络、3 **回程 BSS**（名称形如 `ZMESH-xxxxxx_5G`，隐藏、使能，`MapAccessAttr=64`，`BeaconType=6`，ACL 策略 0）。
- 网页请求（`vueData/advanced_separated_data`）：`Enable2/Enable6`、`guestAccessTime`、`GuestOffTime`、`ESSIDHideEnable`、`ACLPolicy`、`LimitEnable/DownRate/UpRate`（限速，单位 Mbps）、`ESSID`、`KeyPassphrase`（加密）、`BeaconType`…，`encode`。限速与限时未单独测试。

## 14. Wi-Fi 5 兼容网络（开启，`ForceWiFi5`）

- **库：** `WLANCfg[2]/[6]`（AP3、AP7）：`Enable 0→1`、`ForceWiFi5 0→1`、`ESSIDHideEnable`、`BeaconType 6→12` 随用户设置变化，`WLANPSK[2]/[6]` 变化。
- **厂商字段对应：** `hdr=010101` 回复里的 **`46[4]` = `WLANCfg.ForceWiFi5`**：整个采集中只有槽位 2 在 `ForceWiFi5=1` 时 `46=00000001`（两个频段各一次），其余全为 0。
- 其余同第 5 节（按频段 Renew、M2、12 对厂商查询）。网页请求（`vueData/vue_wifi_merger`）：`SubmitFlag=SSIDIY`，`EnableI/EnableY`、`ESSIDI/ESSIDY`、`ForceWiFi`、`BeaconTypeI/Y`、`ESSIDHideEnableI/Y`、`KeyPassphraseI/Y`（加密）、`_InstID_PSKI/Y`。

## 15. Wi-Fi 定时关闭（`WlanTimeCfg.TimerEnable`）

- **库：** `WlanTimeCfg[0].TimerEnable 0→1`；`TimePolicy` 删除最前一行并新增一行（行名 `IGD.TpCfg0..2` → `TpCfg1..3`，`RelTime` 以分钟计：0 到 360 为 `ExePolicy=1`、360 到 1440 为 0），新行 `ExePolicy` 为 0。
- **线上与日志：** **没有任何 1905 报文**，日志只有 `dbAPISave`。这是本机本地功能，未见推送给子节点；SR1010 无射频，实际效果需看子节点或本机 wlan 驱动，未验证。
- 网页请求（`hiddenData/vue_internet_advanced_rftimer_data`）：`TimeStartHour/Min`、`TimeEndHour/Min`、`TimerEnable`、`IF_ACTION=Apply`、`_InstID=`（空）。

## 16. 网页请求格式与令牌（还原时实测，已验证可用）

- **令牌：** 每个响应的 HTTP 头 **`X_XSRF_TOKEN`** 带下一次要用的令牌；POST 体末尾追加 `&_sessionTOKEN=<令牌>`。先发一个无害的 GET 即可取得最新令牌（需带 `Cookie: SID=…` 和 `Referer`，缺 `Referer` 会 404）。登录令牌流程：`Password = sha256(明文密码 + logintoken)`。
- **加密（前端 `encodePara`/`encodeKey`）：** 随机两个 16 位数字串 `key`、`iv`；`AES-256-CBC`，密钥 = `SHA256(key)`，IV = `SHA256(iv)` 的前 16 字节，零填充，输出 Base64，用于 `KeyPassphrase*` 等密码类字段；`encode` = RSA（PKCS#1 v1.5）加密字符串 `key+"+"+iv`，公钥是页面内置的固定 RSA-2048 公钥。只有带密码类字段时才需要 `encode`。
- **各页面提交（均为 `POST /?_type=vueData&_tag=<tag>`，`hiddenData` 用于定时）：**

  | 功能 | tag | 关键字段 |
  |---|---|---|
  | 漫游阈值与弱信号强制切换 | `mesh_threshold_data` | `IF_ACTION=Apply&_InstID=IGD&RoamRssiLmt24G&RoamRssiLmt5G&EnLegacyStaRoam&EnBtmKickoff&EnRoamKick`（后三个同值） |
  | MESH 兼容、设备角色 | `mesh_setting_data` | `_InstID=IGD&CurrentMode&CurrentEnable&PEnable(兼容开=0)&Mode(Master/Slave/自适应)&Enable&old_Enable` |
  | Wi-Fi 基本页（单频段形式） | `wifi_advancedssid_data` | `DualBandSync&Enable&ESSID&_InstID(DEV.WIFI.APn)&_WLANCONIG=Y&_WEPCONIG&_PSKCONIG&_InstID_PSK&BeaconType&WEP*/WPA*/11i*&ESSIDHideEnable&MLOEnable&PowerEnhance1/TxPower1/PowerEnhance5/TxPower5&Band1/Band5&_InstID_Frq1/Frq5&RadioStatus1/5`；**两频段合并形式（`Enable1/Enable5…`）实测返回 404 且只应用一半，改用单频段形式有效**；`_PSKCONIG=N` 且不带密码时服务端不动密码 |
  | 访客 | `advanced_separated_data` | 见第 13 节 |
  | Wi-Fi 5 兼容 | `vue_wifi_merger` | 见第 14 节；最小请求不够，需带 `_InstID_PSKI/Y` |
  | 定时关闭 | `hiddenData/vue_internet_advanced_rftimer_data` | 见第 15 节 |

- **数据库命令行与网页路径的区别：** `sendcmd 1 DB set <表> <行> <字段> <值>` 只改库（`save` 才落盘），**不触发 CM 通知、不触发 Renew、不推送子节点**；两个 `sendcmd` 同时运行会输出串台（实测监视的整库导出里混进了另一条命令的表内容），写库期间必须停掉监视。

## 还原结果（2026-10-16 14:05，经 `DB p` 差分与哈希核对）

全部还原，方式为网页路径（令牌 + 提交），部分未使用 SSID 的字段用 `DB set` 补齐：

| 项 | 方式 | 结果 |
|---|---|---|
| 漫游阈值 2.4G/5G、弱信号强制切换 | 网页 `mesh_threshold_data` | 已还原（线上 `4a=64 50…`、`5a=00`） |
| MESH 兼容 | 网页 `mesh_setting_data` | 已还原（`P1905ComCfg.Enable=1`） |
| 双频合一、MLO、名称 `cc`、隐藏 SSID | 网页 `wifi_advancedssid_data`（单频段形式） | 已还原（`EnBandSteer` 随之自动回到 1；子节点日志 `SSID[cc]`、`ESSIDHideEnable[0]`） |
| 5G 密码 | `DB set`（把 2.4G 密码复制给 5G，不打印） | 哈希回到原始 `02af1b63`，证明原 5G 密码 = 2.4G 密码 |
| 访客 | 网页 `advanced_separated_data` | 哈希精确回到 `69b5df56` / `dfc05990` |
| Wi-Fi 5 兼容 | 网页 `vue_wifi_merger`，再 `DB set` 补 `BeaconType`、隐藏、名称 | `WLANCfg[2]/[6]` 哈希精确回到 `2c041635` / `522442ee`（名称 `SSID3`/`SSID7`） |
| 定时关闭 | 网页 `rftimer` + `DB set` 末行 `ExePolicy=1` | 已还原 |

**残留（无法或无需恢复）：** `TimePolicy` 行名 `TpCfg0..2`→`TpCfg1..3`（实例计数器，内容一致）；Wi-Fi 5 兼容网络两个 SSID 的密码（`WLANPSK[2]/[6]`，原值无从得知，闪存里只有二进制配置和被覆盖的备份；已确认它们不等于主密码），两个 SSID 是关闭状态，不影响使用。
`SECRET_WLANCfg[0]/[4]` 哈希与"首次值"不同是预期的（首次值是双频合一关闭时记的），可见字段与基线完全一致。

日志级别已还原为 7，原厂上的采集与临时文件已清理，mac 上的会话文件已删除。

## 17. 子节点管理、拓扑页、IPTV、访客、定时（2026-10-18，静态分析 + 真子节点；没有再用原厂路由器抓包）

原厂 Web 前端和 Lua 层在 `out/led-vendor-kmodule/home/httpd`（`webmodules/modules/*.lua` 是字节码，用 `strings` 看字段；前端 JS 在 `public/jquery/static/js`）。

| 页面/动作 | Web 请求（`POST /?_type=vuejsonData&_tag=…`） | 往下的路径 | 子节点 Z506 |
|---|---|---|---|
| 拓扑页：重启子节点 | `router_manage_lua`：`IF_ACTION=Apply&ACTION_TYPE=RebootDev&DestMac=<AL MAC>&RebootSwitch=1` | `OBJ_ROUTER_MANAGE` → libieee1905cmapi `CmMultiApSendMsg2TopoDev`（"RebootDev" → 事件 0x90a0）→ `ASEND(0x90a0, 16 字节, "multiapd.topo.dev")` → `_topoDevRebootSetMsgHandle` → `_topoDevSendVendorSetByTunnel(0x20)` → `tunnelFloodSend`（`ASEND(0x90d2, "multiapd.base.tunnel")`）→ 单播 VendorSpecific | 支持，真机验证 |
| 拓扑页：开/关指示灯 | 同上，`ACTION_TYPE=LightSet&DestMac=…&LightSwitch=0|1` | "LightSet" → 事件 0x909f → `_topoDevLightSetMsgHandle` → 内层 TLV 0x21 | 支持，真机验证；子节点的灯状态由它的 HigherLayerResponse 里厂商子 TLV 0x21 `{AL MAC, 状态}` 报给主控 |
| 拓扑页：改名 | `getEditTableLua`：`InstIdentity=<MAC>&Alias=<名称>` | `CmMultiApTopoSetDevInfo` → `CmSendMsg2MM("multiapd.topo.topo", 0x2401, 0x47000b)` → `topoDevSet` → `i5DmAddOrModifyAliasName`：只改主控自己保存的别名 | 不下发 |
| 拓扑页：每个 AP 一张卡 | `vue_topo_data`（`OBJ_LISTAPTOPO_ID`/`OBJ_GETTOPOAD_ID`） | 字段 `DeviceName`、`IpAddr`、`MacAddr`、`BhStaRssi`、`LightStatus`、`AccessType`、`accdevCount`、角色 AC/AP | — |
| AC→AP 的 IPTV 同步（`acap_iptv`） | （cspd 内部，无 Web 请求） | cspd `AcApIptv*`：AP 上线时 `IdmSsendApi` → `idm_service`（libidmapi），**不是 1905** | Z506 没有 `idm_service`/libidm，数据库没有 `ApIptv` 表，`multiapd` 里没有 IPTV 代码 |
| 访客 Wi-Fi：限速、限时 | `advanced_separated_data`：`LimitEnable/DownRate/UpRate`、`guestAccessTime/GuestOffTime` | 都是 `WLANCfg` 字段；cspd 的 wlan 模块在本机处理（限时 = 到点把访客 SSID `Enable` 写 0 并发 `CmMultiAPWlanRenewNotify`）；SSID 查询回复里没有限速/限时字段 | 子节点自己的 cspd 有同名功能（`dynQosSetGuestSSIDSpeed`、`handle_wlan_guestoff_timer`），主控不下发 |
| Wi-Fi 5 兼容 | `vue_wifi_merger`（第 14 节） | `ForceWiFi5` → 厂商查询回复子 TLV 0x46；CM 输出结构偏移 2268 | Z506 较旧的 `multiapd` 没有 FORCEWIFI5 代码，忽略 |
| Wi-Fi 定时关闭 | `hiddenData/vue_internet_advanced_rftimer_data`（第 15 节） | cspd `wlancfg_hdl_time_action`：写全局 WLAN 状态，再对每个无线卡/实例发 `CmMultiAPWlanRenewNotify`（没有新的 1905 报文，子节点通过正常 Renew 看到关闭的 BSS） | 通过 M2 的使能位 |

隧道设置报文（重启/指示灯）的线上格式：`VendorSpecific`（CMDU 4）单播给子节点 AL MAC，厂商 TLV `0b`：`00 19 c6`（OUI）`02`（版本）`<计数|分片>` `07`（消息类型）+ 内层 TLV `<类型 0x20 重启 | 0x21 指示灯> 00 07 <AL MAC 6 字节> <值>`。重启的值要 1，指示灯 0/1。
示例（真机抓包）：`0019c6 02 00 07 | 21 0007 d4b7090d7730 00`（关灯）、`… 02 01 07 | 21 0007 d4b7090d7730 01`（开灯）、`… 02 02 07 | 20 0007 d4b7090d7730 01`（重启）。
另有一条 `CfIeee1905SendRebootMsgForBaffalo`（事件 0x90a8，Buffalo OEM 的厂商 TLV），与中兴子节点无关。
