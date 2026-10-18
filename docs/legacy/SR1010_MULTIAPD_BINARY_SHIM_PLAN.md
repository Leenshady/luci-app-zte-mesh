# SR1010 multiapd 二进制垫片方案

## 目标与建议

用户已限定前期目标：**OpenWrt 主路由继续管理保留原厂固件的子节点**。前期先验证有线连接，不移植本地无线 Agent，不替换原厂子节点，也不重新实现整套 1905/Multi-AP 协议。

在此范围下，建议把“原厂 multiapd + 同版本运行库 + ARM32 垫片”的启动/互通 PoC 放到原生协议重写之前。它能保留原厂的私有报文、认证、重传和 Controller 策略，减少协议层重写；目前完成加载、八表/391 字段逻辑配置与真实 multiapd 私有 Controller 初始化实验，尚未经过完整 Controller/节点互通验证，不能宣称肯定比重写省时或已能管理节点。

垫片应优先覆盖**平台边界**，保留协议引擎和 OSS 内部调度。不是写一个空函数库，让所有调用都返回成功。

## 新确认的静态事实

基线仍是 `rootfs.jffs2` 的嵌套 `kmodule.img` 中提取的同版本程序/库，来源和 SHA256 见 `SR1010_VENDOR_MESH_OPENWRT_PORTING.md`。

- `multiapd` 的静态 `DT_NEEDED` 传递闭包为 27 个库，全部能在 `out/led-vendor-kmodule/lib/` 找到。
- 对该闭包及主程序按动态符号名检查，没有发现无法由闭包提供的强未定义符号。**这不是符号版本、动态加载模块或运行服务的完整验证。**
- 程序和所检查的原厂接口库 `e_flags=0x05000200`：ARM EABI5、soft-float ABI。不能给 ARM32 程序注入 ARM64/musl 或不匹配的 hard-float `.so`。
- 检查的 `liboss.so`、`libdb.so`、`libcmapi.so`、`libcfapi.so` 没有报告 `DT_SYMBOLIC`/相关动态标志；反编译也能观察到部分 PLT 调用，支持优先试验符号 interposition。是否覆盖所有调用仍需绑定跟踪与汇编验证。
- `liboss.so` 导入 `shmget/shmat/shmctl`，提供线程、事件、timer、socket、SSEND/ASEND 等运行时功能，优先保留而非重写。
- `libdb.so:DBShmCliInit @ 0x6e80` 连接 SysV 共享内存并初始化进程内数据库指针；`dbAPIGetView @ 0x3e38` 使用共享计数、锁和数据库数据。这些状态缺失时，空实现 `DBShmCliInit()` 不能保证后续安全。
- `DBShmOpenLib @ 0x6c6c` 尝试 `dlopen("libdbcspview.so", RTLD_NOW)`；在当前两个提取目录未找到此文件。其是否可选或由运行态生成，需继续调查，不能只按 `DT_NEEDED` 打包。
- `SSEND @ 0xb63c` 区分内部任务和外部服务，具有同步响应和返回长度语义。不能一概吞掉 `SSEND/ASEND`，否则会破坏 multiapd 内部任务通信。
- 初始检查的主线诊断内核同时关闭 `CONFIG_COMPAT` 和 `CONFIG_SYSVIPC`；`CONFIG_PACKET` 已开启。这是初始诊断配置，不代表当前运行中的 OpenWrt 内核。

