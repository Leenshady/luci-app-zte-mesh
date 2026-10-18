# 逆向与真机发现汇总

本文汇总整个项目里已经查实的事实，按主题组织。操作流程见 [TESTING.md](TESTING.md)，配置语法与各功能的验证结果见
[REAL_CHILD.md](REAL_CHILD.md)，控制器能下发什么见 [CONTROLLER_CAPABILITIES.md](CONTROLLER_CAPABILITIES.md)。

可信度标记：**【真机】** 在测试子路由 ZXHN Z506（V1.0.0.2B2.8000）上抓包或读回验证过；**【代码】** 读到了反汇编，没在真机上跑；
**【推测】** 由证据推断，没有验证。没有标记的是定义或来源说明。

## 1. 总体原理

不重写 mesh 控制器，而是把原厂 ZTE 路由器上的 ARM32 `multiapd` 搬到 SR1010（ImmortalWrt，无 Wi-Fi，只做有线网关）上当控制器跑，
周围用一层"垫片"伪造它依赖的环境，再用一个配置文件间接指挥它：

```
/run/zte-mesh/config ──▶ controller-service ──▶ 私有 netns 里的原厂 multiapd ──IEEE 1905 (0x893a)──▶ 子路由 multiapd ──▶ cspd ──▶ 无线驱动
                              │  写数据库行 / 触发 Renew / 自己发 0x8006        ▲
                              └───────────────▶ 原厂共享内存数据库（表结构从二进制还原）◀── CM 接口替身 libzte_controller_profile.so
```

- 数据库：原厂把配置放在共享内存数据库（`libdb`）里。表结构（字段、类型、行数）是从 `cspd` 的建表函数反汇编还原的
  （`startup-schema.py`、`schema.py`、`profile_schema.py`），服务启动时把 SSID、密码、BSS、BeaconInterval、ACL 等写成行。
- CM 接口替身（`controller-profile.c`，190 行）只替换两个函数：`CmWLANGetAPConfig`、`CmWLANGetPsk`。原厂版本会走内部 WEP setter、给 `cspd`
  发 CSP/SSEND 消息、查无线驱动，SR1010 没有这些。替身从我们填的 `WLANCfg`/`WLANPSK` 行读，按 43 个偏移把字段搬进 2496 字节的 CM 输出，
  只接受 WPA2-PSK + AES，要求 `ZTE_MESH_DB_SANDBOX=1`。其余所有 CM 调用走的都是原厂真库（`CmWLANGetAclListValid`、`CmDevGetDevInfoConf` 等）。
- 不启动 `cspd`。守护进程需要的"路由器其余部分"由数据库行 + 替身 + 身份环境变量提供。
- 服务自己构造的唯一一类 1905 报文是信道选择请求（CMDU `0x8006`）；其余都是原厂守护进程自己发的。

## 2. 两份固件不是同一个构建

| | 子路由 Z506 `multiapd` | SR1010 `multiapd`（我们跑的控制器） |
|---|---|---|
| 大小 | 593 KB，1296 个函数 | 988 KB，2393 个函数 |
| ZTE vendor 策略 TLV 解析（漫游阈值、11r、BTM 踢人、VLAN、`P1905ComCfg` 同步、VBSS） | **没有** | 有 |
| STA 绑定、CAC、信道扫描、流量分离、Wi-Fi 7/MLO | 没有 | 有 |
| `P1905ComCfg` 表 | 3 个字段（`Enable`、`MarketConfEn`、`MarketSign`） | 8 个字段（见第 5 节） |

两份的 `libcmapi.so` 也不同（SHA256 不同）。M2 由 SR1010 一侧构造，所以控制器行为必须读 SR1010 自己的二进制，
子路由能不能收则必须读子路由自己的二进制。 【真机/代码】

## 3. 子路由的角色、选举与"组网"

### 3.1 字段
- `MultiAPCfg.Mode`：CM API 枚举 0 = 自适应、1 = Slave（副路由 Agent）、2 = Master（主路由 Controller）、3 = 两者。【代码】
- `MultiAPCfg.Enable`：MESH 配置总开关。`MultiAPCfg.SetRoleEnable`：自适应角色开关；网页上选"自适应"时前端把它置 1，选其它模式置 0
  （`internet_mesh_common_lua.lua`）。【代码】
