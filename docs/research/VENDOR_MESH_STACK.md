# 中兴 E2631（AX3000）的 mesh 栈：结构、能力面、与 SR1010 的差异（2026-11-01）

一台开了 telnet 的市售 ZTE AX3000（`/tmp/sysinfo/model` = `E2631`，hostname `Z500_w9bZ`）把
`/kmodule` 整个拿出来做静态比对。和上一份编程器固件的关系见第 7 节：同型号、同芯片，
**但不是同一版固件，那台的 rootfs 密文在这里也解不开**；这里拿到的是另一件事——
**原厂 mesh 栈的用户态实现，明文**。

## 1. 样本与取法

| 项 | 值 |
|---|---|
| 设备 | ZTE AX3000，`E2631`，`Z500_w9bZ` |
| 内核 | `Linux 4.1.25 #2 SMP Wed Aug 26 13:36:12 CST 2026`，`armv7l` |
| libc | `uClibc-0.9.33.2`（不是 glibc） |
| 根文件系统 | `/dev/mtdblock8`，**JFFS2 明文**，17 MiB，**100% 占满** |
| mesh 进程 | `cspd`，PID 597，`/kmodule/bin/cspd`，3,045,829 B |
| 库所在 | `/kmodule` = `/dev/loop0` 挂载的 12.6 MiB 镜像 |

取出来的是 22 个文件（`cspd` + 6 个 mesh 库 + 15 个支撑库），走 tftp 上传到实验机：

```
meshstack.tgz  sha256 f6508f456af1655ff5aae15629c360f259b225838dae3f53a933d3c6cdb86265
  bin/cspd                  806e29f505f18bf330a99f7c63530dcd111c323a36bfff04e03157a536806b12
  lib/libcmapi.so           e0363dfac365880350731f20f9effac0fa2587417120caa1af5db857b4c4f18f
  lib/libmultiapcmapi.so    17c08515a3b6a35fa3e3b4102c1dad89524e4821bb4bc858b04d53b2966047f4
  lib/libieee1905cfapi.so   8d532eb20145704dc20fb442f94afd22fc55753d537ab7e20a508d0b63c2ec87
```

对照样本是 SR1010 的原厂库（`out/runtime/lib/`，AArch64 + glibc）。**两边同名库成套存在**，
所以可以做跨机型 diff，这是这次最重要的收获。

## 2. 运行结构：cspd 是唯一的 mesh 进程

`/proc/597/maps` 给出的完整依赖链，按层看：

| 库 | 大小 | 层 |
|---|---|---|
| `libcmapi.so` | 1,617,296 | 配置管理层（`Cm*`，整个设备配置 API，不只有 mesh） |
| `libcfapi.so` | 539,802 | 控制面（`Cf*`） |
| `libmultiapcmapi.so` / `libmultiapcfapi.so` | 58,965 / 17,900 | Multi-AP 的 CM / CF |
| `libieee1905cmapi.so` / `libieee1905cfapi.so` | 47,800 / 25,657 | 1905.1 的 CM / CF |

另外两个映射暴露了通道形状：`/SYSV0000ffff`、`/SYSV0000abab` 两个 **System V 共享内存段**，加 `/dev/mem`。
配合上一份固件里 vmlinux 的符号（`wlan_cspkernel_ops`、`get_wlan_cspkernel_param`），
可以确定：**cspd 与 Wi-Fi 驱动之间走私有共享内存，1905 帧由驱动收发，不走 socket、不走 netlink**。

模块划分从日志 view 名和残留源文件名直接读出来：

```
cspd.cspd.multiapd_mgr          总控
multiapd.topo.topo              IEEE 1905.1 拓扑
multiapd.acd.acd                AP Autoconfiguration
multiapd.map.master             Multi-AP 角色/配置
source/ieee1905_cfapi.c
source/multiap_sub.c
```

## 3. 三层 API 与内部总线

