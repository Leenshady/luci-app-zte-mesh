# 早期里程碑的验收记录（B.1 – B.3.3，2026-10-10 到 10-12）

> **已冻结（2026-11-03）**：本文把 B.1/B.2（数据库引擎）、B.2.3（WLAN/DevInfo 布局）、B.2.4（配置桥）、
> B.3（沙箱里初始化真实 `multiapd`）、B.3.1（CM 替身读取）、B.3.2/B.3.3（1905 线上校验）六篇当时的验收记录
> 合成一份，保留的是"那天在哪台机器上跑什么命令、看到什么输出"。文中的 `controller_ready=false`、
> "M2 尚未完成"、"未接入真实子节点"都是当时的状态，**不是现在的状态**——现在的逐项状态与判据看
> [FEATURE_STATUS](../FEATURE_STATUS.md)，排期看 [PLAN](../PLAN.md)，真子路由联调看
> [REAL_CHILD](../REAL_CHILD.md)，跨里程碑的结论看 [FINDINGS](../FINDINGS.md)。字段级还原仍以
> [SCHEMA](SCHEMA.md)、[POLICIES](POLICIES.md) 为准（这两份是查表用的参考资料，不在本文范围）。

## B.1 / B.2：原厂数据库引擎与配置实验

**通过的是 DB 服务端/客户端与八表逻辑配置边界，不是 Controller 启动验收。**
B.1 验证真实缺失配置错误；B.2.1/2 恢复 Cfg/Master/Domain；B.2.3 新增五张
WLAN/DevInfo 表 metadata，共 391 字段。WLAN 只使用公开 fixture 和纯 CF GET，
不读取原厂 default/凭据，不伪造成功，不运行 `cspd`/`multiapd`。
**当前实验命令运行八表扩展版**，最新验收见本文 B.2.3 节，
历史策略与 Cfg ABI 见 [POLICIES.md](POLICIES.md)、[SCHEMA.md](SCHEMA.md)。

### B.1 历史设备验收

`root@192.168.5.1`，已启用 ARM32 compat 的 Linux 6.18.38+：

```text
ZTE_DB_ENGINE server_initialized=1 shared_bytes=4194304 callbacks_available=0
ZTE_DB_ENGINE client_attached=1 allocator=1 missing_view_status=-2
ZTE_DB_ENGINE factory_map_query=1 config_missing=1 map_status=-1 payload_logged=0
ZTE_DB_ENGINE PASS shared_counter=2 native_engine=1 controller_ready=0
ZTE_DB_SANDBOX private_ipc=1 private_net=1 readonly_root=1 caps=IPC_OWNER
ZTE_MESH_SHIM call=dbAPIGetView view=MultiAPCfg
ZTE_MESH_SHIM return=dbAPIGetView status=-2
```

服务端调用原厂 `DBShmSrvInit`，初始化空表/视图链表与原厂哈希；
客户端经过 fork + exec，在新的 ARM32 地址空间调用原厂 `DBShmCliInit`。
随后验证原厂共享内存池的加锁/分配/释放、缺失视图错误以及真实
`MapGetDB → MapGetMultiAPCfgDB → dbAPIGetView` 调用。
服务端观测到两次跨进程读取计数；退出前移除实验共享内存。
宿主 `/proc/sysvipc/{shm,sem}` 和网络接口名称前后枚举一致。

`libdbcspview.so` 仍未提供，实验中 `g_pShmHandler == NULL`。
这次测试证明其缺失不阻止基础 DB/锁/分配器工作；**不代表需要 callback
的业务视图可以正常运行**。`dbcGetView` 对通用视图和 dlsym callback 视图
采用不同路径，仍需按真实 Controller 查询逐项恢复。

### 可重复运行

```sh
# 只编译/做离线回归，不会自动部署或接入设备网络。
sh build.sh

# 显式进行设备侧数据库实验。
python3 device-db-smoke.py --host root@192.168.5.1
```

主要产物：

- `out/db-engine-probe`：ARM32 原厂 glibc 2.26 的 DB 实验程序。
- `out/ns-run`：AArch64 静态隔离启动器。
- `out/device-db-report.json`：最新报告；单表状态为 true，整体 schema/Controller 仍为 false。
- `out/multiap-cfg-schema.json`：50 个字段的原始类型、默认值和视图 offset。
- `out/device-db/`：校验、宿主前后状态和完整 stdout/stderr。

程序没有用交叉工具链默认 glibc 链接最终 ELF；显式用 crt objects 和原厂
库链接，实际只要求 `GLIBC_2.4`。所有强符号和 GNU 版本需求均通过闭包审计。
原厂 DSOs 的部分符号由 multiapd 的其他直接依赖提供，单靠各库自身的
DT_NEEDED 不足，因此实验程序仍加载审计过的 27 库闭包。

### 隔离与失败行为

`ns-run` 只允许固定的 `/bin/db-engine-probe` 命令，不允许启动原厂主程序：

1. 在 fork 后创建新的 IPC、网络、mount namespace，并检查三个 namespace
   inode 与宿主不同；失败退出 80，绝不退回宿主 IPC。
2. mount propagation 设 private；实验根目录 bind-remount 为只读，提供
   私有 tmpfs `/tmp` 和 `/dev/shm`，不挂载 `/proc`、硬件设备或 MTD。
3. 关闭额外 fd，清空继承环境，限制内存/CPU、关闭 core dump，启用
   no-new-privileges；只保留 `CAP_IPC_OWNER`。
4. 原厂 DB 创建共享内存的权限位为 000，因此实验需要 `CAP_IPC_OWNER`
   来使用真实引擎；不保留 SYS_ADMIN、NET_ADMIN、NET_RAW 或 SYS_RAWIO。