- `MultiAPCfg.CurHop`、`MaxHop`：见第 7 节。`P1905_ACD.ACDRole`：ACD 的 Server/Client 角色。

### 3.2 自动角色选举（`source/acd/roleplay.c`）【代码】
- `acdGetRole`：`SetRoleEnable != 0` 且 `Mode == 0`（自适应）时日志 `SetRoleEnable TRUE`，ACD 角色是"未定"，不会开始自动配置。
- 选举：对每个接口发自己的 AP-Autoconfig Search（`_autoSendSearch`，日志 `auto search send retrytimes ...`），收到控制器的响应
  （`_autoCheckResponse`：支持频段、Supported Service 为控制器、消息号匹配）就 `_autoSetRole(1)`；超时没有邻居就当 Controller；
  多台自适应设备按 MAC 比较（日志 `acd auto elect by mac as Agent/Controller`）。
- `_autoSetRole(角色)` 写 `MultiAPCfg.Mode = 角色` 和 `SetRoleEnable = 1`，日志 `Role change to %d(1:SLAVE 2:MASTER)`，再通知 `cspd`
  （`ASEND` 事件 `0xa26d`，目标 `cspd.cspd.multiapd_mgr`）。
- 选举由事件触发（`0x909b` 开始、`0x909e` 停止、`0x9095` 直接设为控制器）。`multiapd` 里还有同名调试命令：
  `sendcmd multiapd.base.base roleplay`。

### 3.3 谁触发选举：`cspd` 里的两个调用点【代码】+ 真机读内存【真机】
`CfIeee1905RoleElect` 在 `cspd` 里只有两处调用：`MapCooperatorACDUpProc`（0xa86c0）和 `MapCooperatorV4AddrProc`（0xa8d28）；
停止在 `MapCooperatorV4AddrProc`（0xa8da8，`CfIeee1905RoleElectStop`）。全局标志 `g_TriggerSelectFlag`（`.bss` 0x203370）有两位：
- bit0（值 1）= br0 已获得 IPv4 地址（`V4AddrProc` 收到事件 1 时置位；事件 2 地址丢失时停止选举并清位）；
- bit1（值 2）= 收到 ACD 上线通知且当时 `Mode == 0`（`ACDUpProc`，且 `IsBridgeMode() == 1`）。
- **两位都为 1（值 3）时，后到的那个事件调用 `CfIeee1905RoleElect`。**

`ACDUpProc(mode)`：`mode==0` 且桥接模式 → 置 bit1、`CfIeee1905WanForward`，标志为 3 才选举；`mode==0` 且路由模式 → `AutoSetup==0` 时
`CfIeee1905RoleSetMaster`（自己当 Master），否则 `MapStartWanListen1905`；`mode!=0` → 仅 `AutoSetup==1` 时 `MapStartWanListen1905`。
`IsBridgeMode()` 看 `WANCPPP`/`WANCIP` 里有没有连接名为 `Bridge`；`IsAutoSetupEnable()` 是 `MapCooperatorCfg.AutoSetup`。

通知来源：`multiapd` 的 `AcdMain` 启动时调用一次 `AcdNotifyUp(Mode)`（事件 4），`cspd` 用 `RegACDNotify` 订阅；角色变化走事件 8（`AcdNotifyRoleChange`）。

本机实测：`IsBridgeMode()==1`（`WANCPPP.WANCName="Bridge"`），`MapCooperatorCfg.AutoSetup=0`（`WpsActiveDelayTime 90`、`Addr4EffectDelayTime 10`、
`EnRebootElect 0`），读 `cspd` 内存得 `g_TriggerSelectFlag=1`（bit0 有、bit1 缺，记录的 br0 地址是子路由自己）。

### 3.4 观察到的行为
- 手动设 Slave（`SetRoleEnable=0`）：选举没有发生，子路由照常入网；`cspd` 没收到角色变更通知。【真机】
- 改成自适应后（网页上改不会重启 `multiapd`）：**子路由一个 Search 都不发**，停在"角色未定"，所以"组网失败"。【真机】
- `sendcmd multiapd.base.base roleplay` 手动开始选举：子路由发 Search，我们的控制器回 Response，M1/M2 交换完成，`Mode` 0→1，
  `SetRoleEnable` 保持 1。说明我们的控制器能被选举接受，缺的只是触发。【真机】
