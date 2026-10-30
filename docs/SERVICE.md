# B.4：长期运行的 Controller 服务模式、节点清单与配置下发

> **状态（2026-10-18）：** 服务模式已与真实子节点 Z506 联调（入网、配置下发、热更新、守护进程重启、节点动作、调度器，见各节和
> [REAL_CHILD.md](REAL_CHILD.md)），并打成 OpenWrt 包在真实 procd/rpcd 上跑过（隔离网桥，见下文“未验证 / 已知缺口”）。
> 紧接着的“已验证”“关键发现”两节是 B.4 当时（2026-10-11，隔离实验室和合成对端）的记录。

## 已验证

- **节点清单**：`P1905DeviceInfo`（11 字段/788 字节/1024 行，主键 ViewName+MacAddr）
  从 `cspd` 静态恢复，真实 `multiapd` 在收到对端后自己写入行（AL MAC、Active、
  WSC M1 带来的 ModelName）。`controller-run.c:controller_read_nodes()` 只读导出，
  不含序列号/ProductClass。
- **服务模式** `ns-run … --controller-service`（`controller-service.c`）：不再有 40 秒限时，
  配置由宿主写入 `/run/zte-mesh/config`（0600，严格 key=value），状态每秒写成
  `/run/zte-mesh/state.json`（`/run/zte-mesh` 是沙箱里唯一可写的宿主路径）。
  在路由器上私有运行通过：状态/AL MAC 正常，宿主 IPC/接口快照不变。
- **Wi-Fi profile 热更新**：配置文件变化后经带回滚的类型化桥写入 `WLANCfg`/`WLANPSK`，无需重启。
- **配置下发链路（lab 实测）**：改 profile → 事件 `0xa93e` 发给 `multiapd.acd.acd` →
  原厂 `_handleWlanRenew` → 真实 daemon 发出 AP-autoconfiguration **Renew（CMDU 10）**，
  合成对端收到（`fixture=autoconfig-renew … peer_received=1`）。
- M2 的 Authenticator/Key Wrap/profile 由独立验证器通过（`wsc_cryptographic_verified` 现已接入报告）。

## 关键发现（逆向，已用实测核对）

- 厂商 `CmMultiAPWlanRenew` 发给 **cspd** 的 `cspd.cspd.multiapd_mgr`（事件 `0x9c8a`）；
  `CmMultiAPWlanRenewNotify` 在已知设备数 `P1905DevNum<=1` 且 Mode>1 时才直接
  `SSEND(0xa93e)` 给 multiapd，多于 1 个设备时又走 cspd。我们不运行 cspd，所以
  helper 直接发 `0xa93e`（8 字节 `{band,0…}`），这正是 cspd 管理任务最终做的事。
- 外部进程不能调用 `ASEND/SSEND`：它们需要 OSS 身份。厂商小工具（`diagget`）用
  `liboss_pub.so`（客户端 OSS 库）+ `InitAppComm`，**不是** daemon 用的 `liboss.so`。
  从监督进程直接调 CM 会崩溃。因此 `zte-meshctl`（ARM32）链接 `liboss_pub`，由监督进程
  fork/exec；`prepare.py` 现按固定哈希提取 `runtime-extra/lib/liboss_pub.so`。
- 标准模式（VersionMode bit1）下节点 `ZTEDevFlag=0`、型号来自 WSC M1。

## MLO 开关与跨设备模块消息（2026-10-17，真实子节点 Z506 验证）

- 服务配置新键 `mlo.enable=0|1`（默认 1，与原厂基线 `WLANSetting.MLOEnable=1` 一致；启动时写入
  `WLANSetting`；2026-10-17 起可热更新：写库、两个频段 Renew、`0200fe` 重发，见下一节）。`wifi.dual_band_sync` 同时写入
  `WLANSetting.DualBandSync`（只是启动值，热改不重启）。
