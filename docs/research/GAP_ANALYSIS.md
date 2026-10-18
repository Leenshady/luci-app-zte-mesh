# shim 对照原厂 mesh 的缺口分析与工作清单

> **路径说明（2026-10-18 迁入 sr1010-mesh 仓库）：** 本文写于迁移之前，文中路径按当时的布局：mac 上的 `zx279133/port/mesh-shim/` 即现在的仓库根目录，`out/mesh-shim/` 即 `out/`；`session/…`、pcap、`db-baseline-s000.txt`、`vendor-log-debug.txt` 在 `evidence/`（只在 mac 上，不入库）；`tools/<脚本>` 即 `tools/capture/<脚本>`；`/tmp/buildNN.sh` 由 `tools/build/container-make.sh` 取代；`/tmp/shim-bak-*` 备份已删除（由 git 历史取代）；当时手工编的 `ns-run-al` 即现在 Makefile 的 `ns-run-svctest` 目标（运行包内容逐字节相同），当时说的旧 `ns-run-svctest` 文件已不存在。

证据来源：同目录 `FEATURE_MAP.md`（逐项实验）、`session/`（抓包、日志、库快照）、`db-baseline-s000.txt`（原厂最初整库）、
mac 上 `port/mesh-shim`（shim 源码与文档，规范副本在 mac，不是本机 `~/code/build-mesh`）。时间 2026-10-16。

## 1. 结论

shim 的**字段层面是完整的**，缺口在**表之外的机制**：

- 7 张已注册表（`MultiAPMaster` 55、`MultiAPDomain` 106、`WLANBase` 70、`WLANCfg` 53、`WLANSSID` 8、`WLANPSK` 4、`DevInfo` 45）的字段与原厂整库中的字段**逐个相等**，`MultiAPCfg` 50 字段与原厂一致（前期文档同）。
- 原厂 `multiapd` 本体由 shim 原样运行，WSC M1/M2、厂商 TLV 的组装、M1 能力取交集、厂商查询的回复（频段配置、SSID 槽位、ACL）都在它里面，不需要重写。
- 缺的是 `cspd` 一侧的几块逻辑，和把配置**通知**给 `multiapd` 的 CM 层。

**产品范围（用户 2026-10-17 确认）：只做 AC（主控，master-only）。** 子节点（Slave）角色、桥接模式判断、ACD 自己的 1905 选举协议不在范围内，不算缺口；
第 7 项里提到的这些只是原厂页面上的对应物，不再跟进。`mesh.role=auto` 只是对原厂"自适应"页的复刻，不是产品必需。

## 2. 已有且已验证的（本次实验再次确认原厂行为）

| 能力 | 证据 |
|---|---|
| 子节点发现、拓扑、AP-autoconfig、WSC M1/M2 | 前期真机验证（Z506）；原厂冷启动入网基准 `vendor-master-coldjoin-eth4.pcap` |
| 12 对厂商查询（频段配置、4 个 SSID 槽位、ACL） | `FEATURE_MAP.md` 第 5、6、14 节 |
| SSID、密码、开关、BeaconInterval、信道、功率、ACL | 前期真机验证 |

## 3. 缺口（按优先级）

### P0：不补则"改了配置不生效"