5. 固定加载本实验根目录内的 ARM32 垫片，显式开启 trace；此时已有真实
   后端，所有 DB 调用继续原样透传，不伪造返回值。
6. 15 秒总时限，结束或超时清理整个实验进程组。IPC namespace 销毁时
   回收其中遗留的 semaphore/shared-memory 对象。

新增 9 个隔离启动器回归场景，覆盖错误路径、symlink 逃逸、禁止启动
multiapd/cspd、命令/库路径替换、额外参数、shell 文本与 namespace 被拒绝。
这些测试不是完整沙箱安全审计；此启动器目前仅适用于此受控 DB 实验。

### 已恢复的 ABI 证据

基线文件：`out/runtime/lib/libdb.so`，SHA256
`b8316a82f3726eddcf96dfa997c7a55bd08f825dc9808167600f751c302f7a1f`。

| 项目 | 恢复结果 / 位置 |
| --- | --- |
| `DBShmSrvInit` | `0x6cf0`；key 65535，4 MiB，flags `0x600` |
| `DBShmCliInit` | `0x6e80`；同 key，大小/flags 为 0 |
| attach 地址 | `0x60000000`；原厂未检查 shmat 失败 |
| 全局指针初始化 | `0x6b48`；表头 +0，视图头 +8，get counter +2164 |
| pool | base +2200；free-list、统计、原厂 allocator |
| DB lock | semaphore key `0x30004`；`libcommfun.so:cspLockInit @0x63b8` |
| table/view hash | `dbInitHashTble @0xee5c`；600/500 buckets |
| get ABI | size at +0，magic `0x01020304` at +4；缺表返回 -2 |
| MultiAPCfg 查询 | `libmultiapcfapi.so:MapGetMultiAPCfgDB @0x13c4`，2952 bytes，空 instance |
| 高层 query | `MapGetDB @0x14e0`；DB query 失败转为 -1 |

两个 init 在成功路径最终返回共享内存中的地址，而不是约定的 0 成功码。
因此实验用实际 mapping、全局指针、锁和计数验收；垫片保留原返回值，
但只记录 `returned=1`，不把它作为 status 打印，不泄漏指针。
同一进程重复 attach 固定地址会失败，故客户端必须 exec 新地址空间。

### B.2 八表逻辑 fixture 已通过；下一步为配置桥与 CM facade

已定位同基线 `cspd` 中的生成式注册函数，仅做静态分析，不运行它：

```text
cspd SHA256: 8b8477e2598660653c27d796fcaaa5484f879cd263fb6caa5e0a982b1cee5ac1
0x1f16c0 dbCreateMultiAPConfigTbl
0x1f1274 dbDefMultiAPCfg
0x1f2218 dbCreateMultiAPMasterTbl
0x1f1cd8 dbDefMultiAPMaster
0x1f2900 dbCreateMultiAPDomainTbl
```

Cfg/Master/Domain 的注册字段、原厂类型、长度、primary/save 标志和测试行
已恢复并原生验收；源机器码逐项核对见 [SCHEMA.md](SCHEMA.md) 和
[POLICIES.md](POLICIES.md)。后续新增五张 WLAN/DevInfo 表布局和纯 CF 逻辑
查询，见本文 B.2.3 节。仍需 UCI/helper、安全模式/
真实身份映射及 Controller-only CM facade。不能把原始 ARM32 结构直接当成
RPC，也不能复制原厂凭据或执行含硬件/无线操作的默认初始化函数。
完整 OSS/internal task/socket 启动和节点管理尚未验收。

## B.2.3：WLAN / DevInfo 布局与隔离逻辑 profile

**通过的是字段布局、纯 DB/CF 查询和公开 fixture 的读写，仍不是可用
Controller / 完整 WLAN backend。** 没有启动 cspd/multiapd，没有设置真实
无线或读取原厂密码/序列号，也没有接入 LAN。

### 实际 Controller 调用链与风险

先检查同基线 multiapd 的实际 PLT 调用，而不是凭表名推测：

| multiapd caller | 原厂 API |
| --- | --- |
| `_meshWlanGetPsk @0x44cc4` | `CmWLANGetPsk`，callsite `0x44d20` |
| `MeshWlanGetAPCfgByBandAndIfIndex @0x462b4` | `CmWLANGetAPConfig`，`0x4634c` |
| `MeshWlanGetAPCfgByIfName` | `CmWLANGetAPConfig`，`0x463f0` |
| `MeshWlanGetBasicByBand` | `WlanGetBasic`，`0x45988` |
| `MeshWlanListAPCfg` | `WLANListCfg`，`0x460cc` |
| `i5DmQueryDevAliasName_LanInfo @0xa0a68` | `CmDevGetDevInfoConf`，`0xa0ab8`；instance=`IGD` |

不要把原厂函数名里的 Get 当作无副作用保证：

- `libcmapi:CmWLANGetAPConfig @0x140cd8` 会调用
  `CmWLANSetWlanWepEncryptionLevel @0x1412f0`、
  `wlan_set_wifi_ssid_type @0x141634`，并访问 band/setting 依赖。
- `libcfapi:WlanGetCfg @0x52a60` 在 `0x52d80` 发送 `SSEND` CSP 消息。
- `libcmapi:CmDevGetDevInfoConf @0x3d690` 除 DB 外还调用
  `CmDevGetWanMacConf` 及系统信息查询。

本次**不调用上述高层函数**，只验收原厂纯 CF 查询：
`WlanCfgDbRead @0x595b8`、`WlanPSKDbRead @0x58a94`、
`WlanBaseDbRead @0x5d9f8`。将来需要隔离 Controller-only facade，不能靠
启动整个 cspd 或盲目调用 CM Getter 补齐这些边界。

