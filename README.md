# luci-app-zte-mesh：让 OpenWrt 路由器与中兴路由器组网

给 OpenWrt / ImmortalWrt 做的中兴组网插件。OpenWrt 侧既能当**主控**（把 Wi-Fi 配置下发给子路由），也能当**子节点**
（加入网络，配置由主控下发）；**中兴侧保留原厂固件，不刷机**。做法是把原厂 ARM32 `multiapd` 当控制器跑在私有沙箱里，
周围用数据库行、两个 CM 函数替身和一个服务配置文件指挥它——**不重写 mesh 协议**，所以线上帧与原厂一致。

参考实现机：ZXSLC SR1010（又叫中兴星云 MAX）刷 OpenWrt/ImmortalWrt，是有线网关、不带 Wi-Fi。中兴原厂路由器：ZXHN Z506（又叫中兴小方糖），保留原厂固件不刷机。

## 功能表

状态只有三种：**✅ 已实现并验证** / **◐ 已实现，还没在真实路由器上验** / **⬜ 没做**。逐项状态与证据在
[docs/FEATURE_STATUS.md](docs/FEATURE_STATUS.md)，等上机的项在 [docs/PLAN.md](docs/PLAN.md) 第 5 节。

### 本机当主控（给中兴原厂路由器下发 Wi-Fi）

| 功能 | |
|---|---|
| 网页上配主 SSID：名称 / 密码 / 加密 / 是否隐藏 | ✅ |
| 自动发现新子路由并接纳（约 5 分钟一轮，不用手工配对） | ✅ |
| 多台子路由同时在线 | ◐ |
| 改完配置热更新下发，不重启服务 | ✅ |
| 多网络（访客 / 备用 / 隐藏回程，网络序号 1–3）的名称、密码、开关 | ✅ |
| 信道与发射功率 | ✅ |
| 终端黑白名单，可按每个网络单独设"关 / 拦 / 只放行" | ✅ |
| BeaconInterval | ✅ |
| 访客限时、Wi-Fi 定时关闭 | ✅ |
| 远程重启子路由、开关指示灯 | ✅ |
| 自组网 V2 开关（受市场码门槛） | ✅ |
| 拓扑页：节点清单（型号、名称、地址、MAC、在线状态） | ✅ |
| 拓扑页：直连还是经中继、BSS 数、上报新鲜度、每节点终端数（按 BSSID 记账，按 BSSID 一列）、固件版本 | ◐ |
| 拓扑页：回程 RSSI、树状图、每 AP 的终端列表 | ⬜ |
| 引导漫游（BTM）/ 封禁终端 / 踢终端下线 | ◐ |
| 漫游与频段引导阈值、MLO 开关、Wi-Fi 5 兼容标志下发 | ◐ |
| 本机也发 Wi-Fi（原厂意义上的"主路由"） | ◐ |

### 本机当子节点（加入网络）

| 功能 | |
|---|---|
| 网线插主控 LAN 口就入网，本机不用填任何 Wi-Fi 参数 | ✅ |
| 收主控下发的名称 / 密码 / 加密并写进本机无线配置，只在确实有改动时才写文件 | ✅ |
| 入网时先沿用本机正在广播的名字，重启服务**不再把本机广播的 SSID 改两次**（不掉线） | ✅ |
| 应用结果回传页面（成功 / 失败 / 改了哪些频段） | ◐ |
| 额外网络（访客等）按序号写到本机射频 | ◐ |
| 接另一台 OpenWrt 路由器当主控（有线回程） | ✅ |
| 接中兴原厂路由器当主控（本机当子节点） | ◐ |
| 无线回程（4 地址回程 STA）：配置面已生成，端到端没有真实路由器上的数据 | ◐ |

### 页面、安装与运行环境

| 功能 | |
|---|---|
| LuCI 页面，中英双语（中文随 CI 出 `zh-cn.lmo`） | ✅ |
| 两个开关摆出三种拓扑，页面按角色只显示有效项 | ✅ |
| 四个 apk：`zte-mesh` / `zte-mesh-launcher` / `luci-app-zte-mesh` / `luci-i18n-zte-mesh-zh-cn` | ✅ |
| 一条命令装机验收（装包 + 读回版本 + 四条冒烟 + 读回这批的标记） | ✅ |
| 卸载与重装（`apk del` / `apk add` 循环走通） | ✅ |
| 作为 ImmortalWrt 的 feed 登记，四个包在那棵树里编出来（CI 出的 apk 就是这么来的） | ✅ |
| 预装进固件镜像、刷到设备 | ⬜ |
| arm64 设备直接跑 32 位原厂程序：内核能跑就原生跑（探测已修，ax3600 实测） | ✅ |
| 内核跑不了 ARM32 时用 `qemu-arm-static`：**支持手工放，不随包分发**（有意的取舍） | ◐ |
| 两个频段广播不同名字时各按各自的写（不派生） | ◐ |

## 设备角色（两个开关）

| 组合 | 作用 |
|---|---|
| `mesh_role=master` + `apply_local=0`（默认） | 只当主控，把配置下发给子路由，**本机无线一个字节都不动** |
| `mesh_role=master` + `apply_local=1` | 主控且自己发 Wi-Fi（原厂页面里的"主路由"）；旧值 `main`/`auto` 仍被读，等于这一组合 |
| `mesh_role=slave` | 本机当子节点，配置由主控下发；`backhaul=multi_ap` 时回程写成客户端段，需要完整 `wpad` |

## 上手