| # | 缺口 | 证据 | 建议做法 |
|---|---|---|---|
| 1 | **（已完成 2026-10-17，见 REPLAY_RESULTS 第 11 节）CM 通知层**：写库后必须通知 `multiapd`，直接写库**不会**推送 | 13:45 实验：`DB set` 漫游阈值，无 `PolicyConfigReq`；同值走网页路径，立即发出（`FEATURE_MAP.md` 文首更正） | 不要自己写库后再猜事件；**调用原厂的 CM 函数**（`CmMultiAPSetRoamCfg`、`CmMultiAPSetMasterCfg`、`CmMultiAPSetMultiAPCfg` 等，在 `libmultiapcmapi.so`/`libcmapi.so` 里），它们同时写库并发通知。运行包里已有这些库；前期文档的事件号：`0x10205` 主控配置、`0x10206` 域/漫游、`0x10208` BTM、`0x10216` 无线电信息 |
| 2 | **（已完成 2026-10-17，见 REPLAY_RESULTS 第 11 节；第 27 轮线上与原厂第 8 节一致）WLAN 变化 → 按频段 Renew 的触发判断**（原厂 cspd 的 `multiapd_mgr`）。**触发手段 shim 已经有**：`SERVICE.md` 记录 helper 直接发事件 `0xa93e`（8 字节 `{band,0…}`），等价于 `multiapd_mgr` 的最后一步（`zte-meshctl renew-acd <band>`）；缺的是"什么变化触发哪个频段"的判断 | 第 5、8、11、13、14 节：只改一个频段只 Renew 那一个；`WlanRenewToCtr` 约 2 秒，ACD 再 `RENEW_MSG_FIRST delay 3s`；`webd` 总是写 AP1/PSK1/RD1/RD2，触发频段由比较得出 | shim 侧维护每个频段的配置摘要（SSID、认证、使能、隐藏、PSK 哈希…），变化的频段各触发一次 Renew；`controller-service.c` 的热更新已对 SSID/密钥/开关/BeaconInterval/信道/ACL 这样做，需核对是否覆盖隐藏、安全模式、槽位等字段 |
| 3 | **（已完成 2026-10-17，见 REPLAY_RESULTS 第 11.5 节；只在停用槽位上试，用子节点读回而不是守护进程日志验证）`BeaconType` → 认证类型位掩码** 在 shim 的 CM 读函数里要对 5 个值正确 | 第 12 节：0→`0x01`、3→`0x20`、6→`0x22`、10→`0x40`、12→`0x60` | 对 5 个值各跑一次 shim，核对 M2 取交集后的认证类型与原厂日志一致（`Change Auth Type` 行） |

P0 的实际做法（2026-10-17）：第 1 项**没有**调用 `CmMultiAPSet*Cfg`（它们会带出发往 `cspd.cspd.multiapd_mgr` 的 `ASEND` 和主/从目的地的选择，shim 没有 cspd），
而是反汇编后发它们写库之后发的那条同步消息：`SSEND(0x2401, {消息号, name[32], 0}, "multiapd.map.master")`，消息号 `0x10206`（域/漫游）、`0x10205`（主控）；
写库由监督进程在共享池锁下完成并读回核对。线上帧与原厂 FEATURE_MAP 第 1、2、3 节逐字节相同。第 3 项不是看守护进程日志（级别 8 才输出），而是看子节点自己读回的 `BeaconType`。
未做的相邻项：`SetRoamCatonCfg` 的 `0xa96a`、`0x10208`。UCI/LuCI 映射已在 2026-10-17 做了（SERVICE.md "UCI 与 LuCI 映射"，未在真实 OpenWrt 上运行）。（`CmMultiApTopoSetP1905ComCfg` 的 `0xa93d` 已在 P1 第 6 项做完；`CmMultiAPSetMultiAPCfg` 的 `0xa93c` 只有 `Mode`/`VersionMode` 变化才重启守护进程，shim 固定 `Mode=2`，`version_mode` 仍需重启服务。）

### P1：功能差异明显

