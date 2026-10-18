# 架构：原厂 multiapd 在 OpenWrt 上当主控

本文合并了 2026-10-10/11 的两篇早期方案（`SR1010_VENDOR_MESH_OPENWRT_PORTING.md`、`SR1010_MULTIAPD_BINARY_SHIM_PLAN.md`，
原在 zx279133 的 `docs/`，导入提交里还能看到）里仍然成立的部分，并按现在的实现重写了结构说明。

## 1. 原理

原厂 mesh 的核心是用户态 `multiapd`：IEEE 1905、Multi-AP（EasyMesh）控制逻辑加中兴私有扩展。它不是 802.11s 或 batman-adv，
OpenWrt 的 `mesh_id`、usteer、DAWN 都换不来与原厂子节点的互通。

本项目不重写协议，而是把原厂 SR1010 固件里同一版本的 `multiapd` 和它的 27 个库搬到 OpenWrt 上，在私有命名空间里当控制器运行，
用“垫片”提供它依赖的环境，再用一个配置文件间接指挥它。线上的报文几乎全部由原厂守护进程自己构造；服务自己构造的只有信道选择请求
（CMDU `0x8006`）。实测结果：冷入网、M2、策略、Renew 等报文与原厂主控逐字节一致（见 `research/REPLAY_RESULTS.md`）。

## 2. 运行时结构

```text
UCI /etc/config/zte_mesh ── LuCI 页面（luci-app-zte-mesh）
        │                       │ ubus
        ▼                       ▼
/etc/init.d/zte-mesh        rpcd 插件 luci.zte-mesh（status / restart / reboot_node / light_node）
  write_config：UCI → /run/zte-mesh/config（0600，key=value，服务再校验一遍）
  procd 实例 controller：ns-run                    procd 实例 attach：把 zmesh0 挂进 br-lan
        │                                                       ▲
        ▼                                                       │ veth
ns-run（AArch64 静态）：新建 network/IPC/mount 命名空间，私有根 /usr/lib/zte-mesh/root，只把 /run/zte-mesh 暴露给宿主
        │
        ▼
db-engine-probe --controller-service（ARM32，原厂 glibc 2.26）
  ├─ 原厂共享内存数据库的服务端（libdb），表结构从 cspd 还原，启动时写入配置行
  ├─ 监督守护进程：启动、按需 SIGKILL 重启（mesh 级变化）、调度器、节点动作、写 state.json
  └─ fork/exec zte-meshctl（liboss_pub 客户端）发原厂 CM 通知 / ASEND
        │
        ▼
原厂 multiapd + preload 库
  libzte_mesh_shim.so            数据库初始化边界（DBShmCliInit / dbAPIGetView）
  libzte_controller_profile.so   CM 替身：CmWLANGetAPConfig、CmWLANGetPsk 从我们的行读
  libzte_controller_route.so     接口身份适配；cspd 的 multiapd_mgr 替身（启动计数 0xa96e）
  libzte_controller_trace.so     受限日志；子节点帧摘要写 /run/zte-mesh/peers/
        │ IEEE 1905（EtherType 0x893a）
        ▼
zmesh0 ─ br-lan ─ 子路由的 multiapd ─ 子路由 cspd ─ 无线驱动
```

- **不运行 `cspd`。** 守护进程需要的“路由器其余部分”由数据库行、CM 替身、身份环境变量和几个替身消息提供。
- **配置**：宿主只写 `/run/zte-mesh/config`。服务每秒检查，改动按种类热更新（写库后发原厂 CM 通知、按频段 Renew），
  mesh 级变化重启守护进程（原厂 `multiapd_mgr` 的做法），少数键的出现或消失要重启服务（见 [SERVICE.md](SERVICE.md)）。
- **状态**：服务每秒写 `/run/zte-mesh/state.json`（节点、动作结果、调度状态），rpcd 和 LuCI 读它。

## 3. 源文件