- 原厂 `cspd.wlan_manage.wlan_mgr` 在设备上线（以及 MLO 开关变化）时，通过 `CfIeee1905TunnelToDevice`
  **异步**发事件 `0xa97e` 给 `multiapd.topo.topo`，daemon 把它作为厂商 TLV `0200fe` 的 VendorSpecific CMDU
  发给子节点：体 = `{u16 BE 36, 名字[36]="cspd.wlan_manage.wlan_mgr", u32 BE 1, u32 BE 4, u32 BE 值=MLOEnable}`。
  我们不跑 `cspd`，所以服务在 ZTE 子节点在线满 15 秒后调用 `zte-meshctl wlan-mgr <AL MAC> <0|1>`
  （每个 ZTE 子节点各自计时，最多同时 8 个；子节点重新上线再发一次；每个最多重试 4 次）。**必须用 `ASEND`**：同步 `SSEND` 走的是请求表，不含 `0xa97e`，
  返回 0 但线上没有任何帧（第 19 轮）。第 21 轮的线上帧与原厂逐字节相同（除 MID 与 AL MAC）。
- 原厂在入网时发 3 条（设备上线事件每次触发一次，时间在 PolicyConfigReq 之前几毫秒），shim 每个会话只发 1 条。
  Z506 的 `WLANSetting` 表没有 `MLOEnable` 字段（只有 ViewName/DualBandSync/SSIDSync/AirMonitorDuration/SpecialMode），
  所以这条消息对它是无操作；有 Wi-Fi 7 的子节点才会生效。
- 启动时还要注册三张空视图/小表（`MultiAPRoamCatonCfg`、`WLANSetting`、`MultiAPCacheMac`），否则第一条
  `PolicyConfigReq` 的厂商体会塌成 1 字节（见 REPLAY_RESULTS 第 8 节）。

## CM 通知层：策略类设置的热更新（2026-10-17，GAP P0 第 1 项，真实子节点 Z506 验证）

原厂 CM 设置函数（`libmultiapcmapi.so`）写库之后，用 `CmSendMsg2MM("multiapd.map.master", 0x2401, 消息号, name[32], 无负载)` 通知主控任务，
即**同步** `SSEND(0x2401, {u32 消息号, char name[32], u32 0}, "multiapd.map.master")`，主控在 `master_handle_synmsg` 里经 SynSet 表分发
（`mesh_handle_obj`：表项 {消息号, 函数}）。直接写库（`sendcmd 1 DB set`）不会发它，所以 `multiapd` 不会重读、也不推送。

| 消息号 | 谁发 | 主控处理函数 | 线上效果 |
|---|---|---|---|
| `0x10206` | `CmMultiAPSetRoamCfg`、`SetDomainCfg`、`SetBSCfg`、`SetBSEnable` | `_master_set_doamin_cfg` = `master_update_gtDomain` + `master_update_roam_threshold` + `master_sta_update_bsloadstrategy` | 对每个子节点发一个 `PolicyConfigReq`：`MetricRptPolicy` + 厂商 `4a[12] 4d[1] 60[2]`（83 字节） |
| `0x10205` | `CmMultiAPSetMasterCfg`、`SetMultiAPCfg` | `_master_set_master_cfg` = `master_set_mgr_cfg` + `master_vendor_policy_notify` + `master_set_meshsr_onoff`（版本位 `0x20` 才动） | 一个纯厂商 TLV 的 `PolicyConfigReq`：`4c[2] 5a[1] 4d[1] 60[2]`（52 字节） |
| `0x10208` | `CmMultiAPSetBtmReqModeCfg` | `_master_set_btm_req_mode` | 无帧（更新判断参数） |
| `0x10207` / `0x10216` / `0x10203` | `SaveTopoCfg` / `SetRadioInfo`（name = 无线电 InstId） / 内部 | 拓扑保存 / 无线电信息 / 信道选择 | 未使用 |

服务里的实现：
- `zte-meshctl notify-master <id>...`（链接 `liboss_pub` 的 ARM32 助手；只允许 `0x10205`/`0x10206`/`0x10208`，可一次给多个，在同一个进程里按顺序发，
  对应原厂网页路径两条通知间隔 12 ms）。自检：助手的 `SSEND` 拦截器打印 `event=0x2401 length=40`。
- `mesh-policies.c` 的 `mesh_policies_live_*()`：在共享池锁下把字段写进全部 8 个 `MULTIAPDOMAIN` 行（第 2 到 9 行，与 `CmMultiAPSetRoamCfg` 一样）或 Master 行 0。
- `controller-service.c`：配置键 `roam.rssi24/5`、`band_steer.rssi24/5`、`band_steer.enabled`、新键 `roam.force`（`MultiAPDomain.EnLegacyStaRoam` +
  `MultiAPMaster.EnBtmKickoff/EnRoamKick`）改值时写库并排队通知：Domain 字段变 → `0x10206`；Master 字段变 → `0x10205`；两者都变先 `0x10206`
  再 `0x10205`（原厂弱信号开关的顺序）。启动时不发任何通知。键的出现/消失（无→有、有→无）仍需重启（`policy_keys_changed`）。