这些 CF/CM 库**自己不往线上发帧**：它们写配置库（`dbAPI{Add,Get,Qry,Set}View`，视图名如
`MultiAPCfg`、`MultiAPRoamCatonCfg`、`MultiAPCacheMac`、`MultiAPStaCfg`），
然后用 `ASEND`/`SSEND` 把消息投进 cspd 的私有总线。逐个函数反汇编出来的消息号：

| 消息号 | 入口 | 含义 |
|---|---|---|
| `0x9095` | `_Ieee1905RoleSet` | 写角色 |
| `0x909b` / `0x909e` | `CfIeee1905RoleElect` / `...ElectStop` | 选举开始/停止 |
| `0x90a3`–`0x90a7` | `…TransParam` 系列 | 隧道参数注册/响应/续约/注销 |
| `0x90aa` | `CfIeee1905ActiveSendTopologyQuery` | **主动发拓扑查询** |
| `0x90d3` | `CfIeee1905DevSearch` | **发 AP Autoconfig Search** |
| `0xa932` `/ 0xa93c` `/ 0xa93d` | `CmMultiAPSetMultiAPCfg` / `CfMultiapSetCfg` / `SetMasterCfg` | Multi-AP 配置整包 |
| `0xa94e` | `CfMultiAPSetVlanCfg` | VLAN 配置（载荷 300 B） |
| `0xa96a` | `CmMultiAP{Add,Del}CacheMac`、`SetRoamCatonCfg` | 缓存 MAC / 漫游 |
| `0xa971` / `0xa972` | `…BsFilter` / `…RoamFilter` 增删 | 引导/漫游过滤表（载荷 788 B） |
| `0xa974` | `CmMultiAPSetBsPolicies` | 引导策略（载荷 1056 B） |
| `0xa97f` / `0xa980` | `CmMultiAPSendCAC` / `ChannelScan` | CAC / 信道扫描 |
| `0x9c8a` | `CmMultiAPWlanRenew` | 触发 Renew |
| `0x909a` `0x90a2` `0x90d6` `0x9627` | 同步 `SSEND`，超时 6000/5000 ms | Hop 信息、隧道参数、公共设备发现、上行口 |

和 [FINDINGS](../FINDINGS.md) 第 4 节已记录的 `SSEND(0x2401, 0x10205/0x10206)` 对得上：
**`0x2401` 是 wlan 模块号（本地无线命令），`0x9xxx`/`0xa9xx` 才是 cspd 的 mesh 控制面**。
`CmMultiApSetStaBind` 走的就是 `0x2401`——它是本地绑定，不是 1905 下发。

> 这一条同时是 [MESH_INTEROP](MESH_INTEROP.md) 硬约束 1 的第三个独立证据：
> 厂商自己在这个进程里也是「改状态 → 由 cspd 决定发不发帧」，没有任何 API 能绕过它直接发 CMDU。

## 4. 和 SR1010 的能力面差异

两边 mesh 相关导出各 91 / 87 个，**名字对得上的只有 30 个**。所以不能按名字比，只能按语义比。

**拓扑读取**：E2631 拆得细，SR1010 是整包：

| E2631（只有它有） | SR1010（只有它有） |
|---|---|
| `CmMultiApTopoGet1905Dev` / `List1905DevMac` | `CmMultiApTopoAllDev` |
| `CmMultiApTopoList1905Neighbor` / `List1905NborMetric` | `CmMultiApTopoGetInterfaceInfo` / `SetInterfaceInfo` |
| `CmMultiApTopoGetLinkMetric` | `CmMultiApTopoListChangeLog` |
| `CmMultiApTopoGet1905IPv4Addr` | `CmMultiApTopoGetRadioInfo` |
| `CmMultiApTopoGetInterfaceLinkInfo` / `ListInterfaceLink` | `CmMultiApTopoDelOffLineDevFromDB` / `ListOffLineDevFromDB` |
| `CmMultiApTopoGetP1905ComCfg` / `GetAccessDev` / `ListAccessDevInfo` | `CmMultiApTopoSetEnable` |

