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
| **这台机器能不能直接跑 32 位 ARM 程序** | `uname -m` 是 `aarch64` 且固件带 `CONFIG_COMPAT=y` → 能。`x86_64` 或没有 compat 的 arm64 → 不能，必须自己把 `qemu-arm-static` 放进私有根 `/usr/lib/zte-mesh/root` | 控制器实例每 5 秒以 `execv errno=8` 退出，页面显示没跑起来。模拟器**不在任何包里**，判定表与三条出路见 [LAB](LAB.md) 第 4.2 节。多数固件不导出 `/proc/config.gz`（两台实验机都没有），所以这一条别看配置文件，看服务起来没起来、日志有没有 `errno=8` |
| **无线回程要完整 `wpad`** | `apk info wpad`（`wpad-openssl` 这类变体也算，它 provides `wpad`）。25.12 起的 ImmortalWrt 用 apk，**机器上没有 `opkg` 这个命令**，`opkg list-installed \| grep wpad` 只在老 release 上有意义；`wpad-basic` 不够 | 只有开 `backhaul`（第 5、6 节）才受影响；`wlan-sync` 会把原因写进日志与页面状态行，不会静默失败 |
| **flash 够不够** | `zte-mesh` 这个 apk 是 3.9 MB（r17 实测 3 872 842 字节），`apk info -s zte-mesh` 报 8 007 KiB，装完 `apk del` 释放 7.8 MiB——文档以前写的"约 4.1 MB / 4.9 MB"是 r5 那一批的数；LuCI 与中文包是 KB 量级。**要跑模拟器的那台机器上，占地方的是你自己放的那份 `qemu-arm-static`（10.7 MB），不是包** | 换 `wpad` 变体也可能挤爆小 flash |

## 2. 装包

四个包，名字与作用：

| 包 | 架构 | 内容 |
|---|---|---|
| `zte-mesh` | `all` | 服务本体：`/etc/init.d/zte-mesh`、`attach` / `wlan-sync`、rpcd 插件、默认 `/etc/config/zte_mesh`、带原厂 `multiapd` 的运行包（解到 `/usr/lib/zte-mesh/root`） |
| `zte-mesh-launcher` | `aarch64_cortex-a53` | 现编的 `ns-run` 与 `mesh-search`；依赖 `ip-full`、`kmod-veth`、`kmod-sched-core`。缺 veth 内核模块时启动器停在 `error=private-service-network errno=2`（建私有网线之前先 `stat /sys/module/veth`） |
| `luci-app-zte-mesh` | `all` | 配置页面（`网络 → ZTE Mesh`） |
| `luci-i18n-zte-mesh-zh-cn` | `all` | 中文文案。**装应用包不会自动带上它**，要中文就单独装 |

**手工装（最常用）**：从 `build-apk.yml` 那次运行的 `zte-mesh-apk` artifact 里拿四个 apk（解开是一个平铺目录，
`SHA256SUMS.txt` 就在里面，所以 `sha256sum -c` 要 `cd` 进那个目录再跑）。包没签名，所以要 `--allow-untrusted`：

```sh
ssh root@<这台路由器的IP> 'mkdir -p /tmp/zte-apk'
scp dist/*.apk root@<这台路由器的IP>:/tmp/zte-apk/     # 这个目录只放这一批的 apk
ssh root@<这台路由器的IP> 'cd /tmp/zte-apk && sha256sum -c SHA256SUMS.txt'
ssh root@<这台路由器的IP> 'apk add --allow-untrusted /tmp/zte-apk/zte-mesh*.apk /tmp/zte-apk/luci-*.apk'
```

（`scp` 的目标目录不存在时它会把最后一个文件**写成那个名字**，所以先 `mkdir`；artifact 解开是平铺的，
`dist/*.apk` 那一行按你放的位置换。）

- **一个目录只放一批**。`sha256sum -c` 只证明"文件没坏"，证明不了"这是这一批"——上一批那四个 apk 和它们的
  `SHA256SUMS.txt` 一起留在 `/tmp/zte-apk` 里时，校验照样全 OK。而 glob 一旦同时命中两批，`apk` 解出来的目标版本
  连文件名都对不上（实测在 owrt 上报 `Downgrading zte-mesh (0.1.0-r17 -> 0.1.0-r9)`，而目录里根本没有 r9 的文件）。
  换批之前把旧 apk 挪走或改名成 `*.apk.stale`，别指望 `sha256sum -c` 替你把关。