- 同一轮还把这些改成热更新：已有槽位的 ssid/key/hidden/beacon_type（写 `WLANCfg`、`WLANPSK`，写后读回核对）、主 SSID 的 `hidden`/
  `beacon_type`（新键 `radioN.hidden`、`radioN.beacon_type`；主 SSID 的热更新没有试过，`beacon_type` 只在 5G 试过 12→6→12）、`mlo.enable`（写 `WLANSetting.MLOEnable`，
  两个频段 Renew，`0200fe` 由 `mlo_reconcile` 补发）；WLAN 类变化**只 Renew 变化了的频段**（原厂 `multiapd_mgr` 的行为，FEATURE_MAP 第 8 节）。
  槽位 `enabled`（写 `WLANCfg.Enable` + Renew）和增减槽位（写表 + 重启守护进程）也已热更新（2026-10-17，REPLAY_RESULTS 第 14 节）；主 SSID 的 `radioN.enabled` 热更新未验证。同一无线电上的槽位必须是 1..k。

验证（第 26 轮，与原厂 FEATURE_MAP 第 1、2、3 节逐字节比较）见 REPLAY_RESULTS 第 11 节。

## 守护进程重启：mesh 级变化（2026-10-17，GAP P1 第 6 项，真实子节点 Z506 验证）

原厂的"mesh 级 Renew"其实是 cspd 的 `multiapd_mgr` 重启 `multiapd`（`CmMultiApTopoSetP1905ComCfg` 写库后无条件 `ASEND(0xa93d)`；`CmMultiAPSetMultiAPCfg` 在 `Mode`/`Enable`/`VersionMode`
变化时 `ASEND(0xa93c)`）；两个频段的 Renew 和 6 到 40 秒的拓扑抖动是新进程起来的副作用，见 FEATURE_MAP 第 9、10 节的更正和 REPLAY_RESULTS 第 12 节。

服务里（只做了 `mesh.v2`）：
- `mesh.v2` 改动 → `v2_update`（在池锁下写 `P1905ComCfg.Enable` 并读回，日志带 `db_before`/`db_after`）→ 主循环 `daemon_respawn`：SIGKILL 旧守护进程、`waitpid`、探测数据库池锁和订阅池锁（被杀的进程可能握着锁，
  探测失败则不起新进程、服务退出，由 init 重启）、在同一数据库上 `fork/exec` 新的真守护进程。数据库和订阅池属于监督进程，不丢；服务不退出，`state.json` 的 `daemon_pid` 换新、`daemon_restarts` 加 1。
  写库或读回失败退回 `restart_required`。
- 重启后清掉每个守护进程的推送记录，`0200fe`、信道/功率推送要等 `P1905DevNum >= 2`（`topology_ready()`，日志 `topology dev_num=…`）再计 15 秒：新进程起来时数据库里子节点的 `Active` 是旧进程留下的 1，
  抖动期里推出去的消息会被守护进程丢弃。
- 原厂重启后第一对 Renew：守护进程自己在登记接口时发 ACD 事件 2（`baseDoPhyLinkUp`），ACD 启 3 秒定时器，到期时 `acdsCheckMultiapdRestart` 向 `cspd.cspd.multiapd_mgr` 取启动次数（`SSEND(0xa96e)`），大于 1 才发。
  shim 里没有 cspd：`controller-route.c`（守护进程里的预加载库）拦截那一条 `SSEND(0xa96e)`，按监督进程环境变量 `ZTE_MESH_START_COUNT` 回答启动次数（首次开机为 1，所以冷启动行为不变）。事件 2 不用我们发（第 36–38 轮试过，多余，
  第 38 轮还因此丢了一次 Renew 对）。
- 真机结果（第 33–39 轮，15 次重启）：重启后子节点掉出拓扑，控制器的 `TopoResponse` 不带 `1905Nbr`，8 到 58 秒后重新入网；`PolicyConfigReq` 的 `60[2]` 跟着 `mesh.v2` 变（`0001` / `0101`）；重启后 `roam.*` 通知和 `renew-acd` 都到线上；
  第 39 轮三次重启每次都有 Renew 对（守护进程收到子节点第一帧后 3.0 s）、M1 ×2、M2 ×2，序列与原厂一致。