| 文件 | 运行位置 | 职责 |
|---|---|---|
| `ns-run.c`、`controller-net.c` | 路由器，AArch64 静态 | 沙箱启动器：命名空间、私有根、私有网桥和到宿主的 veth |
| `db-engine-probe.c`、`controller-run.c`、`controller-service.c` | 沙箱，ARM32 | 数据库服务端、守护进程的启动与重启、长期运行的服务（配置、调度器、动作、状态） |
| `multiap-cfg.c`、`mesh-policies.c`、`wlan-profiles.c`、`*.def`、`startup-views.inc` | 同上 | 从原厂二进制还原的表结构和写表逻辑（带读回） |
| `mesh-bridge.c` | 同上 | 配置值的类型化校验（`mb_profile`、`mb_validate`） |
| `zte-meshctl.c` | 沙箱，ARM32 | 原厂 `diagget` 式的 OSS 客户端：发 CM 通知、Renew、重启/指示灯等 ASEND |
| `shim.c` | 沙箱，preload | 数据库初始化边界 |
| `controller-profile.c`、`cm-profile-layout.inc` | 沙箱，preload | CM 替身；43 个字段偏移由 `cm-profile-evidence.py` 从原厂机器码推出 |
| `controller-route.c` | 沙箱，preload | 接口身份；`SSEND(0xa96e)` 启动计数替身 |
| `controller-trace.c` | 沙箱，preload | 受限日志，ASEND 记录，子节点指示灯和终端数摘要 |
| `controller-packet.c`、`controller-wsc*.c` | 实验室 | 合成对端和独立的 M2 校验器（只用公开测试值） |
| `config-bridge-probe.c`、`exec-check.c`、`zte-debugcmd.c` | 实验室 | 早期桥接测试、加载检查、原厂 `sendcmd` 调试通道客户端 |
| `prepare.py`、`jffs2.py`、`vendor_elf.py`、`schema.py`、`profile_schema.py`、`startup-schema.py`、`bridge_evidence.py`、`cm-profile-evidence.py` | mac | 从固件提取运行环境，按固定哈希审计 ABI，核对表结构和偏移证据 |
| `openwrt/` | 路由器 | init（UCI 映射）、rpcd 插件、LuCI 页面、`make-runtime.py` |

## 4. 固件来源

运行环境只从一个固定的原厂镜像提取，`prepare.py` 校验哈希，换镜像要重新调查并更新固定值，不提供跳过校验的选项。

```text
zx279133/vendor-reference/2b5/rootfs.jffs2
  └─ /kmoduletmp/kmodule.img            嵌套 SquashFS（inode 496，4944 个数据节点，19,673,088 字节，无缺口）
       ├─ /bin/multiapd
       ├─ /bin/cspd
       └─ /lib/lib{ieee1905,multiap}{cfapi,cmapi}.so 等
```

| 文件 | 字节数 | SHA256 |
| --- | ---: | --- |
| `kmodule.img` | 19673088 | `3fbb2e29fe7a94124aba084217c372d2694c880c8657781c9c641f7d0d1fad46` |
| `bin/multiapd` | 988140 | `a0ba6beeb1eb4e0c900b24d06e9d37e7174f9f640320d12803d7d80c4276280a` |
| `bin/cspd` | 3329124 | `8b8477e2598660653c27d796fcaaa5484f879cd263fb6caa5e0a982b1cee5ac1` |
| `lib/libieee1905cfapi.so` | 26000 | `7ed1fb54aedbb9f346152a171d4ccce03f84e047cb677beb6553067f1fc951c3` |
| `lib/libieee1905cmapi.so` | 48016 | `17ae6ce610ff7999aafeba305721601fbae8138e10d338cc1704f95b7ec06329` |
| `lib/libmultiapcfapi.so` | 18148 | `e11166d71f6d5ef9cafe80ff946545ac181c73f4d5e82cfac751a0072fe813dd` |
| `lib/libmultiapcmapi.so` | 59544 | `bf47d0b05d059aa85bb92fbc7e2f1e4502832eb35d3bf394ed44f2db9854be75` |

- `zx279133/vendor-reference/2b5/cspd`（3,106,388 字节，`eb7062b9…`）是**另一个版本**，来源不明，它的地址和 ABI 不能套到镜像里的程序上。
  表结构的还原都以镜像内的 `cspd` 为准（`prepare.py --analysis-cspd` 提取到 `out/analysis/cspd`，0600，不进运行环境）。
- 真子路由 Z506 的 `multiapd`/`libcmapi.so` 与 SR1010 的也不是同一个构建：控制器行为读 SR1010 的二进制，子路由能否接收读子路由自己的。

`multiapd` 保留了大量符号（IDA 识别 2628 个函数）。最早定位的入口（链接地址，属于上表哈希的 `multiapd`）：

