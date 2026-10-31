# 中兴 mesh 功能清单、完成度与推进顺序

> **状态（2026-10-31）：** 主控侧在真子路由（ZXHN Z506，原厂固件）上验证；子路由侧在实验室与两台实验机上验证。
> 本表是唯一的功能总账：每完成一项就在"状态"列打勾并把证据补上，**不要**在别的文档里另起一份清单。
> 协议与逆向细节见 [CONTROLLER_CAPABILITIES](CONTROLLER_CAPABILITIES.md)、[REAL_CHILD](REAL_CHILD.md)、
> [research/AGENT_PROBE](research/AGENT_PROBE.md)；部署与测试见 [DEVELOPMENT](DEVELOPMENT.md)。

## 实验机

| 代号 | 机器 | 架构 / 系统 | 角色 |
|---|---|---|---|
| **VM** | `192.168.1.20`，VMware 里的 ImmortalWrt | x86_64，25.12.2 r38135，内核 6.12.103，**没有无线网卡** | `master`（测 `main` 时临时切） |
| **ax3600** | `192.168.1.1`，小米 AX3600 刷 ImmortalWrt（非中兴硬件） | aarch64 cortex-a53，同上 | `slave`（`radio1`=5G、`radio2`=2.4G、`radio0` 空卡） |

两台都在 `192.168.1.0/24`，root 密码 `root`，公钥已装；都没有 `tcpdump`（`apk add tcpdump-mini` 可装），
dropbear 没有 sftp（传文件用 `ssh A 'cat f' | ssh B 'cat > f'`）。

**当前轮次的产物**：`main` 上 run 41181993285，`zte-mesh-apk` 3.86 MB，拆包后四个包。**这一轮还没装上两台机器**——
下表"实验机"的证据来自上一次装机（拆包前三个包 + 手工补的 `ns-run`/`db-engine-probe`）；r10 装机验收待做。
装法：ax3600 装 `zte-mesh` + `zte-mesh-launcher`；VM 是 x86_64，只能装 `arch: all` 的 `zte-mesh`
（`launcher` 在它上面报 `uninstallable`，属预期）。都加 `--allow-untrusted`，`/etc/config/zte_mesh` 不会被覆盖。

## 图例

| 记号 | 含义 |
|---|---|
| ✅ | 已实现，**真机**验证过 |
| ☑️ | 已实现，只在实验室 / 测试桩上验证（未上真机，或对端固件不支持） |
| ⬜ | 未做 |
| ➖ | 做不到：对端没有接收代码或没有下发通道（不是我们没写） |

难度：**1** 改几行、**2** 一天内可验证、**3** 需要新逻辑或新测试面、**4** 需要真机驱动侧配合、**5** 需要逆向或换硬件。

## A. 主控（本机是主路由）