**漫游与引导**（这是本次最有用的一组）：

| 能力 | E2631 | SR1010 |
|---|---|---|
| BTM 请求 | `CmMultiApSetStaBTMRequest`（172 B）、`CmMultiAP{Get,Set}BtmReqModeCfg` | 无（只有 `CmMultiApSetStaDisassociate`） |
| 漫游配置 | `CmMultiAP{Get,Set}RoamCfg`（**1728 B，本库最大函数**）、`SetRoamCatonCfg` | `CmMultiAP{Get,Set}RoamCfg` |
| 引导/漫游过滤表 | `Add/Set/Get/List…BsFilter`、`ListRoamFilter` | `Del…BsFilterList`、`DelRoamFilter` |
| 引导策略 | `CmMultiAPSetBsPolicies` | 无 |
| 引导历史 | `GetSTASteerHistoryInfo`、`CmMultiRoamHistoryLog`、`…ForDbus` | `GetSTASteerHistoryList`、`GetSteerStats` |
| STA 绑定 | `CmMultiApSetStaBind`（走 `0x2401`） | 无 |
| VLAN | `CfMultiAPSetVlanCfg`（152 B，`0xa94e`） | 无 |
| 信道扫描 | 无 | `CmMultiAPChannelScan` |

`cspd` 本体（未 strip 的 3 MB）里还有完整的**接收侧**字符串，这比库更能说明「能不能被控制」：

```
EV_WLAN_MULTIAP_STEERING_REQUEST      handle_wlan_multiap_steering_req
EV_WLAN_MULTIAP_BACKHAUL_STEER_REQUEST handle_wlan_multiap_backhaul_steer_req
EV_WLAN_MULTIAP_ASSOC_CTRL_REQUEST    handle_wlan_multiap_assoc_ctrl_req
wnm_send_btm_req_raw / wnm_btm_onoff / EnBtmKickoff / LazyBtmFailedLmt
/proc/wlan_cfg/map/btm_kick          /proc/wlan_cfg/map/btm_cache_en
En11rSteer EnAgentSteer EnAgileSteer EnBandSteer EnBhSteerCheck EnDevSteer EnStaBandSteer EnSteerCurBand EnSteerNo11k
EnVbss ToVbssMode
```

也就是说 E2631 的 agent 侧**有 Steering Request / Backhaul Steering / Association Control 的接收
和处理路径，也有真正的 WNM BTM 实现**（`wnm_send_btm_req_raw`），并且 VBSS 有配置位。

## 5. 对 `FEATURE_STATUS` D 段的修订

D 段原来整段写「没有通道的（换子路由固件才谈得上）」，依据是 **Z506** 的固件。
现在有了 E2631 的证据，按证据强度分三级重写：

| # | 原结论 | 新证据 | 修订 | 强度 |
|---|---|---|---|---|
| D4 VBSS | Z506 无 | `cspd` 有 `EnVbss`/`ToVbssMode`，但**无任何 `Cm*`/`Cf*` 导出** | 配置位存在，**无下发入口** | 中 |
| D5 VLAN / 流量分离 | Z506 无 | `CfMultiAPSetVlanCfg`，152 B 实函数，`ASEND(0xa94e, ptr, 300)` | **有下发入口**，是否落成 CMDU 待实测 | 中 |
| D6 STA 绑定 | Z506 无 | `CmMultiApSetStaBind`，走 `0x2401`（wlan 本地命令） | 有入口，但**不是 1905 通道** | 中 |
| D7 802.11r / BTM 踢人 | Z506 不支持 FT | `EV_WLAN_MULTIAP_STEERING_REQUEST` + `handle_wlan_multiap_steering_req` + `wnm_send_btm_req_raw` + `En11rSteer` | **大概率真的能踢**，缺实测 | **强**（接收侧处理路径齐全） |
| D1 带宽 20/40/80/160 | Z506 不写 `BandWidth` | 未找到新证据 | 不变 | — |
| D2 访客限速 | 1905 无此字段 | 未找到新证据 | 不变 | — |
| D3 信道扫描 / CAC | Z506 无接收代码 | E2631 有 `SendCAC`/`ChannelScan` 的**发送**入口，接收侧未查 | 可能可做 | 弱 |