- 用户把子路由设为"MESH 开、自组网 V2 关、自适应"并**重启**后，它自行完成选举成为 Slave，`CurHop=2`、`P1905DevNum=2`。【真机（用户操作）】
- 反复 `kill` 子路由 `multiapd` 之后，日志里每次启动都有 `SubScribPublish server 10022 already exist!`、`acdInit SubScribPublish multiapd ACD Service Fail`
  和 `ACD Notify event[4] to 0 clients`，计数 `multiapd restart unexpected 17 times`。ACD 上线通知发给了 0 个订阅者，`cspd` 收不到，bit1 永远置不上。【真机】
  **推测**：非正常重启留下了残留的发布登记（日志里没有出现过任何一次通知送达订阅者）。干净重启后选举能自行完成，与这个推测一致，但没有单独证明。
  **所以：不要再用 `kill multiapd` 触发重新入网。**

## 4. 入网准入链

控制器是原厂 `multiapd`，子路由的接纳逻辑是一串门槛，每一项都经反汇编确认并在真机上从失败走到成功（表见 REAL_CHILD.md 第 2 节）：
设备身份（厂商名 ZTE、型号 `ZXSLC SR1010`）、市场码（`P1905ComCfg.MarketConfEn=1`、`MarketSign=8000`）、`SSIDSyncRadio=1`
（有线控制器绕过 `bRadioStatusOn()`）、`P1905AcdBssCfg.CustomTag=68`（0x44 = bit2+bit6；2026-10-17 前是 69，bit0 的作用改由 CM 适配层补 `Enable` 字节代替，REPLAY_RESULTS 第 14 节）、`CmWLANGetAPConfig` 对槽位 0 到 3 都成功、
每 BSS 的 `MapAccessAttr`（主 BSS 32）和 `ACLPolicy`（主/访客 1，回程 0）。

报文层面的事实：
- 子路由只处理它已经认识的控制器发来的 Multi-AP 消息；在收到控制器的拓扑响应（Supported Service TLV = 控制器）并转 `DEV_ONLINE` 之前，
  早到的 `0x8001`/`0x8003`/`0x8004` 被丢弃（日志 `query message 0x... is not from controller[00:..] ignore`）。控制器只在启动后约 25 到 80 ms 内发一次，
  不会重发。控制器下线后子路由约 1 到 2 分钟才忘记它：这段时间内重启控制器，策略被接受。【真机】
- 向 `multiapd.map.master` 发域配置事件 `0x10206`，和工厂调试通道 `/var/tmp/<进程>_asy`（报文格式已还原，见 `zte-debugcmd.c`），
  都没有效果，没有可靠的"热更新策略"。【真机】**（2026-10-17 更正）有可靠的热更新：** 当时的发送方式没有记录（推测把 `0x10206` 当成了事件号）；
  正确的是原厂 CM 设置函数用的同步 `SSEND(0x2401, {u32 消息号, char name[32], u32 0}, "multiapd.map.master")`，消息号 `0x10206`（域/漫游阈值）、
  `0x10205`（主控策略），写库后发，子节点收到新的 `PolicyConfigReq` 并回 ACK。见 REPLAY_RESULTS（`mesh-vendor-capture`）第 11 节与 SERVICE.md。
- 控制器的 `AL MAC` 目前固定为 `02:00:00:00:19:06`（需要做成可配置/唯一）。

## 5. 自组网 V2（`P1905ComCfg`）

### 5.1 它是什么
- 子路由 MESH 页有两个开关：**MESH 配置开关**（`MultiAPCfg.Enable`）和 **自组网 V2**（`P1905ComCfg.Enable`，网页字段 `PEnable`，
  对象 `OBJ_P1905_COM_CFG_ID`；MESH 关闭时不显示也不提交）。**它和 `MultiAPCfg.VersionMode` 无关**：用户关掉 V2 后 `VersionMode` 仍是 0。
- 界面里另有一个"v1/v2"，是**拓扑页面的显示格式**（`MeshEnable=="1" && OpenAndSlave=="false"` 用 v2），与这个开关无关。
- 子路由 `P1905ComCfg` 当前值：`Enable=0`、`MarketConfEn=1`、`MarketSign="8000"`。（`cspd` 里同名的 `EnableP1905` 属于另一张表 `P1905Cfg`，没展开。）

