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

## 2. 协议层：我们到底在跟谁对齐

| 层 | 内容 | 本项目状态 | 依据 |
|---|---|---|---|
| IEEE 1905.1 | CMDU 0/1/2/3（拓扑发现/通知/查询/响应）、5/6（链路度量） | 已实现 | FINDINGS 第 4 节，真机通过 |
| 1905.1a | 13/14（邻居/非 1905 邻居相关） | 已观测到，未主动用 | REAL_CHILD 第 1 节 |
| WFA Multi-AP R1 | `0x8001` AP Policy Config Request、`0x8003`/`0x8004` 能力与配置、`0x8007` 系列、WSC M1/M2 | 已实现，M2 在 Z506 上真机生效 | REAL_CHILD 第 1 节 |
| ZTE 私有 TLV | 厂商 TLV（`0xdd`/`0xe0`），由 `MultiAPCfg.VersionMode` 决定发不发 | 已实现，`version_mode=128` 时发 | REAL_CHILD 第 2 节 |
| ZTE 私有策略 | `SSEND(0x2401, 0x10205/0x10206)` 热更新域/漫游阈值 | 已实现 | FINDINGS 第 4 节 |

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

## 5. 兼容性原则（做功能时的硬约束）

1. **必须落到线上**：新功能要能指出它用哪个 CMDU/TLV；指不出来的，要么做成本机 UCI 行为，要么不做。
2. **不在自己节点之间新增控制通道**：角色协商、状态同步走 1905 或本机 UCI，不发明"插件内部协议"。
   一旦发明，装了这个插件的两台机器能组网、装一台就不行 —— 那就是独立 mesh 插件。
3. **私有扩展必须可选、可降级**：`mesh_v2=0` 时只发标准 TLV，中兴设备照样能入网（现在就是这个行为）。
4. **验收门槛是真中兴子路由**：只有 Z506 上实测生效的功能才写"支持"；只在自己沙箱里成立的，写"沙箱"。
5. **不碰对端身份**：发给子路由的身份（DevInfo 的 ZTE / `ZXSLC SR1010`）是为了过准入检查，
   **不要**换成宿主的真实型号 —— 换掉的表现是子路由直接不收（REAL_CHILD 第 2 节）。
   宿主型号只用于页面显示（`state.json` 的 `self`）。

## 6. 下一步（按性价比排序）

| 事项 | 为什么值得做 | 需要什么 |
|---|---|---|
| 用 Wireshark `ieee1905` 对照我们发出的帧 | 不用真机就能查编码错误，比"装上去看现象"快 | 抓一份 lab 的 `0x893a`（`tools/lab/spark.c` 已有） |
| 找一台**非中兴**的标准 agent 做对照 | 证明我们不是"只会跟 ZTE 说话"；这是"兼容任意 OpenWrt"这句话的唯一证据 | 一台支持 Multi-AP 的 OpenWrt 设备（hostapd 带 multi_ap） |
| `slave` 角色补 Topology Response 的 BSS/终端 TLV | 现在中兴主控看不到我们下面挂了哪些终端 | Z506 或任意主控做对照 |
| 纯 OpenWrt 组网（无中兴设备）另开一档 | 想做就复用 prplMesh 当 agent，而不是自己再写一套控制面 | 明确的范围与独立验收 |

## 7. 参考

- IEEE 1905.1 技术报告（CMDU 帧格式、`0x893a`、组地址 `01:80:C2:00:00:13`）：
  <https://www.ieee802.org/1/files/public/docs2012/802-1-phkl-P1095-Tech-Presentation-1207-v01.pdf>
- Wi-Fi Alliance Multi-AP 规范（R1/R2，EasyMesh 的底座）；prplMesh 的架构讲稿把 1905 与 Multi-AP
  的关系讲得最清楚：<https://events19.linuxfoundation.org/wp-content/uploads/2017/12/prplMesh-An-Open-source-Implementation-of-the-Wi-Fi-Alliance®-Multi-AP-Specification-Arnout-Vandecappelle-EssensiumMind.pdf>
- 中兴 EasyMesh 认证公告：[F680（2019，R1）](https://zte.com.cn/global/about/news/20190620e1.html)、
  [F689（2020，R2）](https://www.zte.com.cn/global/about/news/20201229e1)
- prplMesh：<https://github.com/prplfoundation/prplmesh>；BBF 1905.1a：<https://github.com/BroadbandForum/1905.1a>
- E2631 移植记录（固件槽位表、rootfs 加密算法）：<https://blog.zlion.top/posts/zte-e2631-mainline-linux-port/>