| 地址 | 符号 | 说明 |
| --- | --- | --- |
| `0x13964` | `main` | `DBShmCliInit`、`OSSInitConfig`、`OSSStart` |
| `0x655b8` | `baseCreateSockfd` | AF_PACKET、SOCK_RAW、EtherType `0x893a` |
| `0x65d2c` | `baseBindSocketToBr0` | 按名字找 `br0` 再 bind（`controller-route.c` 适配接口身份） |
| `0x66b0c` | `I5MessagePacketSend` | `sendto` 发 1905 报文 |
| `0x414dc` | `master_init` | 控制器初始化、注册控制消息和 LLC socket |
| `0x60070` | `slave_init` | Agent 侧（`EnMasterOnly=1` 时跳过） |
| `0x6f758` | `acdsRcvApAutoConfigSearch` | AP-autoconfiguration Search → Response，含设备过滤 |
| `0x6edd0` | `_acdsRcvApAutoConfigWSCM1.constprop.10` | WSC M1 处理 |
| `0x7ca90` | `acdsBuildVendorMsg` | 私有消息：按 msgtype 找构造回调，加 6 字节厂商头 |
| `0x464ac` | `multiap_get_vendor_oui` | OUI `00 19 c6` |
| `0x6a344` | `baseVendorTypeExtract` | 按设备身份维护通信许可（准入链见 [FINDINGS.md](FINDINGS.md) 第 4 节） |
| `0x3be98` | `I5TlvVendorP1905ComCfgSyncInsert` | 厂商 TLV `0x60`（`P1905ComCfg` 同步） |

## 5. ABI 与沙箱约束

- `multiapd` 和原厂库是 ARM EABI5 soft-float（`e_flags=0x05000200`），解释器 `/lib/ld-linux.so.3`，glibc 2.26。不能给它注入 AArch64、musl
  或 hard-float 的库；我们的 preload 库和 ARM32 程序用 `arm-linux-gnueabi-gcc -mfloat-abi=soft` 编译，直接链接原厂 glibc，
  `make-runtime.py` 再审计一遍强符号和 GNU 符号版本。
- 内核要 `CONFIG_COMPAT`（AArch32 用户态）和 SysV IPC（原厂数据库是 SysV 共享内存）。
- 私有根里保留原厂 loader 和 glibc，不碰 OpenWrt 的 `/lib`；原厂进程看不到 MTD、厂商设备和宿主配置。
- chroot 和 preload 都不是完整的安全边界。沙箱靠命名空间：私有 network/IPC/mount，宿主可写的只有 `/run/zte-mesh`，
  只读字符设备只有内核 urandom。
- `SSEND`/`ASEND` 区分内部任务和外部服务，有同步应答语义，不能一律吞掉。只替身确实发给 `cspd` 而控制器需要应答的消息
  （例如启动计数 `0xa96e`），其余照原样走原厂 OSS。
- 外部进程不能直接调 `ASEND/SSEND`（需要 OSS 身份）：`zte-meshctl` 用原厂客户端库 `liboss_pub.so` + `InitAppComm`，由服务 fork/exec。
- 守护进程环境里只能有一个变量（`ZTE_MESH_START_COUNT`），多一个 `setenv` 会让原厂 ld.so 断言失败（exit 127）。要给 preload 库传状态，经数据库。
- 原厂程序带旧 OpenSSL（1.0.0），要考虑安全维护和再分发许可。

## 6. 为什么选这条路线

早期方案比较过三条路：

| 路线 | 结论 |
| --- | --- |
| 原厂 `multiapd` + 兼容运行环境 + 垫片 | 采用。中兴子节点的准入、私有 TLV、M2 内容都在原厂代码里，实测逐字节一致，协议层不用维护 |
| 开源 EasyMesh 栈（prplMesh）+ 中兴扩展 | 没有走：中兴子节点有一串身份、市场码、私有 TLV 的准入门槛（[FINDINGS.md](FINDINGS.md) 第 4 节），需要逐项复刻 |
| 自写轻量 1905/中兴控制器 | 没有走：等于重写原厂协议和 WSC，维护和安全测试成本最高 |

早期方案给垫片定的边界现在仍然适用：

- 垫片覆盖**平台边界**（数据库、接口身份、少数外部服务消息），保留协议引擎和 OSS 内部调度，不写“全部返回成功”的空函数库。
- 没有本地 Wi-Fi 不等于没有无线配置：控制器仍要远端 AP 的 SSID、密码、安全模式和频段配置，这些是逻辑配置，不伪造本地无线能力。
- 同一 LAN 上不能有两个 1905 控制器（测试时原厂主控已撤掉）。