### 5.2 Z506（旧固件）：只有接收方向的准入检查【代码】
`multiapd` 里读这张表的只有 `baseGetCommDBCfg`，值进入 `baseInterComm`（`source/base/ieee1905_intercommunication.c`）：
- `Enable==0`：直接放行，**不检查对方是谁**（包括非 ZTE 设备）。
- `Enable==1`：对还没通过的对端，要求消息带厂商 TLV（类型 `0x0b`）。没有 → 原因 3；厂商头无法识别，或厂商名与本机不同 → 原因 1；
  `MarketConfEn==1` 且市场码与本机 `MarketSign` 不同 → 原因 2；通过后记入"可互通设备"列表。被拒日志
  `The device[mac] is not allowed to MESH with the current device,reason [n]`。
- 发送方向没有按这个开关取舍：所有设备都会带自己的厂商身份 TLV。

### 5.3 SR（新固件）：一整套互通控制【代码】
`P1905ComCfg` 有 8 个字段：`Enable`、`EnableIsSync`、`Policy`、`DropNum`、`MarketConfEn`、`MarketSign`、`DropRoam`、`A1Enable`（偏移 36、68、100、132、164、196、292、324）。
- **`Policy`**：对未通过身份检查的对端——0 = 只记录不拦截；1 = 拒绝；2 = "限制模式"：放行，但 `DropNum≠0` 时把该设备 MAC 和限制标志经
  `ASEND`（事件 `0xa94d`，目标 `cspd.wlan_manage.wlan_mgr`）发给无线管理模块，并对其发来的 `0x8014`（Client Steering Request）按随机间隔丢弃
  （间隔上限由 `DropRoam` 决定，返回原因码 4）。通过检查的设备会发"解除限制"。
- **`EnableIsSync`**：主路由在策略配置报文里加厂商子 TLV（类型 `0x60`，2 字节 = `Enable`、`MarketConfEn`）；子节点只有自己的 `EnableIsSync` 也为 1
  才接收，并改写本机这两个值，再通知 `cspd`（`0xa93d`）。Z506 没有这个解析代码。
- **`A1Enable`**：WPS M1 里 `wps_set_model_name_m1` 给型号名追加 `(A1)`；控制器侧 `_eap_wps_get_model_name` 在 `A1Enable` 时要求对端型号名含 "A1"，
  不含就记日志并返回 -1（拒绝）。
- `DropRoam` 也被 `masterGetRandRoamNumMAX`/`mapCheckRandRoamEn` 读取（漫游随机计数，细节没读）。
- **没验证的**：`cspd` 收到限制事件后实际怎么限制、`DropNum` 的单位、`A1` 的含义。

### 5.4 组合与我们程序的默认值
每一方只按自己的开关检查对方，两个方向都通过才能组网：

| 控制器 | 子路由 | 结果 |
|---|---|---|
| 开 | 开 | 双向严格，需同为 ZTE 且市场码一致。【真机】项目早期整套准入门槛（身份、市场码……）就是在这个组合下从失败走到成功的（当时控制器 `Enable=1`，子路由开着 V2，据用户口述） |
| 开 | 关 | 只有控制器检查子路由（它是 ZTE/8000，通过）。【真机，`mesh.v2=1`】子路由在线，节点识别为 `ZXHN Z506`、`zte:true`，没有拒绝记录 |
| 关 | 开 | 只有子路由检查控制器：需要控制器带 ZTE 身份和相同市场码，控制器不检查子路由。【代码】没有在真机上单独验证 |
| 关 | 关 | 谁也不检查，任何 1905 设备都能接。【真机，默认配置】子路由在线，设备表里有控制器，节点识别为 `ZXHN Z506`、`zte:true` |

**我们程序的默认：自组网 V2 关闭**（服务配置 `mesh.v2=0`，写入控制器自己的 `P1905ComCfg.Enable=0`，启动日志 `p1905_v2=0`；`mesh.v2=1` 恢复严格检查；改动需重启服务）。
这只影响控制器**接收**时检查对方；控制器对外声明的身份（ZTE / `ZXSLC SR1010`）和市场码（`MarketConfEn=1`、`8000`）不变，
所以子路由开着 V2 时仍然接受我们。`EnableIsSync`、`Policy`、`DropNum`、`DropRoam`、`A1Enable` 保持默认 0。

