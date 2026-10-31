# 中兴 mesh 组网：外部资料、固件证据与互操作边界（2026-10-31）

> 这一篇回答三个问题：中兴的"自组网"到底是什么协议、网上和 GitHub 有什么可用的、我们做功能时
> 哪些事不能做。结论直接进 [FEATURE_STATUS](../FEATURE_STATUS.md) 的口径，实现细节仍在
> [FINDINGS](../FINDINGS.md) 与 [REAL_CHILD](../REAL_CHILD.md)。

## 1. 结论

1. **中兴的 mesh 就是 IEEE 1905.1 + Wi-Fi Alliance Multi-AP**，不是自研二层协议。线上帧是
   `0x893a`，消息号落在 1905 的 0 到 7 与 13/14，配置走 Multi-AP 的 `0x8001`/`0x8003` 系列。
   中兴自己的公告也这么讲：ZXHN F680 在 2019 年是业界第一台过 EasyMesh 认证的 PON ONT，
   F689 在 2020 年过了 R2。
2. 所以"兼容中兴 mesh"= **在 0x893a 上按 Multi-AP 说话**，不是复刻中兴固件。本项目把原厂
   `multiapd` 当控制器跑，方向是对的；换成自己写一套控制面反而是倒退。
3. **固件解包对"抄代码"没用**：E2633 的 rootfs 是 AES-128-ECB 的 JFFS2，密钥运行时推导、镜像里没有，
   而 `multiapd`/`libcmapi`/`cspd` 全在那里面（[E2633_FIRMWARE](E2633_FIRMWARE.md)）。
   已经拿到的内核 `.config` 有价值，但它证明的是"原厂无线不在标准栈里"，不是"可以照抄"。
4. 因此两条路不能走：**给非中兴机器刷中兴固件**（无线是 CSP_WLAN 私有栈，没有 cfg80211/mac80211，
   我们的 `wlan-sync` 落不上去）；**在插件内部自造一套组网协议**（那就是独立 mesh 插件，且与中兴
   设备互不相通，正好违背项目目标）。
5. 第三方项目（prplMesh、BBF 1905.1a 栈）可用来**对照编码**，不能整套搬进来：它们各自都是完整的
   mesh 栈，搬进来等于把项目变成另一个 prplMesh，而且都要求 mac80211 + hostapd。
6. **原厂内核里没有 mesh 协议**：它只做三件事——认 `0x893a`（连 802.1Q 内层一起认）、把 1905 组播帧
   从桥接路径里丢掉、用一个 u32 位表示"当前是不是 multi-ap 模式"。协议栈全在用户态 `cspd`（§5）。
   这条把第 2 条硬约束从推论变成了实证。

## 2. 协议层：我们到底在跟谁对齐

| 层 | 内容 | 本项目状态 | 依据 |
|---|---|---|---|
| IEEE 1905.1 | CMDU 0/1/2/3（拓扑发现/通知/查询/响应）、5/6（链路度量） | 已实现 | FINDINGS 第 4 节，真机通过 |
| 1905.1a | 13/14（邻居/非 1905 邻居相关） | 已观测到，未主动用 | REAL_CHILD 第 1 节 |
| WFA Multi-AP R1 | `0x0007`/`0x0008`/`0x000a`（AP 自动配置）、`0x8006`/`0x8007`（信道选择请求/响应）、`0x0009` 里的 WSC M1/M2、`0x8003` Multi-AP Policy Config Request、`0x8001`/`0x8002` 能力查询/报告 | 已实现，M2 在 Z506 上真机生效（编号已用 dissector 的表核过，见 [WIRESHARK_1905](WIRESHARK_1905.md)） | REAL_CHILD 第 1 节 |
| ZTE 私有 TLV | 厂商 TLV（`0xdd`/`0xe0`），由 `MultiAPCfg.VersionMode` 决定发不发 | 已实现，`version_mode=128` 时发 | REAL_CHILD 第 2 节 |
| ZTE 私有策略 | `SSEND(0x2401, 0x10205/0x10206)` 热更新域/漫游阈值 | 已实现 | FINDINGS 第 4 节 |
| ZTE 内部总线（不是协议） | `ASEND`/`SSEND` 消息号：`0x9095` 角色、`0x90d3` DevSearch、`0x90aa` 拓扑查询、`0xa932`/`0xa93c` Multi-AP 配置、`0xa94e` VLAN、`0xa980` 信道扫描 | 不适用——我们直接把 CMDU 组出来发 | [VENDOR_MESH_STACK](VENDOR_MESH_STACK.md) 第 3 节 |
| ZTE 内核（不是协议层） | `br_ieee1905_check_is1905_msg` 认 `0x893a` 与 802.1Q 内层；桥接钩子丢弃 1905 组播；`wlan_get_wlan_multiap_mode()` 只读一个 u32 位 | 不适用——内核里没有可复用的实现 | 本文 §5 |

