# SR1010 原厂 Mesh 移植到 OpenWrt：静态调查与实施路线

## 结论与范围

原厂 Mesh 核心是用户态 `multiapd`，包含 IEEE 1905、Multi-AP/EasyMesh 相关控制逻辑以及中兴私有扩展。它不是 OpenWrt 的 802.11s/batman-adv 网络，不能靠启用 `mesh_id`、usteer 或 DAWN 获得原厂互通。

如果目标是 **SR1010 刷 OpenWrt 后继续管理原厂子节点**，推荐先做纯有线 Controller：保留 OpenWrt 的路由、交换和 DHCP 数据面，复现必要的发现、入网、配置和拓扑控制协议。先验证标准 EasyMesh 互通，再依据抓包补齐必要的中兴扩展。不要从移植整个 `cspd` 或无线内核模块开始。

这是静态调查和工程方案，**没有实现或验证原厂节点加入 OpenWrt**。节点型号、固件版本、实际工作模式、是否依赖厂商认证，以及无线回程需求，仍需确认。若所有子节点也能刷 OpenWrt，则可以选择全开源组网，不必复现全部中兴扩展；但这不是“保留原厂互通”的等价方案。

## 1. 固件证据及来源校验

### 1.1 核心程序实际在嵌套 SquashFS 中

来源链为：

```text
vendor-reference/2b5/rootfs.jffs2
  └─ /kmoduletmp/kmodule.img       # SquashFS
       ├─ /bin/multiapd
       ├─ /bin/cspd
       └─ /lib/lib{ieee1905,multiap}{cfapi,cmapi}.so
```

本次复用以下已有提取目录：

- `out/led-vendor-rootfs/`
- `out/led-vendor-kmodule/`

已从 `vendor-reference/2b5/rootfs.jffs2` 的 inode 496 重组 `kmodule.img`：4944 个数据节点，覆盖全部 19,673,088 字节、无缺口，SHA256 与已有提取文件相同。随后通过 `unsquashfs -cat` 对照上述六个程序/库，确认均与嵌套镜像内容逐字节一致。

| 文件 | 字节数 | SHA256 |
| --- | ---: | --- |
| `kmodule.img` | 19673088 | `3fbb2e29fe7a94124aba084217c372d2694c880c8657781c9c641f7d0d1fad46` |
| `bin/multiapd` | 988140 | `a0ba6beeb1eb4e0c900b24d06e9d37e7174f9f640320d12803d7d80c4276280a` |
| `bin/cspd` | 3329124 | `8b8477e2598660653c27d796fcaaa5484f879cd263fb6caa5e0a982b1cee5ac1` |
| `lib/libieee1905cfapi.so` | 26000 | `7ed1fb54aedbb9f346152a171d4ccce03f84e047cb677beb6553067f1fc951c3` |
| `lib/libieee1905cmapi.so` | 48016 | `17ae6ce610ff7999aafeba305721601fbae8138e10d338cc1704f95b7ec06329` |
| `lib/libmultiapcfapi.so` | 18148 | `e11166d71f6d5ef9cafe80ff946545ac181c73f4d5e82cfac751a0072fe813dd` |
| `lib/libmultiapcmapi.so` | 59544 | `bf47d0b05d059aa85bb92fbc7e2f1e4502832eb35d3bf394ed44f2db9854be75` |

**注意版本混用：** `vendor-reference/2b5/cspd` 是 3,106,388 字节，SHA256 为 `eb7062b96f8885035a719fb428a6b100933d8be0c7fe5d65876550bd49106250`，与 `rootfs.jffs2` 内的 `cspd` 不同。本次没有确认这个散放副本的来源，不能把它的函数地址或内部 ABI 当成嵌套镜像程序的地址或 ABI。后续应以同一镜像提取的一整套程序/库为基线。

### 1.2 可直接定位的逆向入口

以下均为上述 SHA256 的 `multiapd` 的链接地址，不是内核或另一个 `cspd` 的地址。程序保留大量符号，IDA 识别出 2628 个函数，有利于逐层恢复协议。