## 6. `MultiAPCfg.VersionMode`：功能位掩码

不是 V2 开关。界面里没有入口（前端只有空的占位函数 `clickVersionModeBtn`）；默认值由 `cspd` 的 `dbSetDefVersionMode` 建表时写入，Z506 写的是 0。
从各调用点后的 `tst`/`ands` 立即数读出的位（位的名字按它控制的行为起）【代码】：

| 位 | 值 | 观察到的作用 |
|---|---|---|
| bit0 | 1 | 插件（plugin）BSS 配置（`acdInitPluginBssCfg`、`acdInitConfigBssCfg`、`acdcInitBssCfg`）；报文转发处理（`baseGetMsgForward`）；早期分析还记为"旧版，不带 Profile TLV" |
| bit1 | 2 | **置位 = 偏标准 Multi-AP，不发 ZTE 私有厂商 TLV；清零 = ZTE 私有行为。** 发送端 `I5TlvVendorTypeInsert`/`I5TlvVendorSpecificTypeInsert` 置位时直接不插入厂商 TLV；控制器 `master_dev_update` 置位时不主动发 AP 能力/信道偏好查询和综合度量；`CombinedInfrastructureMericsSend` 置位发标准 Tx/Rx 链路度量 TLV，清零发 ZTE 私有邻居表 TLV；`ChannelSelectResponseReceive` 置位时回 ACK；`acdsIsTeardown`、`acdDoPhyLinkMsg` 也读它 |
| bit3 | 8 | Multi-AP Profile-2：WSC M1 里带 Profile-2 AP 能力 TLV（`_acdcSendApAutoConfigWSCM1`）；物理链路上下线处理（早期分析把 bit3/bit4 记为 Profile-2/3） |
| bit7 | 0x80 | Wi-Fi 7 的 AP MLD / STA MLD 配置 TLV 编解码 |

- 协商：每台设备把自己的值放进 ACD 厂商 TLV（`I5TlvAcdVendorTypeInsert`）；子节点收到 M2 或控制器的 1905 报文时 `_acdcSaveCtrlVersionMode` 记下**控制器的值**，
  处理 M2 时 `acdGetCurVersionMode` 已知控制器的值就用控制器的，否则用本机的。即子节点应用配置时**跟随控制器**的模式。
- **对我们：** 控制器必须保持 `VersionMode=0`（bit1 清零），这样才会发 ZTE 私有厂商 TLV（抓包里有厂商号 `00:19:c6` 的 TLV `0x0b`），
  子路由开着 V2 时的身份检查正需要它；`CustomTag=68` 也是这条路径。实验室没有 ZTE 对端时用过 2。
- 没验证：在真子路由上换成别的值（按约定从未改过）；bit2、bit4 到 bit6 的含义。

## 7. CurHop（跳数）

- 控制器把自己的 `MultiAPCfg.CurHop` 原样放进厂商私有 TLV（子类型 `0x43`，大端 4 字节，在 Higher Layer Response 即 CMDU `0x000e` 的厂商 TLV 里），
  `CfMultiapGetCurHop` 读的就是数据库 `MultiAPCfg.CurHop`（偏移 260，失败返回 -2）。子节点收到后：有线接入直接采用，无线接入时减 1（≤0 则 -1）。【代码】
- 主路由的 `CurHop` 由 `slave_init` 设成 `MaxHop`；但 `multi_ap_slave_main` 在 `EnMasterOnly != 0` 时**跳过 `slave_init`**。我们的控制器
  `EnMasterOnly=1`（SR1010 没有无线电，必须如此），`CurHop` 因此停在数据库默认值 -2，子路由日志里是 `Get hop -2 from 02:00:00:00:19:06`。【真机】
- **修复：** `multiap-cfg.c` 启动时写 `CurHop=2`（等于 `MaxHop`）。**真机验证：** 子路由 `MultiAPCfg.CurHop` 变成 2。

## 8. 子路由 Web 的"受控后锁定"