### 新恢复的五张表

| 表 | 字段 | 视图 bytes | constructor | 实验行数 |
| --- | ---: | ---: | --- | ---: |
| WLANBase | 70 | 2576 | `0x1ecd5c`，static domains 4828 bytes | 2 |
| WLANCfg | 53 | 2312 | `0x1ecfe0`，domains 3672 bytes | 2 |
| WLANSSID | 8 | 712 | `0x1edcd0`，domains 612 bytes | 2 |
| WLANPSK | 4 | 328 | `0x1ed850`，domains 340 bytes | 2 |
| DevInfo | 45 | 6024 | `0x1d0728`，domains 3128 bytes | 1 |

新增 **180 字段**；与 Cfg/Master/Domain 合计八表 **391 字段**。
字段顺序、类型、primary/save、其他 domain 字节和视图 offset 均核对
SHA256 `8b8477e2598660653c27d796fcaaa5484f879cd263fb6caa5e0a982b1cee5ac1`
的静态 cspd。DevInfo 没有 primary 字段；其他四表 primary 为 ViewName。

新增类型长度同时核对原厂 libdb 的 type-length 表：0/1 小字符串、16 IPv4、
18 MAC、22 8-byte map。所有视图使用 +0 size、+4 magic、每字段 +0 u16 type、
+2 u16 write mask、+24 result、+28 value。字符串从 value 直接开始，不是
length-prefixed RPC；value_length 的额外四字节不能当作可写字符串容量。

### 不复制原厂默认值

`profile_schema.py` **不解引用任何 source-default 指针**，包括数字、密码、
序列号及 MAC 默认值。生成数据只保留 metadata，独立指定公开零值：
字符串空、scalar/map 0、IPv4 `0.0.0.0`、MAC 全零。
这些不是产品/原厂默认值，也不是已经导入的 UCI 配置。

原厂 constructor 中涉及硬件的部分只做固定 PC/target/argument 的数据模型：

- `WLAN_MAX_DWORD` 的 runtime limits 不执行/不恢复；分析和实验只取明确的
  fixture 上限（Base=2、Cfg/SSID/PSK=32），不宣称目标有 32 个实际 BSS。
- PSK constructor `0x1ed920` 的 `CspHardCodeParamGet` 不执行；不读其默认
  password 内容。所需 selector/65-byte capacity 只验证 metadata。
- 保留 source 中 `dbSetDmFlag` 的 ESSID=2、EAPSecret/PSK/Passphrase=1。

原厂 core-view callback 注册和生命周期还没有恢复。本实验为四张 keyed
表建立明确的 test-only common views（flags=63、arg5=1024）；DevInfo 是
单行 common view（flags=12、arg5=0），以真实 caller 的 `IGD` instance 查询。
**不要把这些测试注册参数宣称为原厂 core views 已完全复现。**

### 原生设备验收

启用 COMPAT 的 `root@192.168.5.1`：

```text
ZTE_DB_ENGINE profile_tables=5 profile_fields=180 identity_get=1 cf_profile_get=1 profile_get_set=1 profile_row_isolation=1 profile_guards=1 source_defaults_read=0 hardware_calls=0 payload_logged=0
ZTE_DB_ENGINE PASS shared_counter=45 native_engine=1 multiap_cfg_ready=1 policies_ready=1 profile_fixture_ready=1 controller_ready=0
```

- 行 key 由原厂 `WlanCardIndex2BaseInstId`、`WlanIndex2InstId`、
  `WlanIndex2SSIDInstId`、`WlanIndex2PSKInstId` 生成；逐个验证 32-byte buffer
  guard、终止符及两行 key 不同，不猜测实例名称。
- 五表所有字段 GET 类型/result 正常；原厂三个 CF getter 成功。
- 仅公开 fixture：实验 ESSID、非真实测试 key、`PUBLIC-NOT-DEVICE-SERIAL`
  等身份；两行模拟配置不是正在广播的 Wi-Fi，也未验证实际 band 映射。
- masked SET 更新第一行 ESSID 和测试 passphrase；第二行保持不变。
  child exec 退出后，服务端再读五次确认共享数据库中的写入与行隔离。
- setter 拒绝 primary/scalar/不存在字段和保守字符串限长；原厂 GET 对
  不存在行、错误长度/magic 分别返回 -5/-15/-19，不伪造成功。
- 视图外及短视图尾部 buffer guards 完整。用 volatile wipe 清除成功路径
  本地 query bytes；不是生产凭据生命周期/所有错误路径擦除验收。
- SSH 脚本验证测试 key/序列号 canary 不在 stdout/stderr；host IPC/接口名称
  快照一致，private IPC/net/mount、只读根、只保留 IPC_OWNER。

28 个新增离线测试通过。其中把**所有 source-default 指针替换成无效地址**
后 metadata 验证仍通过，证明恢复器无需读这些指针。其余覆盖原厂 metadata、
长度、key、公开 default、baseline mismatch、未知调用、生成器不覆盖/失败
不输出等。加上已有 67 个场景，完整构建共 **95 个回归场景通过**。

### 文件与下一步

- `profile_schema.py`、`wlan_*.def`、`dev_info.def`：metadata/公开零值及验证。
- `wlan-profiles.c`：私有 DB 注册、纯 CF 原厂查询和跨进程测试；不是 ubus/RPC。
- `out/wlan-profile-schema.json`：180 字段/类型/default-policy/offset。
- `out/device-db-report.json`：设备验收。

```sh
sh build.sh
python3 device-db-smoke.py --host root@192.168.5.1
```

