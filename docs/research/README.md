# 原厂逆向与上机取证（只读，已冻结）

> **已冻结（2026-11-03）**：本目录记录"那天在哪台机器上跑什么、看到什么"，结论变了不回来改这里——逐项状态看
> [FEATURE_STATUS](../FEATURE_STATUS.md)，排期看 [PLAN](../PLAN.md)。只有要核对当年的原始读数、查原厂字段或某项能力时，
> 才按名字打开对应的一份；通读没有收益。
>
> 早期里程碑目录（`docs/milestones/`）在 2026-11-07 并到这里：`SCHEMA`/`POLICIES` 两份字段还原、机型对照表都移到本目录，
> 目录本身撤销。B.1–B.3.3 那份验收记录同日删除，仍成立的三条（订阅者池的尺寸与 key、配置桥 V1 的 120/24 字节线上格式、
> 写两表不宣称原子性）已搬进 [ARCHITECTURE](../ARCHITECTURE.md) 第 5 与第 7 节。

| 文档 | 内容 |
|---|---|
| [FINDINGS](FINDINGS.md) | 逆向与上机发现的汇总：角色选举、准入链、自组网 V2、`VersionMode` 位掩码、CurHop、ACL、频段与漫游 |
| [CONTROLLER_CAPABILITIES](CONTROLLER_CAPABILITIES.md) | 原厂控制器能向子路由下发什么：五条通道、策略 TLV、信道选择、一次性动作，Z506 的实际支持矩阵 |
| [SCHEMA](SCHEMA.md) | `MultiAPCfg` 50 字段的还原（偏移、原厂读写、CM 转换）——**查原厂字段先读这份** |
| [POLICIES](POLICIES.md) | Master / Domain 策略表与跨进程写入的还原 |
| [ZTE_ROUTER_MODELS](ZTE_ROUTER_MODELS.md) | 机型对照：市售/运营商名称、认证型号、无线规格 |
| [E2631_DB_SNAPSHOT](E2631_DB_SNAPSHOT.md) | E2631（巡天 AX3000）整库快照与逐表读数 |
| [E2631_DB_TABLES](E2631_DB_TABLES.md) | 同机的全表清单（276 张）与表名存在性判定 |
| [E2631_ROOTFS](E2631_ROOTFS.md) | 同机 rootfs：取回、解开、里面有什么 |
| [E2633_FIRMWARE](E2633_FIRMWARE.md) | E2633 官方固件解包记录 |
| [E2633_TELNET_BOOT](E2633_TELNET_BOOT.md) | E2633「转正」工具链：开 telnet、改型号、刷 boot |
| [CAPTURE_1905](CAPTURE_1905.md) | 测试主机上的 1905 实测：线上有哪些帧、哪些字段 |
| [STEER_1905](STEER_1905.md) | 终端动作消息 `0x8014` 这一类到底长什么样 |
| [WIRESHARK_1905](WIRESHARK_1905.md) | 用 tshark 的 `ieee1905` dissector 核对发出的帧，与沙箱抓帧步骤 |
| [MULTIAPD](MULTIAPD.md) | 原厂 mesh 的 1905 协议实体：入口、线程、关键函数 |
| [VENDOR_MESH_STACK](VENDOR_MESH_STACK.md) | E2631 的 mesh 栈：结构、能力面、与 SR1010 的差异 |
| [BLOB_SHIM](BLOB_SHIM.md) | 二进制 + 垫片：原厂 mesh 栈的复用可行性 |
| [MESH_INTEROP](MESH_INTEROP.md) | 外部资料、固件证据与互操作边界 |
| [AGENT_PROBE](AGENT_PROBE.md) | 子路由（Slave）角色的判定性实验：M2 到底写进哪里 |
| [FEATURE_MAP](FEATURE_MAP.md) | 界面控件 → 数据库 → 线上报文 → 日志 |
| [REPLAY_RESULTS](REPLAY_RESULTS.md) | 回放对比：shim 控制器 vs 原厂控制器（Z506 当子节点） |
| `coldjoin-sequence.txt` | 冷加入的原始报文时序 |

原厂固件镜像不在仓库里：`tools/dev/prepare.py` 默认读 `../zx279133/vendor-reference/2b5/rootfs.jffs2`，可用
`SR1010_VENDOR_ROOTFS` 指定。
