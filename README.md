# 原厂 multiapd 二进制垫片：在 SR1010 上用原厂控制器管理真实 ZTE 子路由

目标：OpenWrt/ImmortalWrt 主路由（SR1010，有线网关，无 Wi-Fi）管理保留原厂固件的有线子节点。做法：把原厂 ARM32 `multiapd` 当 Controller 跑在私有沙箱里，
用数据库行、一个只替换两个函数的 CM 接口替身和一个服务配置文件来指挥它，**不重写 mesh 协议**。原理见 [FINDINGS.md](FINDINGS.md) 第 1 节。

## 当前状态（2026-10-12）

仍**不是可用产品**：procd init、UCI 映射和 LuCI 表单已写好（2026-10-17，覆盖服务配置的全部键，见 SERVICE.md "UCI 与 LuCI 映射"），但只用桩和私有实验室测过，**没有在真实 OpenWrt 上运行过**；`cspd` 不启动；控制器目前由测试脚本 `dev-service-smoke.py` 拉起，不是常驻服务；
代码尚未提交到 git（见 [TESTING.md](TESTING.md) 第 8 节）。

已在一台真实子路由（ZXHN Z506，V1.0.0.2B2.8000）上验证（[REAL_CHILD.md](REAL_CHILD.md)）：

- 发现、拓扑、AP-Autoconfig + WSC M1/M2、子路由实际应用 SSID/密钥、重复入网、改配置后 Renew。
- 配置项：主/访客/备用 SSID 的名称、密码、开关（多 BSS），BeaconInterval，信道与发射功率（`0x8006`），MAC 过滤名单与主 SSID 的策略（含真实终端被拒绝接入；槽位上的 ACL 没试过），
  `CurHop`（已修复为 2）。热更新大多可用。
- 子路由自适应角色选举能被我们的控制器接受，子路由成为 Slave；其 Web 里主/备用 Wi-Fi 配置和射频页随之被锁（由子路由本地标志决定，见 FINDINGS.md 第 8 节）。
- **自组网 V2 默认关闭**（服务配置 `mesh.v2=0`；`mesh.v2=1` 恢复严格检查）。两种取值接关着 V2 的子路由都正常。

没有验证或做不到：子路由开着 V2 且控制器关 V2 的真机组合、`allow` 策略、频段引导的实际效果；Z506 固件不解析漫游阈值，也没有下发带宽的路径；终端动作（踢/封/引导）、状态输出、长稳未做。

## 文档索引

| 文档 | 内容 |
|---|---|
| [REAL_CHILD.md](REAL_CHILD.md) | **从这里开始。** 真子路由联调：状态、入网门槛、服务配置语法、各功能验证记录、联调流程、风险、未完成项 |
| [FINDINGS.md](FINDINGS.md) | 逆向与真机发现汇总：角色选举、自组网 V2、`VersionMode`、`CurHop`、Web 锁定、ACL 内部、两份固件差异、子路由调试方法、未解决问题 |
| [TESTING.md](TESTING.md) | 机器与连接、构建/打包、跑控制器、离线测试、真机测试守则、踩过的坑、验证清单、版本控制状态 |
| [CONTROLLER_CAPABILITIES.md](CONTROLLER_CAPABILITIES.md) | 原厂控制器能下发的全部配置（五条通道）与 Z506 的实际支持，以及本项目的覆盖情况 |
| [tools/README.md](tools/README.md) | 构建/打包脚本、子路由 telnet 客户端、只读读回脚本、抓包解析、示例测试驱动 |
| STARTUP、CM_PROFILES、WIRE_PROTOCOL、DATABASE、SCHEMA、POLICIES、WLAN_PROFILES、CONFIG_BRIDGE | 早期里程碑（B.1 到 B.3.3）的实测记录，每篇开头有状态更新说明 |

## 快速开始