| # | 缺口 | 证据 | 建议做法 |
|---|---|---|---|
| 4 | **（已完成 2026-10-17，见 REPLAY_RESULTS 第 8 节和第 15.3 节：表已注册、M2 的 MLO 属性和 `0200fe` 值对了；"5G 名称 = 2.4G 名称 + `ESSIDSuffix`"的派生在第 15.3 节做了，实验室验证，**派生的改名没有在真子节点上试**，只在子节点上试了名称不变的 `DualBandSync` 热更新）`WLANSetting` 表未注册**——同时发现还要注册 `MultiAPRoamCatonCfg` 与空表 `MultiAPCacheMac` | 第 4、7 节：它决定 5G 名称派生、M2 的 MLO 属性（第 2 分片少 30 字节）、`0200fe` 的值 | 注册该表（字段取自原厂 `DB p WLANSetting`）；实现 5G 名称 = 2.4G 名称 + `ESSIDSuffix`（双频合一关闭时） |
| 5 | **（部分完成 2026-10-17，见 REPLAY_RESULTS 第 9 节；第 15.1 节查过"入网当时发"的触发源，没有找到可用的，没有改；每个子节点上线满 15 秒后发 1 条，MLO 开关变化时（`mlo.enable` 热更新）再发一条新值——紧跟在 Renew 之后、新 M2 之前，原厂是在重新入网序列之后；原厂入网时在 PolicyConfigReq 之前发 3 条）跨设备模块消息 `0200fe`**：主控 `wlan_mgr` → 子节点 `wlan_mgr`，值 = `MLOEnable` | 第 4、7 节 | 若要与原厂子节点完全一致，在 Renew 序列里按原厂顺序发送该消息（出现在 `VendorSpecific 0200` 之后、`PolicyConfigReq` 之前） |
| 6 | **（已完成 2026-10-17，见 REPLAY_RESULTS 第 12 节；原描述的机制是错的）mesh 级变化 = 守护进程重启，不是 Renew**：`CmMultiApTopoSetP1905ComCfg` 写库后无条件 `ASEND(0xa93d)` 给 cspd 的 `multiapd_mgr`，它 `MultiapRestart`（停进程、再起一个）；两个频段都 Renew 是新守护进程 ACD 的 `RENEW_MSG_FIRST`，6 到 40 秒抖动是拓扑表丢失后重建。`SetRoleEnable` 路径（`CmMultiAPSetMultiAPCfg` → `0xa93c`）只在 `Mode`/`Enable`/`VersionMode` 等变化位置位时才重启 | 第 9、10 节（含 2026-10-17 更正）；`session/vendor-log-filtered-final.txt` 里 6 次 `MultiapRestart`（start count 2 到 7）；cspd `MultiapMgr*` 反汇编 | shim：`mesh.v2` 热更新 = 写 `P1905ComCfg.Enable`（读回）+ 在同一数据库上重启真守护进程（`controller_respawn_daemon`，SIGKILL 旧进程、锁探测、再 fork/exec）；`SetRoleEnable`/角色选举不适用（见 #7，shim 固定 `Mode=2`） |
| 7 | **（已完成 2026-10-17，见 REPLAY_RESULTS 第 13 节；范围：只支持 master / 自适应落在 master，Slave 不支持）角色选举**：`MultiAPCfg.Mode`：0=自动、1=Slave、2=Master、3=主从合一；"自适应" = `Mode 0` + `SetRoleEnable 1`，路由模式选举结果是 Master，终态 `Mode 2` + `SetRoleEnable 1`；一次提交 = 约 3 秒内两次守护进程重启 | FEATURE_MAP 第 10 节（含 2026-10-17 的角色语义补充）；第 40 轮真子节点 | 服务键 `mesh.role=master\|auto`（可选，缺省不动数据库）：`auto` 写 `SetRoleEnable 1`、`Mode 0`、重启守护进程，约 3 秒后写 `Mode 2` 再重启一次（**这一步是监督进程替 cspd 的 `cooperator_mgr` 做的模拟**：守护进程自己不会把 Mode 写回 2）；`auto→master` 只清 `SetRoleEnable`、不重启；`slave` 被拒绝。ACD 自己的 1905 选举协议、桥接模式判断没做 |
| 8 | **（已完成 2026-10-17，见 REPLAY_RESULTS 第 13 节）** `CurHop` 在选举/拓扑重置期间会被置 -2 再回 2 | 第 10 节；`slave_init` 反汇编；原厂 4 帧 `HigherLayerResponse` 的子 TLV `0x43` 恒为 2（含 -2 快照后 1 秒的一帧） | shim 的 `EnMasterOnly=1` 让 `slave_init` 跳过（`multiap-cfg.c` 的注释，早先反汇编的结论），`CurHop` 一直是 2：第 40 轮和实验室里的 `cfg` 日志在每次守护进程重启前后都是 2；与观测到的每一帧原厂报文一致。原厂的 -2 是库里几秒的瞬态，采样的线上帧里没有看到 |

补充（2026-10-17）：槽位 `enabled` 和增减槽位已热更新（REPLAY_RESULTS 第 14 节：`enabled` 不重启守护进程；增减 = 写表 + 重启守护进程；槽位只能是 1..k）。
仍需重启服务的只剩 `version_mode` 和若干键的出现/消失。

### P2：管理面与周边