- 限制：新守护进程要收到子节点的下一帧才会往外发、才登记接口（原厂启动后 1 到 2 秒内自己发 TopoDiscovery），所以重新入网和第一对 Renew 都跟着子节点的周期帧（:00.8/:30.8）走。`version_mode`、增减槽位、槽位 `enabled` 仍要重启服务。

## 设备角色与 CurHop（2026-10-17，GAP P1 第 7、8 项，真实子节点 Z506 验证）

原厂"自适应"= `MultiAPCfg.Mode 0` + `SetRoleEnable 1`（`Mode`：0 自动、1 Slave、2 Master、3 主从合一）→ 守护进程重启；cspd 的 `cooperator_mgr` 判定路由模式 → ACD 的 `_autoSetRole(2)` 写 `Mode 2`、`SetRoleEnable 1`，
再 `ASEND(0xa93d)` 要求第二次重启（约 3 秒后）。细节见 FEATURE_MAP 第 10 节的补充和 REPLAY_RESULTS 第 13 节。

服务里：
- 可选键 `mesh.role=master|auto|slave`。启动：`master`/`auto` 写 `Mode 2` 和 `SetRoleEnable`（0/1）；`slave` 写
  `Mode 1 / SetRoleEnable 0 / EnMasterOnly 0 / CurHop -2`（子路由），并在运行时把主控下发的 Wi-Fi 配置发布成
  `/run/zte-mesh/wlan-plan`（`agent-sync.c`），由沙箱外的 `wlan-sync` 落到 `/etc/config/wireless`。键的出现/消失需重启服务。
- 同一个 plan 还有第二个生产者：**主路由**角色（`mesh_role=main`，见下一节）。它自己是主控、没有 M2 可等，所以
  `write_config` 用刚校验过的档案直接写这个文件（`write_local_plan`），格式与 `agent-sync.c` 完全相同；`wlan-sync`
  不关心是谁写的。一个角色只有一个生产者，两者不会写同一个文件：子路由由服务写，主路由由 init 写，仅主控不写（还会删掉旧文件）。
- 热更新 `master→auto`：写 `SetRoleEnable 1`、`Mode 0`，重启守护进程；约 3 秒后写 `Mode 2`，再重启一次。**第二步是监督进程替 cspd 的 `cooperator_mgr` 做的模拟**（守护进程自己不会把 Mode 写回 2：实验室里 `Mode 0` 停了 12 秒以上）。
  `auto→master`：只清 `SetRoleEnable`，不重启。日志 `role_update`、`role_settle`、`cfg mode=… set_role_enable=… en_master_only=… cur_hop=… max_hop=…`（变化时打印）。
- `CurHop` 恒为 2：`EnMasterOnly=1` 让原厂会把它重置成 -2 的 `slave_init` 跳过；线上 `HigherLayerResponse` 子 TLV `0x43` 与原厂一致（原厂的 -2 是库里几秒的瞬态，采样的线上帧里没看到）。
- 没做：Slave、桥接模式判断、ACD 自己的 1905 选举协议。

## UCI 与 LuCI 映射（2026-10-17）

`openwrt/zte-mesh/files/zte-mesh.init` 的 `write_config` 把 `/etc/config/zte_mesh` 翻译成服务配置（`/run/zte-mesh/config`，0600，原子替换）；
`openwrt/luci-app-zte-mesh/.../zte-mesh.js` 是对应的表单（主区段 3 个标签页 + 两个列表区段，中文翻译在 `po/zh_Hans`）。init 只为让日志说出是哪个选项不对而做检查，
服务自己仍会再校验整份文件；任何一项不合法都不启动/不替换配置（日志 `zte-mesh: <选项> ...; not starting`，不记录 SSID 和密钥的值）。

主区段 `config zte_mesh 'main'`（没写的选项不产生对应的服务键，路由器保持固定默认值）：