```sh
# 装：把四个 apk 放到机器上，一条命令装完 + 读回 + 冒烟
sh tools/lab/verify-install.sh /tmp/zte-apk        # 会真装包、真重启服务
sh tools/lab/verify-install.sh                     # 只读检查，不装不重启
```

LuCI → 网络 → **ZTE Mesh**。「常规」里选设备角色，「Wi-Fi 配置」填 2.4G 名称与密码（只有本机也发 Wi-Fi 时才用到）。
装完记得重启 `rpcd` 与 `zte-mesh`——上面那条命令已经做了。完整步骤、三种拓扑怎么摆、"设了没动静"的四种情况见
[docs/INSTALL.md](docs/INSTALL.md)。

## 开发

```sh
sh build.sh                                          # 提取运行环境、审计、编译、主机测试、离线加载测试
sh tools/build/container-make.sh test-host           # 只跑主机测试
sh tools/build/runtime.sh install                    # 重新生成随仓库提交的运行包
python3 tests/test_i18n.py                           # 改了界面文案必须跑
```

分支、`prebuilt/`、CI（**手动触发**）与测试基线条数见 [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md)；
编码工具的项目指令在 [AGENTS.md](AGENTS.md)。

## 目录

| 路径 | 内容 |
|---|---|
| `src/` | C 侧：沙箱启动器、服务、垫片与 preload 库、命令行工具，以及原厂表结构的 `.def`/`.inc`（职责见 [ARCHITECTURE](docs/ARCHITECTURE.md)） |
| `build.sh`、`Makefile`、`Dockerfile*` | 构建入口：提取并审计运行环境、交叉编译、跑主机测试 |
| `tests/` | 主机测试（C 编解码、Python、shell、LuCI 的 JS） |
| `openwrt/` | OpenWrt 包：`zte-mesh`、`zte-mesh-launcher`、`luci-app-zte-mesh` |
| `prebuilt/` | lab 用的预编译件，**不进任何 apk** |
| `tools/` | `tools/dev/` 是构建与测试用的 17 个 Python 脚本，另有构建容器与打包、抓包重放、子路由 telnet、测试环境里的脚本 |
| `docs/` | 文档（下） |
| `out/`、`evidence/` | 构建产物与验证证据，含原厂二进制，**不入库** |

C 侧在 2026-11-04 挪进了 `src/`：`Makefile` 逐文件列源、`tests/*.c` 的 `../` 包含前缀、`.def`/`.inc` 的同目录包含
都要跟着改，一共动了 74 个跟踪文件（51 个挪、23 个改）；随包分发的 `ns-run.c`/`controller-net.c` 用的是 tar 成员名，成员名一个没动。
Python 同日挪进 `tools/dev/`：`tests/`、`openwrt/make-runtime.py`、`tools/lab/` 按模块名 import 它们，改的是同一批
`sys.path.insert` 加上 `build.sh`／`Makefile` 的调用路径；根目录只剩构建入口。

## 文档

**要跟着改动更新的四份**：

| 文档 | 内容 |
|---|---|
| [FEATURE_STATUS](docs/FEATURE_STATUS.md) | **功能总账**：逐项状态与判据 |
| [PLAN](docs/PLAN.md) | **唯一排期**：分批做什么、主机可测与需上机分开、上机清单 H-01…H-14 |
| [INSTALL](docs/INSTALL.md) | 用户手册：装包、页面、三种拓扑、验收、卸载 |
| [DEVELOPMENT](docs/DEVELOPMENT.md) | 构建、打包、测试基线、上机守则、分支与 `prebuilt/` |

**原理与接口，改代码前读**：[ARCHITECTURE](docs/ARCHITECTURE.md)（结构与源文件职责、固件哈希）、
[SERVICE](docs/SERVICE.md)（UCI/LuCI/CM 映射与热更新）、[LAB](docs/LAB.md)（测试机清单、ARM32 模拟器判定）、
[REAL_CHILD](docs/REAL_CHILD.md)（中兴原厂路由器的配置语法、基线与红线）。

**只读资料，不再随结论更新**：[docs/research/](docs/research/)（原厂逆向与上机取证：DB 快照与表名全集、rootfs、
1905 抓包与逐字段核对、`multiapd` 与厂商栈、垫片可行性、agent 探测、重放结果）、[docs/milestones/](docs/milestones/)
（早期里程碑的验收记录）、[FINDINGS](docs/FINDINGS.md) 与
[CONTROLLER_CAPABILITIES](docs/CONTROLLER_CAPABILITIES.md)（角色选举触发位、网页锁定公式、`sendcmd` 报文格式、
Z506 的能力矩阵）、[机型对照](<docs/ZTE router model.md>)、[docs/journal/](docs/journal/)（开发日志，过程叙事只在
这里）。原厂固件镜像不在仓库里：`tools/dev/prepare.py` 默认读 `../zx279133/vendor-reference/2b5/rootfs.jffs2`，可用
`SR1010_VENDOR_ROOTFS` 指定。

## 约定

- 仓库里不放任何密码和密钥。子路由 telnet 口令只从环境变量 `ZTE_TN_PASS` 读；服务配置里的 Wi-Fi 密钥只写进
  0600 临时文件，用完删除。
- `evidence/` 和 `out/` 里有原厂二进制、WSC 材料和原厂整库，不入库，不外传。
- 原厂程序和库带旧 OpenSSL，产物只用于私有工程验证；对外分发前要另外处理安全维护、最小权限和二进制再分发许可。