对表的直接用途：**任何新功能先问"它对应哪个 CMDU/TLV"**。答不出来，说明这个功能要么是本机行为
（UCI 侧），要么就是自造协议。

## 3. 外部资料与 GitHub 上有什么

| 来源 | 是什么 | 对本项目的用法 |
|---|---|---|
| [prplMesh](https://github.com/prplfoundation/prplmesh)（prpl 基金会，Intel 贡献） | 开源的 Multi-AP/EasyMesh 控制器 + agent，OpenWrt/RDK-B 双平台，带 EasyMesh 认证测试脚本 | **对照 CMDU/TLV 编码**；agent 侧（我们 `slave` 角色）的参考实现 |
| prplMesh-openwrt | prplMesh 的 OpenWrt 集成层 | 看它怎么把 hostapd 的 multi_ap 用起来（我们 `backhaul=multi_ap` 走的是同一条） |
| [BBF 1905.1a 参考栈](https://github.com/BroadbandForum/1905.1a) | 1905.1/1a 的参考实现，prplMesh 的前身 | 查 1905 基础消息的字段定义 |
| Technicolor `multiap_controller` | 另一套开源 Multi-AP 控制器（libuv 事件循环 + TLV 引擎） | 结构对照：它的拓扑树、策略下发与我们 `controller-run.c` 的差异 |
| Wireshark `ieee1905` dissector | 现成的 1905/Multi-AP 解析器 | 抓我们的帧逐字段核对，不需要中兴设备 |
| Zlion-Y 的 E2631 移植记录 / `e2631-openwrt` | 同一平台的第三方 OpenWrt 移植，给了固件槽位表与 rootfs 加密算法 | 固件线的唯一可复用外部事实来源 |

一条现实约束：OpenWrt 官方**没有**中兴这几代 SoC（ZX2791xx 系列）的支持，论坛的答复是"驱动与
主控都没有，等于从零做"。这正是"互操作"而不是"替换"的理由 —— 我们的目标只能是"让 OpenWrt
设备当中兴 mesh 的控制器/子节点"，不是"让中兴设备跑 OpenWrt"。

## 4. 固件解密：现状与为什么不作为路线

已经做过的事（[E2633_FIRMWARE](E2633_FIRMWARE.md) 有完整记录）：

- E2633 官方固件 26.8 MB，容器头明文，uImage 解开后是 zImage，再解开得到 4.1.25 内核；
- 内核带 IKCONFIG，**完整 `.config` 已提取**，这是本次唯一"能直接用"的产物；
- rootfs 23.4 MB 全程熵 7.99，判定 AES-128-ECB 加密的 JFFS2，**密钥不在镜像里**；社区工具
  （binwalk / unblob）能处理容器和 squashfs/UBI，对这一层没有公开密钥，也就没有下一步。

所以固件线只有两个产出值得记，都不是代码：

1. `CONFIG_CSP_WLAN_*` + 没有 `CONFIG_CFG80211`/`CONFIG_MAC80211` → 原厂无线是私有栈，
   **第二个机型独立确认**（此前只有 e2631-openwrt 一个来源）。
2. `EXTENDER_LINK_BY_DRIVER=y`、`br_ieee1905_filter_init` → 回程由驱动建、内核网桥对 1905 有专门钩子。
   后者解释了 lab 与真机可能的行为差异（沙箱里没有这个钩子）。

**真正有效且合法的取代码方式是运行态**：本项目用的 `libcmapi.so`、`libzte_controller_route.so`
就是从自己拥有的设备上取的（root 后读分区），不是从加密镜像里撬出来的。这条路的成本比破解低两个
数量级，而且拿到的是**和目标机同版本**的库。固件线在拿到可 root 的新机型之前不再推进。

### 4.1 内核读路径里没有解密代码（E2631 2024-09 vmlinux 实证，2026-11-01）

编程器固件的 rootfs 是 AES-ECB 密文，于是最自然的假设是"内核在 NAND 读路径上解密，密钥就编在里面"。
把这颗内核（6,627,680 B，`zx_denali_nand.c` 有 BUG 点可定位）拆开逐条核过，**假设不成立**：

| 查什么 | 结果 |
|---|---|
| `zx_denali_nand.c` 的读页路径 | 3 处 BUG 点落在 `0xC023FE2C`(L1261) / `0xC0240080`(L1454) / `0xC0240B6C`(L1370)；整段是 Denali 控制器的寄存器序列（命令码 `0x2200`/`0x2300`/`0x2400`、bank/地址/`dsb st`、DMA 启动），**没有一次对 crypto 的调用** |
| 内核编了哪些 crypto | 只有 `crypto/algapi.c`、`crypto/api.c`、`crypto/scatterwalk.c` 三个文件留下 BUG 点 |
| 有没有 ECB/CBC 模板实例 | `"ecb(aes)"`、`"cbc(aes)"`、`"ecb"`、`"cbc"` 在镜像里**一个都没有** |
| AES 原语在不在 | 在：`crypto_aes_set_key` @ `0xC015A194`、`crypto_aes_expand_key` @ `0xC0159D1C`、T 表 @ `0xC045D690` / `0xC0464668`。但 `crypto_aes_set_key` 是 `cra_cipher.cia_setkey` 回调，靠函数指针调用，**没有直接调用者**；`"aes"` 字符串（`0xC052AF40`、`0xC063A278`）也**零引用** |
| `CBC decrypt OK!` 在哪 | `0x00619E`，和 `HASH timeout`/`RSA timeout`/`AES timeout`/`verify OK!` 连在一起 —— 属于 **Bootloader 分区（`0x0`–`0x100000`）的一级加载器**，不是内核 |

所以密钥派生只有三个可能的挂点，**都不在这颗 Linux 里**：

1. **BOOTROM / cspboot**（`0x0`–`0x100000`，LZ 之后是 ARM32，这次没反汇编）；
2. **NAND 控制器硬件透明解密** —— 编程器关 ECC 裸读拿到的是物理密文，设备通过控制器读同一页拿到明文。
   这一条能同时解释三件事：OOB 全 `0xFF`、rootfs 高熵、以及同型号活机器的 rootfs 是明文 JFFS2；
3. eFuse/OTP 里的设备唯一密钥，运行时喂给上面两者之一。

**一条能证伪的判据**：在任意一台能 root 的 ZTE 上读 `dd if=/dev/mtdblock8 bs=2048 count=1 | hexdump -C`
——若看到 `hsqs`/`UBI#`/JFFS2 节点而编程器 dump 同页是密文，就是第 2 条（硬件透明解密），
密钥根本不在可读的镜像空间里；若也是高熵，才回落到第 1/3 条。

## 5. 内核里的 1905：只识别、只丢弃，不做协议

这一节回答"原厂内核为 mesh 做了什么"。素材是 E2631（2024-09 版）的 `vmlinux.bin`，方法见
§5.4，可复现。结论先说：**内核里没有 Multi-AP/1905 协议逻辑**，只有三件事——识别帧、把 1905
组播帧从桥接路径里摘掉、暴露一个"当前是不是 multi-ap 模式"的位。协议栈全在用户态 `cspd`。

### 5.1 全部相关字符串（原始命中 76 条，去噪后逐条列出）

按性质分四组，地址为文件内虚拟地址（基址 `0xC0008000`，与文件偏移差 `0x8000`）。

**(a) 导出符号 / 名（11 条）**

| 地址 | 名字 | 性质 |
|---|---|---|
| `0xC02AE2D4` | `wlan_get_sta_bandcap` | 函数，"这个 STA 支持哪些频段" |
| `0xC02AE460` | `wlan_get_wlan_multiap_mode` | 跳转桩，实体在 `0xC02AF210` |
| `0xC02AE45C` | `wlan_get_limit_num` | 跳转桩，实体在 `0xC02AF170` |
| `0xC02ADD08` | `get_wlan_cspkernel_param` | cspd 取内核侧参数的入口 |
| `0xC02AFB3C` | `br_ieee1905_check_is1905_msg` | **判定 1905 帧**（§5.2） |
| `0xC05B0A23` | `wlan_cspkernel_ops` | 数据对象（`ops` 表），不是函数 |
| `0xC04097D0` | `CSPKernel_skb_SelectQueue` | QoS 选队列 |
| — | `CSPKernel_QC_AppSet` / `CSPKernel_SKBQC_AppSet` / `CSPKernel_InMcast_SKBQC_AppSet` / `CSPKernel_QoS_QueueRun` | QoS/队列，与 mesh 无直接关系，列此备查 |

**(b) cspd 的模块名，13 条**（写进内核日志的 view 名，说明 cspd 的日志通道直连内核）

```
cspd.dbd_task.DB                cspd.cspd.dev_mgr             cspd.wlan_manage.wlan_mgr
cspd.cspd.led_ctrl              cspd.cspd.led_priority_mgr    cspd.adev_task.adev_mgr
cspd.cspd.fw_core_mgr           cspd.cspd.simulation_mgr      cspd.cspd.addr6_mgr
cspd.cspd.slaac_mgr             cspd.cspd.ppp_mgr             cspd.cspd.trafficsts_by_dev
cspd.cspconfig.wlan_config
```

外加一条打印格式 `[wlan_cspkernel] [%s(%d)] : %s(%s)!` @ `0xC0572198`——`cspd` 与内核共用一个
带模块名/行号的日志门面，这解释了为什么 `/kmodule` 里的用户态进程能出现在内核日志里。

**(c) ZTE 私有内核补丁的源文件路径，22 条**（全部在 `/zte/csp/opensource/cspkernel4.1/` 下）

```
common/common_ioctl.c                 HAL/ver_info_nand_v2.c        HAL/ledkey_mod_v2.c
protocol/netif_flag.c                 protocol/bridge/br_component.c
protocol/wan_bridge.c                 protocol/wan_bridge_if.c      protocol/nd_agent.c
protocol/dev_mirror.c                 protocol/netfilter/{xt_limitperip,nf_conntrack_pt,
                                      nf_conntrack_rtsp,nf_nat_rtsp,xt_layer7}.c
protocol/ffe/{ffe_main,ffe_pkt_misc,ffe_sharedata,ffe_sk_buff,ffe_fk_buff,ffe_proc,
              ffe_flush,npu_main}.c   protocol/include/ffe/ffe_main.h
```

`protocol/bridge/br_component.c` 与 `protocol/netif_flag.c` 是网桥改造的落点，
`protocol/ffe/*`（Fast Forwarding Engine，NPU 加速）是转发面——**没有一个是 mesh/1905 的**。

**(d) 其余相关串**

| 地址 | 字符串 |
|---|---|
| `0xC0572614` | `br_ieee1905_filter_init register nf hooks failed` |
| `0xC05B0974` / `0xC05B0A0F` / `0xC05B0C26` | `wlan_get_wlan_multiap_mode` / `get_wlan_cspkernel_param` / `br_ieee1905_check_is1905_msg`（`__ksymtab_strings`） |
| `0xC064EC00` | `wlan_multiap_mode`（变量名/调试名） |
| 散落在 `0xC04E1A8E`–`0xC0530570` | 另有 8 处 `ieee1905_msg` 碎片（`0xC04E1A8E`、`0xC04FDCB9`、`0xC05194B6`、`0xC051FFAC`、`0xC0521359`、`0xC0523C48`、`0xC05256CD`、`0xC0530570`），**查不到任何引用**，推测是 trace/调试字符串池的残留，未能确证，不据此下结论 |

### 5.2 三个函数的行为（反汇编实证）

**`br_ieee1905_check_is1905_msg(skb)` @ `0xC02AFB3C`**

```asm
ldrh r3, [r0, #168]      ; skb->protocol
movw r2, #0x3a89         ; htons(0x893a) —— 注意是按网络序存的值
cmp  r3, r2   ; beq -> return 1
cmp  r3, #0x81           ; ETH_P_8021Q=0x8100，按网络序存成 0x0081
              ; beq -> 到 VLAN 分支
                                  ; VLAN 分支：
ldrh r1, [r0, #174]      ; 以太帧头在 skb->head 里的偏移
ldr  r3, [r0, #212]      ; skb->head
add  r3, r3, r1
ldrh r3, [r3, #16]       ; 跳过 14 B 以太头 + 2 B TPID + 2 B TCI
rsb  r0, r2, r3 ; clz ; lsr #5    ; return (内层 ethertype == 0x893a)
```

**它认 802.1Q 封装的 1905 帧**——这是我们此前没记录的一条：ZTE 允许 1905 走 VLAN tag，
判定时剥掉一层 VLAN 再比 ethertype。对我们意味着"带 VLAN 的回程链路上，1905 仍然要被认出来"。

**桥接钩子 @ `0xC02AFB84`**（`0xC0617954` 处的 ops 结构第 3 个字段指向它）

```asm
if (*(u32*)0xC064EED8 == 0) return NF_ACCEPT   ; 全局开关，关掉就不过滤
p = skb->head + *(u16*)(skb + 174)
if ((*p & 1) == 0) return NF_ACCEPT            ; 目的 MAC 首字节的组播位为 0 -> 放行
return !check_is1905_msg(skb)                  ; 是 1905 -> 返回 0 = NF_DROP，否则放行
```

也就是：**目的 MAC 是组播、且是 1905 帧 → 丢弃**。这是一条"把 1905 从 Linux 桥接转发路径里摘出去"
的规则，和 `EXTENDER_LINK_BY_DRIVER=y` 是同一套设计：1905 帧由驱动直送 cspd，不该被网桥泛洪、
不该被送到协议栈。`br_ieee1905_filter_init()` @ `0xC05F723C` 负责注册它，失败就打印 §5.1(d) 那条串。

**`wlan_get_wlan_multiap_mode()` @ `0xC02AF210`——全函数三条指令**

```asm
movw r3, #0xBFA8 ; movt r3, #0xC066
ldr  r0, [r3, #0x94C]      ; return *(u32 *)(0xC066C8F4)
bx   lr
```

**内核眼里的 Multi-AP 就是一个 u32 位。** 没有任何 TLV 解析、没有 CMDU、没有拓扑。

### 5.3 这些证据改变了什么

1. **§6 的第 2 条硬约束（不新增控制通道）从"推论"变成"可复现的事实"**：内核里连解析器都没有，
   任何 mesh 语义只可能存在于 1905 帧里。想做新功能又没有对应 CMDU/TLV，就必然无处落地。
2. **解释了 lab 与真机的行为差**：沙箱里没有这个钩子，`0x893a` 帧会在 `br-lan` 上被泛洪回
   `zmesh0`——上一轮自检功能的假阳性正是这个原因（[WIRESHARK_1905](WIRESHARK_1905.md) 记的
   `/run/zte-mesh/search-sent` 绕法，本质是在用户态模拟这个钩子该做的事）。
3. **对我们自己的实现没有要求变更**：我们发的帧本来就是单播/组播的裸 `0x893a`，不带 VLAN。
   要留意的只有一种情况——将来若在回程上启用 VLAN（D5），1905 必须被对端认出来，而 ZTE 的
   判定支持"VLAN 内层 1905"，说明这条路上它是有准备的。

### 5.4 方法（可复现）与局限

镜像没有 ELF 头，也没有可用的 kallsyms token 表，但有两张可解析的表：

- **`__bug_table`**：ARM 4.1 是 `{u32 bug_addr; u32 file; u16 line; u16 flags}` 绝对指针。
  用"指针必须落在镜像内且指向 `.c/.h/.S` 路径"筛出 **1908 条** `(地址, 文件, 行号)`，
  覆盖 **375 个源文件**——等价于一张函数级地图，`zx_denali_nand.c` 的 3 个 BUG 点就是这么定位的。
- **`__ksymtab`**：`{u32 value; u32 name}`，要求 `value` 处的 4 字节像 ARM 指令（排除把 name
  指针误当 value），恢复 **6393 个导出符号**。
- 基址 `0xC0008000` 由 `w - 文件偏移` 投票得到，脚本 `_analysis/kmap2.py` + `_analysis/xref.py`
  （后者实现 `movw/movt` 配对与 `ldr pc` 字面量两种绝对地址加载方式，用来从字符串反查引用代码）。

局限：非导出符号拿不到名字（`br_ieee1905_check_is1905_msg` 的调用者就是这样一段匿名代码）；
`0xC0617954` 那个 ops 结构的 `pf`/`hooknum` 字段布局未能确证，所以本节只按"钩子 + 丢弃 1905 组播"
描述行为，不引用具体 hook 点编号。



## 6. 兼容性原则（做功能时的硬约束）

1. **必须落到线上**：新功能要能指出它用哪个 CMDU/TLV；指不出来的，要么做成本机 UCI 行为，要么不做。
2. **不在自己节点之间新增控制通道**：角色协商、状态同步走 1905 或本机 UCI，不发明"插件内部协议"。
   一旦发明，装了这个插件的两台机器能组网、装一台就不行 —— 那就是独立 mesh 插件。
3. **私有扩展必须可选、可降级**：`mesh_v2=0` 时只发标准 TLV，中兴设备照样能入网（现在就是这个行为）。
4. **验收门槛是真中兴子路由**：只有 Z506 上实测生效的功能才写"支持"；只在自己沙箱里成立的，写"沙箱"。
5. **不碰对端身份**：发给子路由的身份（DevInfo 的 ZTE / `ZXSLC SR1010`）是为了过准入检查，
   **不要**换成宿主的真实型号 —— 换掉的表现是子路由直接不收（REAL_CHILD 第 2 节）。
   宿主型号只用于页面显示（`state.json` 的 `self`）。

## 7. 下一步（按性价比排序）

| 事项 | 为什么值得做 | 需要什么 |
|---|---|---|
| ~~用 Wireshark `ieee1905` 对照我们发出的帧~~ **已做**（2026-11-01） | 不用真机就能查编码错误，比"装上去看现象"快 | 结果与改法见 [WIRESHARK_1905](WIRESHARK_1905.md)：`0x0007` 缺 EOM 已修，`0x0008` 的 EOM/`0xb3` 留待真机复验 |
| 拿 **AX3000（E2631）** 当子路由，验 Steering / BTM | D7 的接收路径（`handle_wlan_multiap_steering_req`、`wnm_send_btm_req_raw`）在原厂 `cspd` 里是齐的，但线上没有证据 | 停机窗口 + 限时抓包，步骤见 [VENDOR_MESH_STACK](VENDOR_MESH_STACK.md) 第 8 节 |
| 找一台**非中兴**的标准 agent 做对照 | 证明我们不是"只会跟 ZTE 说话"；这是"兼容任意 OpenWrt"这句话的唯一证据 | 一台支持 Multi-AP 的 OpenWrt 设备（hostapd 带 multi_ap） |
| `slave` 角色补 Topology Response 的 BSS/终端 TLV | 现在中兴主控看不到我们下面挂了哪些终端 | Z506 或任意主控做对照 |
| 纯 OpenWrt 组网（无中兴设备）另开一档 | 想做就复用 prplMesh 当 agent，而不是自己再写一套控制面 | 明确的范围与独立验收 |

## 8. 参考

- IEEE 1905.1 技术报告（CMDU 帧格式、`0x893a`、组地址 `01:80:C2:00:00:13`）：
  <https://www.ieee802.org/1/files/public/docs2012/802-1-phkl-P1095-Tech-Presentation-1207-v01.pdf>
- Wi-Fi Alliance Multi-AP 规范（R1/R2，EasyMesh 的底座）；prplMesh 的架构讲稿把 1905 与 Multi-AP
  的关系讲得最清楚：<https://events19.linuxfoundation.org/wp-content/uploads/2017/12/prplMesh-An-Open-source-Implementation-of-the-Wi-Fi-Alliance®-Multi-AP-Specification-Arnout-Vandecappelle-EssensiumMind.pdf>
- 中兴 EasyMesh 认证公告：[F680（2019，R1）](https://zte.com.cn/global/about/news/20190620e1.html)、
  [F689（2020，R2）](https://www.zte.com.cn/global/about/news/20201229e1)
- prplMesh：<https://github.com/prplfoundation/prplmesh>；BBF 1905.1a：<https://github.com/BroadbandForum/1905.1a>
- E2631 移植记录（固件槽位表、rootfs 加密算法）：<https://blog.zlion.top/posts/zte-e2631-mainline-linux-port/>