```sh
# 构建与打包（构建机上编译，见 TESTING.md 第 2 节）
scp -q <改过的文件> 192.168.174.5:/Volumes/code/zx279133/port/mesh-shim/
ssh 192.168.174.5 'sh /tmp/build8.sh'                     # 副本：tools/build/macmini-build.sh
python3 -I /tmp/svctest-repack/repack.py                    # 副本：tools/build/repack-svctest.py

# 在路由器上跑控制器（真子路由示例；配置语法见 REAL_CHILD.md 第 3 节，默认自组网 V2 关）
python3 dev-service-smoke.py --seconds 3000 --expect-macs <允许的 MAC 列表> --watch-mac <子路由 MAC> \
  --config-file <配置> --version-mode 0 --no-change \
  --real-manufacturer ZTE --real-model "ZXSLC SR1010" --real-market-sign 8000 --pcap-out run.pcap --log-out run.log
# 停止：pkill -INT -f "dev-service-smoke[.]py --seconds 3000"

# 离线测试（逐个脚本，第一个参数是产物目录）
for t in tests/test_*.py; do python3 $t ../../out/mesh-shim; done
```

## 重要的默认值与约定

- 自组网 V2（控制器 `P1905ComCfg.Enable`）：**默认关**；对外声明的身份（ZTE / `ZXSLC SR1010`）和市场码（`MarketConfEn=1`、`8000`）不变。
- `version_mode=0`（bit1 清零，才会发 ZTE 私有厂商 TLV）；`EnMasterOnly=1`（SR1010 没有无线电）且 `CurHop=2`（等于 `MaxHop`）。
- 真机测试只用配置文件里的显式值（永远不推占位值），不改子路由的模式，不用 `kill multiapd` 触发重新入网，未知 MAC 先问用户（TESTING.md 第 5、6 节）。

## 历史里程碑（B.1 到 B.3.3）

> 以下是各里程碑当时的验收记录，保留原文；后续进展见上面的"当前状态"。其中"仍为 `controller_ready=false`""M2 尚未完成"等是当时的状态。


- 从 `vendor-reference/2b5/rootfs.jffs2` 重组嵌套 SquashFS，校验 SHA256、
  JFFS2 header/dirent/inode/data CRC、解压长度及完整覆盖。
- 提取 `multiapd` 和 27 个依赖库。库别名在私有运行包内展平为普通文件；
  不使用来源不同的散放 `cspd`，不包含原厂启动脚本或内核模块。
- 校验 ARM32 EABI5 soft-float、强符号及 GNU 符号版本需求；垫片直接链接
  原厂 glibc 2.26 库，避免交叉工具链的新 glibc 依赖进入目标。
- 隔离 Docker/QEMU 中，原厂 loader 加载成功；`LD_BIND_NOW=1` 下垫片
  constructor 执行，`DBShmCliInit` 被拦截并受控退出 78。
- 17 个证据处理单元测试、16 个 Cfg schema 测试、20 个策略表测试、28 个
  WLAN/identity metadata 测试、49 个 bridge codec（含 100k corpus）、5 个 preload
  和 15 个沙箱场景通过，共 150 个回归场景；codec ASan/UBSan 通过。
- 首次通过 SSH 在 `root@192.168.5.1` 测试时，原生 ARM32 execve 返回
  ENOEXEC（8），当时使用显式 QEMU 验证库加载和拦截。
- 用户启用 `CONFIG_COMPAT` 后，在运行中的 Linux 6.18.38+ 上重新实测：
  **原生 ARM32 loader 退出 0，原生数据库拦截探针退出 78**，
  `device-report.json` 记录 `execution=native-arm32`，不再需要设备端 QEMU。
- 加载测试只写独立 `/tmp/zte-mesh-probe.*` 目录，没有刷机、重启、启动
  Controller 或执行网络配置；测试前后接口名称枚举一致。
- B.1 数据库实验在 `/tmp/zte-mesh-db.*` 的私有 IPC/网络/mount namespace
  通过：原厂 server/client、4 MiB shared memory、锁、allocator 和跨进程
  计数均工作；真实 `MultiAPCfg` 查询因缺少 schema 返回 -2，高层 MapGetDB
  返回 -1，没有伪成功。详情见 [DATABASE.md](DATABASE.md)。
- B.2 扩展恢复 `MultiAPCfg` 的 50 个字段和 2952-byte 视图，逐项核对固定
  基线机器码；设备上真实 MapGetDB、CM getter 和 masked SET/读回均通过，
  malformed query 仍返回错误，宿主状态快照未变化。详情见 [SCHEMA.md](SCHEMA.md)。
