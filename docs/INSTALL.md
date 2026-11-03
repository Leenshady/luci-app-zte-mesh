# 装上并用起来

这份文档只回答一件事：**从一台刚刷好 OpenWrt / ImmortalWrt 的路由器开始，怎么把它和中兴路由器组起来**。
原理见 [ARCHITECTURE](ARCHITECTURE.md)，每个选项的完整语义见 [SERVICE](SERVICE.md)，构建与装机脚本见
[DEVELOPMENT](DEVELOPMENT.md)，实验机台账见 [LAB](LAB.md)。这里不重复它们，只给最短路径和判据。

目标是这四个场景里的前两个（[PLAN](PLAN.md) 第 1 节）：

- **S1 主控**：OpenWrt 当中控，中兴子路由跑原厂固件，用网线连过来。
- **S2 子节点（有线回程）**：OpenWrt 当中兴主控下面的子节点，一根网线。
- S3（无线回程）配置面已经就绪，**端到端还没在真机验过**（[PLAN](PLAN.md) 的 H-01），第 6 节标了出来。

## 1. 动手前确认三件事

| 要确认 | 怎么看 | 不对会怎样 |
|---|---|---|
| **这台机器能不能直接跑 32 位 ARM 程序** | `uname -m` 是 `aarch64` 且固件带 `CONFIG_COMPAT=y` → 能。`x86_64` 或没有 compat 的 arm64 → 不能，必须自己把 `qemu-arm-static` 放进私有根 `/usr/lib/zte-mesh/root` | 控制器实例每 5 秒以 `execv errno=8` 退出，页面显示没跑起来。模拟器**不在任何包里**，判定表与三条出路见 [LAB](LAB.md) 第 4.2 节 |
| **无线回程要完整 `wpad`** | `apk info wpad` / `opkg list-installed \| grep wpad`；`wpad-basic` 不够 | 只有开 `backhaul`（第 5、6 节）才受影响；`wlan-sync` 会把原因写进日志与页面状态行，不会静默失败 |
| **flash 够不够** | `zte-mesh` 这个 apk 约 4.1 MB，装上占 jffs2 约 4.9 MB、解压约 8 MB（大头是原厂库）；LuCI 与中文包是 KB 量级 | 换 `wpad` 变体也可能挤爆小 flash |

## 2. 装包

四个包，名字与作用：

| 包 | 架构 | 内容 |
|---|---|---|
| `zte-mesh` | `all` | 服务本体：`/etc/init.d/zte-mesh`、`attach` / `wlan-sync`、rpcd 插件、默认 `/etc/config/zte_mesh`、带原厂 `multiapd` 的运行包（解到 `/usr/lib/zte-mesh/root`） |
| `zte-mesh-launcher` | `aarch64_cortex-a53` | 现编的 `ns-run` 与 `mesh-search`；依赖 `ip-full`、`kmod-veth`、`kmod-sched-core`。缺 veth 内核模块时启动器停在 `error=private-service-network errno=2`（建私有网线之前先 `stat /sys/module/veth`） |
| `luci-app-zte-mesh` | `all` | 配置页面（`网络 → ZTE Mesh`） |
| `luci-i18n-zte-mesh-zh-cn` | `all` | 中文文案。**装应用包不会自动带上它**，要中文就单独装 |

**手工装（最常用）**：从 `build-apk.yml` 那次运行的 `zte-mesh-apk` artifact 里拿四个 apk（同一个 artifact 带
`SHA256SUMS.txt`，先 `sha256sum -c` 再装）。包没签名，所以要 `--allow-untrusted`：

```sh
scp dist/*.apk root@<这台路由器的IP>:/tmp/
ssh root@<这台路由器的IP> 'apk add --allow-untrusted /tmp/zte-mesh*.apk /tmp/luci-*.apk'
```

- 装与升级**不会覆盖**你改过的 `/etc/config/zte_mesh`（它是 conffile）。
- `x86_64` 的机器（比如虚拟机）**装不上 `zte-mesh-launcher`**——架构不匹配是预期的，`apk` 会报 `uninstallable`。
  这种机器要自己编 `ns-run` / `mesh-search`，命令见 [LAB](LAB.md)；`zte-mesh` 本体照装。
- 只有 `zte-mesh` 没有启动器 = 页面能打开、控制器起不来（缺 `ns-run`）。四个包是"一套"，不是可选菜单。

**编进固件**（长期用这台机器当控制器时更省事）：把本仓库当 feed 加进 ImmortalWrt 树，
`./scripts/feeds update zte_mesh && ./scripts/feeds install -a -p zte_mesh`，在 `.config` 里勾上这四个包
（`CONFIG_PACKAGE_{zte-mesh,zte-mesh-launcher,luci-app-zte-mesh,luci-i18n-zte-mesh-zh-cn}=m`）。feed 登记与编译命令见
[DEVELOPMENT](DEVELOPMENT.md) 第 8 节。

## 3. 打开页面并确认服务起来了

