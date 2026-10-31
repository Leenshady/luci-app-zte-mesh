# B.2.1：MultiAPCfg 单表、原厂读写与 CM marshaling

> **状态更新（2026-10-12）：** 本文记录 B.2.1 里程碑（`MultiAPCfg` 单表）。2026-10-12 的补充见文末"补充"一节：`MultiAPCfg` 里 `VersionMode`、`CurHop`、`SetRoleEnable` 等字段的实际含义、`P1905ComCfg` 的真实字段、`AclCfg` 表。

本文保留最初单表阶段的验收；当前命令已扩展至八表/391 字段，
最新结果见 [WLAN_PROFILES.md](WLAN_PROFILES.md)。
**这不是完整 Controller schema 或节点管理验收。**
默认 `multiapd` 加载探针仍在数据库初始化前退出 78。这里只运行受限的
`db-engine-probe`，不启动 `multiapd`、`cspd`、OSS 业务 task 或本地 WLAN。

## 证据与注册

- 同基线 cspd SHA256：
  `8b8477e2598660653c27d796fcaaa5484f879cd263fb6caa5e0a982b1cee5ac1`。
- `dbCreateMultiAPConfigTbl @0x1f16c0`：164-byte 表描述符、最多一行；
  50 个 68-byte domain 描述符，加一个终止项，共 3468 bytes。
- `_dbInitTbl @0x1f1c68`：通用 `MultiAPCfg` 视图，2952 bytes，flags 12
  （通用 GET/SET），callback 为 NULL。因此该表不依赖缺失的
  `libdbcspview.so`；这不能推广到其他 callback 视图。
- `dbDefMultiAPCfg @0x1f1274` 提供部分默认值，但其中调用产品相关的
  `dbSetDefVersionMode`。没有执行这些原厂初始化函数，也没有猜测其产品结果。

`multiap_cfg.def` 保留原始字段顺序、类型、save 标志和**注册默认值**。
`schema.py` 按固定 SHA256 对原厂初始化机器码做受限数据流解码，逐项核对
全部描述符，包括未命名的零值字段；只模拟本函数的 mov/load/store/add/sub
和四个已知数据操作，不执行 cspd、其他函数或系统调用。另核对原厂 libdb
的类型长度表。未知基线、指令、调用目标或不一致字段均拒绝。

`prepare.py --analysis-cspd` 从校验过的 JFFS2/SquashFS 提取静态证据到
`out/analysis/cspd`（0600），不会放入 runtime 或上传设备。
`build.sh` 自动做这项提取和静态核对。不要改用其他固件的散放 cspd。

实际注册仍调用原厂 `dbCreateTbl`、`dbCreateDomainNew`、`dbAddRow` 和
`dbMgrCommViewAddItem`；原厂将描述符字符串复制到自己的共享内存池。
读写使用原厂数据库，不是用垫片返回预制缓冲区或伪造 status=0。

## ARM32 视图布局

```text
+0  uint32 length = 2952
+4  uint32 magic  = 0x01020304
+8  第一个参数记录，随后按原厂类型的 record_length 顺序排列

单个记录：
+0  uint16 原厂类型
+2  uint16 写入使能；SET 时 1 才修改该字段
+4  20-byte 元数据区域（除特殊 ViewName 外，通用 GET 不填充字段名）
+24 int32 操作结果
+28 固定容量值区域
```

| 原厂 type | 值布局 | record bytes |
| --- | --- | ---: |
| 12 | signed int32 | 32 |
| 13 | uint32 | 32 |
| 14 | enum/uint32 | 32 |
| 15 | uint8，**不能一律理解为 bool** | 32 |
| 3 | 固定字符串容量 64，value_length 68 | 96 |
| 5 | 固定字符串容量 256，value_length 260 | 288 |
| 7 | 固定字符串容量 1024，value_length 1028 | 1056 |

原厂 `TopoDiscoverInterval` 的 type=15、注册默认值=60，是不能把此 type
统一限制为 0/1 的直接证据。字符串按内联内容读写，不把数据库中的指针
传给外部 helper。字段与 offset 的完整清单生成于
`out/multiap-cfg-schema.json`；示例：Mode value +36、MeshName
+868、IFLink +1444、NoSteeringOUIs +1540。