- 装与升级**不会覆盖**你改过的 `/etc/config/zte_mesh`（它是 conffile；实测往里面加一行注释、`apk del` 再 `apk add`，
  那一行还在）。但页面存过一次之后，本地这份就和包里那份默认的长得不一样了，于是**每次装都会在旁边落一个
  `/etc/config/zte_mesh.apk-new`**（8 KB 的出厂默认档案，带全部注释）。它不是备份也不是垃圾，是 apk 的"这里有冲突"
  记号；想清掉就按第 8 节那条 `zte_mesh*` 的 glob 一起删。
- **x86_64 的机器（比如虚拟机）上面那条 `apk add` 会一条都不装**：`zte-mesh-launcher` 是
  `aarch64_cortex-a53`，架构不匹配 → `ERROR: unable to select packages: ... uninstallable`，`rc=1`，另外三个
  `all` 的包**也跟着不装**（apk 的求解是全有或全无）。这种机器把启动器从那条命令里拿掉再装，
  然后自己编 `ns-run` / `mesh-search`，命令见 [LAB](LAB.md)；`zte-mesh` 本体照装。
- 只有 `zte-mesh` 没有启动器 = 页面能打开、控制器起不来（缺 `ns-run`，它在 `/usr/libexec/zte-mesh/`）。
  四个包是"一套"，不是可选菜单。

**编进固件**（长期用这台机器当控制器时更省事）：把本仓库当 feed 加进 ImmortalWrt 树，
`./scripts/feeds update zte_mesh && ./scripts/feeds install -a -p zte_mesh`，在 `.config` 里勾上这四个包
（`CONFIG_PACKAGE_{zte-mesh,zte-mesh-launcher,luci-app-zte-mesh,luci-i18n-zte-mesh-zh-cn}=m`）。feed 登记与编译命令见
[DEVELOPMENT](DEVELOPMENT.md) 第 8 节。

## 3. 打开页面并确认服务起来了

1. LuCI 登录 → **网络 → ZTE Mesh**（页面标题 `ZTE Mesh`，中文版是「中兴组网」）。
2. 「常规」tab 最上面是**启用**（默认开）与服务状态；状态框会写这台机器现在的角色和有没有在跑。
3. 命令行等价检查（页面打不开时先看这三条）：

```sh
ubus call luci.zte-mesh status          # 返回 role / local / 实例是否在跑
logread | grep zte-mesh | tail -30      # 起不来的原因都写在这里
cat /run/zte-mesh/wlan-sync.status      # 档案落到本机无线的结果（只有会落地的角色才有）
```

- 配置被拒绝时这三条长什么样（实测 owrt）：`logread` 里是一行 `daemon.warn zte-mesh: <哪个选项> ...; not starting`，
  `status` 变成 `running=false`、`attached=false`、`age=-1`、`state=null`，`wlan-sync.status` 干脆不存在。
  看到 `state=null` 就别再读后面那些键了——服务根本没起来。反过来，`ns-run` 打的那几行
  `ZTE_CONTROLLER_TRACE unix_bind ... errno=98/111` 是套接字重绑的过程记录，不代表故障。
- `wlan-sync.status` 的 `bands=` 那一行说的是**这一次落地时改动了哪些频段**，不是"这台机器只管这些频段"：
  ax3600 上实测 `bands=5g` 而两个频段的名字都是这次档案里带的（日志会一行一个频段说清楚）。
- **装完要重启两次**：`/etc/init.d/rpcd restart` 和 `/etc/init.d/zte-mesh restart`。`apk add` 只换文件——owrt 上实测
  替换完 `luci-app-zte-mesh` 之后状态里连 `local` 这个键都没有（rpcd 还拿着装之前加载的那份插件），`attach` 实例报着
  running 而 `zmesh0` 早就不存在。重启之后 `attached=true`、`zmesh0` 重新挂回 `br-lan`。