1. LuCI 登录 → **网络 → ZTE Mesh**（页面标题 `ZTE Mesh`，中文版是「中兴组网」）。
2. 「常规」tab 最上面是**启用**（默认开）与服务状态；状态框会写这台机器现在的角色和有没有在跑。
3. 命令行等价检查（页面打不开时先看这两条）：

```sh
ubus call luci.zte-mesh status          # 返回 role / local / 实例是否在跑
logread | grep zte-mesh | tail -30      # 起不来的原因都写在这里
cat /run/zte-mesh/wlan-sync.status      # 档案落到本机无线的结果（只有会落地的角色才有）
```

- **装完要重启两次**：`/etc/init.d/rpcd restart` 和 `/etc/init.d/zte-mesh restart`。`apk add` 只换文件——owrt 上实测
  替换完 `luci-app-zte-mesh` 之后状态里连 `local` 这个键都没有（rpcd 还拿着装之前加载的那份插件），`attach` 实例报着
  running 而 `zmesh0` 早就不存在。重启之后 `attached=true`、`zmesh0` 重新挂回 `br-lan`。
- 这三条加"实例集合对不对、主控给的 SSID 落没落地"合起来就是装机验收的一条命令：
  `sh tools/lab/verify-install.sh /tmp/zte-apk`（把 apk 放机器上 `/tmp/zte-apk` 再跑；不带目录就只读不装）。
  它输出 `KEY=VALUE` 证据行和最后的 `RESULT=PASS|FAIL`，判定逻辑由 `sh tests/test_verify_install.sh` 在主机上守着。

## 4. 场景 S1：OpenWrt 当中控，接一台原厂中兴子路由

**这是出厂默认**，装完不用改角色：`zte_mesh.main.mesh_role=master`、`apply_local=0`（两个开关各自的完整语义见
[SERVICE](SERVICE.md) 的「设备角色（两个开关）」）。

1. 页面上决定**要不要自己也发 Wi-Fi**（这就是第二个开关）：
   - 「本机也广播」**关**（默认）：这台机器只当中控，把档案下发给子路由，**自己的无线一个字节都不动**。
     SR1010 这类刷 OpenWrt 的有线网关就该选这个。
   - 「本机也广播」**开**：同一份档案既下发、也落到本机 `/etc/config/wireless`——即原厂页面里的"主路由"。
     这时必须去「Wi-Fi 配置」tab 填 2.4G 名称与密码（名称必填、密码 8–63 字符），默认值是
     `ImmortalWrt` / `Ztemesh2631!`，**上线前改成自己的**。
2. 保存并应用。改这两个开关会让服务重启一次（实例集合变了），子路由会短暂掉出拓扑。
3. 中兴那台子路由：**保留原厂固件，不刷机**。在它自己的原厂界面上有 MESH 开关与"自组网 V2"——
   **那是用户自己的设置，本插件从不改动**（红线，见 [REAL_CHILD](REAL_CHILD.md)）。要它入网，只需要：
   - 子路由和这台 OpenWrt 在同一个二层（默认挂 `br-lan`；不是就改页面上的「LAN 桥」）；
   - 子路由**重启一次**，或者等控制器下一次广播搜索（每 300 秒一次，最坏等 5 分钟）。
4. 「自组网」（`mesh_v2`，中控侧的准入开关）默认**关** = 接受任何子路由；**开** = 只接受市场码相同的中兴子路由。
   接不上时先确认这一项。

**判据**（S1 怎么算过）：在页面「Wi-Fi 配置」里改 SSID / 密码 / 信道 / ACL，子路由的空中名称跟着变；
节点表里这台子路由显示 `ZXHN Z506` 这类型号、状态在线，「重启」与「开灯 / 关灯」两个按钮点得动（只中兴子路由有）。

## 5. 想让它连访客网络、额外 SSID

「本机也启用额外 SSID」（`extra_slots`，默认关）+「额外的 Wi-Fi 网络」列表（每条选频段、槽位、开关、名称、密码；
槽位 1=访客、2=备用、3=隐藏回程，同一频段必须按 1、2、3 连续，去掉 1 而留着 2 会被拒绝）。开与关的区别只在
**会不会把这些槽位落到本机射频**；下发本身不受它影响。注意：额外槽位和该频段的主网络**共用同一个网桥**，
这个镜像还没有做 VLAN 隔离，所以访客网络并没有和主网络隔开。

## 6. 场景 S2：OpenWrt 当中兴主控下面的子节点

1. 「常规」→ **设备角色** 选「子路由」。页面随即只留下这台机器用得上的几项（Wi-Fi / 漫游 / 额外网络的配置消失，
   因为档案由主控下发）。
2. 网线插到主控的 LAN 口。入网时**先沿用本机正在广播的无线**，等主控下发自己的名称/密码/加密——
   所以不需要在 OpenWrt 上填任何 Wi-Fi 参数。
3. 判据（S2 怎么算过）：**中兴主控**上改 SSID，20 秒内在这台机器上 `iw dev` 看到的名称跟着变，改回来也能回退。