| 地址 | 符号 | 本次观察 |
| --- | --- | --- |
| `0x13964` | `main` | 调用 `DBShmCliInit`、`OSSInitConfig`、`OSSStart`，并非独立标准事件循环 |
| `0x655b8` | `baseCreateSockfd` | `CreateSocket(17, 3, 14985)`：AF_PACKET、SOCK_RAW、网络字节序 EtherType `0x893a` |
| `0x65d2c` | `baseBindSocketToBr0` | 通过 SIOCGIFINDEX 查找 `br0`，某模式使用 `lo`，随后 bind packet socket |
| `0x66b0c` | `I5MessagePacketSend` | 用 `sendto` 发送以太网控制报文 |
| `0x414dc` | `master_init` | 根据配置初始化 Controller、注册控制消息及 LLC socket |
| `0x60070` | `slave_init` | Agent 侧逆向入口；本次未展开其完整状态机 |
| `0x6f758` | `acdsRcvApAutoConfigSearch` | 解析标准及 Vendor TLV，生成 type 8 AP-autoconfiguration Response；包含工作模式/设备过滤条件 |
| `0x6edd0` | `_acdsRcvApAutoConfigWSCM1.constprop.10` | 标准 WSC 入网路径入口；完整认证与凭据流程待恢复 |
| `0x72f1c` | `_acdcRcvApAutoConfigWSCM2` | WSC M2 接收路径入口 |
| `0x7173c` | `_acdcRcvApAutoConfigVendor` | 厂商自动配置消息接收路径入口 |
| `0x7ca90` | `acdsBuildVendorMsg` | 按私有 msgtype 查找构造回调，并添加六字节厂商头 |
| `0x464ac` | `multiap_get_vendor_oui` | 返回 `VendorOUI @ 0xe1764`，前三字节为 `00 19 c6` |
| `0x68b54` | `I5TlvVendorHeadExtract` | 检查厂商头长度、OUI 和部分 msgtype |
| `0x6a344` | `baseVendorTypeExtract` | 根据设备身份/配置字段维护通信许可状态，不能假设任意 1905 设备均获准加入 |
| `0x53018` | `I5TlvVendorZteCapTypeInsert` | 写出 type `0x4b`、三字节能力内容 |
| `0x3c98c` | `I5TlvVendorCompatibleVersionTypeInsert` | 写出 type `0x4d`、一字节兼容版本 |
| `0x3be98` | `I5TlvVendorP1905ComCfgSyncInsert` | 从 `P1905ComCfg` / `IGD` 数据库读取字段，构造 type `0x60` 内容 |

后三项的 type 值不能未经调用链/抓包验证就视作标准顶层 TLV 编号；需要确认它们在厂商内容中的封装位置、长度、字节序与语义。反编译结果也不能机械照抄为源码，尤其是栈数组、结构体重叠和缺失参数的情况。

此外存在 VLAN、11r、BTM、邻居列表、STA 列表、漫游阈值、策略、VBSS 等 Vendor Insert/Extract 符号。符号存在不证明当前板型全部启用，也不证明所有功能都是首次入网的必要条件。

### 1.3 当前平台情况

- 原厂运行态的 `network/interfaces/` 和 `ip-address.txt` 未显示 WLAN 接口，`proc-net-wireless.txt` 只有表头。只能说明该快照未观察到工作的本地无线数据面，不能据此断言所有板型都没有无线硬件。
- 当前 `port/mainline/zxdbg.fragment` 和 `out/kernel/.config` 有 `CONFIG_PACKET=y`、`CONFIG_BRIDGE=y`，但 `CONFIG_COMPAT` 和 `CONFIG_CFG80211` 未启用。
- 纯有线 Controller 的首个原型不需要本机 WLAN 驱动。若目标包含 OpenWrt 无线 Agent/无线回程，才需要另外解决无线硬件、驱动和 AP+STA/四地址/Multi-AP backhaul 能力。
- 现有硬件接收、DSA 和加速支持不自动证明 1905/LLDP 控制报文可达 CPU，必须单独验收控制流量的接收、组播和交换机 trap 行为。

## 2. 为什么不能直接复制 multiapd

`multiapd` 是 ARM32 ELF，解释器为 `/lib/ld-linux.so.3`，其同镜像的 libc/loader 为 glibc 2.26。OpenWrt 通常是 musl；本仓库当前主线内核又没有开启 AArch32 compat。