报告中的 profile/identity **metadata ready 与 fixture read/write 为 true**；
`schema_ready`、`wlan_profile_ready`、`controller_ready` 仍为 false。
后续 B.2.4 已实现受限管道配置桥 PoC，并验证公开 11i/PSK/AES tuple，见
本文 B.2.4 节。真实 UCI/ubus 服务、其他安全模式、完整
32-byte/binary SSID、真实身份、通知/持久化和 CM/driver facade 仍未完成。
下一步验证 Controller-only CM profile 查询，再推进 OSS 启动/受限 1905 LAN。

## B.2.4：ARM64 → ARM32 受限语义配置桥

**已通过管道配置桥的隔离 PoC，不是生产配置服务，也未启动 Controller。**
只使用公开测试值；不读 UCI/真实 PSK/设备身份，不安装服务，不运行
cspd/multiapd，不设置真实无线。原加载探针仍在 DB init 前受控退出 78。

### 结构与权限边界

```text
native AArch64 config-bridge-probe
  → anonymous stdin/stdout pipes (no credential argv/path)
  → native ns-run, fixed --bridge-test mode
  → private IPC/net/mount + read-only root + IPC_OWNER
  → ARM32 db-engine-probe
  → decode/validate → factory libdb GET/SET → readback → non-secret response
```

`mesh-bridge.c/h` 不传原厂 view struct、指针、任意表名/字段名/driver opcode；
两端共享的是**显式字节格式**，不是 C struct packing 或宿主端序。
`ns-run` 只额外允许两个固定模式 `--bridge-test` / `--bridge-rollback-test`，
且 stdin/stdout 必须是 FIFO；任意额外参数、regular-file 输入/输出被拒绝。
继承的 FD≥3 用 `close_range` 全部关闭，含 ≥1024 的 FD；失败仍拒绝运行，
没有 host-IPC 降级。目标内核支持此 syscall；旧内核不是自动降级路径。

### V1 wire format

每进程只处理一个请求；必须收到完整 message 和 EOF，任何追加 byte/第二帧
都拒绝。读/写有 deadline，长度上限固定；输入拒绝发生在创建私有 DB 之前。

请求 **120 bytes**，big endian：

| offset | 字段 |
| ---: | --- |
| 0 | magic `ZMB1` |
| 4 | u16 version=1 |
| 6 | u16 opcode=1：更新逻辑 AP profile |
| 8 | u32 length=120 |
| 12 | u32 request_id，不能为 0 |
| 16 | u8 radio，只允许 0/1 |
| 17 | u8 bss，当前只允许 0 |
| 18 | u8 security=1：WPA2-PSK/AES |
| 19 | u8 enabled=0/1 |
| 20 / 21 | u8 SSID/key 长度 |
| 22 | u16 reserved=0 |
| 24 | 32-byte SSID slot |
| 56 | 64-byte key slot |

保守限制：SSID 为 1..31-byte printable ASCII、passphrase 为 8..63-byte
printable ASCII；slot 未使用部分必须全零。**二进制/完整 32-byte SSID、
64-hex 原始 PSK、其他 BSS、安全模式、接口名和真实 radio 映射均不支持。**

响应 **24 bytes**：`ZMR1` / version=1 / u16 status / length=24 / request_id /
u32 generation / u32 flags。没有 SSID、key、DB 缓冲区、指针或诊断原文。
成功只允许 generation=1、flags=`READBACK|OTHER_ROW`；失败 generation=0，
区分 `REJECTED` / `ROLLED_BACK` / `ROLLBACK_ERROR`。generation 是新建私有
DB 的单次测试序号，**不是持久化版本号、事务 ID 或 replay protection**。

### 真实原厂映射与更新

`bridge_evidence.py` 固定核对 CFAPI SHA256
`66b4f5a8e3761e82a13960718733c45e076decdfd6b560af844782239d10b099`
中的公开 enum 表，并核对实际编译使用的 header constants：

- `tWlanBeaconTypeTable @0x77868`：`11i`=3 → BeaconType。
- `tWlanWPA11IAuthModeTable @0x78790`：`PSKAuthentication`=0 → 11iAuthMode。
- `tWlanWPA11IEncrypTypeTable @0x77848`：`AESEncryption`=1 → 11iEncryptType。

ARM32 端只更新 WLANCfg 的 ESSID/Enable/上述三个 enum，以及 WLANPSK 的
KeyPassphrase；清空 PreSharedKey，避免旧 raw-PSK 优先级冲突。
行 key 仍由原厂 formatter 生成，wire 不接受任意 instance。
写前完整校验并 snapshot，写后读回选中字段、同一行未选中字段及另一行。
成功路径保留其他数据，query/wire/profile 缓冲区以 volatile wipe 清除。

两张表通过两次真实 `dbAPISetView` 更新，**不宣称跨表原子性**：失败后按
snapshot 恢复并读回验证；恢复失败返回明确 ROLLBACK_ERROR，进程退出 81，
不能当作成功。真实 cspd callbacks/并发 Controller reader/OSS 通知还未接入，
不能直接将这个实验更新流程用作生产事务。

### 原生设备验收

```text
ZTE_CONFIG_BRIDGE PASS scenarios=16 native_arm64_to_arm32=1 typed_wire=1 profile_readback=1 other_row_preserved=1 rejected_before_db=1 rollback_verified=1 payload_logged=0 controller_ready=0
```

16 个场景，每个建立独立私有 namespace：

1. radio=0 / enabled=1 的合法更新及读回。
2. radio=1 / enabled=0，验证另一行保持不变。
3–12. 错误 magic/version/length/opcode/reserved/security/radio/key length/
   SSID length/padding 全部拒绝。