锁只有一个来源，是子路由本地标志，与主路由在不在线无关：后端环境变量 `OpenAndSlave = (MultiAPCfg.Mode == "Slave" && MultiAPCfg.Enable == "1")`
（`/home/httpd/webmodules/config/globalenv_register.lua`，每次请求重新计算）。【代码】

**新版界面（Vue，`/home/httpd/public/jquery/static/js`）：**
- 主 Wi-Fi 配置（名称、密码、开关）：`isDisable = (OpenAndSlave != "false")`。
- 备用 Wi-Fi 配置：`isDisabled = (OpenAndSlave != "false")`。
- Wi-Fi 射频配置（信道、带宽）：`isSlaveMode = (Mode == "Slave")`；2.4G/5G 开关：`OpenAndSlave == "true"`。
- MAC 过滤页的模式单选：`disabled = OpenAndSlave`。

**旧版界面（`webmodules` 的 `.lp`）：** 高级 SSID 页输入框置灰；基本 SSID 页**只禁用"双频合一"复选框，名称和密码没有任何锁定代码**；
`dmenu_modifier.lua` 的 `filterSlave` 作用在 WAN、LAN IPv4/IPv6、DMZ、端口转发、DDNS、UPnP、高级 Wi-Fi 等菜单项。

真机观察：用户设"MESH 开 / V2 关 / 自适应"并重启后，主 Wi-Fi 的 2.4G/5G 开关、名称、密码都被禁止修改，与上面的规则一致。
**更早一次"主 Wi-Fi 能改、备用和射频被锁"无法由代码解释**（主配置和备用配置用同一个条件），可能是页面在状态变化前加载过，没有验证。
任何控制器都不可能直接让某个页面变只读：它只能让子路由成为 Slave。

## 9. ACL

- 同步方向：入网时子路由上报自己的 ACL，控制器回完整列表；子路由按列表**删除多余、新增缺失**（`CmSetAclCfg`，日志 `Del %d ACLs`）。控制器列表是权威的。
  请求 TLV：`0x1c` 频段号、`0x1d` SSID 数、`0x1b` ACL 数/上限；响应每条：`0x14` 接口、`0x11` 策略、`0x15` 名称、`0x16` MAC（6 字节）。【代码】
- 数据源：`AclCfg` 表（`ViewName`（主键）、`Name`、`Interface`、`MACAddress`、`ACLCfgPolicy`；268 字节、256 行；`cspd` 构造函数 `dbCreateAclCfgTbl`
  范围 0x1cd334 到 0x1cd41c）。控制器经 `CmWLANGetAclListValid` → `adevQueryAclCfgListByIf` 读，走数据库视图，不需要无线硬件。
- 策略值（`WLANCfg.ACLPolicy`，`cspd` 的 `rlk_wlan_do_mac_filter`）：0 = 关闭（驱动 `AccessPolicy=0`）、1 = 拦截名单内（驱动 `AccessPolicy=2`）、
  2 = 只放行名单内（驱动 `AccessPolicy=1`）。驱动是 Ralink 风格（`iwpriv ... set ACLAddEntry=` / `ACLClearAll=1` / `AccessPolicy=`），
  读回：`iwpriv wlan0 set ACLShowAll=1` 后看内核日志（需要带 `dmesg` 的 busybox）。【代码+真机】
- **Z506 的实际行为：** 每条规则不分接口，都会出现在 AP1、AP2、AP5、AP6 上（5G 主 SSID 上名称带 `_5G`）；策略变化会连带改 AP3（备用 SSID）。【真机】
- **拦截已验证：** 把一加手机当前的 MAC 加进 `ban` 名单，驱动 `wlan0` 显示 `Policy is 2 : ... rejected` 和该 MAC，站点立刻消失，用户确认"被拒绝接入"；
  删除规则后重新连上。**手机的私有 MAC 会变**：同一台一加用过 `32:83:90:d9:0f:62` 和 `42:be:ab:fd:c3:88`（DHCP 名都是 `OnePlus-12`），只封旧的无效。【真机】
- 没验证：`allow` 策略、5G 驱动读回。
- 槽位（本机子路由）：0 主 Wi-Fi，1 访客，2 备用 SSID，3 回程隐藏网络；AP 到接口：AP1 = `wlan0`，AP2 = `wlan1`，AP3 = `wlan2`，AP4 = `wlan3`，AP5 = `wlan5g0`，AP6 = `wlan5g1`（2.4G radio id `MAP24G`，5G `MAP5GL`）。