**无线回程（S3，未端到端验证）**：同一页会出现「回控制器的方式」，选「Multi-AP（4 地址）」后，主控下发的回程网络
会落成一个客户端接口（`zte_backhaul_sta_<band>`，`mode sta` + `multi_ap 1` + `wds 1`）。它要求完整 `wpad`，
且**拔掉网线还能不能连上、1905 帧能不能过无线，目前只有配置面的证据**（[PLAN](PLAN.md) H-01）。
当主控的那一端在同一页上叫「给子路由的回程网络」，把回程做成独立 BSS（`multi_ap=2`）、主 SSID 标成前传。

## 7. 四类"我设了却没动静"

这几条的共同点：**选项写了，不代表这个角色的服务会用它**。角色决定两件事——页面上出现哪些字段，以及这台机器上有没有一个
applier 会把档案落到本机射频；换过角色或手改过 `/etc/config/zte_mesh` 的机器，最容易出现"配了没动静"。逐条对照：

| 现象 | 为什么 | 怎么办 |
|---|---|---|
| 服务起不来，日志 `execv errno=8` | 内核跑不了 32 位 ARM 程序，且没有模拟器 | 第 1 节第一条 |
| 回程开关设了，射频上什么都没有 | ①这个值只接受 `off` / `multi_ap`：页面上写错的写法保存时会被拒绝并说明原因，但**直接编辑 UCI 写进去的 `multi-ap` 这类值会被读成 off**（宁可不半开回程）；②这台机器是不落地档案的主控，本来就不动射频 | 用页面而不是编辑器改它；确认这台是会落地档案的角色（「本机也广播」开着，或角色是子路由）。看页面「状态」与 `logread` |
| 一个频段有两块射频，改了却没生效 | 控制器档案不知道该写到哪块，`wlan-sync` 会**放弃这个频段**并在日志里点名让你去设 `2.4 GHz 射频设备` / `5 GHz 射频设备` | 按日志列出的候选名填进那两个字段（例如 `radio1`）。保存这两项会重启服务，因为不重启就等于没选 |
| 有线口显示"未接入" | veth 两端一起没了，只有重启实例才能重建 | `attach` 兼任看门狗：连续 20 秒看不到这个口就重启沙箱实例，通常两分钟内自己恢复（这一条还没在实验机上复现过，见 [PLAN](PLAN.md) H-10）；想立刻恢复就重启服务 |

## 8. 卸载与版本

```sh
/etc/init.d/zte-mesh stop && /etc/init.d/zte-mesh disable
apk info -e | grep zte                 # 先看清这台装过哪几个
apk del luci-app-zte-mesh zte-mesh     # 再把装过的包名补进这条：zte-mesh-launcher、luci-i18n-zte-mesh-zh-cn
```

- **一条 `apk del` 里只要有一个包名不在这台机器上，整条就失败**。虚拟机上没装 launcher 和中文包，
  第一次整条报错——按实际装过的分开删，或者先 `apk info -e | grep zte` 看清楚。
- apk 清不掉的要手工删：`/etc/config/zte_mesh*`（受保护的配置，apk 故意留着）、`/run/zte-mesh`、
  `/usr/lib/zte-mesh`（运行环境解出来的目录树）。手工编过 `ns-run` / `mesh-search` 的机器，那两个文件
  不属于任何包，apk 不会碰它们。
- 装的是哪一批可以从包版本读出来：`apk list --installed | grep zte`。三个包的 `PKG_RELEASE` 各自计数、批末对改过的包加一
  （[PLAN](PLAN.md) 第 6.3 节），现在分别是 `zte-mesh` 0.1.0-r17、`luci-app-zte-mesh` r9、`zte-mesh-launcher` r1。
  不加这一位，装机本身不会失败——`apk add` 给同名同版本的 apk 也会 `Replacing (0.1.0-r7 -> 0.1.0-r7)`，实测在 owrt 上
  中文包就是这样被换掉的。不动的是**记录**：`apk list` 报不出这台机器装的是哪一批，走 feed 的 `apk upgrade` 更不会把它
  当升级推给你。批末不加号，等于这批只在构建机上存在过。
- 产物是 `build-apk.yml` **手动触发**跑出来的，push 不会自动构建；发布是另一回事，见 [DEVELOPMENT](DEVELOPMENT.md) 第 8 节。

## 9. 别做这些事

- **不改子路由的模式**（`Mode`、自组网 V2、`VersionMode`、MESH 开关）——那是用户在子路由上自己设的。
- 向真机推配置会**真实且持久**地改变设备设置；改 SSID、关主 SSID、ACL 封禁真实终端、信道/功率之前先确认
  你能接受设备掉线，并留着还原的基线（[REAL_CHILD](REAL_CHILD.md) 末尾有读回命令与基线）。
- 仓库里不放任何密码和密钥：telnet 口令只从环境变量 `ZTE_TN_PASS` 读，Wi-Fi 密钥只写进 0600 临时文件。