证据强度三级：**强** = 有接收/处理路径的字符串或反汇编；**中** = 有导出函数且函数体非空壳；
**弱** = 只有配置位或单侧入口。

## 6. 命名差异不等于实现差异

同一个功能在两套库里可以完全不同的名字：

| 功能 | E2631 | SR1010 |
|---|---|---|
| ACL 是否来自 1905 slave | `CmWLANSetACLIsFrom1905Slave` | `CmWLANSetAclList_1905_Slave` |
| 取 hop | `CmMultiAPGetHop` / `CfMultiapGetCurHop` | `CmMultiAPGetHop` |
| FTTR 主→从 | `FTTR_ASEND_Master_to_Slave` | `CM_FTTR_Set_Slave_Link_Status` |

所以「SR1010 有、E2631 没有」不能直接读成「E2631 不支持」——只说明**这一版没导出这个包装**。
反过来也一样。真正能定性的是 `cspd` 里的处理路径（第 4 节那组字符串）。

## 7. 和那份编程器固件的关系

同一颗 128 MiB NAND、同一 SoC 家族，前 7 MiB 分区边界逐项吻合
（Bootloader 1M / tag 1M / wifi 1M / usercfg 2M / defcfg 2M → kernel 从 `0x700000` 起、大小 `0x340000`），
但**不是同一版固件**：

| | 编程器 dump | 这台活机器 |
|---|---|---|
| 内核 | 4.1.25，**2024-09-25** | 4.1.25，**2026-08-26** |
| 根分区 | UBI/UBIFS（`ubi.mtd=5`），AES-ECB 密文 | JFFS2 明文 |
| kernel 槽位 | 单份 | `kernel1`+`kernel2` 双份 |
| 内存 | DT 写 128 MB | `mem=256M` |

dump 里那个 22.25 MiB 的密文 rootfs 在这台上**依然解不开**（密钥运行时派生，跨固件版本不同），
但**已经不需要**：这台给的是明文用户态，而且是更新的一版。

## 8. 待验证（按性价比）

| 事项 | 为什么值得做 | 需要什么 |
|---|---|---|
| 把它拉成我们控制器的 slave，看 M2 是否被接受 | 第二个真机样本；E2631 的 `MarketSign`/`VersionMode` 可能和 Z506 的 `8000` 不同 | 改它 Wi-Fi（会断网），需停机窗口 |
| 抓 `0x893a` 看有没有 Steering / Assoc Control | 第 4 节的接收路径是否真被触发，只有线上证据算数 | static 编 `spark`（armv7l musl），限时开 promisc |
| 本地触发 BTM：`/proc/wlan_cfg/map/btm_kick` | 不用控制器就能验证 BTM 通路是否存在 | 只写一个 `/proc`，可回退 |
| `EnVbss`/`ToVbssMode` 在 DB 里属于哪个视图 | 判断 D4 到底是「没入口」还是「入口在别处」 | 只读查 DB |

## 9. 局限

- **没有反汇编 `cspd` 本体**（3 MB、已 strip）。第 4 节的结论来自导出符号、字符串和 6 个小库的反汇编。
- 「字符串存在」只证明代码路径编进去了，**不证明当前配置是开的**。
- 两台设备固件差近两年（Z506 是 Wi-Fi 5 老货，E2631 是 Wi-Fi 6），行为差异不能全归给协议版本。
- 只有 30 个符号同名，跨机型比对必须逐条人工判语义，机器 diff 只能用来**找候选**。