以上为初始静态调查结论。后续已实现 `port/mesh-shim/` 第一阶段：证据打包、ARM32 垫片、ABI/符号版本审计、隔离加载测试和 SSH 设备探针。首次设备测试原生 ARM32 返回 ENOEXEC，显式 QEMU 测试通过；**用户启用 CONFIG_COMPAT 后，SSH 复测已确认原生 loader 退出 0、DBShmCliInit 拦截探针退出 78，无需设备端 QEMU**。设备 SysV IPC 接口也已存在。默认探针在数据库初始化前受控退出，尚未实现 Controller 数据库/配置适配或验证节点入网；测试工具没有调整内核或重启设备。**后续 B.1 实验已在私有 IPC/网络/mount namespace 中通过原厂 DB server/client、4 MiB 共享内存、锁、allocator 与跨进程计数测试**；B.1 的原厂 `MapGetDB` 查询因缺表返回真实错误；后续 B.2 已从同基线 cspd 逐项恢复 `MultiAPCfg` 的 50 个字段/2952-byte 视图，设备侧原厂 GET、CM 转换、masked SET/读回及错误请求拒绝均通过。B.2.2 继续恢复 Master 55 字段/2312 bytes 和 Domain 106 字段/3464 bytes，原厂 CF/CM 读写、Domain 列表/过滤、行隔离及服务端跨进程写入读回均通过，共 211 字段。B.2.3 新增 WLANBase/Cfg/SSID/PSK/DevInfo 五表 180 字段 metadata，共八表 391 字段；纯原厂 CF GET、实验 ESSID/PSK 更新、两行隔离及跨进程读回通过，95 个回归场景通过。B.2.4 实现受限 ARM64→ARM32 管道配置桥 PoC：150 个离线回归场景、codec ASan/UBSan 与 16 个原生设备跨架构/拒绝/真实 DB 错误 rollback 场景通过；核对公开 11i/PSK/AES enum tuple。仅使用公开私有测试行，不读 UCI/真实 credential 或原厂 default。发现部分 CM Get 也调用 setter/SSEND，尚需 Controller-only facade；生产配置服务/认证、安全模式/真实身份、core callbacks、变更通知/持久化仍未完成。B.3 进一步原生执行真实 multiapd：修复私有 OSS socket 路径，初始化真实 subscriber pool；恢复 P1905ComCfg/IPInterface metadata，使用实际私有 Linux bridge identity 适配 GetRouteIFInfo。实测进入 MASTER INIT、创建本地 1905 device，1905/LLC packet sockets 成功打开；40 秒后受控停止。仍未执行 cspd、未接宿主 LAN、未观察到 1905 TX，controller_ready=false；尚缺 STAIF/WLANSetting/WLANWEP/P1905_ACD/MultiAPRoamCatonCfg。B.3.1 已实现两个只读 Controller-only CM profile reader：2496-byte AP getter 的 43 个 payload copy 来自 pinned ARM 指令证据；324-byte PSK 输出在原厂 `%s.PSK1` caller 形式下与真实 getter 完整对照一致。新增 19 个 host CM 边界场景；以只读内核 urandom 为唯一字符设备例外，真实 legacy RNG/DH 成功。B.3.1 完全私有 lab veth 链路上首先观察到真实 daemon 的 Topology Discovery RX。后续 B.3.2 修复 lab-only physical ingress 绑定及 AL identity：真实原厂 daemon 收到 Discovery/Query，自行生成 284-byte Topology Response；独立私有 peer 校验公开地址、MID、DeviceInfo/TLV/EOM 通过。245 项离线检查、packet ASan/UBSan、native 隔离回归与自动目录清理均通过，宿主状态快照未变化。尚未测试 AP-autoconfiguration/WSC、stock 节点入网或真实 radio，仍为 controller_ready=false，不部署服务或接宿主 LAN。详见 `../port/mesh-shim/STARTUP.md`、`../port/mesh-shim/CM_PROFILES.md`、`../port/mesh-shim/WIRE_PROTOCOL.md`。构建、测试与验收详情见 `../port/mesh-shim/README.md`、`../port/mesh-shim/DATABASE.md`、`../port/mesh-shim/SCHEMA.md`、`../port/mesh-shim/POLICIES.md` 、`../port/mesh-shim/WLAN_PROFILES.md` 和 `../port/mesh-shim/CONFIG_BRIDGE.md`。

## 建议架构

```text
OpenWrt 原生服务（musl / ARM64）
  procd、UCI、ubus、接口与配置管理
                 │
  zte-mesh-bridge：受限 Unix socket RPC
                 │
隔离的原厂运行环境（glibc / ARM32）
  libzte_mesh_shim.so
    ├─ 配置/数据库适配
    ├─ 接口、MAC、链路、设备信息适配
    └─ 必要的外部服务/通知适配
                 │
  原厂 multiapd + liboss + 其余必要库
                 │
  LAN 1905 控制报文 → 原厂子节点
```

建议使用独立 rootfs，例如 `/opt/zte-mesh/`，保留原厂 ARM32 loader/glibc，垫片用匹配的 ARM32/glibc 工具链编译。通过原厂 loader 与 `LD_PRELOAD` 加载，不能覆盖 OpenWrt 的 `/lib` 或让 ARM32 垫片污染原生服务。

chroot 只解决路径/运行库隔离，不是完整安全边界。生产化还需最小权限、私有临时目录、必要的 mount/IPC namespace、设备访问限制和 LAN 范围控制。原厂进程不应获得 MTD、厂商硬件设备和任意 OpenWrt 配置修改权限。

原生 helper 和 ARM32 垫片之间采用带长度、版本和固定宽度字段的 RPC。不要跨进程直接传指针、原始 ARM32 结构体或共享其指针数据库；helper 按语义返回字段，垫片再按恢复的原厂 ABI 打包。