| # | 功能 | 状态 | 证据 | 难度 |
|---|---|---|---|---|
| A1 | 原厂 `multiapd` 在私有命名空间里跑（IPC/net/mount、veth 到 `br-lan`） | ✅ | `ns-run` + `attach`，真路由器 + 真 procd | — |
| A2 | 发现 / 拓扑 / 链路指标 / 高层数据（CMDU 0、1、2、3、5、6、7、13、14） | ✅ | REAL_CHILD §1 | — |
| A3 | AP-Autoconfig + WSC M1/M2（真 DH、HMAC、Key Wrap） | ✅ | 子路由 `config M2[0]` | — |
| A4 | 配置变更后 Renew（热更新，不重启服务） | ✅ | 改配置约 4 秒生效 | — |
| A5 | 主 SSID / 密码 / 加密 / 隐藏 | ✅ | 2.4G、5G 均读回 | — |
| A6 | 多 BSS（访客、备用、隐藏回程，槽位 1–3）名称 / 密码 / 开关 | ✅ | REAL_CHILD §1 | — |
| A7 | BeaconInterval（含热更新） | ✅ | 120 → 读回 120 | — |
| A8 | ACL 名单 + 每 BSS 策略（关 / 拦 / 只放行） | ✅ | 一加手机被踢并拒绝重连；`allow` 与 5G 驱动读回未验证 | — |
| A9 | 信道与发射功率（`0x8006`） | ✅ | 两个频段验证并还原 | — |
| A10 | 重启子节点 / 指示灯开关 | ✅ | REPLAY_RESULTS 16.1 | — |
| A11 | 访客限时、Wi-Fi 定时关闭 | ✅ | `scope=guest` 真机；`scope=all` 仅实验室 | — |
| A12 | 自组网 V2（`mesh.v2`）与市场码门槛 | ✅ | 两种取值都正常入网 | — |
| A13 | 控制器角色 `master` / `auto`（含自适应选举替身） | ✅ | 第 40 轮 | — |
| A14 | MLO 开关下发（`0200fe`） | ☑️ | 线上帧逐字节一致；Z506 无该字段，等于无操作 | — |
| A15 | Wi-Fi 5 兼容标志（`ForceWiFi5`） | ☑️ | 线上值正确；Z506 不认 | — |
| A16 | 漫游 / 频段引导阈值下发 | ☑️ | 能发；Z506 固件没有解析代码 | — |
| A17 | 引导（BTM）/ 封锁 / 踢终端 | ⬜ | `0x8014` 等，接口已在 libcmapi | 3 |
| A18 | 拓扑页：回程 RSSI、树状图、每 AP 终端列表、固件版本 | ⬜ | 现在只有节点清单与动作 | 3 |
| A19 | 服务配置校验：`zte-mesh-timer` / ACL / 槽位连续性的错误定位 | ☑️ | init 79 项 | — |
| A20 | **界面中英双语**：文案全走 `_()`，`po/zh_Hans` 与源码逐条对齐 | ✅ | i18n 12 项；两台随包装出 `zte-mesh.zh-cn.lmo`（11632 B），`/cgi-bin/luci/admin/translations/zh-cn` 返回中文 | — |

## B. 子路由（本机是子节点）

| # | 功能 | 状态 | 证据 | 难度 |
|---|---|---|---|---|
| B1 | agent 模式启动（`Mode=1` / `SetRoleEnable=0` / `EnMasterOnly=0`） | ☑️ | 实验室：三个值均读回 | — |
| B2 | 发 M1、收 M2、凭据落 `WLANCfg` / `WLANPSK` | ☑️ | 两个频段各一份，`m2_match=1` | — |
| B3 | setter 兜底（配置管理器返回 -3 时读回校验再报 0） | ☑️ | `libzte_agent_mm.so`，沙箱专用 | — |
| B4 | 库 → 计划的发布（`agent-sync` → `/run/zte-mesh/wlan-plan`） | ☑️ | `published=1 rows=2` | — |
| B5 | 计划 → `/etc/config/wireless`（`wlan-sync`，幂等、只写真变化；plan 启用某 band 时把关着的 radio 打开） | ✅ | 86 项测试；ax3600 人为 `disabled=1` 后重启服务，日志 `switched on ... applied`，空卡 `radio0` 未被碰 | — |
| B6 | 回程 BSS：`MapAccessAttr&64` → `multi_ap=2` 独立段，主 SSID `multi_ap=1` | ☑️ | 86 项测试含开关双向复原 | — |
| B7 | UCI / init / LuCI 暴露 `mesh_role` + `backhaul` | ✅ | init 79、rpcd 18；两台 `uci` 写的三个角色都被 init 接受 | — |
| B8 | **子路由免本地 profile**：没有配置时沿用本机正在广播的网络 | ☑️ | init `role-slave-borrow` | — |
| B9 | **LuCI 按角色只显示有效设置**（子路由隐藏 Wi-Fi/漫游/额外网络/子路由清单） | ☑️ | `test_luci.js`（主控 31 项 / 子路由 4 项） | — |
| B10 | rpcd `status` 返回角色 | ✅ | rpcd 18 项；两台各自读到 `master`/`main`/`slave` | — |
| B11 | 子路由侧应用额外槽位（slot 1–3 现在只记日志） | ⬜ | `wlan-sync` 明确跳过 | 3 |
| B12 | **应用结果回传**：`wlan-sync.status` + `status.sync` + 状态页行 | ☑️ | wlan-sync 86 项、rpcd 18 项 | — |
| B13 | 无线回程的数据面：本机作为 backhaul STA 去关联主控的回程 BSS | ⬜ | 只做了 AP 侧 | 4 |
| B14 | 真机：本机当子路由，接真主控（有线回程） | ✅ | 两台实验机打通。抓包确认**子路由自己会发** `0x0007` Search、主控回 `0x0008` 带 WSC；原判断"子路由被 cspd 门控"是错的，真因是 C6。r7 后主控改 SSID，子路由 `wlan-plan` 与 `iw dev` 跟着变成 `MASTERTEST`/`MASTERTEST-5G` | — |
| B15 | `CmWLANMapSsidCfgSync2Repeater` 对齐（libcmapi 内部调用，替身命中不到） | ⬜ | 沙箱里是 no-op | 5 |
| B16 | `CurHop` 在子路由侧的真实更新 | ⬜ | 实验室用 `-2` 顶替 | 2 |
| B17 | **三角色**：`master` 只控制 / `main` = 主控 + 本机落地 / `slave` 只被控；`main` 的 plan 由 init 写 | ✅ | init 79；VM 切 `main` 后 init 写出 `wlan-plan`（`rows=2`）、`wlan-sync` 在跑、日志 `no usable 2g radio; left alone`；切回 `master` 后 plan 与实例一起消失（一个 plan 只有一个生产者） | — |
| B18 | **cspd 替身**：控制器侧自己发 `0x0007` Search、并代答子路由的 Search（`0x0008`） | ✅ | `tools/lab/enroller.c` → `/usr/libexec/zte-mesh/mesh-search`，init 在非 `slave` 角色下多起一个 `search` 实例，带 `--al-mac 02:00:00:00:19:<al_last>`（子路由的 M1 发给控制器 AL MAC）。`--search` 每次启动只发**一帧**，所以周期就是"多久收一次新子节点"，已从 `86400` 改成 `300`（最坏等 5 分钟）；实测不抖：`plan applied nothing new`、`daemon_restarts=0`。装 r6 后真机自动入网，不再需要手工 enroller | — |

