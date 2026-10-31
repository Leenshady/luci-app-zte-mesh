# sr1010-mesh：SR1010 上用原厂 multiapd 当 mesh 主控（AC）

SR1010 刷 OpenWrt/ImmortalWrt（有线网关，没有 Wi-Fi）以后，继续管理保留原厂固件的中兴子路由（测试机 ZXHN Z506）。做法是把原厂 ARM32
`multiapd` 当控制器跑在私有沙箱里，周围用数据库行、两个 CM 函数的替身和一个服务配置文件指挥它，**不重写 mesh 协议**。

主控是主线。设备角色有三个（`zte_mesh.main.mesh_role`，2026-10-31 起）：

- **`master`（默认）仅主控**：把 Wi-Fi 档案下发给子路由，**本机无线完全不动**。SR1010 这类有线网关就属于这种，所以是默认值。
- **`main` 主路由**：主控 + 自己发 Wi-Fi。同一份档案既下发，也通过 `/etc/config/wireless` 落到本机，控制器和子路由在空中是同一个网络。
  它就是原厂的"主路由 / 自适应"（线上仍发 `mesh.role=auto`），旧值 `auto` 继续有效、含义相同。
- **`slave` 子路由（实验路径）**：本机作为子节点入网，接收主控下发的 M2 并把 Wi-Fi 名称/密码/加密写到本机
  `/etc/config/wireless`；`backhaul=multi_ap` 再把主控下发的隐藏回程网络落成一个 Multi-AP 回程 BSS（4 地址），
  这是 1905 报文跨无线的那条数据面。它需要完整的 `wpad`，4 地址是否真在驱动里生效还没有真机数据，细节与缺口见
  [docs/research/AGENT_PROBE.md](docs/research/AGENT_PROBE.md) 第 11 节。

## 当前状态（2026-10-18）

对照原厂功能的工作清单在 [docs/research/GAP_ANALYSIS.md](docs/research/GAP_ANALYSIS.md)，每项的实验记录在
[docs/research/REPLAY_RESULTS.md](docs/research/REPLAY_RESULTS.md)。

| 部分 | 状态 |
|---|---|
| P0 配置改了真的生效（CM 通知层、按频段 Renew、安全模式） | 完成，真子节点验证 |
| P1 与原厂行为一致（`WLANSetting`/5G 名称派生、`0200fe`、mesh 级变化 = 守护进程重启、角色、CurHop） | 完成；`0200fe` 是上线约 15 秒后发 1 条（原厂入网时发 3 条） |
| P2 管理面：重启子节点/指示灯、拓扑摘要、访客限时、Wi-Fi 5 兼容、Wi-Fi 定时关闭 | #9、#13 完成；#10、#12 部分；#11（IPTV 同步）不适用 |
| P3 原厂没有的功能 | 未展开 |
| OpenWrt 打包 | `zte-mesh`、`luci-app-zte-mesh`、`luci-i18n-zte-mesh-zh-cn` 三个 apk；装在测试路由器上（默认关闭），在真实 procd/rpcd 上用隔离网桥跑过 |

包装上默认关闭，启用前先确认一件事：原厂 `multiapd` 是 **32 位 ARM** 程序，设备内核跑不了它就需要 `qemu-arm-static`，
而这个模拟器**不在任何包里**（OpenWrt 不编它，且它必须与设备架构匹配），要手工放进私有根；arm64 设备刷
`CONFIG_COMPAT=y` 的固件即可原生运行。判定逻辑、三种出路和报错含义见 [docs/LAB.md](docs/LAB.md) 第 4.2 节。

还没做或没验证：用已安装的包接真实子节点；LuCI 页面在浏览器里的效果；长时间稳定性；多个子节点；关主 SSID 的路径
（`radioN.enabled=0`、定时 `scope=all`）没在真子节点上试过，因为会断开用户自己的 Wi-Fi。

## 目录

| 路径 | 内容 |
|---|---|
| 根目录 `*.c` `*.h` `*.def` `*.inc` | 沙箱启动器、服务、垫片和 preload 库、命令行工具（见 [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)） |
| 根目录 `*.py`、`build.sh` | 运行环境提取与审计、证据核对、实验室/设备测试脚本 |
| `tests/` | 主机测试（C 编解码、Python、init/rpcd 的 shell 测试、LuCI 的 JS 测试） |
| `openwrt/` | OpenWrt 包：`zte-mesh`（运行环境、init、rpcd）、`luci-app-zte-mesh`，以及 `make-runtime.py` |
| `tools/build/` | 构建容器入口 `container-make.sh`、运行环境打包 `runtime.sh` |
| `tools/capture/` | 抓包、对比、重放工具（`mreplay.py` 等），研究原厂时用的脚本 |
| `tools/child/` | 子路由 telnet 客户端和只读读回脚本 |
| `docs/` | 文档（索引见下） |
| `out/` | 构建产物，含原厂二进制，**不入库** |
| `evidence/` | 实验证据：pcap、原厂整库、日志、每轮重放的记录，**只在 mac 上，不入库** |