## 垫片职责和边界

| 边界 | 建议适配 | 不能做的简化 |
| --- | --- | --- |
| 配置/数据库 | Controller 模式、设备身份、远端 AP 期望 SSID/安全配置、策略和节点运行态；保证查询、列表、写入及通知一致 | 所有 Get 返回全零，或只拦截 DBShmCliInit |
| 网络接口 | 将原厂逻辑接口名与 OpenWrt 的 bridge、DSA port、ifindex、MAC、链路状态映射 | 为兼容 br0 而随意改 OpenWrt bridge 名，或只替换一个字符串 |
| 外部 IPC | 仅接管确实发往 cspd 等外部服务、且 Controller 所需的请求；实现对应同步响应、异步事件和生命周期 | 把所有 SSEND/ASEND 都返回成功；吞掉内部任务通信 |
| 原厂副作用 | 限制进程启动、设备写入和厂商脚本调用；无关操作返回明确不支持 | 运行整个 cspd/原厂启动脚本，或在宿主开放任意命令执行 |
| 协议与调度 | 保留 multiapd、OSS 内部事件/计时/消息、密码及报文逻辑 | 一开始替换 OSSStart，或重写 1905 传输/认证流程 |

配置/数据库适配需根据实际调用选择：

1. 在高层 API 截住控制面需要的配置和状态，并完整覆盖实际调用；或
2. 若原厂库存在不可绕过的共享状态/底层访问，复用原厂数据库引擎并实现最小初始化和 schema/default 服务。

现在尚未恢复所有 Controller 需要的结构体和数据库表，不能断言第一种只需几个函数，或第二种能无改动启动。应以启动跟踪选择，而不是先写大量猜测结构体。

**没有本地 WLAN 不等于没有无线配置。** 主控仍需要远端 AP 的期望 SSID、密码、安全模式和频段 profile；必须区分这些逻辑配置与真实本地 radio 能力，不能把全部 `CmWLAN*` 都实现为“没有设备”。

此外，`LD_PRELOAD` 主要适用于动态绑定的外部符号。主程序里的 `baseBindSocketToBr0` 等内部函数，不保证能用同名 preload 函数覆盖。优先适配其外部接口；必要时对工作副本做小型、可追溯补丁，记录原始 SHA256 与偏移，保持固件证据不变。

## PoC 顺序与继续/停止条件

### A. 先证明运行库可加载

在目标 ARM64 Linux/兼容内核上，用原厂 loader 的 `--list` 验证库加载；再用匹配的最小 ARM32 垫片验证 preload 和外部符号绑定。验证强符号、版本、TLS、动态模块和可能的 constructor 行为。可先使用隔离实验环境，不立即写入 flash。

**通过：** 无 exec 格式、loader、GLIBC 版本或 relocation 问题。否则先解决运行环境，垫片不能模拟缺失的 CPU 执行能力。

### B. 无 cspd 的启动闭环

恢复 Controller 启动路径实际调用的 ABI，加入限量/脱敏跟踪、必要配置与数据库状态、外部服务适配；保留 OSS 内部调度。先固定测试配置，不急着接入完整 UCI。

**通过：** 原厂内部任务、timer、socket 初始化并稳定运行，没有缺失数据库状态、同步请求超时或错误伪成功。进程不退出不等于通过。

### C. 接入隔离 LAN

适配真实 LAN bridge/接口身份，验证 1905 双向收发和原厂节点发现。测试网中移除另一个主控，避免争抢 Controller。

**通过：** 收发正确、节点身份/拓扑正确、不会监听或修改 WAN。

### D. 管理单个原厂子节点

用原厂基线抓包对照首次入网、配置同步、修改 SSID/密码、双方重启和重连；随后接入原生 helper 的 UCI/ubus 配置及状态接口。

**通过：** 子节点实际采用配置，客户端可经子节点获得 DHCP 并正常联网，重启后恢复。再增加多节点和策略。

如果 B 阶段发现必须模拟大量不可切分的 cspd 状态或厂商内核 ABI，应重新评估 shim 路线，不继续无边界搬迁原厂系统。相反，如果主要障碍集中在有限的配置/身份/事件接口，就继续用垫片保留协议核心。

## 下一步

优先完成 A/B 阶段所需的启动 ABI 清单、动态加载调查和最小日志型垫片，而不是先恢复全部 Vendor TLV。仍需确认原厂子节点型号/固件，并取得真实组网基线用于 C/D 阶段。