| UCI 选项 | 服务键 | 说明 |
|---|---|---|
| `enabled`、`bridge` | `version_mode` 固定 128 | `VersionMode` 是 SR1010 原厂值 128（Z506 自己的值是 0，子节点应用 M2 时跟随控制器的值）；原厂界面没有这个设置。以前的 `mode` 选项（`standard` 2、`zte` 128、`zte_compat` 129）已删除，写了也被忽略；129（bit0）从没在真子节点上验证过，2026-10-19 用户的配置用它时子节点关掉了 Wi-Fi |
| `wifi_24g_enabled` / `wifi_5g_enabled`、`wifi_24g_ssid` / `_key` | `radio0.enabled/ssid/key`、`radio1.enabled` | 2.4G 名称和密钥必填；布尔值接受 1/yes/on/true/enabled 和 0/no/off/false/disabled；`enabled=0` 会让子节点这个频段的主 SSID 真的关掉 |
| `wifi_dual_band_sync` | `wifi.dual_band_sync` | 1：5G 用 2.4G 的名称、密码、安全方式、隐藏设置和 MAC 过滤策略，`wifi_5g_ssid/key/security/hidden/acl_policy` 被忽略（不写 `radio1.ssid/key`，`radio1.beacon_type/hidden/acl_policy` 取 2.4G 的值），LuCI 里这几项也不显示；0 或不写：见下 |
| `wifi_5g_ssid`、`wifi_5g_key` | `radio1.ssid/key`（仅在 sync 关时） | 都可以留空：5G 名称 = 2.4G 名称 + `wifi_5g_suffix`（原厂规则，REPLAY_RESULTS 第 15.1 节），5G 密钥 = 2.4G 密钥 |
| `wifi_5g_suffix` | `wifi.essid_suffix` | 1 到 8 个可打印字符，默认 `-5G`；派生名超过 31 字节会被拒绝 |
| `wifi_24g_hidden` / `wifi_5g_hidden` | `radioN.hidden` | |
| `wifi_24g_security` / `wifi_5g_security` | `radioN.beacon_type` | `none`=0、`psk2`=3、`psk-mixed`=6、`sae`=10、`sae-mixed`=12；`none` 会让真实网络变成开放网络 |
| `wifi_*_beacon_interval`、`_channel`、`_tx_power`、`_acl_policy` | `radioN.beacon_interval/channel/tx_power/acl_policy` | 范围同 REAL_CHILD.md 第 3 节（信道按频段白名单，0 = 自动） |
| `mesh_v2`、`mesh_role`（`master`/`main`/`slave`，旧的 `auto` = `main`）、`mlo_enable` | `mesh.v2`、`mesh.role`、`mlo.enable` | `mesh_v2` 是 LuCI 的"自组网"开关，即原厂的 MESH 开关（`P1905ComCfg.Enable`）：开 = 中兴增强模式，只接受市场码相同的中兴子路由；关 = 中兴兼容模式，接受任何子路由（默认）。`mesh_role` 三角色见下 |
| `band_steer`、`band_steer_rssi_24g/_5g`、`roam_rssi_24g/_5g`、`roam_force` | `band_steer.*`、`roam.*` | 两个频段的阈值必须成对给出 |

### 设备角色（三个）

原厂那台"主路由"是既当主控、又自己发 Wi-Fi 的盒子；但 SR1010 这类刷 OpenWrt 的机器大多是**有线网关、没有无线**，
所以把原厂的一个开关拆成三个角色（2026-10-31）：

| `mesh_role` | 给子路由下发档案 | 应用到本机 `/etc/config/wireless` | 写 `mesh.role` |
|---|---|---|---|
| `master`（默认；未设置也等于它） | 是 | **不动** | `master` |
| `main` | 是 | **是**（plan 由 `write_config` 写） | `auto` |
| `slave` | 否 | 是（plan 由服务从 M2 读出来写） | `slave` |

`main` 就是原厂的"主路由 / 自适应"，所以线上仍然发 `auto`：守护进程一侧一行没改，`Mode 0 → 2` 的选举替身照旧跑。
旧的 `auto` 值继续被接受、含义与 `main` 完全相同（升级后不用改配置）。仅主控会删掉
`/run/zte-mesh/wlan-plan`、**不启动** `wlan-sync` 实例，万一它还有射频，启动日志里会明说"本机无线不动"。