13–15. 截断、追加 byte、EOF 空输入在创建 DB 前拒绝。
16. 第一次真实 SET 成功后，固定 rollback 测试模式故意缩短第二个 view，
   **真实原厂 dbAPISetView 返回 -15**；两表恢复、未选中字段/另一行完整，
   响应为 DB_ERROR + ROLLED_BACK，不是伪成功。

设备 `root@192.168.5.1` 原生 AArch64 helper → 原生 ARM32 厂库通过；两段
ELF 架构、完整强符号/版本 closure、上传 SHA256 均核对。公开的实验 SSID/
key/序列号 canary 不在 stdout/stderr；host IPC/接口名称快照一致。

离线新增 49 个 codec/I/O 场景（含 100000 次确定性 mutation corpus），增加
6 个 launcher 拒绝场景。连同此前回归，完整构建 **150 个场景通过**。
codec corpus 的 ASan/UBSan 检查通过；这是 codec 检查，不是原厂库安全审计。
原八表 device-db-smoke 复测保持 counter=45、通过，不破坏既有实验。

### 文件和命令

- `mesh-bridge.c/h`：语义 codec、状态/flag 校验、deadline I/O、wipe。
- `config-bridge-probe.c`：native ARM64 固定公开测试客户端。
- `wlan-profiles.c`：ARM32 typed update/snapshot/readback/restore。
- `device-bridge-smoke.py`：独立审计/打包/SSH/宿主快照验收。
- `out/device-bridge-report.json`：验收报告。
- `out/bridge-security-map.json`：公开 enum 证据。

```sh
sh build.sh
python3 device-bridge-smoke.py --host root@192.168.5.1
```

此程序**没有读取实际 UCI 或提供公开 socket/daemon/ubus endpoint**；只发送
内置公开 fixture，接收端也只更新瞬时私有 DB。`production_bridge_ready`、
`controller_ready` 仍为 false。无 endpoint authentication/replay protection、
真实 credential 生命周期/所有错误路径擦除、通知/持久化或生产并发保证。

下一步恢复 Controller-only CM profile façade：基于已校验逻辑数据构建
原厂调用者期待的 CM struct，不吞掉 setter/SSEND、不伪造硬件成功；然后
再验证 OSS 启动、接口/1905 适配和原厂子节点互通。UCI/ubus/native helper
真实配置接入仍需独立完成，不能把本 PoC 宣称为已能管理节点。

## B.3：真实 multiapd 的隔离 Controller 初始化

### 当前实测结果

这次执行的是原厂 **multiapd 本体**，不是只调用它的数据库 API。
在 `root@192.168.5.1` 上原生 ARM32 运行，40 秒后受控停止：

- 原厂 DB attachment、OSSInitConfig 和四个 OSS task socket 初始化推进成功。
- 真实 `SubShmSrvInit` 创建 key `0xabcb`、128 KiB pool，映射 `0x68000000`；
  链表初始状态、共享段尺寸及原厂锁均验证。不把 OSSStart 替换为空成功。
- 实际进入 `MASTER INIT`，原厂程序创建本地 `1905Device`。
- 在**无物理端口的私有网络 namespace** 中，原厂 AF_PACKET/RAW sockets
  打开成功：1905 `0x893a`，LLC `0x0004`。
- B.3.1 新增两个只读 CM profile reader；PSK 324-byte 输出与真实原厂 getter
  对照一致。真实 RAND_bytes、DH_generate_key 成功，未使用弱随机或成功 stub。
- B.3.2 通过完全私有的独立 lab veth 链路，真实 daemon 收到 Discovery/Query
  （CMDU 0/2），自行发出 284-byte Topology Response（CMDU 3），独立 peer
  校验 MID、地址、DeviceInfo、TLV/EOM 成功。见本文 B.3.2 / B.3.3 节。
- B.3.3：独立 peer 校验双频 AP Autoconfiguration Response（CMDU 8），
  包含正确 MID、band 和 Controller service。公开 WSC M1 使用真实 RNG/DH，
  原厂处理后发出 CMDU 9，但目前只有 34 bytes、没有 WSC M2 配置 TLV。
  **M2 加密/Authenticator 验证和子节点配网均未完成**，报告保持 false。
- 限时停止为 SIGKILL（9），报告明确记录 `controller_ready=false`。
  未验证 stock 节点发现、配网或稳定运行。见本文 B.3.1 节。

完整构建、已有 DB/配置桥设备回归，以及新增真实启动实验均通过；这只表示
实验里程碑和隔离检查通过，**不表示功能完备**。

### 实际修复的平台边界

1. `_initPCBPool` 原先失败，并非已证明无线驱动缺失：原厂任务要在
   `/var/tmp/<task>_{asy,snd}` bind Unix sockets。私有 root 中将 `/var/tmp`、
   `/var/run`、`/var/log` 指向私有 tmpfs 后，真实任务创建继续执行。
2. `OssSubScribInit` 需要 CSP 平时建立的 subscriber pool；单独调用原厂
   server initializer 并验证状态即可。不启动整个 `cspd`。
3. 新增 `P1905ComCfg`（8 字段/328 bytes）、`IPInterface`（48 字段/2404 bytes）
   metadata，与同基线 constructor/type lengths 核对，**不读原厂 defaults**。
   另建立原厂请求的 `MULTIAPDOMAIN1..8`，均为未启用、空凭据的实验行。
   B.3.3 增加 `P1905_ACD`（9/456）、`P1905AcdBssCfg`（5/264）metadata；
   两条 BSS fixture 使用原厂 `DEV` 查询前缀和公开逻辑 radio 配置，非实际 RF 状态。
   本实验显式设置 `VersionMode=2`，不是推断或复用厂商产品默认值。