| B19 | **控制器地址末字节可配**（UCI `al_last` / 页面"控制器地址末字节"）：两台设备 LAN MAC 相同时手工错开 | ✅ | init 83 条（含 `al_last` 空/大小写/非法值四个分支，VM 上 busybox ash 实测）；只暴露末字节，前五字节固定为子路由准入检查认的形状 | 1 |
| B20 | 页面上本机型号取系统型号（`/tmp/sysinfo/model`）、名称取主机名，取不到都显示 `router` | ✅ | `controller_self_identity()`（`controller-run.c`），`state.json` 的 `self`；子路由行缺型号/名称时同样显示 `router`。runtime 包里的 probe 已按这份源码重打（2026-11-01），设备上才会有 `self` | 1 |
| B21 | `mesh-search` 的 `0x0007` 补 End of Message TLV；线上帧用 Wireshark `ieee1905` 逐字段核对过一遍 | ✅ | 沙箱 90 秒抓 34 帧五类消息：改前 12 个 `0x0007` 全被标 `[Malformed Packet]`，补三个 0 后 0 malformed 且对端照样回 `0x0008`；`0x8006` 的 TLV 编码与 dissector 解析一致（静态构造核对）；`0x0008` 少的 EOM/`0xb3` 故意不改（Z506 验过的形状）。见 [WIRESHARK_1905](research/WIRESHARK_1905.md) | 1 |
| B22 | 查证原厂内核（E2631 2024-09 `vmlinux`）**不含 mesh 协议实现**，也不含 rootfs 解密 | ✅（查证，无代码改动） | 1908 条 `__bug_table` + 6393 个导出符号恢复出函数级地图：`zx_denali_nand.c` 读页路径只有 Denali 寄存器序列与 DMA、无 crypto 调用；内核无 `ecb(aes)`/`cbc(aes)` 模板实例、`"aes"` 字符串零引用；`wlan_get_wlan_multiap_mode()` 全函数只有"读一个 u32"；`br_ieee1905` 钩子对 1905 组播帧在转发路径返回 DROP（**§5.5 修正：丢的是转发，本地投递走 `br_ieee1905_local_in_handle` 放行**）；`CBC decrypt OK!` 位于 Bootloader 分区（`0x619E`）。见 [MESH_INTEROP](research/MESH_INTEROP.md) §4.1 与 §5 | 0 |
| B23 | 摸清**原厂 mesh 的 1905 协议实体是 `multiapd`，不是 cspd**；它未 strip，可直接按函数名调试与逆向 | ✅（查证，无代码改动） | 第三方同型机 `ps` 显示 `cspd`(597) 与 `multiapd`(1293) 是两个进程——我们此前 22 个成员**漏取了 `multiapd`**。该二进制 988 140 B / 6835 符号 / 2184 具名函数：`I5Tlv*` 360 个（TLV 编解码全在此，不在任何 `.so`）、M1/M2 两端对称（`_acdcSendApAutoConfigWSCM1` / `_acdsRcvApAutoConfigWSCM1` 2388 B / `_acdcRcvApAutoConfigWSCM2`）、`g_ieee1905_sockfd` + `bind socket to br0` 证明 1905 走用户态 raw socket。自带 `pktdump 1`（落 `/var/tmp`）、`setrole`、`querydev`、`showMsgStatistics`，**设备无 tcpdump 也能抓 1905**。见 [MULTIAPD](research/MULTIAPD.md) | 0 |