## 10. 入网时的频段配置、信道、发射功率、带宽

- **FreqBand vendor 消息（入网 vendor 阶段）只有三个子 TLV**：`0x07` BeaconInterval（每频段，毫秒，4 字节大端）、`0x19` MaxHop（`MultiAPCfg.MaxHop`）、`0x55` STA 绑定支持标志；
  **没有 Fragment、TxPower、带宽、信道**（早先"Fragment/TxPower"是把控制器的调试打印误当成了报文内容）。Z506 只处理 `0x07`、`0x19`。
  BeaconInterval 写入 `DEV.WIFI.RD<n>`（`CmWLANSetBaseConfig`）。【真机】
- **信道/功率**：标准 Multi-AP 信道选择请求 `0x8006`（TLV `0x8b` 信道偏好：运行类别 81/82（2.4G）或 115/118/121/124/125（5G）、一个信道、偏好 14；TLV `0x8d` 发射功率）。
  子路由 `ChannelSelectRequestReceive` → `slave_set_radio_cfg` 校验后写入信道、功率和无线电使能，复位射频；应答 `0x8007`（TLV `0x8e` 响应码 0）。
  服务进程自己构造这个报文（`controller-packet.c`，源地址是 `br0` 的 MAC，消息号从 `0xe000` 起）。写信道会同时写 `AutoChannelEnabled=0`；信道 0 只置 `AutoChannelEnabled=1`，
  所以还原要先发原信道再发 0。功率 dBm → 档位（`mutiap_power_convert`）：2.4G >19→1、16–19→2、12–15→3、8–11→4、其余 5；5G >21→1、20–21→2、16–19→3、10–15→4、其余 5。【真机】
- **带宽（20/40/80/160）没有下发路径**：M2 和 FreqBand 都不带；`0x8006` 里带宽只作校验不写入（日志 `165 channel invalid in 80M bandwidth`）。硬件：2.4G 为 MT7592（20/40），
  5G 为 MT7613（20/40/80），不支持 160。【代码】
- 控制器自己的路径 `master_set_radio_cfg`（事件 `0x10203`）需要代理的无线电清单，我们的 master 没有，所以服务直接发 `0x8006`。

## 11. 漫游阈值、频段引导、MLO

- **漫游阈值：** 配置键 `roam.*` 写 `MultiAPDomain` 行，控制器会发出 ZTE vendor 策略 TLV（RCPI = (RSSI+110)×2，抓包验证），子路由回 CMDU 层 ACK；
  **Z506 没有解析这个 TLV 的代码，不会生效**（`MultiAPDomain.RoamRssiLmt` 一直没变）。早先"子路由确认并生效"的说法是错的。
- **标准 Metric Reporting Policy（`0x8a`）会生效**：子路由 `rssithr` 读回随之变化。公开零默认值曾把 STA 上报阈值推成 RCPI 220，现在启动时设 -110，与子路由一致。【真机】
- **频段引导/双频合一**：`band_steer.*` 只写控制器自己的 `MultiAPMaster.EnBandSteer` 和 `MultiAPDomain.BsRssiLmt24G/5G`（不在任何发给子路由的 TLV 里）；
  `wifi.dual_band_sync` 只让控制器的 5G 配置跟随 2.4G，不写子路由自己的 `WLANSetting.DualBandSync`。实际引导行为没验证（需要有终端关联在子路由上）。
- **MLO**：SR 的 `multiapd` 有完整 Wi-Fi 7/MLO 代码；Z506（Wi-Fi 5）固件里一行都没有，无法验证。

## 12. 运行环境与 OSS/CM 事实

- 原厂 `multiapd` 要求的平台：私有 `/var/tmp`、`/var/run`、`/var/log`（它在 `/var/tmp/<task>_{asy,snd}` bind Unix socket）；128 KiB 订阅者池（`SubShmSrvInit`，key `0xabcb`）；
  `/dev/urandom`（旧 OpenSSL，只绑定这一个设备）；`CONFIG_COMPAT` 的 ARM32 原生执行。网络上在私有 netns 里：`veth-lab` 对端，LLC `0x0004` 与 1905 `0x893a` 的 packet socket。