4. Controller-only `GetRouteIFInfo` 适配器只接受 `DEV.IP.IF1`，实际通过
   Linux ioctl 获取私有 `br0` 的 MAC、flags 和 MTU，失败返回错误。
   B.3.2 另将实验 raw socket 的 br0 alias 绑定到真实独立 veth ingress；
   实际 bind/getsockname、失败及其他协议保留均验证。不是生产 LAN mapper。
   不模拟 WAN、IP、DNS、路由；其余接口返回 -1，且不修改 caller buffer。

路由结构边界来自固定基线 `multiapd:_baseALConfigInit @ 0x632b8`：
初始化 532-byte output，`+172` 为复制到 ALMAC 的六字节地址。
设备端另验证 532-byte 前后 guard、实际 MAC 与未知接口拒绝/缓冲保留。
这个受限适配器**仅用于私有 lab netns 实验，不是生产 LAN mapper**。

### 隔离与日志

- 启动器先 `unshare` IPC/network/mount namespace；无 host network bridge/veth
  移入，没有连接子节点或向宿主 LAN 发送报文。
- 仅此模式在新 netns 中建立 `br-lan`/`br0` 与一对完全私有的 lab veth。
  B.3.2 的 veth-lab/lab-peer 是独立链路，不 enslave 到桥，也不连接宿主接口；
  再丢掉 NET_ADMIN，保留 IPC_OWNER、NET_RAW。
  veth 支持须已存在；不会由测试安装或加载模块。runtime 不保留 SYS_ADMIN。
- root 保持只读，唯一字符设备例外是只读 bind 的内核 `/dev/urandom`（1:9），
  用于真实 legacy crypto。不映射宿主 /dev 目录、MTD、无线/switch 等硬件设备。
- 只读 root、私有 tmpfs、资源上限；子程序 40 秒观察，外层 50 秒 watchdog。
  退出后按原有 process-group 清理和 PID 固持逻辑回收后代。
- 逐文件校验上传包 SHA256。不会安装服务、修改 UCI/flash 或加载厂商模块。
- trace 透传真实 OSS/DB 返回值，仅记录 API/status、固定错误格式和 packet
  header 元信息；不展开格式中的 MAC、实例名、PSK、SSID、serial 或 payload。
- 启动 profile 是公开 fixture，报告检查 fixture credential 未出现在日志；
  **没有读取 OpenWrt UCI/实际凭据或厂商硬编码 defaults**。
- 设备测试前后核对宿主 SysV IPC 与接口名称；私有临时目录已清理。
  该快照并非完整网络状态审计，安全依赖 namespace/capability 边界。

### 重现

```sh
sh build.sh
python3 device-controller-smoke.py --host root@192.168.5.1
```

结果：`out/device-controller-report.json`；限量日志位于
`out/device-controller/probe.{stdout,stderr}`。
默认加载探针仍停止在 DBShmCliInit（78）；需要上述显式启动命令。

### 仍然阻塞可用 Controller 的部分

真实运行观察到这些 DB 请求失败，错误未被吞掉：
`STAIF`、`WLANSetting`、`WLANWEP`、`MultiAPRoamCatonCfg`，
以及实验 WLANCfg 缺少的部分实例。另有硬编码 radio/interface 查询、外部
CSP/SNTP 消息与 change-notify 缺服务；一些 CM getter 仍可能发 setter/SSEND。

Controller-only AP/PSK profile reader 已部分完成，不能据此宣称全部 CM/backend
可用。私有 Discovery/Query/Response 和双频 AP-autoconfiguration 已验证；
WSC M1 已进入真实处理，但尚未生成带配置的 M2。下一步修复必要
getter/platform 状态并验证 M2 认证/解密与公开 profile，而不是
盲目恢复全部 CSP 表或伪造成功。只有上述业务状态进一步确认后，才能谈 stock 节点互通。
在这些完成前，不应把当前包直接接入宿主 LAN 或交给 procd 长期运行。

## B.3.1：Controller-only CM profile 读取与私有 1905 RX

### 结论及边界

在设备 `root@192.168.5.1` 上原生 ARM32 验证了两个受限 CM reader：
`CmWLANGetAPConfig`、`CmWLANGetPsk`。这是公开 fixture 的**逻辑配置视图**，
不是 OpenWrt radio/driver backend，也不宣称本地存在可工作的 BSS。

真实 multiapd 继续启动、执行真实 DH key generation，并从完全私有的 veth
链路收到 IEEE 1905 Topology Discovery（CMDU 0，60 bytes）。后续 B.3.2
已验证 Query RX、真实 284-byte Topology Response 与独立 peer 的 MID/TLV
校验；见本文 B.3.2 / B.3.3 节。
报告保留 `controller_ready=false`，没有测试任何 stock 子节点。

### ABI 与实现

- `controller-profile.c`：只读两张已有私有 DB 表 WLANCfg/WLANPSK。
  避开原厂 `CmWLANGetAPConfig` 内部 WEP setter、CSP/SSEND 和 driver 查询。
  数据库错误、损坏 header/type/status、越界、无终止字符串仍返回错误。
- `cm-profile-evidence.py` 从 hash-pinned `libcmapi.so` 读取 ARM 指令，核对
  AP Getter 的 **43 个 payload copy**，生成 `cm-profile-layout.inc`。
  同时核对 multiapd 的 AP 2496-byte、PSK 324-byte caller buffer。
  只恢复指令/布局 metadata，不读取 factory credentials/defaults。
- AP getter 保留 caller 的 selection/mode records，按恢复的 payload offsets
  填字段及 write/read mask；填逻辑 band/index/BaseViewName。仅调用已检查为
  内存 classifier 的 `wlan_get_websec` / `wlan_set_wifi_ssid_type`，不调用原厂
  高层 CM AP getter，也不把 setter/driver 方法改成空成功。