- 这三条加"实例集合对不对、主控给的 SSID 落没落地"合起来就是装机验收的一条命令：
  `sh tools/lab/verify-install.sh /tmp/zte-apk`（把 apk 放机器上 `/tmp/zte-apk` 再跑；不带目录就只读不装）。
  它输出 `KEY=VALUE` 证据行和最后的 `RESULT=PASS|FAIL`，判定逻辑由 `sh tests/test_verify_install.sh` 在主机上守着。
  **带目录那一档会装、会重启服务**，所以它读到的不是"上一批留下的样子"而是这一批跑起来的样子；SSID 那一项现在会
  等档案落地（`ssid_wait=<秒>`，上限 `SSID_WAIT`，默认 60 秒）再判——早期版本在重启后 12 秒就断言，ax3600 上实测
  把一个健康的子节点判成 `RESULT=FAIL`（那一秒无线配置里还是上一个名字）。等不到才 FAIL，并把没落地的行号写成
  `ssid_pending=0 1`。
  末尾还会**读回装进机器的那份**：apk 打包会剥掉 ELF 节，设备上的 `.so` 和仓库里的成员不是同一串字节，所以只能按
  字符串认——`trace_per_bss_name=yes`、`probe_bss_detail=yes` 说明这一批的写入方确实在机器上；
  `init_seed_from_air=yes`、`sync_flag_by_meaning=yes` 认的是包里的两个 shell 文件（`/etc/init.d/zte-mesh` 里的
  `live_ap_iface`、`/usr/libexec/zte-mesh/wlan-sync` 里的 `set_flag`）——**一批只改脚本时 rootfs 里一个字都没变，
  只读那两个库就会把"没装上"读成"装上了"**；机器上根本没有那份文件时打印 `unreadable`、不判（环境事实不是批次判决）。
  `peers_sta` / `peers_software` / `peer_reports_stations` / `state_bss_detail` 是它读到的数据，不当判据
  （`peers_sta=0` 配 `peer_reports_stations=no` 只说明这台 peer 从没报过终端数）。
- 这一次重启里，无线落地器**真写过几回配置**是第四条冒烟：`applier_writes=<次数>`，数的是日志里
  `applied; wireless committed` 的行数增量。**为什么不能比文件**：旧种子先把空中改名、主控再把它翻回来，
  `/etc/config/wireless` 前后一模一样，可家里已经掉了两次线——只有落地器自己那行留了痕。判 `0` 有个前提：
  重启前空中已经带着档案里每个启用行的名字（`air_matched_plan=yes`）；还在收敛就只报数不判，否则把一次正常
  的收敛说成故障。另外四种形状不算故障：这一趟没重启 `no-restart`、这个角色本来就没有落地器 `no-applier`
  （纯主控）、`logread` 读不出来 `no-logread`（读不出来就当成 0 行，而 `0 - 0` 正长得像一次安静重启）、日志环在
  窗口里翻了圈 `wrapped`（窗口被截断，增量不是个数）。不装包、只想驱动这一次重启来问这句：
  **`RESTART_NOW=1 sh tools/lab/verify-install.sh`——它会真掉线**，默认关着（判据见第 6 节和 [PLAN](PLAN.md) H-14）。
- 同一份日志里还读一个数：`cur_hop=<数字>`，取的是守护进程自己那行 `ZTE_MESH_SERVICE cfg` 里的跳数。子角色的启动
  路径只写 `-2`（"还没接入"），往子节点方向再没有第二处写它——所以读到别的数就说明**这颗原厂守护进程真的更新了它**。
  当主控的那一列本来就是我们自己写的，只当证据不判；另外三种也不判：日志环已经把那一行翻出去了（`no-cfg-line`，
  实验机跑久了就是这一种）、守护进程自己读不到那一列（`-98`）、这一趟刚重启过服务（新的那一行可能落在我们读之后）。
  **第四种是 r18 装机那一趟教出来的**：那一行末尾本来就带着读数的时刻（`epoch=<秒>`），脚本因此顺带报
  `cur_hop_age=<秒>`——比 `CUR_HOP_GRACE`（默认 120 秒）还年轻就只报不判。ax3600 上实测
  `13:26:51 … cur_hop=-2` → `13:27:18 … cur_hop=2`：子节点重启后要在"还没接入"上待约半分钟，只读复跑落进这半分钟
  就把一台健康的机器判成 `RESULT=FAIL`（和上面 SSID 那一项"等档案落地"是同一件事，隔一列重犯）。反过来，没有
  `epoch=` 的行算不出年龄，**照判**——不能因为读不到时刻就悄悄放过这一项。

## 4. 场景 S1：OpenWrt 当中控，接一台原厂中兴子路由

