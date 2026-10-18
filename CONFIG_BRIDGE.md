# B.2.4：ARM64 → ARM32 受限语义配置桥

> **状态更新（2026-10-12）：** 本文记录 B.2.4 里程碑（ARM64 → ARM32 管道配置桥 PoC，`config-bridge-probe`）。控制器服务（`controller-service.c`）直接编进 ARM32 的 `db-engine-probe`，在沙箱里写数据库，**不经过这条管道**，但复用了 `mesh-bridge.c` 里的 `mb_profile` 结构和校验（`mb_validate`）。

**已通过管道配置桥的隔离 PoC，不是生产配置服务，也未启动 Controller。**
只使用公开测试值；不读 UCI/真实 PSK/设备身份，不安装服务，不运行
cspd/multiapd，不设置真实无线。原加载探针仍在 DB init 前受控退出 78。

## 结构与权限边界

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

## V1 wire format

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

## 真实原厂映射与更新

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

## 原生设备验收

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

## 文件和命令

- `mesh-bridge.c/h`：语义 codec、状态/flag 校验、deadline I/O、wipe。
- `config-bridge-probe.c`：native ARM64 固定公开测试客户端。
- `wlan-profiles.c`：ARM32 typed update/snapshot/readback/restore。
- `device-bridge-smoke.py`：独立审计/打包/SSH/宿主快照验收。
- `out/mesh-shim/device-bridge-report.json`：验收报告。
- `out/mesh-shim/bridge-security-map.json`：公开 enum 证据。

```sh
sh port/mesh-shim/build.sh
python3 port/mesh-shim/device-bridge-smoke.py --host root@192.168.5.1
```

此程序**没有读取实际 UCI 或提供公开 socket/daemon/ubus endpoint**；只发送
内置公开 fixture，接收端也只更新瞬时私有 DB。`production_bridge_ready`、
`controller_ready` 仍为 false。无 endpoint authentication/replay protection、
真实 credential 生命周期/所有错误路径擦除、通知/持久化或生产并发保证。

下一步恢复 Controller-only CM profile façade：基于已校验逻辑数据构建
原厂调用者期待的 CM struct，不吞掉 setter/SSEND、不伪造硬件成功；然后
再验证 OSS 启动、接口/1905 适配和原厂子节点互通。UCI/ubus/native helper
真实配置接入仍需独立完成，不能把本 PoC 宣称为已能管理节点。