- 只支持两个公开实验实例 AP1/AP9、WPA2-PSK/AES、8–63-byte passphrase。
  不支持的实例、安全模式或 key 类型拒绝。band 0/1 是批准的逻辑 slots，
  不是由当前设备的 RF capabilities 探测所得。
- PSK getter接受 AP 实例及原厂 caller 的 `%s.PSK1` 形式。返回 CM ViewName
  是 WLANPSK 的 **WLANViewName（AP 实例）**，而不是数据库 PSK row key。
  检查 AP/PSK row 关联。设备测试使用真实 factory getter，核对完整 324-byte
  输出逐字节一致；测试数据只有公开 SSID/key。
- Getter 在 staging buffer 中构造结果，失败不修改 output；临时 profile/key
  buffer 在返回前擦除。日志只有 API/status 和 `hardware_claimed=0`，无 payload。

独立库 `libzte_controller_profile.so` 仅由显式 `--controller-startup` 实验加载。
默认 probe 仍在 DB init 前退出 78。尚未从 UCI 导入实际配置，也未提供生产
认证、动态 reload、真实 WLAN 状态、driver setter 或 ubus helper。

### 真实熵源与 legacy crypto

旧 OpenSSL 需要 `/dev/urandom`。启动模式在私有 mount namespace 中，只 bind
这一项内核字符设备（major/minor 1:9），只读、nosuid/noexec；其余 host `/dev`、
MTD、WLAN/switch 设备不暴露。目标必须是包里的空 regular file，不能是 symlink。

设备先验证真实 `RAND_bytes(32)` 返回 1，并擦除 bytes；启动 trace 透传真实
`DH_generate_key`，观察到两次 status=1，不打印任何 key material。
**没有替换 RAND/DH 为固定值或伪成功**。这只解决初始化前置条件，不代表
obsolete vendor OpenSSL 已通过现代安全审计或适合直接暴露给不可信网络。

### 完全私有的 L2 实验链路

`controller-net.c` 仅在 netns inode 与宿主不同后创建：

```text
private br-lan（空桥，保留实验布局）
private br0（实际 route/AL identity，与独立 ingress 使用相同公开 MAC）
veth-lab ═ 独立私有 veth ═ lab-peer（公开报文 fixture，不 enslave 到桥）
```

没有移动/连接任何 host 接口。veth 支持在当前设备已存在；缺少该前置条件
则拒绝实验，不由测试安装或加载模块。新 veth 先创建 DOWN，两个端点注册后
再 UP，避免 Linux veth peer 尚未建立时的 ENOTCONN。

`controller-packet.c` 仅发送两帧固定、无配置 TLV 的公开报文：Topology Discovery
和 Topology Query，所有流量留在私有 netns。Query 目的 MAC 来自私有 br0 ioctl。
trace 只观察真实 AF_PACKET `recvfrom`/`sendto` header metadata，保留原厂结果。

后续 B.3.2 实测：Discovery/Query 到达实际私有 ingress 的 daemon socket
（本次 ifindex 5、EtherType 0x893a）；原厂 dispatch/Topology Response 与独立
peer 校验通过。**fixture send 或 RX 成功仍不能单独当作业务处理成功**。
Peer 只保留验证元数据，不捕获/保存网络配置或 key payload。

### 验证与重现

```sh
sh build.sh
python3 device-controller-smoke.py --host root@192.168.5.1
```

- 19 个 host CM 场景：正面视图、真实 caller key 形式、损坏 DB header/type/status、
  字符串/guard 越界、不支持安全模式、坏 key/row 关联、未知/NULL 参数、无 sandbox。
- 设备验证两个 AP 的 guards、caller selection/mode 保留、只读 DB snapshot、
  错误 output 保留，和 `%s.PSK1` caller 形式下的 factory PSK 完整输出对照。
- B.3.1 的 169 个离线场景仍通过；加 workspace 14 项和 B.3.2 packet parser
  62 个断言，共 245 项检查。CM/packet ASan/UBSan 与既有回归通过。
  设备前后 SysV IPC 与接口名称不变。
- 原厂进程 40 秒后受控 SIGKILL，外层 watchdog/process-group 清理逻辑不变。

报告：`out/device-controller-report.json`；日志仍做公开 key redaction 检查。
报告区分 `rx1905_observed`、`tx1905_observed`、`native_dh_keygen_succeeded` 和
`controller_ready`。宿主快照不是完整网络状态审计，安全依赖 namespace/capability。

### 下一步

Discovery/Query/Response 闭环已完成；继续验证隔离 lab 内的
AP-autoconfiguration、必要 vendor TLV/WSC 与真实平台状态依赖。
其余 getter、STAIF/WLANSetting/WLANWEP/P1905_ACD/MultiAPRoamCatonCfg、外部 CSP
通知/业务服务仍不完整；错误没有被吞掉。不能因为两个 getter 和 RX 已通过，
就接宿主 LAN、部署 procd 长期服务或宣称 stock EasyMesh 互通。

## B.3.2：真实 IEEE 1905 Topology Query/Response 闭环

### 已实测的边界

在 `root@192.168.5.1` 原生 ARM32、完全私有 network/IPC/mount namespace 中：