## C. 打包与分发

| # | 功能 | 状态 | 证据 | 难度 |
|---|---|---|---|---|
| C1 | 单个 apk（runtime + init + wlan-sync + rpcd + LuCI），4.1 MB | ✅ | 真路由器上 procd/rpcd 全通 | — |
| C2 | 拆包：`zte-mesh`（`arch: all`）+ `zte-mesh-launcher`（aarch64，只装 `ns-run`/`mesh-search`） | ✅ | **只做了"让 x86_64 能直装"这一半**。必须两个目录：`package-pack.mk` 的 `Architecture` 读 Makefile 级全局 `PKGARCH`，一个 Makefile 给不了两个架构。VM 现在 `apk add zte-mesh` 直装且不覆盖自编的 `ns-run`。原方案的 controller/agent 拆分仍未做 | 1 |
| C3 | 统一行尾（`.gitattributes`，`* text=auto eol=lf`） | ⬜ | 工作区 CRLF，测试要转 LF 才能跑 | 1 |
| C4 | **中文翻译包随 CI 构建**：`luci-i18n-zte-mesh-zh-cn` 之前只配 `=m` 没编译，中文从不产出 | ✅ | 两台装完后 `zte-mesh.zh-cn.lmo` 就是包带来的 | — |
| C5 | **包里的 `ns-run` 与 init 不同步**：tarball 里是旧版，不认 `ZTE_MESH_QEMU`/`ZTE_MESH_EMULATED_LAB`，无 `CONFIG_COMPAT` 的 aarch64 上 `execv` 直接 `errno=8` | ✅ | 修法是消除漂移本身：tarball 额外带 `ns-run.c`/`controller-net.c`/`enroller.c`，`Build/Compile` 用 `$(TARGET_CC)` 现编；`test_runtime.py` 比对源码副本，重打包漏了就在本机失败 | — |
| C6 | **tarball 里的 `db-engine-probe` 冻结在打包那天**：ARM32 半边只能 `-nostdlib` 对着原厂 glibc 2.26 编，包里编不了；冻住的那份不认 `mesh.role=slave` → 子路由的 controller 实例每 5 秒退一次 | ✅ | 换重编副本（193364 B，sha256 `7ff631a7…`）后 slave 角色 3/3 实例全起；`test_runtime.py` 断言包里那份必须带 `config-reject` 与 `wlan-plan` | — |
| C7 | **全新装包后服务起不来**：`qemu-arm-static` 不在包里，而 `CONFIG_COMPAT` 探测在官方固件上必然误判（不导出 `/proc/config.gz` → `grep -c` 返回 0 → 一律走模拟器） | ✅ | 改成"没有答案不等于不支持"：读不到 config 就 exec 一次 `$ROOT/lib/ld-linux.so.3` 看它有没有打印 usage。模拟器**仍不入包**（取舍，不是待办），判定表与三条出路见 [LAB](LAB.md) 第 4.2 节 | 1 |

## D. 换子路由固件才谈得上的（原判据只有 Z506）