2952-byte 结构只用于此 ARM32 原厂边界，**不是 ARM64/helper RPC 协议**。
后续配置桥仍须采用语义字段、版本/长度限制及逐字段校验。

## 首份公开测试配置

`multiap-cfg.c` 的 fixture 只写私有数据库：Mode=2、Enable=1、SyncEnable=1、
SetRoleEnable=1、EnMasterOnly=1、MeshName=`zte-shim-lab`、IFLink=`br-lan`，
以及少量公开策略数值。Mode=2 的 `Master` 含义来自原厂 CM API 枚举表
（0=auto、1=Slave、2=Master、3=both），并通过真实 CM getter 核对。

这不是客户配置，也不是已经兼容子节点的生产配置。VersionMode 保留注册
默认值 0，未声称任何 EasyMesh profile 已启用。没有 SSID/PSK、设备身份、
远端 AP profile 或认证材料；只改 IFLink 字符串不等于解决主程序的 br0
绑定。没有修改真实 bridge、接口、无线设置或配置文件。

## B.2.1 历史原生设备验收

设备：`root@192.168.5.1`，Linux 6.18.38+，已启用 ARM32 compat。

```text
ZTE_DB_ENGINE cfg_registered=1 cfg_fields=50 cfg_bytes=2952 test_only=1
ZTE_DB_ENGINE factory_map_query=1 cfg_fields=50 cfg_bytes=2952 map_status=0 payload_logged=0
ZTE_DB_ENGINE cfg_get_set=1 cmapi_get=1 negative_queries=1 buffer_guards=1 payload_logged=0
ZTE_DB_ENGINE PASS shared_counter=6 native_engine=1 multiap_cfg_ready=1 controller_ready=0
```

验证了：

1. 服务端注册表和测试行；fork+exec 客户端真实 attach 4 MiB 共享内存。
2. 原厂 `MapGetDB → MapGetMultiAPCfgDB → dbAPIGetView` 返回 0；逐个检查
   50 个字段的类型/结果，核对非零 Master 策略、字符串和 signed CurHop。
3. 原厂 `CmMultiAPGetMultiAPCfg` 完成真实 CM 输出转换，不只是底层读取。
4. 原厂 `dbAPISetView` 按 mask 只更新 MaxHop、MeshName；再经原厂 getter
   读回，确认其他 Master/bridge 字段未被全零写坏。
5. 不存在的视图返回 -2；错误 magic 返回 -19；错误视图长度返回 -15。
6. DB/CM 缓冲区前后 guard 未被覆盖；服务端看到 6 次跨进程 GET 计数。
7. 私有 IPC/网络/mount namespace、只读根、仅 IPC_OWNER；宿主 IPC 和接口
   名称前后快照一致。DB 原始值、指针及凭据不进入日志。

完整报告：`out/device-db-report.json`。其中
`multiap_cfg_schema_ready=true`、`configuration_read_write_passed=true`，但
整体 `schema_ready=false`、`controller_ready=false`，避免把单表进度误报为
Controller 完成。原隔离保证与资源/进程组清理见 [DATABASE.md](DATABASE.md)。

## 复现与剩余工作

```sh
sh build.sh
python3 device-db-smoke.py --host root@192.168.5.1
```

新增 16 个离线测试，覆盖精确布局/原厂描述符和长度表，以及缺字段、重复
字段/ID、类型/大小/default 越界、基线哈希与地址错误。原 17 个证据测试、
5 个 preload 场景、9 个沙箱场景继续回归。

后续 Master/Domain 策略表已恢复并验收，见 [POLICIES.md](POLICIES.md)。
B.2 仍未完成：产品/节点身份、远端 WLAN profile、配置 helper/RPC、变更
通知和持久化。随后才是 OSS 完整启动、接口映射、受限 1905 LAN 接入及
原厂子节点发现/配置/重启恢复验收。

## 补充（2026-10-12）：几张表的真实语义