| # | 缺口 | 证据 / 状态 | 建议 |
|---|---|---|---|
| 9 | **（已完成 2026-10-18，见 REPLAY_RESULTS 第 16.1 节、FEATURE_MAP 第 17 节；真机验证）子节点管理**：重启子节点、开关指示灯 | 机制：Web `router_manage_lua` → `ASEND(0x90a0/0x909f, "multiapd.topo.dev")` → 隧道 `VendorSpecific`（内层 TLV 0x20 重启 / 0x21 指示灯）；`0xa937`/`0x90a1` 的线索是错的（那是上报/发布） | 已实现：`zte-meshctl reboot-node/light-node`、服务的节点动作通道、rpcd 方法、LuCI 按钮；真机上灯 1→0→1、子节点重启并重新入网；只对已被识别为中兴设备的在线节点执行 |
| 10 | **（大部分完成 2026-10-18，见第 16.2 节）拓扑图与子节点信息**（名称、接入方式、IP、MAC） | 原厂每张卡：名称、IP、MAC、回程 RSSI、指示灯、接入方式、终端数、角色 + 改名/重启/指示灯 | 节点表有名称（可改别名，只存主控）、型号、地址、MAC、软件、类型、指示灯、终端数、状态、操作；指示灯和终端数来自 trace 库对子节点帧的摘要（真机核对了指示灯；终端数 = 2 没有独立核对）。**没做：回程 RSSI（有线无）、树状图、AP 下的终端列表、软件版本字段（数据库没填）** |
| 11 | **（查明 2026-10-18：不适用，没有实现，见第 16.3 节）`acap_iptv`**：子 AP 上线时主控同步 IPTV 配置 | 走的是 IDM（`idm_service`/libidmapi），不是 1905；Z506 没有 IDM 服务、没有 `ApIptv` 表、`multiapd` 里没有 IPTV 代码 | 有带 IDM 的原厂子节点时再做，没有对端可验证 |
| 12 | **（部分完成 2026-10-18，见第 16.4 节）访客限速、限时、Wi-Fi 5 兼容** | 限速和限时都是子节点本机功能，1905 里没有字段；原厂的限时 = cspd 到点把访客 `Enable` 写 0 并 Renew | **限时**：槽位 `access_minutes`（真机验证）；**Wi-Fi 5 兼容**：槽位 `force_wifi5`，同时修了适配器漏掉 `ForceWiFi5`→0x46 的 bug（线上验证，Z506 忽略）；**限速：没有 mesh 表示，没有实现**（OpenWrt 一侧对访客 VLAN 做 SQM） |
| 13 | **（已完成 2026-10-18，见第 16.5 节；`guest` 范围真机验证，`all` 范围只在实验室）Wi-Fi 定时关闭** | 原厂：到点写全局状态并对每个 BSS 发 Renew 通知，子节点从 M2 的使能位看到 | 服务里的调度器：`timer.N.*`；窗口开关有效使能位并 Renew；隐藏回程槽位不动 |

### P3：原厂没有的功能

需要新增协议内容，且子节点固件必须认。**应在 P0 到 P2 完成并通过对比测试后再评估**，与"复刻原厂"是两类工作。

## 4. 验证方法（建议下一步）

1. **回放对比**：把 `db-baseline-s000.txt` 的相关表喂给 shim（去掉打码字段，用同一份配置），冷启动入网，抓 eth 口，和 `vendor-master-coldjoin-eth4.pcap` 做逐帧、逐 TLV 对比（`tools/cmdu.py --vendor` 已能输出厂商子 TLV）。重点：M2 三个分片的长度（1324/1340、1353、209）、`PolicyConfigReq` 的 `4a/4c/5a/4d/60`、12 对厂商查询的回复。
2. **逐项复现**：对 `FEATURE_MAP.md` 第 1 到 15 节每一项，在 shim 上做同样的改动，要求触发的报文序列与原厂一致（Renew 数量与频段、`PolicyConfigReq` 内容、有无 `0200fe`）。
3. 原厂行为里**尚未验证**的点保持标注：Z506 对 M2 认证类型为 0（纯 WPA3）的处理（2026-10-17 在停用槽位上试过：Z506 忽略该项、保持原值、不掉线，见 REPLAY_RESULTS 11.5；主 SSID 上没试）、`0f/1e` 谁是 `GuestSsid` 谁是 `VapIsolationEnable`、`TimePolicy` 的语义、`Map Renew delay 10 s` 的含义。

## 5. 未解与不确定

- `hdr=010101` 字段：`0b`=槽位、`0c`=`Enable`、`0d`=`ESSIDHideEnable`（实验确认）、`11[4]`=`ACLPolicy`（模式吻合）、`46[4]`=`ForceWiFi5`（模式吻合）、`0f`/`1e`=`VapIsolationEnable`/`GuestSsid`（未区分）。
- `hdr=010102`（ACL 查询）的 `1b[4]=0x40`、`1c[4]=2` 含义未确认，可能与回程槽 `MapAccessAttr=64` 有关。
- 日志里 `web set (objname:…)` 的对象名与 Lua 源码字段归属相反，疑为日志标注错位，以库差分为准。