包里的默认配置（2026-10-19 起）把能取出厂值的选项都写上，LuCI 也显示同样的默认值，并在每项下面写明范围：`mesh_v2=0`（自组网关）、
`mlo_enable=1`、`wifi_dual_band_sync=1`、`wifi_5g_suffix=-5G`、`wifi_24g_ssid=ImmortalWrt`、安全方式 2.4G `sae-mixed` / 5G `psk-mixed`、
信标 100 ms、信道 0（自动）、功率 2.4G 20 dBm / 5G 23 dBm（子节点都取最大档，即出厂值）、`band_steer=1`、引导阈值 -60/-110、
漫游阈值 -60/-70、`roam_force=0`（出厂值见 `multiap_domain.def`）。`enabled=1`（默认启用）、`wifi_24g_key=Ztemesh2631!`（默认密码；清空则服务不启动）。
要让子路由保持自己的信道、功率，删掉对应选项（LuCI 里清空）。LuCI 列表区段的编辑框保存失败时会在框内写明原因（LuCI 自带的实现静默吞掉校验错误）。

`config zte_mesh_bss`（可有多段）= 额外 SSID 槽位：`band`（24g/5g）、`slot`（1 到 3）、`enabled`、`ssid`、`key` 必填，`hidden`、`security`、`acl_policy` 可选。
同一频段的槽位必须是 1..k（init 检查并写日志；服务也会拒绝，原因见 REPLAY_RESULTS 第 14.4 节）。
`config zte_mesh_timer`（可有多段，最多 8 条）= Wi-Fi 定时关闭：`start`、`end`（HH:MM）必填，`days`（空格分隔的 mon tue wed thu fri sat sun，默认全部）、`scope`（`all` 或 `guest`，默认 `all`）可选；init 把路由器当前的 `date +%z` 换算成 `timer.utc_offset` 写进服务配置（夏令时切换后要重新保存设置）。
槽位区段还有可选的 `access_minutes`（访客限时，分钟）、`force_wifi5`。`config zte_mesh_node`（`mac`、`name`）= 子路由器的别名，只给 LuCI 节点表用，不进服务配置。
`config zte_mesh_acl`（可有多段，最多 16 条，按文件顺序编号）= MAC 过滤：`mac`（单播、非全零）必填，`name`、`band`（24g/5g/both）、`bss`（0 主网络、1 到 3 槽位；槽位必须已配置）可选。

重载：`reload_service` 只在"需要重启服务"的东西变了才重启：`version_mode` 变了，或者策略键（`mesh.role`、`wifi.dual_band_sync`、`band_steer.*`、`roam.*`、`mlo.enable`）出现/消失
（init 把这些键的名字存进 `/run/zte-mesh/restart.sig` 来比较；值变了不重启，服务热更新）。

测试（都不需要 OpenWrt）：`tests/test_init.sh`（用 busybox ash，UCI 读取桩；47 条：旧配置逐字节不变、随包的配置模板、各选项、非法值、换行注入、槽位空档/重复、ACL 各种非法输入；已加进 `make test-host`）、
`node tests/test_luci.js`（用 LuCI form 的桩把表单跑一遍，核对每个 init 读取的选项都有表单项）、
`dev-config-check.py DIR`（把 init 生成的每份配置交给私有实验室里的真服务，报告接受/拒绝；`DUMP_DIR=DIR sh tests/test_init.sh` 先导出）。
行为变化（对已有配置的影响）：
- 随包的 `/etc/config/zte_mesh` 模板新增 `option wifi_dual_band_sync '1'`（原厂基线）。它是 conffile，已有的 `/etc/config/zte_mesh` 不会被改。**没有这一行、又留空 `wifi_5g_ssid` 的配置以前拒绝启动，现在会派生 `<2.4G 名称>-5G` 并启动，也就是会改子节点真实的 5G 名称。**
- `radio1.ssid` / `radio1.key`（服务配置）在双频合一关闭时变成可选；init 的 5G 名称/密钥也不再必填。
- init 把布尔值归一化成 0/1（接受 yes/on/true/enabled 等），不可打印字符被 init 自己拒绝（以前只拒绝换行）。
- `reload` 在策略键出现/消失时重启服务（`restart.sig`）；值变化仍然热更新。
- 服务配置里带槽位的 ACL 规则和槽位策略以前被整份拒绝，现在接受（见下）。
- trace 库多了 `device_event` 日志行（0xa937/0xa938 的目的地，独立的 48 条预算）。

这条链找到并修好了一个老 bug：服务配置里 `radioN.bssK.acl_policy` 和 `acl.N.bss>0` 以前总被整份拒绝（检查在槽位表填好之前执行）；修好后实验室里 `acl_update rules=… status=0`，
**没有在真子节点上试过槽位上的 ACL**。

