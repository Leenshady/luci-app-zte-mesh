# luci-app-zte-mesh：让 OpenWrt 路由器与中兴路由器组网

**项目目标**：为 OpenWrt / ImmortalWrt 提供与中兴路由器组网的插件。OpenWrt 侧既能当主控（AC，把 Wi-Fi 档案下发给中兴子路由），
也能当子节点（加入网络，配置由主控下发）；中兴侧保留原厂固件，不刷机。包里带 LuCI 页面，装完在路由器界面上配。

做法是把原厂 ARM32 `multiapd` 当控制器跑在私有沙箱里，周围用数据库行、两个 CM 函数的替身和一个服务配置文件指挥它，**不重写 mesh 协议**。
参考实现是 SR1010 刷 OpenWrt/ImmortalWrt（有线网关，没有 Wi-Fi），真子路由是保留原厂固件的 ZXHN Z506。

## 三种角色（`zte_mesh.main.mesh_role`）

| 角色 | 作用 |
|---|---|
| `master`（默认） | 只当主控：把 Wi-Fi 档案下发给子路由，**本机无线完全不动**。SR1010 这类有线网关就是这种 |
| `main` | 主控 + 自己发 Wi-Fi：同一份档案既下发，也落到本机 `/etc/config/wireless`。即原厂的"主路由 / 自适应"，线上仍发 `mesh.role=auto`，旧值 `auto` 同义 |
| `slave` | 本机当子节点：收 M2 并把名称/密码/加密写到本机；`backhaul=multi_ap` 时把隐藏回程落成 4 地址 BSS。需要完整 `wpad`，真机数据还没有（[AGENT_PROBE](docs/research/AGENT_PROBE.md) 第 11 节） |

## 当前状态

**功能总账只有一份**：[docs/FEATURE_STATUS.md](docs/FEATURE_STATUS.md)（逐项状态与证据、做不到的项、按难度排的推进顺序）。
这里只留三条结论：

- 主控侧在真子路由 Z506（原厂固件）上验证通过：入网、SSID/密码/多 BSS、BeaconInterval、ACL、信道与功率、节点动作、定时窗口。
- 子路由侧（本机当子节点）在两台实验机上打通有线回程，主控改 SSID 后子路由的空中名称跟着变。
- 打包完成：`zte-mesh`（`arch: all`）、`zte-mesh-launcher`（aarch64，现编 `ns-run`/`mesh-search`）、`luci-app-zte-mesh`、
  `luci-i18n-zte-mesh-zh-cn` 四个 apk，由 `build-apk.yml` **手动**触发构建。

启用前先确认一件事：原厂 `multiapd` 是 **32 位 ARM** 程序，设备内核跑不了它就需要 `qemu-arm-static`，而这个模拟器**不在任何包里**，
要手工放进私有根；arm64 设备刷 `CONFIG_COMPAT=y` 的固件即可原生运行。判定逻辑、三种出路和报错含义见
[docs/LAB.md](docs/LAB.md) 第 4.2 节。

没做或没验证：用已安装的包接真实子节点；LuCI 页面在浏览器里的效果；长时间稳定性；多个子节点；关主 SSID 的路径
（`radioN.enabled=0`、定时 `scope=all`），后者会断开用户自己的 Wi-Fi。

## 目录

| 路径 | 内容 |
|---|---|
| 根目录 `*.c` `*.h` `*.def` `*.inc` | 沙箱启动器、服务、垫片和 preload 库、命令行工具（见 [ARCHITECTURE](docs/ARCHITECTURE.md)） |
| 根目录 `*.py`、`build.sh` | 运行环境提取与审计、证据核对、实验室/设备测试脚本 |
| `tests/` | 主机测试（C 编解码、Python、init/rpcd 的 shell 测试、LuCI 的 JS 测试） |
| `openwrt/` | OpenWrt 包：`zte-mesh`、`zte-mesh-launcher`、`luci-app-zte-mesh`，以及 `make-runtime.py` |
| `prebuilt/` | lab 主机用的预编译件（`ns-run` 两份架构、ARM32 `db-engine-probe` 与 shim），**不进任何 apk**（见 [DEVELOPMENT](docs/DEVELOPMENT.md) 第 10 节） |
| `tools/build/`、`tools/capture/`、`tools/child/`、`tools/lab/` | 构建容器与打包、抓包重放、子路由 telnet、实验室脚本 |
| `docs/` | 文档（索引见下） |
| `out/`、`evidence/` | 构建产物与实验证据，含原厂二进制，**不入库** |