| # | 功能 | Z506（原判据） | E2631 / AX3000（2026-11-01 补） | **配置库入口（表.列）** |
|---|---|---|---|---|
| D1 | 带宽 20/40/80/160 | 不写 `BandWidth`，也不关"自动信道" | 未找到新证据，不变 | — |
| D2 | 访客限速 | 1905 里没有这个字段 | 协议外的东西，不变 | — |
| D3 | 信道扫描 / CAC | 无接收代码 | 有 `SendCAC` / `ChannelScan` **发送**入口，接收侧没查（弱） | `MultiAPMaster.InChannelScanRptFlag=0` + **`MultiAPCfg.EnReportCenterChannel=1`**（均实测）。**上报本来就是开的**，缺口在接收侧 |
| D4 | VBSS（虚拟 BSS 创建/销毁/迁移） | 无 | `cspd` 有 `EnVbss` / `ToVbssMode` 配置位，但**没有任何 CM/CF 导出**（中） | **`MultiAPMaster.EnVbss=0` / `.ToVbssMode=1`**（实测，与 `EnBtmKickoff` 同表） |
| D5 | VLAN / 流量分离 / Profile-2 | 无 | `CfMultiAPSetVlanCfg`（152 B 实函数，`ASEND 0xa94e`）——有下发入口（中） | 编译期 `CONFIG_CSP_WLAN_VLAN` + `EBTABLES_{VLAN,FFE_T}`；**`P1905Cfg` 已 dump 但只解决"哪些接口跑 1905"**（`eth1`–`eth4`/`wlan0`/`wlan1` 全开），不解决流量分离。VLAN 本体未定位，新候选 `EthVlan`(#79)/`Brport`(#20)/`PortBinding`(#182) |
| D6 | STA 绑定 | 无 | `CmMultiApSetStaBind`，走 `0x2401` wlan 本地命令，**不是 1905 通道**（中） | **三张 per-STA 表全部 0 行**：`P1905StaBind`(#168)、`MultiAPStaCfg`、`MultiAPCacheMac`。**入口存在、出厂未配** |
| D7 | 802.11r/BTM 踢人（部分） | 固件不支持 FT | `handle_wlan_multiap_steering_req`、`wnm_send_btm_req_raw`、`En11rSteer` 都在（**强**）。内核侧 `br_ieee1905_local_in_handle` 放行本地投递（§5.5），帧能到 `multiapd`；而它自带 `pktdump`，线上取证不必再自带抓包程序 | **`MultiAPMaster.{EnBtmKickoff=0,EnRoamKick=0,En11rSteer=0,11rSsidSync=1,EnActiveDeauth=1}`** + `MultiAPDomain` 的 106 列状态机参数 + `MultiAPRoamCatonCfg.BtmCacheEn=0`（全部实测）+ `WlanRoamingCfg`（实测，**与 11r 无关**） |

这一节的判据原来只有 Z506（Wi-Fi 5 老固件）。2026-11-01 从一台市售 AX3000（E2631，2026-08 固件）
拿出明文用户态之后，D5/D6/D7 从"没有通道"变成"厂商有入口"，其中 **D7 的接收侧处理路径是齐的**。
强度分级（强/中/弱）与反汇编证据见 [VENDOR_MESH_STACK](research/VENDOR_MESH_STACK.md) 第 5 节。

**右列是 2026-11-01 新增的**：把整块 rootfs（[E2631_ROOTFS](research/E2631_ROOTFS.md)）解出来后，
从 `cspd` 的 schema 字符串表 + web 模块 `.lua` 两条独立路径交叉得到 mesh 配置库的表名与列名，
**D3–D7 各自"该改哪个字段"不再是猜测**。

当晚又用四轮只读命令把推断核对了一遍（原文整理在
[E2631_DB_SNAPSHOT](research/E2631_DB_SNAPSHOT.md) 与 [E2631_DB_TABLES](research/E2631_DB_TABLES.md)），
结局是**表归属错了三处**：

- `MultiAPMaster`（55 列）、`MultiAPDomain`（8 实例 × 106 列）、`MultiAPRoamCatonCfg`（7 列）、
  `MultiAPCfg`（50 列）、`P1905Cfg`（12 行 × 3 列）、`WlanRoamingCfg`（1 行 × 6 列）**切对了**，
  `MultiAPMaster` 实测还补上 12 个原先漏掉的列；
- `P1905ComCfg` **切错了表**——它其实只有 8 列（`Enable/EnableIsSync/Policy/DropNum/MarketConfEn/MarketSign/DropRoam/A1Enable`），
  `SetRoleEnable`/`En1905Msg`/`STAMac` 那些名字属于别的表，**上一版右列里引用过它们的写法已作废**；
  更糟的是本仓 [SCHEMA](milestones/SCHEMA.md) 2026-10-12 的补充节早就写了这 8 个字段的偏移与字节数，
  重新推断时没先查；
- `MultiAPCacheMac` **也是切错的**：那 7 个帧缓存列实际在 `MultiAPRoamCatonCfg`，且列名没有 `t` 前缀
  （`BtmCacheEn`，不是 `tBtmCacheEn`）；`MultiAPCacheMac` 本身是空表；
- 我上轮猜的 `IGD.P1905CFGn` **作为表名不存在**，角色开关的正解是 **`MultiAPCfg`**；
  但**它作为行名是对的**——见下。

第三轮 `sendcmd 1 DB all` 拿到 **276 张表名全集**，把上面错误的根源（从字符串表推断表名）彻底堵上：

- 又有三个"列名"其实是**表名**：`MultiAPPTK`(#147)、`P1905StaBind`(#168)、`P1905Cfg`(#164)；
- 库里**没有 `IGD` 这张表**，但 `IGD.` 是**行名前缀**（`TimePolicy` 的行名就叫 `IGD.TpCfg0..2`，见
  [FEATURE_MAP](research/FEATURE_MAP.md) §15）。所以 `IGD.P1905CFGn` 应读成"某表第 n 行"，
  不是表名——`DB p` 只收表名，报 not find 只证明它不是表；
- `MESH`/`Netsphere`/`EASYMESH` 也**不在 276 张表里**，[E2631_ROOTFS](research/E2631_ROOTFS.md) §6.4
  把它们写成"DB 视图别名"没有依据，已降级为 web 层名称映射；
- mesh 相关的表不是 5 张而是 **19 张**（10 `MultiAP*` + 9 `P1905*`）。

**第四轮把上面那条行名推断坐实了**：dump `P1905Cfg`(#164) 后，`ViewName` 逐字就是
`IGD.P1905CFG1`…`IGD.P1905CFG6`，对应 `eth1`–`eth4`、`wlan0`、`wlan1` 六个接口，全 `EnableP1905=1`，
后 6 行是空壳（12 行是编译期容量）。所以我最初在 D5 右列写的
`IGD.P1905CFGn.{P1905InterfaceName, EnableP1905}` **内容全对**，只是把「表 `P1905Cfg` 的行
`IGD.P1905CFG<n>`」压缩成了「表 `IGD.P1905CFGn`」——上一轮一刀切作废属于**过度纠正**，现已恢复成正确层级。

`WlanRoamingCfg`(#270) 这一轮也 dump 了，**方向是负的**：`RoamingEnable=0`、`Interval=60`、
`TimeStart=120`、`RssiThresh=-50`、`RssiStart=-30`，是客户端弱信号重关联参数，**六列里没有 11r 开关**。
⇒ "固件不支持 FT"现在有**两条独立证据**（编译期 + 配置层），不必再找 11r 的配置入口。

`MultiAPCfg` 实测还结掉两条待办：**`CurHop=2=MaxHop` 且 `EnMasterOnly=0`**（B16 结案，与 SCHEMA 的
条件句三者自洽）；**`En1905Msg=1`、`EnReportCenterChannel=1`** 都是开的——所以 1905 报文取证那条路
没有落空，D3 的缺口也不在"要不要上报"。

实测顺带纠了一个方向性判断：**出厂踢人走 `EnActiveDeauth=1`，BTM 那条（`EnBtmKickoff`）是关的**。
所以 D7 与厂商线上行为比对时，该预期看到 deauth/disassoc，而不是 11v 的 BTM Request。

另外两条编译期证据（`/etc/autouserconf`，612 个开关）：`CONFIG_CSP_WLAN_SUPPORT_80211KV`
**只有 11k/11v、没有 11r**——与 D7"固件不支持 FT"是同一件事的两面，实测 `En11rSteer=0`、`SupportRrm=1` 与之吻合；
`CONFIG_CSP_WLAN_VLAN` + `CONFIG_CSPCORE_EBTABLES_{VLAN,FFE_T}` 说明 WLAN 侧 VLAN 是编进去的。

一条通用判据（从 `ViewName`/`ESSID`/`DomainUsed` 全是 `isSave=0` 读出来的）：**`isSave=0` 的列是
运行时/派生值，写 DB 不落盘**。改配置前先看这一列，否则"改了不生效"会被误判成"固件不认"。

**七项的"入口位置"现在是实测级，但"功能本身"一项都没实测**，所以仍然没有一项改成 ✅。

## 推进顺序（只做下一项，做完打勾再往下）

| 顺序 | 项 | 难度 | 说明 |
|---|---|---|---|
| — | B8 / B9 / B10 / B12 / A20 / B5 / B7 / B17 / C4 / C5 / C6 / C7 | — | **已完成**（2026-10-31） |
| — | `sendcmd 1 DB p` 配置库 schema 取证（四轮、十张表 + 表名全集） | — | **已完成**（2026-11-01）：`MultiAPMaster` 55 列、`MultiAPDomain` 8×106 列、`MultiAPRoamCatonCfg` 7 列、`MultiAPCfg` 50 列、`P1905Cfg` 12×3 列（`ViewName` = `IGD.P1905CFG1..6`）、`WlanRoamingCfg` 1×6 列切对；`P1905ComCfg` 只有 8 列、`MultiAPCacheMac` 那 7 列其实在 `MultiAPRoamCatonCfg`，两处切错已自纠；三张 per-STA 表（`MultiAPStaCfg`/`P1905StaBind`/`MultiAPCacheMac`）表在行空；`DB all` 拿到 276 张表名全集。11r 线索**结案为负**（`WlanRoamingCfg` 无 FT 开关）。见 [E2631_DB_SNAPSHOT](research/E2631_DB_SNAPSHOT.md)、[E2631_DB_TABLES](research/E2631_DB_TABLES.md) |
| 1 | r10 四包装机验收 | 0 | 产物已在手，两台换上即可 |
| 2 | ax3600 刷 `CONFIG_COMPAT=y` 固件 | 1 | 彻底摆脱手工模拟器，`.github/workflows/build-firmware.yml` |
| 3 | C3 统一行尾 | 1 | 影响整个仓库，**需要单独确认** |
| 4 | 补 dump 五张表：`MultiapCapability` / `EthVlan` / `Brport` / `MapCooperatorCfg` / `WLANSSID` | 0 | **只读、不用停机**。前四个分别定 D7 位图、D5 的 VLAN 本体（两个候选）、角色选举；`WLANSSID` 用来交叉验证 `MULTIAPDOMAINn` 与 SSID 的 1:1 |
| 5 | B11 额外槽位在子路由侧应用 | 3 | 先要把 plan 的槽位与 hostapd 段对上，且有 A6 真机作参照 |
| 6 | A18 拓扑页 | 3 | 需要 RSSI、终端数先落到 `state.json` |
| 7 | A17 引导 / 封锁 / 踢终端 | 3 | 接口已存在，缺策略与 UI |
| 8 | B13 无线回程 STA | 4 | 要真机看 `4addr`，且与 wpad 变体绑定 |
| 9 | B15 `CmWLANMapSsidCfgSync2Repeater` | 5 | 逆向或真机 strace，前 8 项都过了才值得动 |
| 10 | 用 AX3000 当子路由验 D5 / D6 / D7 | 3 | **要停机窗口**；取证改用它自带的 `pktdump 1`（落 `/var/tmp`）或 `logcat | grep "AGENT RECV"`，**不必再自带抓包程序**。停机前先做零成本只读项：`sendcmd 1 DB p`（读 mesh 配置库）、`cat /proc/kallsyms | grep 1905`、取 `/kmodule/bin/multiapd` 原件。见 [MULTIAPD](research/MULTIAPD.md) §5 与 [E2633_TELNET_BOOT](research/E2633_TELNET_BOOT.md) §11 |

> **B16 `CurHop` 已结案**（原第 5 项）：`MultiAPCfg.CurHop=2=MaxHop`，且 `EnMasterOnly=0`，
> 与 [SCHEMA](milestones/SCHEMA.md) "`EnMasterOnly=1` 时 `slave_init` 跳过、`CurHop` 不会被设成 `MaxHop`"
> 的条件句三者自洽。不是"读到一个值"式的结案。原表 5–11 项已顺次前移。