**这是出厂默认**，装完不用改角色：`zte_mesh.main.mesh_role=master`、`apply_local=0`（两个开关各自的完整语义见
[SERVICE](SERVICE.md) 的「设备角色（两个开关）」）。`uci show zte_mesh | grep apply_local` 很可能**一行都不出**——
这一项没写就是 0，页面只在把它打开时才写进去；别把"读不到"当成"文档写的默认不存在"。

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
这条**要有真子路由才能判**：实验台上中控的对端是另一台刷了本插件的 OpenWrt，它报的是插件自己的型号字符串，
按不了"显示 `ZXHN Z506`"这半句，改 SSID 那一半又会把家里正在用的无线翻掉——两边都记在 [PLAN](PLAN.md) 第 5 节。

## 5. 想让它连访客网络、额外 SSID

「本机也启用额外 SSID」（`extra_slots`，默认关）+「额外的 Wi-Fi 网络」列表（每条选频段、槽位、开关、名称、密码；
槽位 1=访客、2=备用、3=隐藏回程，同一频段必须按 1、2、3 连续，去掉 1 而留着 2 会被拒绝）。开与关的区别只在
**会不会把这些槽位落到本机射频**；下发本身不受它影响。注意：额外槽位和该频段的主网络**共用同一个网桥**，
这个镜像还没有做 VLAN 隔离，所以访客网络并没有和主网络隔开。

"被拒绝"是哪种拒绝（owrt 上实测，只留 2 号槽、不给 1 号）：服务**整个不起来**，`logread` 里一句
`zte_mesh_bss '<段名>': ...; not starting`（槽位这一条是 `24g slot 2 needs slot 1 (slots must be 1..k); not starting`），
`status` 是第 3 节写的那个 `state=null` 形状。页面存得下去，但服务不认——改错了先看日志那一行，别猜。
手敲 UCI 时 `uci add zte_mesh zte_mesh_bss` 只回一个段名（`cfg02306b`），要写成
`uci set zte_mesh.cfg02306b.band=24g`，漏掉 `zte_mesh.` 前缀就是五个 `uci: Entry not found` 加一个空段。

## 6. 场景 S2：OpenWrt 当中兴主控下面的子节点

1. 「常规」→ **设备角色** 选「子路由」。页面随即只留下这台机器用得上的几项（Wi-Fi / 漫游 / 额外网络的配置消失，
   因为档案由主控下发）。
2. 网线插到主控的 LAN 口。入网时**先沿用本机正在广播的无线**，等主控下发自己的名称/密码/加密——
   所以不需要在 OpenWrt 上填任何 Wi-Fi 参数。"沿用"是**逐频段从空中读回来**的：服务起来前 `zte-mesh.init`
   看这块板子每个频段正在广播的那一段，把名称、密码、加密、隐藏、开关原样当种子写进服务配置，于是主控档案
   落地时 `wlan-sync` 逐字段一比，**没有要改的**——不 `commit`、不 `wifi reload`（`wifi reload` 是全局的：
   任何一个频段上的一次空写就把所有频段的终端一起踢掉）。两个频段广播的不是同一对时，各频段按各自的样子写，
   并把 `wifi_dual_band_sync` 落成 0，免得守护进程把 2.4G 的名称与安全派到 5G 上；页面上那对
   `wifi_24g_ssid` 若与空中不同，日志会明说以空中为准（`wifi_24g_ssid is not the seed on this router`）。
   **r17 及以前的包还不是这样**：它拿页面填的那一对当种子，ax3600 上实测每次重启服务都把空中名字翻两次——
   头几秒广播自己那份（`radio 0 (2g…): name set to 11 characters, security sae-mixed`，正是本地
   `wifi_24g_ssid` / `wifi_24g_security`），二十秒左右主控的 M2 到了才翻成主控的名字（`name set to 8 characters …
   psk-mixed`）。家里有人连着的时候这就是两次掉线。仓库版本要的是**一次都不翻**：配置面早就证到了（新种子与
   `/etc/config/wireless` 逐字段相同，拿它喂 `wlan-sync` 报 `changed=0`、`wifi` 一次没被调用、配置文件 md5 没变），
   **真重启这一半在装 r18 的那趟读到了**——那次装机自带的重启窗口给出 `air_matched_plan=yes` + `applier_writes=0`
   + `sync_changed=0`，`/etc/config/wireless` 的 md5 装前后都是 `59094b96b83f8f74655f5268b95be2be`（判据见
   [PLAN](PLAN.md) H-14；自己另开一次重启仍属于第 9 节说的"先确认你能接受设备掉线"）。**要主动驱动这一次重启**：
   把脚本传上机器后跑 `RESTART_NOW=1 sh /tmp/verify-install.sh`——只重启、不装包，然后看 `air_matched_plan=yes`
   并且 `applier_writes=0`（这两行是什么意思，第 3 节末尾写了）。
