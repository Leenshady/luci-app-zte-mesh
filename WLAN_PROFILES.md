# B.2.3：WLAN / DevInfo 布局与隔离逻辑 profile

> **状态更新（2026-10-12）：** 本文记录 B.2.3 里程碑（WLAN/DevInfo 布局与逻辑 profile）。之后 `wlan-profiles.c` 增加了：额外 BSS 行（槽位 1 到 3，`wlan_profiles_add_extra_bss`）、BeaconInterval（`wlan_profiles_set_beacon_interval`）、每 BSS 的 `ACLPolicy`（`wlan_profiles_set_acl_policy`）和 `AclCfg` 规则（`wlan_profiles_acl_apply`）。真机验证见 [REAL_CHILD.md](REAL_CHILD.md)。

**通过的是字段布局、纯 DB/CF 查询和公开 fixture 的读写，仍不是可用
Controller / 完整 WLAN backend。** 没有启动 cspd/multiapd，没有设置真实
无线或读取原厂密码/序列号，也没有接入 LAN。

## 实际 Controller 调用链与风险

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

## 新恢复的五张表

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

## 不复制原厂默认值

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

## 原生设备验收

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

## 文件与下一步

- `profile_schema.py`、`wlan_*.def`、`dev_info.def`：metadata/公开零值及验证。
- `wlan-profiles.c`：私有 DB 注册、纯 CF 原厂查询和跨进程测试；不是 ubus/RPC。
- `out/mesh-shim/wlan-profile-schema.json`：180 字段/类型/default-policy/offset。
- `out/mesh-shim/device-db-report.json`：设备验收。

```sh
sh port/mesh-shim/build.sh
python3 port/mesh-shim/device-db-smoke.py --host root@192.168.5.1
```

报告中的 profile/identity **metadata ready 与 fixture read/write 为 true**；
`schema_ready`、`wlan_profile_ready`、`controller_ready` 仍为 false。
后续 B.2.4 已实现受限管道配置桥 PoC，并验证公开 11i/PSK/AES tuple，见
[CONFIG_BRIDGE.md](CONFIG_BRIDGE.md)。真实 UCI/ubus 服务、其他安全模式、完整
32-byte/binary SSID、真实身份、通知/持久化和 CM/driver facade 仍未完成。
下一步验证 Controller-only CM profile 查询，再推进 OSS 启动/受限 1905 LAN。