- B.2.2 继续恢复 Master 55 字段/2312 bytes、Domain 106 字段/3464 bytes，
  真实 CF/CM 读写、Domain 列表/过滤、两行更新隔离和服务端写入读回通过；
  此阶段共享 GET 计数为 21。详情见 [POLICIES.md](POLICIES.md)。
- B.2.3 新增 WLANBase/Cfg/SSID/PSK/DevInfo 五表、180 字段，纯原厂 CF GET、
  实验 ESSID/PSK 更新、行隔离和服务端读回通过；最新共享 GET 计数为 45。
  不读取 source defaults，不执行有副作用的 CM Getter；原厂 core-view callbacks
  尚未恢复。详情见 [WLAN_PROFILES.md](WLAN_PROFILES.md)。
- B.2.4 受限 big-endian wire/pipe 桥设备实测 16 个私有 namespace 场景通过，
  包括 11i/PSK/AES 公开 enum 映射、typed update、同/跨行数据保留、畸形输入
  拒绝及原厂 -15 错误后的 rollback 验证。详情见 [CONFIG_BRIDGE.md](CONFIG_BRIDGE.md)。

- B.3 已在设备上原生启动**真实 multiapd**，保留原厂 OSS 任务/订阅调度：
  修复私有 `/var/tmp` socket 路径，实际初始化 128 KiB subscriber pool；新增
  P1905ComCfg/IPInterface metadata 和 8 个未启用 Domain 行。隔离 netns 中
  建立私有 lab 桥，实际读取 Linux MAC/MTU，并成功打开 1905 与 LLC packet socket。
  40 秒观察后受控停止；未接宿主 LAN，不是可用 Controller。见 [STARTUP.md](STARTUP.md)。
- B.3.1 实现只读 Controller-only AP/PSK CM reader（43 个 AP payload copy、
  2496/324-byte ABI）；native PSK 与原厂 getter 完整对照一致。真实 kernel entropy
  和 DH key generation 通过，私有 veth 链路上观察到 daemon 1905 Discovery RX。
  见 [CM_PROFILES.md](CM_PROFILES.md)。
- B.3.2 已修复私有 ingress 映射：真实 daemon 收到 Discovery/Query，自行发出
  284-byte Topology Response，独立 peer 校验 MID、地址、DeviceInfo/TLV/EOM。
  245 项离线检查及 native 隔离回归通过；目录自动清理、宿主快照不变。
  仍为 `controller_ready=false`，未测试 stock 节点或配网。见 [WIRE_PROTOCOL.md](WIRE_PROTOCOL.md)。

## 构建

主机需要 Python 3、Docker 和 `unsquashfs`。Docker 构建工具镜像时需要联网
安装交叉编译器；编译、测试和 firmware probe 均使用断网、去 capability 的容器。

从仓库根目录执行：

```sh
sh port/mesh-shim/build.sh
```

产物位于 `out/mesh-shim/`，不改动现有 `out/rootfs/` 或 FIT：

```text
runtime/bin/multiapd                 原厂程序
runtime/lib/                        27 个原厂库 + libzte_mesh_shim.so
manifest.json                       证据来源、逐文件哈希与 ELF/版本审计
vendor-abi.json                      下一阶段需要恢复的导入 API/入口清单
smoke-report.json                    离线加载及拦截结果
exec-check                          AArch64 静态 execve/errno 探针，10 秒 alarm
qemu-arm-static                     AArch64 Linux 上运行 ARM32 的实验工具
db-engine-probe                     ARM32 原厂 DB 服务端/客户端实验
ns-run                              AArch64 静态私有 namespace 启动器
config-bridge-probe                 AArch64 固定公开配置桥测试客户端
bridge-security-map.json            同基线公开 WPA2-PSK/AES enum 证据
device-bridge-report.json            16 个跨架构/拒绝/rollback 隔离验收
device-db-report.json                八表逻辑 GET/SET/CF/CM/list/跨进程写入验收
device-controller-report.json        真实 multiapd 初始化里程碑与尚未就绪状态
multiap-cfg-schema.json              Cfg 50 字段原始类型/default/视图 offset
mesh-policy-schema.json             Master/Domain 161 字段及 primary/save/offset
wlan-profile-schema.json            WLAN/DevInfo 180 字段 metadata 与公开零值
analysis/cspd                       0600 静态证据；不进 runtime、不部署执行
```