- `CmMultiAPWlanRenew` 本来发给 `cspd`（`cspd.cspd.multiapd_mgr`，事件 `0x9c8a`）；没有 `cspd` 时辅助程序 `zte-meshctl renew-acd <band>` 直接向 `multiapd.acd.acd` 发事件 `0xa93e`。
  外部调用 CM API 的程序要用 `liboss_pub.so` + `InitAppComm`（像工厂 `diagget`），不能用守护进程自己的 `liboss.so`；在 supervisor 里调 CM API 会崩。
- 数据库启动视图（`startup-views.inc`，由 `startup-schema.py` 从 `cspd` 还原）：`P1905ComCfg`（328 字节/8 字段）、`IPInterface`（2404/48）、`P1905_ACD`（456/9）、
  `P1905AcdBssCfg`（264/5，8 行）、`AclCfg`（268/5，256 行）、`WLANWEP`（232/3）、`P1905DeviceInfo`（788/11，1024 行）。
- 控制器的 `MultiAPCfg` 夹具值（`multiap-cfg.c`）：`Mode=2`、`Enable=1`、`SyncEnable=1`、`SetRoleEnable=1`、`EnMasterOnly=1`、`CurHop=2`、`RetryTimes=4`、`MeshName=zte-shim-lab`、`IFLink=br-lan` 等；
  `VersionMode` 由服务配置给（默认 0）。
- 主控事件表（`multiapd` 数据段 0xe1520 一带）：`0x10203` 无线电配置、`0x10205` 主控配置、`0x10206` 域配置、`0x10208` BTM 模式、`0x10216` 无线电信息、`0x10226` STA BTM、`0x10227` STA 断开、`0x10238` 代理频段引导。
- `ACLPolicy` 回归：控制器曾不设它，默认报 0，子路由把原来的 1 改成 0。现在按子路由原值报告（主/访客 1，回程 0）。

## 13. 子路由（Z506）只读调试方法

在子路由 shell（telnet）：`sendcmd 1 DB all`（所有表）、`sendcmd 1 DB p <表>`（`MultiAPCfg`、`P1905ComCfg`、`WLANCfg`、`WLANBase`、`AclCfg`、`MapCooperatorCfg`、`WANCPPP` 等）、
`sendcmd multiapd.map.slave dev|status|bss|sta|neighbor`（`dev` 含各无线电的 `CurChannel`/`CurTxpower`/`RCPISteerThr`/`rssithr`）、
`sendcmd multiapd.<模块>.<模块> -l 8`（调试日志级别，重启后会重置，用 `-l 7` 还原）、`logctrl -r -g 1 -n`（跟日志，只给新行）、`iwconfig`/`iwpriv`。
`sendcmd` 的报文：AF_UNIX 数据报发到 `/var/tmp/<进程>_asy`，0x224 字节，`+0x24` 目标名，`+0x48` 魔数 `0x11102`，`+0x4c` 长度 `0x1c4`，`+0x60` 载荷。
子路由自带的工具只有 `grep tr cut sed awk pgrep wget tftp`；没有 `head tail od dd sort dmesg`。`/var/tmp` 里放的 busybox 重启即丢。
Web 代码在 `/home/httpd/webmodules`（Lua + `.lp`）和 `/home/httpd/public/jquery/static/js`（Vue 打包）。

## 14. 还没有查清的问题

1. 选举在"干净重启"下自行完成已有一次真机观察；上面"残留发布登记"的解释没有单独证明。
2. `Policy=2` 的限制在 `cspd` 一侧具体做什么；`DropNum` 单位；`A1` 的含义。
3. `VersionMode` 其余位；换值对真子路由的影响。
4. 更早一次"只锁备用和射频、不锁主 Wi-Fi"的原因。
5. 子路由 SSID 查询（索引 1 到 3）的真实应答（当前答"没有这个 SSID"，不影响 M2 路径）。
6. 重启、LED、steering 等消息的触发路径；长稳；控制器异常退出后的恢复；`AL MAC` 可配置。
7. 一次干净的"控制器关 V2 + 子路由开 V2"组合的真机验证（子路由当前关 V2；需要用户在子路由上打开开关）。