其 25 项直接 `DT_NEEDED` 包括：

```text
liboss.so libpthread.so.0 libdl.so.2 libcommfun.so libm.so.6
libzte_slibc.so libevent.so libdb.so libcmapi.so libctype.so
libcfapi.so libcapability.so libieee1905cfapi.so libmultiapcfapi.so
libdhcp4scfapi.so libdhcp4ccfapi.so libieee1905cmapi.so
libmultiapcmapi.so libdhcp4scmapi.so libdhcp4ccmapi.so
libsha256.so libjsonunify.so libssl.so.1.0.0 libcrypto.so.1.0.0 libc.so.6
```

导入接口还包括 `SSEND`/`ASEND`、数据库 `dbAPI*`、`CmWLAN*`、`CmEth*`、配置/拓扑通知等。这些接口的服务、共享内存、结构体和事件语义都不是 OpenWrt 的 UCI/ubus，复制 `.so` 并不等于它们可正常工作。

可把运行原厂二进制作为隔离的研究实验，但前提包括 CPU 的 AArch32 支持、compat 内核、同版本 glibc 私有 rootfs 和完整依赖调查。不要覆盖 OpenWrt 的 `/lib`，也不要直接在生产主机网络上启动整个 `cspd`：它可能改变配置或访问厂商驱动。旧 OpenSSL 的维护、安全和固件再分发许可也必须单独评估。

## 3. 实现方案选择

| 路线 | 用途 | 判断 |
| --- | --- | --- |
| 原厂 ARM32 程序 + 兼容运行环境 + 原厂服务或 shim | 实验性保留厂商行为 | 能做技术验证，但耦合深；不默认它是工作量最小或适合发布的路线 |
| 开源 EasyMesh 栈 + 中兴兼容扩展 | 完整、可维护的 Controller/Agent | 标准互通部分优先评估；中兴子节点是否接受标准入网必须实测 |
| 原生轻量 1905/中兴 Controller，复用成熟协议/密码库 | 仅管理指定原厂子节点 | 若实测主要依赖厂商路径，可限制范围；代价是自有状态机、协议和安全测试维护 |

prplMesh 是候选 Multi-AP Controller/Agent 栈，不是已验证可直接安装到本机的解决方案。必须评估目标 OpenWrt 版本、资源、平台接口、无本地 radio 的 Controller 模式和节点互通。其 GitHub 仓库明确标为过时，应使用当前 GitLab 仓库：

- <https://gitlab.com/prpl-foundation/prplmesh/prplMesh>
- <https://prplfoundation.org/prplmesh/>

需要更低层的 IEEE 1905 参考时可评估 meshComms，但不能把它等同于完整 EasyMesh 产品：

- <https://github.com/BroadbandForum/meshComms>

usteer 的职责是 OpenWrt AP 间的客户端漫游辅助，不提供这里需要的原厂入网/配置协议：

- <https://github.com/openwrt/usteer>

不要同时在同一 LAN 启动两个独立 1905 Controller。若采用现有栈，扩展应集成到同一套 CMDU 接收、MID、重传及状态机中，而不是旁路争抢控制。

## 4. 推荐开发顺序和验收门槛

### P0：固定对端，抓出真实工作路径

先确认主路由角色、子节点型号/固件、哪些节点保留原厂固件，以及是否必须支持无线回程。在隔离 LAN 中，使用原厂主控 + 一台原厂子节点建立基线。

在镜像口或可观察完整链路的位置抓包，例如：

```sh
tcpdump -i <镜像接口> -s 0 -U -w factory-mesh.pcap \
  'ether proto 0x893a or ether proto 0x88cc'
```

上述过滤适合未加 VLAN 的链路；如果链路有 VLAN，应额外捕获带标签报文或不加过滤抓取后离线分析。交换机镜像比只观察桥接口更可靠。抓包不要进入公开仓库，可能包含身份、配置或凭据相关材料。

至少记录：

1. 子节点上线/允许入网的过程；仅在同意清除其配置的测试设备上做恢复出厂测试。
2. 一次修改 SSID/密码与一次配置更新。
3. 子节点重启、断线、重连及主控重启。
4. 若有相应需求，加入第二个节点、漫游、回程切换及访客 VLAN。