3. 判据（S2 怎么算过）：**中兴主控**上改 SSID，20 秒内在这台机器上 `iw dev` 看到的名称跟着变，改回来也能回退。
   实验台上能测的是"本插件当主控"这一半：从子节点服务起到主控的名字翻上去实测 21 秒，与"20 秒内"同量级；
   真中兴主控那一半还在等真机窗口。

**无线回程（S3，未端到端验证）**：同一页会出现「回控制器的方式」，选「Multi-AP（4 地址）」后，主控下发的回程网络
会落成一个客户端接口（`zte_backhaul_sta_<band>`，`mode sta` + `multi_ap 1` + `wds 1`）。它要求完整 `wpad`，
且**拔掉网线还能不能连上、1905 帧能不能过无线，目前只有配置面的证据**（[PLAN](PLAN.md) H-01）。
当主控的那一端在同一页上叫「给子路由的回程网络」，把回程做成独立 BSS（`multi_ap=2`）、主 SSID 标成前传。

## 7. 五类"我设了却没动静"

这几条的共同点：**选项写了，不代表这个角色的服务会用它**。角色决定两件事——页面上出现哪些字段，以及这台机器上有没有一个
applier 会把档案落到本机射频；换过角色或手改过 `/etc/config/zte_mesh` 的机器，最容易出现"配了没动静"。逐条对照：

| 现象 | 为什么 | 怎么办 |
|---|---|---|
| 服务起不来，日志 `execv errno=8` | 内核跑不了 32 位 ARM 程序，且没有模拟器 | 第 1 节第一条（owrt 上实测：`ZTE_DB_SANDBOX error=execv errno=8` 每 5 秒一条，页面状态这时还是 `running=true`，只有 `attached=false` 露出馅） |
| 回程开关设了，射频上什么都没有 | ①**手改 UCI 写进 `multi-ap` 这类第三种写法，不是"读成 off"，是整个配置被拒绝、服务不起**（`zte-mesh.init` 的取值表只认 `off` / `multi_ap`；owrt 上实测日志 `backhaul must be off or multi_ap; not starting`，状态变第 3 节那个 `state=null` 形状）——页面保存同样的写法是另一回事，它会当场拒绝并说明原因；②值本身合法但这台机器是不落地档案的主控，本来就不动射频：日志会明说 `zte_mesh.main.backhaul=multi_ap has nothing to do while apply_local is off: ...`（owrt 实测），并把两条出路写在那一句里 | 用页面而不是编辑器改它；确认这台是会落地档案的角色（「本机也广播」开着，或角色是子路由）。看页面「状态」与 `logread` |
| 一个频段有两块射频，改了却没生效 | 控制器档案不知道该写到哪块，`wlan-sync` 会**放弃这个频段**，日志原文是 `several 5g radios (radio0 radio1) and no way to tell which one carries the mesh; set zte_mesh.main.wifi_5g to one of them, this band is left alone until then`——它点名的是 UCI 项，页面上对应「2.4 GHz 射频设备」/「5 GHz 射频设备」两个字段 | 按日志列出的候选名填进那两个字段（例如 `radio1`）。保存这两项会重启服务，因为不重启就等于没选。ax3600 正是这块有两张 5G 卡的板子，它现在靠 `radio0.disabled=1` 避开这一条（候选只剩一块），不是测不出来 |
| 有线口显示"未接入" | veth 两端一起没了，只有重启实例才能重建 | `attach` 兼任看门狗：连续 20 秒看不到这个口就重启沙箱实例，owrt 上实测删掉 `zmesh0` 后 25 秒自己长回来（[PLAN](PLAN.md) H-10，2026-11-02 已验）；想立刻恢复就重启服务 |
| 角色是子路由，手改的 `wifi_24g_ssid` 没动静 | 子路由的档案由主控下发，而服务起来时的**种子**是从空中读回的（第 6 节第 2 条）。这几项在子角色上顶不上本机正在广播的那一段，页面上也根本不显示它们（[FEATURE_STATUS](FEATURE_STATUS.md) B9）；日志会明说 `child role: wifi_24g_ssid is not the seed on this router: ...` | 想改空中的名称就改**主控**的档案；想把这台变成能自己定的，就改成主控并开「本机也广播」。手工改 `/etc/config/wireless` 也能生效，但下一次 M2 到达时会被主控的档案盖掉 |