## 节点管理、拓扑摘要、定时与访客限时（2026-10-18，GAP P2，REPLAY_RESULTS 第 16 节）

- **节点动作**：宿主往 `/run/zte-mesh/actions/<id>.act`（0600，文件名只用字母数字 `-` `_`）写一行 `reboot <mac>` 或 `light <mac> 0|1`；服务每秒取走（读完即删，服务启动时先清掉遗留的请求），只对在线且已识别为中兴设备的节点执行（`topology_ready` 之后），结果进 `state.json` 的 `actions`（最新在前，最多 8 条：`id`、`action`、`mac`、`value`、`status`、`epoch`；`status` 0 已发出、-2 格式错、-3 无此在线中兴节点、-4 守护进程没就绪、其它 = helper 状态）。rpcd 方法 `luci.zte-mesh reboot_node {mac}`、`light_node {mac,on}` 写请求并最多等 8 秒取结果（-5 = 服务没运行）。线上格式见 FEATURE_MAP 第 17 节。
- **`state.json` 的节点**多了 `light`（0/1，-1 未知）、`light_age`（秒）、`stations`（终端数，-1 未知）；`sched`：`rules`、`off_all`、`off_guest`、`access`（各访客倒计时的剩余秒数和是否已到期）。`light`/`stations` 来自 trace 库对子节点帧的摘要（`/run/zte-mesh/peers/<AL MAC>.light|.stations`，服务启动时清空）。
- **调度器和访客限时**：见 REAL_CHILD.md 第 3 节的 `timer.*`、`radioN.bssK.access_minutes`。配置里的 `enabled` 是"用户的设置"，有效值 = 设置 减去 定时窗口和已到期的限时；日志 `sched off_all=… off_guest=…`、`access_expired`。
- **守护进程每次启动都按"重启"计**（`ZTE_MESH_START_COUNT`），所以服务单独重启时已入网的子节点会在约 40 到 60 秒内重新入网并被识别（原因见 REPLAY_RESULTS 16.1）。
- `MAX_CONFIG` = 8192 字节。
- 测试：`tests/test_rpcd.sh`（15 条，rpcd 插件）、`dev-sched-check.py`（实验室，真实时钟约 5 分钟：定时窗口和访客限时）；`tests/test_init.sh` 已增到 64 条。

## 未验证 / 已知缺口

- **OpenWrt 包**（2026-10-18）：`zte-mesh`、`luci-app-zte-mesh`、`luci-i18n-zte-mesh-zh-cn` 已装在测试路由器上（默认 `enabled=0`）。
  在真实 procd/rpcd 上验证过：空配置时 init 拒绝启动并说明原因；挂在一个空的隔离网桥上启动；`reload_config` 热更新漫游阈值，守护进程 PID 不变；
  rpcd `status`/`restart`/`reboot_node`/`light_node`；配置不合法时重载会停掉服务；停止后进程、`zmesh0` 和运行文件都清干净。
  **没做**：用已安装的包接真实子节点（真子节点测试一直用 tmpfs 运行包，程序相同）；LuCI 页面在浏览器里的效果。
- 关主 SSID 的路径（`radioN.enabled=0`、主 SSID 隐藏的热更新、定时 `scope=all`）没在真子节点上试过（会断开用户的 Wi-Fi）。
- 拓扑页：回程 RSSI、树状图、每个 AP 下的终端列表、软件版本字段没有做；终端数没有独立核对（GAP P2 #10）。
- 访客限速：1905 里没有字段，主控推不下去（GAP P2 #12）。
- 长时间稳定性、多个子节点、子节点开着自组网 V2 而控制器关的组合。
- 原厂为什么不发 `BeaconMetricsQuery`（REPLAY_RESULTS 15.2）。

## 复现

```sh
sh build.sh                                        # mac：完整构建和主机测试
python3 device-controller-smoke.py                 # 实验室：真实 multiapd 在私有命名空间里初始化（含 CMDU 10）
sh tools/build/runtime.sh svctest                  # tmpfs 运行包（启动器根 /tmp/zte-mesh-svc/root）
python3 dev-service-smoke.py --no-attach           # 完全私有地跑服务（含 mesh.v2 重启步骤）；接 LAN 需 --expect-macs
```

x86_64 主机可用 `Dockerfile.cross`（`make ROUTERCC=aarch64-linux-gnu-gcc`）交叉构建。