结合函数逆向，确认：标准 WSC M1/M2 还是厂商自动配置路径；Profile/版本协商；角色选举；OUI/身份限制；是否要求公钥认证；MID/ACK/重传/分片和配置完成条件。现有引用资料没有提供原厂 Mesh 组网抓包。

**门槛：** 得到一份带方向、消息类型、TLV、状态变化及超时的入网时序，而不是仅从函数名推断支持情况。

### P1：1905 传输与发现，不先碰无线和配置

- 实现/复用 AF_PACKET 1905 传输、CMDU/TLV 编解码、AL MAC 与接口 MAC 区分。
- 接口从 UCI/ubus 获取，不硬编码原厂 `br0`；支持实际 `br-lan`/DSA/VLAN 拓扑。
- 处理组播、必要的 LLDP、拓扑 Discovery/Query/Response，以及分片、去重、MID 和重传。
- 只允许指定 LAN/回程接口；不默认监听 WAN，不盲目打开所有 link-local multicast 转发。
- 用抓包确认软桥、交换芯片和硬件加速不会吞掉或错误复制控制帧。

**门槛：** OpenWrt 与原厂子节点双向发现，拓扑身份正确，重连/重启不会出现重复节点。此时只算“发现”，不算“入网或配置成功”。

### P2：单节点自动入网及配置同步

- 优先试验已有标准 EasyMesh/WSC 实现是否被目标子节点接受。
- 必要时补齐中兴 OUI、兼容版本、设备身份、角色/通信许可和厂商配置消息。
- 恢复认证、nonce、密钥派生、M2 加密设置/完整性校验及厂商公钥流程；不自创或省略这些环节。
- 明确 SSID/PSK/安全模式、前传/回程 BSS、频段和配置完成的含义，不伪造不存在的本地 radio 能力。
- 若主控没有无线，配置是远端 AP 的期望状态，不依赖本地主控已经有 hostapd/WLAN。

**门槛：** 原厂子节点接受首次配置及后续更新；客户端关联后可通过子节点获得 DHCP 和正常访问；双方分别重启后可以恢复。仅出现拓扑节点或“在线”标志不算通过。

### P3：多节点、策略、漫游与生产化

- 按需求补拓扑变化、AP/STA 指标、信道选择、11k/11v/11r、BTM、黑名单、漫游阈值和 VLAN。
- 主控只下发它实际能实现、对端也支持的策略；未知可选 TLV 应兼容处理，不能盲目宣称全部能力。
- 验证多 Controller 冲突、重复帧、恶意/畸形 TLV、凭据访问权限、节点授权和日志脱敏。
- 做无线回程时，另开 Agent/无线 HAL 任务，验证 AP+STA、四地址或 Multi-AP backhaul、桥接和回程切换；这不是纯有线 Controller 的前置条件。
- 用 OpenWrt package、procd、UCI 和 ubus 接入生命周期、配置和状态；优先使用现有框架的成熟实现。

**门槛：** 多节点重连、配置更新、回程/主控重启和客户端漫游均通过定义好的场景；控制流量不泄漏 WAN；配置失败有回滚和可观察状态。

## 5. 建议的原生集成边界

可把实现按职责划分，而不是强制新造一套框架：

```text
UCI / ubus / procd
       │
OpenWrt 平台适配（接口、期望配置、状态、通知）
       │
Controller 状态机 / 节点数据库 / 配置同步
       │
标准 IEEE 1905 / Multi-AP + 必要的中兴扩展
       │
AF_PACKET / LAN bridge / DSA ports
       │
保留原厂固件的子节点
```

若后续需要本地无线 Agent，再增加 `hostapd`/`wpa_supplicant` 控制接口、nl80211 和必要的驱动适配，替代原厂 `CmWLAN*`/WLAN 事件服务。无需为了 Mesh 重新实现 OpenWrt 的 PPPoE、路由、NAT 或整个原厂管理系统。

**下一项应做的工作：固定子节点型号并取得原厂入网抓包，然后以 `acdsRcvApAutoConfigSearch`、WSC 和 Vendor 自动配置路径为入口，确定 P2 的最小协议集合。**