以下来自读 `multiapd`/`cspd` 反汇编和真机读回，细节见 [FINDINGS.md](../FINDINGS.md)。

- **`MultiAPCfg`（偏移 = 36 + 32 × 字段序号，前 25 个字段（序号 0 到 24）都是 32 字节）：**
  `Mode`（0 自适应、1 Slave、2 Master、3 两者）、`Enable`（MESH 总开关）、`SetRoleEnable`（自适应角色开关，网页选"自适应"时置 1）、`RoleElect`、`WANForward`、
  `MaxHop`（偏移 228）、`CurHop`（偏移 260，默认 -2；控制器把它放进 Higher Layer Response 的厂商 TLV 告诉子节点）、`VersionMode`（偏移 356，**功能位掩码**，不是"自组网 V2"开关）、
  `P1905DevNum`（偏移 388）、`EnMasterOnly`（偏移 804；为 1 时 `multi_ap_slave_main` 跳过 `slave_init`，所以 `CurHop` 不会被设成 `MaxHop`）。
- **`P1905ComCfg`：** Z506 上只有 `Enable`（界面"自组网 V2"）、`MarketConfEn`、`MarketSign`；SR 上有 8 个字段：`Enable`（36）、`EnableIsSync`（68）、`Policy`（100）、`DropNum`（132）、
  `MarketConfEn`（164）、`MarketSign`（196，字符串）、`DropRoam`（292）、`A1Enable`（324），共 328 字节。
- **`AclCfg`：** `ViewName`（主键）、`Name`、`Interface`、`MACAddress`（6 字节）、`ACLCfgPolicy`，268 字节、最多 256 行（`cspd` 构造函数 `dbCreateAclCfgTbl`，0x1cd334 到 0x1cd41c，
  `startup-schema.py` 的 `SPECS` 可复现）。
- **`MapCooperatorCfg`（子路由 `cspd`）：** `AutoSetup`、`WpsActiveDelayTime`、`Addr4EffectDelayTime`、`EnRebootElect`、`WpsLongPress`；`AutoSetup` 影响角色选举的触发分支。

## 补充（2026-11-01）：真机 `sendcmd 1 DB p MultiAPCfg` 验证 `36 + 32n`

E2631 真机 dump 出 `MultiAPCfg` 的 **50 列**（原文见
[E2631_DB_SNAPSHOT](../research/E2631_DB_SNAPSHOT.md) §6），实测列序与本文还原的字段序
**逐字段一致**，`偏移 = 36 + 32 × 序号` 六个锚点全中：

| 字段 | 序号 | `36+32n` | 本文记的 |
|---|---|---|---|
| `MaxHop` | 6 | 228 | 228 |
| `CurHop` | 7 | 260 | 260 |
| `VersionMode` | 10 | 356 | 356 |
| `P1905DevNum` | 11 | 388 | 388 |
| `EnMasterOnly` | 24 | 804 | 804 |
| `MeshName` | 26 | 868 | 868 |

两处精确化：

1. 上文"前 25 个字段（序号 0 到 24）都是 32 字节"应改成**前 27 个（序号 0–26）都是 32 字节**——
   `PluginLimit`(25)=836、`MeshName`(26)=868 也落在这个区里；
2. 序号 27 起进入变长区：`IFLink`(36) 实测在 **1444**、`NoSteeringOUIs`(37) 在 **1540**，
   不服从 `36+32n`。`1540 - 1444 = 96`，暗示 `IFLink` 是 96 字节的路径字符串槽；
   这一段要算宽度得读 `dbDefMultiAPCfg` 的初始化。

另外验证了本文那句"`EnMasterOnly` 为 1 时 `CurHop` 不会被设成 `MaxHop`"的**反面情形**：
该机 `EnMasterOnly=0`、`CurHop=2`、`MaxHop=2`，三者自洽（`Mode=1` 即 Slave）。

**教训**：`P1905ComCfg` 那 8 个字段的偏移本文早就写对了，后来却从 `cspd` 字符串表重新推断并切错了表。
凡要动配置库 schema，**先查本文再上机**。