原厂固件镜像不在这里：`prepare.py` 默认读 `../zx279133/vendor-reference/2b5/rootfs.jffs2`，可用 `SR1010_VENDOR_ROOTFS` 指定。

## 快速开始（在 mac 上）

```sh
orbctl start                                   # Docker（OrbStack）默认不运行
sh build.sh                                    # 提取运行环境、审计、编译、主机测试、离线加载测试
sh tools/build/container-make.sh test-host     # 只跑主机测试；改某个产物：container-make.sh db-engine-probe
sh tools/build/runtime.sh svctest              # 实验室 tmpfs 运行包 out/zte-mesh-svctest.tar.gz
python3 dev-service-smoke.py --no-attach --seconds 24   # 路由器上完全私有地跑一次服务（不接 LAN）
sh tools/build/runtime.sh install              # 包用的运行包 openwrt/zte-mesh/zte-mesh-runtime.tar.gz（随仓库提交）
```

包的构建、真子节点测试的规矩和命令、分支与产物托管见 [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md)。

## 文档

| 文档 | 内容 |
|---|---|
| [ARCHITECTURE](docs/ARCHITECTURE.md) | 原理、运行时结构、源文件职责、固件来源与哈希、ABI 与沙箱约束、为什么选这条路线 |
| [DEVELOPMENT](docs/DEVELOPMENT.md) | 机器与连接、构建、打包、跑控制器、测试、真机守则、踩过的坑、**分支与 `prebuilt/`** |
| [LAB](docs/LAB.md) | x86_64 虚拟机：交叉编译、qemu 模拟、跑服务、测试现状；ARM32 模拟器**不入包**的原因与手工放置（第 4.2 节） |
| [SERVICE](docs/SERVICE.md) | 服务模式：配置文件、热更新、守护进程重启、节点动作、UCI/LuCI 映射 |
| [REAL_CHILD](docs/REAL_CHILD.md) | 真子路由联调：入网门槛、**服务配置语法**、各功能验证记录、风险 |
| [FINDINGS](docs/FINDINGS.md) | 逆向与真机发现：角色选举、自组网 V2、`VersionMode`、`CurHop`、ACL 等 |
| [CONTROLLER_CAPABILITIES](docs/CONTROLLER_CAPABILITIES.md) | 原厂控制器能下发什么、Z506 实际支持什么、本项目覆盖了什么 |
| [FEATURE_STATUS](docs/FEATURE_STATUS.md) | **功能总账**：状态与证据、做不到的项、推进顺序 |
| [MESH_INTEROP](docs/research/MESH_INTEROP.md) | 中兴 mesh 的协议归属、外部项目（prplMesh / BBF 1905.1a）能借什么、固件解密的边界（含**内核读路径无解密代码**的实证）、**原厂内核里 1905 的全部字符串/符号**、做功能时的兼容性硬约束（§5.5 已修正为「丢转发、送本机」） |
| [WIRESHARK_1905](docs/research/WIRESHARK_1905.md) | 用 Wireshark `ieee1905` 逐字段核对线上帧：我们发的 `0x0007`/`0x0008`/`0x8006`、原厂发的 `0x0000`/`0x0001`/`0x0008`/`0x000a`、差异清单与处置 |
| [VENDOR_MESH_STACK](docs/research/VENDOR_MESH_STACK.md) | 从市售 AX3000（E2631）拿到的**明文原厂 mesh 栈**：cspd 的模块划分、`ASEND`/`SSEND` 内部总线号、和 SR1010 的能力面差异、**D 段（VLAN/绑定/BTM/VBSS）的修订依据**（§2.1 记录了「漏取 multiapd」的自纠） |
| [MULTIAPD](docs/research/MULTIAPD.md) | **没被 strip 的 1905 协议实体**：6835 个符号 / 2184 个具名函数、TLV 与 M1/M2 的收发两端、`br0` 上的 raw socket、`pktdump`/`setrole`/`querydev` 调试命令表、DB 通道 |
| [E2633_TELNET_BOOT](docs/research/E2633_TELNET_BOOT.md) | 市售 E2633「转正」工具链（开 telnet / 改型号 / 刷 boot）：**`config.bin` 容器格式与密钥派生**、`boot.bin` 两段式结构、升级密钥对闸门、安全启动未启用的实证、与英文参考实现 `zcu` 的逐字段对照（§10）、**`sendcmd`/换区开关与 `/kmodule/bin` 工具清单**（§11） |
| [E2631_ROOTFS](docs/research/E2631_ROOTFS.md) | 真机取回的 `rootfs.jffs2` 解包结果：**68 字节 inode 头 + 小端 + 非 zlib CRC 三个坑**、mtd→挂载点权威表、`/etc/autokernelconf` 与 `/etc/autouserconf`、**mesh 配置库的表名与列名**（§6.4.1 的 `MultiAPCfg` 与 D3 入口、§6.4.2 的 276 张表名全集）、`kmodule` squashfs 里的 257 个 lua 与 `OBJ_*_ID` 对照、`db_default_auto_cfg.xml` 解不开的负结果 |
| [E2631_DB_SNAPSHOT](docs/research/E2631_DB_SNAPSHOT.md) | 真机 `sendcmd 1 DB p` 的实测原文（五轮十三张表）：`MultiAPMaster` 55 列、`P1905ComCfg` 8 列、`MultiAPDomain` 8×106 列、`MultiAPRoamCatonCfg` 7 列、**`MultiAPCfg` 50 列**（角色/跳数/`MeshName`/`IFLink`/`En1905Msg`/`EnReportCenterChannel`）、**`P1905Cfg` 12×3**（`ViewName` = `IGD.P1905CFG1..6`）、`WlanRoamingCfg` 1×6、`MultiapCapability` 1×2、**`Brport` 16×23**（端口拓扑 + VLAN 八列全默认）；**出厂踢人走 `EnActiveDeauth` 不走 BTM**；**`36+32n` 偏移公式六个锚点全中**；**11r 与 D5 的配置层两条线索结案为负**；与离线推断的逐条对照（**四处错**均已自纠） |
| [E2631_DB_TABLES](docs/research/E2631_DB_TABLES.md) | `sendcmd 1 DB all` 的 **276 张表名全集**：#141–150 十张 `MultiAP*`、#162–170 九张 `P1905*`、`WlanRoamingCfg`(#270)、`MultiapCapability`(#154)、`MapCooperatorCfg`(#139)；**三个"列名"其实是表名**（`MultiAPPTK`/`P1905StaBind`/`P1905Cfg`）；负结果：`IGD` 表与 `MESH`/`Netsphere`/`EASYMESH` 都不在库里，但 **`IGD.` 是行名前缀**（`IGD.P1905CFG1..6` 已实测） |
| [research/](docs/research/) | 原厂对照研究（存档）：`FEATURE_MAP`、`GAP_ANALYSIS`、`REPLAY_RESULTS`、`AGENT_PROBE`、`E2633_FIRMWARE` |
| [milestones/](docs/milestones/) | 早期里程碑 B.1–B.3.3 的验收记录（存档） |
| [tools/README.md](tools/README.md) | 各个脚本 |

`docs/research/` 与 `docs/milestones/` 是**证据存档**，按时间保留原始记录、不随结论更新；结论以 `ARCHITECTURE` 到 `E2631_DB_TABLES` 这十六份为准。

## 约定

- 仓库里不放任何密码和密钥。子路由 telnet 口令只从环境变量 `ZTE_TN_PASS` 读；服务配置里的 Wi-Fi 密钥只写进 0600 临时文件，用完删除。
- `evidence/` 和 `out/` 里有原厂二进制、WSC 材料和原厂整库，不入库，不外传。
- 原厂程序和库带旧 OpenSSL，产物只用于私有工程验证；对外分发前要另外处理安全维护、最小权限和二进制再分发许可。