重复准备运行包会核对已有原厂文件哈希，不静默替换不同版本的固件。
换固件基线需要重新调查并明确更新固定哈希，不提供跳过校验选项。

## SSH 设备测试

```sh
# 原生 ARM32 探针；当前已启用 COMPAT 的设备通过，不携带 QEMU。
python3 port/mesh-shim/device-smoke.py --host root@192.168.5.1

# 显式允许在原生 ENOEXEC 后做 QEMU 探针（不是生产运行方案）。
python3 port/mesh-shim/device-smoke.py --host root@192.168.5.1 --emulate
```

设备上无需 Python、strace 或 timeout applet。测试包逐文件校验 SHA256，
通过静态 `exec-check` 避免 shell 把 ARM32 ELF 当脚本解释造成误诊。
输出保存在 `out/mesh-shim/device-report.json` 与 `out/mesh-shim/device/`。
临时测试目录保留用于调查；不会安装服务或修改 `/etc`、`/lib`。

数据库实验需要真正的 IPC/网络/mount namespace，失败不降级；该命令不启动
multiapd/cspd、不部署 QEMU，按固定受限命令使用已有真实后端进行 trace：

```sh
python3 port/mesh-shim/device-db-smoke.py --host root@192.168.5.1

# 只用公开 fixture 的 ARM64 -> ARM32 管道配置桥。
python3 port/mesh-shim/device-bridge-smoke.py --host root@192.168.5.1

# 显式启动真实 multiapd，40 秒限时，只在空的私有网络/IPC/mount namespace。
python3 port/mesh-shim/device-controller-smoke.py --host root@192.168.5.1
```

## 垫片模式

- 未设置模式或 `ZTE_MESH_SHIM_MODE=probe`：默认安全边界探针，在 DB init
  之前退出 78；直接遇到 DB read 也拒绝。**78 是预期停止，不是管理成功。**
- `trace` 还要求显式 `ZTE_MESH_SHIM_ALLOW_TRACE=1`：仅在已有合法数据库后端
  的隔离实验环境使用。向原厂函数透传调用和返回值，日志只记录 API、view 名和
  状态，不记录 instance/key、配置缓冲区、PSK 或指针。
- 未知模式退出 78；找不到真实透传函数退出 79。日志全进 stderr，限量 128 条。

默认 `libzte_mesh_shim.so` 仅拦截两个外部 ABI（DB 实验另调用原厂初始化/锁/allocator API；
B.3 显式启动模式另加载独立启动 trace、受限私有 L2 adapter 与只读逻辑 CM reader）：

```c
int DBShmCliInit(void);
int dbAPIGetView(const char *view, const char *instance, void *data);
```

没有提供数据库模拟、生产 WLAN backend、外部 cspd 事件或原生 ubus helper。
独立 DB 实验中有公开的逻辑 profile fixture，不是已经导入 UCI 的真实配置。
**不要在当前运行包上手动开启 trace 模式来绕过 probe**：原厂 DB init 失败后
主程序可能继续运行，随后解引用未初始化数据库状态。

## 下一步

1. 补齐小项配置与终端动作：MaxHop、认证/加密方式、无线电开关、上报参数；踢/封/引导终端；终端与 AP 指标的状态输出（LuCI 要用）。
2. 真机补验：子路由开着 V2 时控制器 `mesh.v2=0`；`allow` 策略；`Policy=2` 的限制行为。
3. 长稳与控制器异常退出后的恢复；`AL MAC` 可配置/唯一。
4. 把代码提交进 git（见 [TESTING.md](TESTING.md) 第 8 节）。
5. 最后：procd 服务化与 LuCI 应用。

原厂程序/库包含旧 OpenSSL，产物仅用于私有工程验证；发布前还需处理安全维护、最小权限、设备/网络隔离和二进制再分发许可。chroot 或 preload 均不构成完整沙箱。