- 原厂 multiapd 收到公开 Topology Discovery（CMDU 0）及 Topology Query（CMDU 2）。
- 真实 OSS `ASEND` 将接收报文交给内部任务，观察到 event 37010、4-byte payload、status 0；只记录元数据，不解引用/输出内部消息指针。
- 原厂程序自行构造并 `sendto` 发送 Topology Response（CMDU 3，284 bytes）。
- 独立 `lab-peer` 接收该帧，校验公开源/目的 MAC、query MID `0x1202`、CMDU/fragment flags、TLV 边界、Device Information/ALMAC 和 EOM/padding。
- Peer 验证与 daemon TX/RX 都成为设备脚本的强制通过条件，不再把 fixture send 或 socket receive 当作完整业务成功。

```text
ZTE_CONTROLLER_TRACE rx1905 cmdu=0 bytes=60 ifindex=5 protocol=35130
ZTE_CONTROLLER_TRACE rx1905 cmdu=2 bytes=60 ifindex=5 protocol=35130
ZTE_CONTROLLER_TRACE tx1905 cmdu=3 bytes=284 sent=284
ZTE_CONTROLLER_PACKET fixture=topology-response private_only=1 peer_received=1 cmdu=3 mid_match=1 tlv_sane=1 device_info=1 bytes=284 body_logged=0
```

**这是公开合成 peer 与原厂 daemon 的真实基础协议闭环，不是 stock 节点互通，
也不是 EasyMesh 配网/WSC/M1/M2、真实 BSS 或可用 Controller 验收。**
报告仍为 `controller_ready=false`，40 秒后受控停止。

### 接收阻塞与 lab 平台适配

固定基线机器码显示：

- `multiapd:I5GetInterfaceMacByIfName @0x65370` 对非 WLAN 接口按 `eth`/`veth`、
  `plc`、`moca`、`lo` 前缀处理。`br0` 不是可用的 physical socket-record 名称。
- `_baseRecv1905PacketProc.isra.4 @0x639c0` 在 packet validation/dispatch **之前**
  要找到/建立接口 socket record；仅观察 `recvfrom` 不能证明已经进入消息处理。
- `baseAddSock @0x65868` 调用上述 MAC 获取函数，错误没有通过“返回成功”屏蔽。

最初桥端 RX 用真实 Linux `br0` ifindex，进入内部接口路径后无法推进。
将 raw socket 绑定到已 enslave 的 veth 端口也不能代替桥端绑定：当前 Linux
bridge RX handler 会消耗/重新归属 protocol-specific AF_PACKET 报文。

现在的 **lab-only** 网络为：

```text
br-lan：私有空桥，保留兼容实验布局
br0：私有空桥，DEV.IP.IF1/IFLink/ALMAC 的实际 Linux identity
veth-lab ═ 私有 veth ═ lab-peer：不 enslave 到任何桥的独立实验链路
```

`controller-net.c` 在独立 netns、尚有 NET_ADMIN 时创建以上对象：
`br0` 与 `veth-lab` 设置相同的公开、locally-administered MAC；两端先 DOWN
创建，再 UP。没有从宿主移动任何设备或连接宿主 LAN，随后丢掉 NET_ADMIN。

`controller-route.c` 中的受限 `bind` mapper：

- 只在 sandbox、AF_PACKET、精确 sockaddr_ll 尺寸、1905/LLC 协议且目标为
  实际私有 br0 ifindex 时，改绑到实际私有 `veth-lab` ifindex。
- 调用真正 Linux bind；接口不存在或 syscall 失败仍报错。不会改写
  `recvfrom`、MAC ioctl、原厂 PDU、caller sockaddr 或其他协议绑定。
- GetRouteIFInfo 实际读取 `br0` 的 MAC/flags/MTU；与 IPInterface Name、
  MultiAPCfg IFLink、原厂 ALMAC 一致。不是虚构无线驱动状态。
- 设备验证 getsockname 的实际绑定、输入保留、坏 fd 的真实 EBADF，以及
  IPv4 协议仍绑定原始 br0；均是启动强制检查。

这不是生产 `br-lan`/DSA mapper，也不能直接用来接宿主 LAN。
生产实现仍需明确 Linux bridge/物理 ingress 与原厂接口模型的对应关系。

### 日志、验证与资源

- Packet/crypto/dispatch 元数据有独立 64-line budget；普通 DB/启动日志上限
  160。普通错误耗尽配额不会遮蔽 Query/Response；报告分别记录配额耗尽。
- Trace 透传原始函数结果、errno，不设置 OSS task verbosity/业务状态，不输出
  日志 varargs、网络 TLV body、SSID/PSK 或 DH material。
- `controller-packet.c` 在发送 Query **前**绑定私有 peer RX，最多等待 4 秒、
  处理 512 帧；不写 pcap/raw frame 文件，仅输出验证元数据。
- Peer parser 有 62 个断言、100,000 次确定性变异，ASan/UBSan 通过；覆盖
  截断、错误 MID/MAC/header、缺失/重复 DeviceInfo、畸形长度/接口记录及 EOM。
- 当前离线合计 245 项检查通过（包含 14 项 workspace 清理测试与新 62 个
  parser 断言；两套各 100,000 次 codec 变异不算入检查数量）。
- 设备回归确认公开 credential canary 不出现在日志、临时目录自动删除、
  宿主 SysV IPC 与接口名称快照未变化。快照不是完整网络状态审计；隔离
  依赖 namespace、capability、只读 root、watchdog/process-group 边界。

```sh
sh build.sh
python3 device-controller-smoke.py --host root@192.168.5.1
```

主要证据：`out/device-controller-report.json`、
`out/device-controller/probe.{stdout,stderr}`。

下一步仍留在隔离 lab：验证 AP-autoconfiguration/必要的 ZTE vendor TLV、
WSC/M1/M2 及真实 profile/identity getter 依赖。不启动 cspd，不恢复厂商
硬编码 credentials，不部署 procd 或接入 stock 子节点，直到平台与协议
状态机边界经独立验证。