## 8. 卸载与版本

```sh
/etc/init.d/zte-mesh stop && /etc/init.d/zte-mesh disable
apk info -e | grep zte                 # 先看清这台装过哪几个
apk del luci-app-zte-mesh zte-mesh     # 再把装过的包名补进这条：zte-mesh-launcher、luci-i18n-zte-mesh-zh-cn
```

- **一条 `apk del` 里只要有一个包名不在这台机器上，整条就失败**。虚拟机上没装 launcher 和中文包，
  第一次整条报错（实测 owrt：`ERROR: No such package: zte-mesh-launcher`，`rc=1`，一个包都没被删）——
  按实际装过的分开删，或者先 `apk info -e | grep zte` 看清楚。
- apk 清不掉的要手工删：`/etc/config/zte_mesh*`（受保护的配置，apk 故意留着，第 2 节说的 `.apk-new` 也在这一条里）、
  `/run/zte-mesh`（tmpfs，重启就没了）。**`/usr/lib/zte-mesh` 那棵运行环境树是包自己装进去的，`apk del` 会连它一起删**——
  owrt 上实测删完只剩你手工放的 `root/qemu-arm-static`（10.7 MB，占地方的一直是它，不是包）和那个空目录。
  手工编过 `ns-run` / `mesh-search` 的机器，那两个文件在 `/usr/libexec/zte-mesh/`，跟同目录里属于包的
  `attach` / `wlan-sync` 放一起，不属于任何包，apk 不碰它们（实测删完只剩这两个）。
- 装的是哪一批可以从包版本读出来：`apk list --installed | grep zte`，它报的就是 `pkgver`（实测与文件名里那一位一致，
  `zte-mesh-0.1.0-r17` / `luci-app-zte-mesh-0.1.0-r9` / `zte-mesh-launcher-0.1.0-r1`）。三个包的 `PKG_RELEASE` 各自计数、批末对改过的包加一
  （[PLAN](PLAN.md) 第 6.3 节），现在分别是 `zte-mesh` 0.1.0-r17、`luci-app-zte-mesh` r9、`zte-mesh-launcher` r1。
  不加这一位，装机本身不会失败——`apk add` 给同名同版本的 apk 也会 `Replacing (0.1.0-r7 -> 0.1.0-r7)`，实测在 owrt 上
  中文包就是这样被换掉的。不动的是**记录**：`apk list` 报不出这台机器装的是哪一批，走 feed 的 `apk upgrade` 更不会把它
  当升级推给你。批末不加号，等于这批只在构建机上存在过。
- 产物是 `build-apk.yml` **手动触发**跑出来的，push 不会自动构建；发布是另一回事，见 [DEVELOPMENT](DEVELOPMENT.md) 第 8 节。

## 9. 别做这些事

- **不改子路由的模式**（`Mode`、自组网 V2、`VersionMode`、MESH 开关）——那是用户在子路由上自己设的。
- 向真机推配置会**真实且持久**地改变设备设置；改 SSID、关主 SSID、ACL 封禁真实终端、信道/功率之前先确认
  你能接受设备掉线，并留着还原的基线（[REAL_CHILD](REAL_CHILD.md) 末尾有读回命令与基线）。
- **重启这个服务也算在上面那一类**：r17 及以前，子节点每重启一次，空中名字翻两次、家里连着的人掉线两次
  （第 6 节）。r18 把种子改成从空中读回，ax3600 上装它那一趟的重启窗口读到 `applier_writes=0`——这一台机器上
  重启一次都不写无线。**这句话只担保它自己那台机器那一批**：换机型、换批次，先跑一次带重启的验收
  （`RESTART_NOW=1`，[PLAN](PLAN.md) H-14）看到 `air_matched_plan=yes` + `applier_writes=0`，再决定要不要在
  有人连着的时候动。
- 仓库里不放任何密码和密钥：telnet 口令只从环境变量 `ZTE_TN_PASS` 读，Wi-Fi 密钥只写进 0600 临时文件。