原厂固件镜像不在这里：`prepare.py` 默认读 `../zx279133/vendor-reference/2b5/rootfs.jffs2`，可用 `SR1010_VENDOR_ROOTFS` 指定。

## 快速开始（在 mac 上）

```sh
cd /Volumes/code/sr1010-mesh
orbctl start                                   # Docker（OrbStack）默认不运行
sh build.sh                                    # 从固件提取运行环境、审计、编译、主机测试、离线加载测试
sh tools/build/container-make.sh test-host     # 只跑主机测试；改了某个产物：container-make.sh db-engine-probe
sh tools/build/runtime.sh svctest              # 实验室用的 tmpfs 运行包 out/zte-mesh-svctest.tar.gz
python3 dev-service-smoke.py --no-attach --seconds 24   # 路由器上完全私有地跑一次服务（不接 LAN）
sh tools/build/runtime.sh install              # OpenWrt 包用的运行包 openwrt/zte-mesh/zte-mesh-runtime.tar.gz（随仓库提交）
```

OpenWrt 包的构建、真子节点测试的规矩和命令见 [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md)。

## 文档

| 文档 | 内容 |
|---|---|
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | 原理、运行时结构、各源文件职责、固件来源与哈希、ABI 与沙箱约束、为什么选这条路线 |
| [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) | 机器与连接、构建、打包、跑控制器、测试、真机守则、踩过的坑 |
| [docs/LAB.md](docs/LAB.md) | 在 x86_64 Ubuntu 虚拟机里搭环境：交叉编译、qemu 模拟、跑服务、测试现状，与真机构建的差异，以及 ARM32 模拟器**不入包**的原因和手工放置方法（第 4.2 节） |
| [docs/SERVICE.md](docs/SERVICE.md) | 服务模式：配置文件、热更新、守护进程重启、节点动作、UCI/LuCI 映射 |
| [docs/REAL_CHILD.md](docs/REAL_CHILD.md) | 真子路由联调：入网门槛、**服务配置语法**、各功能验证记录、风险 |
| [docs/FINDINGS.md](docs/FINDINGS.md) | 逆向与真机发现：角色选举、自组网 V2、`VersionMode`、`CurHop`、ACL 等 |
| [docs/CONTROLLER_CAPABILITIES.md](docs/CONTROLLER_CAPABILITIES.md) | 原厂控制器能下发什么、Z506 实际支持什么、本项目覆盖了什么 |
| [docs/FEATURE_STATUS.md](docs/FEATURE_STATUS.md) | **功能总账**：每项功能的状态与证据、做不到的项、按难度排的推进顺序 |
| [docs/research/E2633_FIRMWARE.md](docs/research/E2633_FIRMWARE.md) | ZXHN E2633 官方固件解包：容器布局、内核 `.config` 的关键宏、rootfs 为何解不开、对复现 mesh 的价值边界 |
| [docs/research/](docs/research/) | 原厂对照研究：`FEATURE_MAP`（每个网页设置 → 表字段 → 线上帧）、`GAP_ANALYSIS`（工作清单）、`REPLAY_RESULTS`（每轮实验记录）、`REPLAY_PLAN`、`AGENT_PROBE`（子路由角色：静态判定、运行时实验与实现）、冷入网时序 |
| [docs/milestones/](docs/milestones/) | 早期里程碑 B.1 到 B.3.3 的验收记录（历史） |
| [tools/README.md](tools/README.md) | 各个脚本 |

## 约定

- 仓库里不放任何密码和密钥。子路由 telnet 口令只从环境变量 `ZTE_TN_PASS` 读；服务配置里的 Wi-Fi 密钥只在测试时写进 0600 的临时文件，用完删除。
- `evidence/` 和 `out/` 里有原厂二进制、WSC 材料和原厂整库，不入库，不外传。
- 原厂程序和库带旧 OpenSSL，产物只用于私有工程验证；对外分发前要另外处理安全维护、最小权限和二进制再分发许可。
