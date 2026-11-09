# 开发计划：让 OpenWrt 与中兴路由器组网

> 这份文件是**唯一排期表**：先做哪几件、每件怎么算做完、哪些必须等上机。
> 功能状态与证据只在 [FEATURE_STATUS](FEATURE_STATUS.md)（唯一总账）；协议与逆向在 [research/](research/)；
> 构建与测试命令、条数基线在 [DEVELOPMENT](DEVELOPMENT.md) 第 4 节；装机在 [INSTALL](INSTALL.md)；
> 机器清单在 [LAB](LAB.md) §2.1；中兴原厂路由器的语法基线在 [REAL_CHILD](REAL_CHILD.md)；过程叙事在 [journal/](journal/)。
> 别处出现的"已完成/未做"都不算数。最后更新：2026-11-05。

## 1. 终点：四个可验收的场景

每个场景一条判据，判据都是**看得到的现象**，不是日志里的 "status=0"。

| # | 场景 | 拓扑 | 现在 | 判据（怎么算过） |
|---|---|---|---|---|
| **S1** | 主控 | OpenWrt 有线网关（`master`）→ 中兴原厂路由器（原厂固件，有线回程） | ✅ 已在 ZXHN Z506 上过 | 中兴原厂路由器的 SSID/密码/信道/ACL 跟着页面变；节点动作（重启、指示灯）生效 |
| **S2** | 子节点（有线回程） | OpenWrt 主控 → OpenWrt 子节点（`slave`），一根网线 | ☑️ 两台测试机打通（中兴原厂主控那部分还没上过） | 主控改 SSID，20 秒内子节点的 `iw dev` 跟着变，且改回来能回退 |
| **S3** | 子节点（**无线**回程） | 主控 ⇝ 无线 ⇝ OpenWrt（`slave` + `backhaul=multi_ap`） | ⬜ 只有 AP 半边 | 拔掉网线，OpenWrt 仍能拿到配置、能上网（4 地址回程 BSS 关联成功） |
| **S4** | 多子节点 + 可见 | S1/S2 加第二台子路由、页面显示拓扑与终端 | ⬜ 单节点 | 页面列出两个及以上节点、每个节点的回程 RSSI 与终端数，与真实路由器上的读数对得上 |

S1 是"能用"，S2 是"能当子路由"，S3 是"不用网线"，S4 是"能运维"。批次计划围绕这四个场景排。

## 2. 这一轮定下的开发规则

1. **主机可测的项先做**。判定标准是：这件事的结论能不能只靠仓库里的代码 + 测试桩 + 构造的帧/配置得出。
   能 → 直接做、直接测；不能 → 只登记到第 5 节，不占当前批次的时间。
2. **需上机的项不阻塞**。登记要写到"谁来验、几步、判据是什么"，这样等上机窗口到了不用重新想。
3. **CI 按批跑，不按项跑**。一批（3–5 项）做完才手动触发一次 `build-apk.yml`，然后装机验一次。
   单改一个文件就烧一次 20–30 分钟的构建、再装一次机，是把时间花在等上面。
4. **一批的收尾固定三件事**：本机能跑的测试全绿（第 4 节的命令）→ 文档同步（[FEATURE_STATUS](FEATURE_STATUS.md)
   的状态列 + 本文件批次表，涉及操作的还有 [INSTALL](INSTALL.md)/[DEVELOPMENT](DEVELOPMENT.md)；其余文档已冻结，不回头改）
   → `commit`（不推送，攒到批末一起推）。
5. **不许用"编译通过"当验收**。这个仓库有过三类假通过：`(void)write` 在 GCC 11 下编不过、
   `tcpdump` 流式输出切错列把 771 字节的 M1 读成"最大 68 字节"、`state[0]:3` 被当成故障。
   每项都要留一条能重复跑的判据。

## 3. 批次计划

类型：**H** = 主机可测，**D** = 需上机（登记在第 5 节）。批 1–26 已全部做完；**批 27 已全部做完**（A27 只读设备页、
B20 型号，r26 已由 CI 构建并装到 `ax3600` 与 `owrt` 两台，页面在浏览器里验过——
H-20 的 ①②③；④⑤ 仍待，登记在第 5 节）。**逐项状态、判据条数与证据只在
[FEATURE_STATUS](FEATURE_STATUS.md)（唯一总账）**，每一轮当时的 CI 读数与装机读回在 [docs/journal/](journal/)，
这份排期不复述过程。

**排给下一批（主机可测）**——攒够 3–5 项再按第 6 节的节奏触发一次 CI：

- **25 待排（H）· init 的 ARM32 判定表漏了 32 位 ARM 那种架构**（2026-11-06 写 README「支持的架构」时读
  `zte-mesh.init` 读出来的）：`zte-mesh.init` 的 `case "$(uname -m)"` 只有 `aarch64|arm64)` 那一个分支去问内核，
  `*)` 那一个分支的注释写"x86-64 and anything else can never run the ARM32 daemon natively"——`armv7l` 走进这支被判
  `compat=0`，于是**在能原生执行 ARM32 的机器上**打一条"这个内核跑不了 32 位 ARM 守护进程并且 `$ROOT` 里没有
  `qemu-arm-static`"再照常原生起服务（`emulator` 仍是空，`ns-run` 直接 exec `ld-linux.so.3`）。服务是对的，日志是错的；
  更糟的是这台机器如果真放了一份 `qemu-arm-static`，它会被用上去——白慢一层。修法：`armv7*` 单独一个分支判
  `compat=1`（32 位内核执行 32 位 ARM 不需要 COMPAT），注释里那句"anything else"改成真的只剩非 ARM。**主机可测**：
  `test_init.sh` 把假的 `uname` 冒充成 `armv7l`，断言不出现那句、`ZTE_MESH_QEMU` 不传给 `ns-run`。上机读数与 H-16
  一起等一台 32 位 ARM 的 OpenWrt 路由器（三台测试机是 x86_64／aarch64／x86_64，Z506 不吃 apk），所以这一条只有主机判据。
- **26 待排（H）· CI 里 `tools/install` 那一步到底有没有少做事**：构建缓存命中的那一轮它仍走满 772 个目录访问、
  约 1000 秒，是整轮里最大的一块。要读的是它的 stamp 与 `build_dir/hostpkg` 有没有活过缓存恢复（在不在缓存 path 里）。
  判据是 `run_stage` 报的**目录访问计数掉下来**；不掉就说明这一步与缓存无关，那两百秒的浮动只是镜像与磁盘的。
  计时与计数的实现见 [DEVELOPMENT](DEVELOPMENT.md) 第 8 节。

- **27 待排（H）· 宿主侧还有 6 处往交换目录写，跟着沙箱放的符号链接走**（2026-11-07 顺着 B12 那一类查出来的）：
  `zte-mesh.init` 的 `.wlan-plan.$$`（`:510/540`）、`.config.$$`（`:630/763`，**这份带 Wi-Fi 密钥**）、`.bss-plan.$$`（`:734`）
  三个临时文件都建在共享目录里、名字用可预测的 `$$`，最后一步 `mv` 没有 `-T`；`version_mode.host`（`:764`）与
  `restart.sig`（`:795`）是直接 `>`，连临时文件都没有；`sta-collect:115/128` 的 `.ap-sta.$$` 同第一组。
  沙箱与宿主同一个 UID，这些名字它都能预先放成符号链接。**修法与 B12 已修的那处完全一样**：`mktemp` 把临时文件建在
  `/run`（chroot 只 bind 挂 `/run/zte-mesh` 本身，父目录够不着），再 `mv -fT` 一次 rename 覆盖。这 6 处的目标父目录就是
  `/run/zte-mesh` 本身，沙箱换不掉它（换它要宿主 `/run` 的写权限），所以不必动沙箱挂载。主机可测：`test_init.sh` 与
  `test_sta_collect.sh` 各加一组"预先放链接 → 读回受害文件未改、目标是普通文件、目录里没被搬进东西"。
- **28 待排（H）· rpcd 的 `actions/` 收件目录多一层：目录本身就能被换掉**（同上轮查出，**原方案已撤销**）：
  `luci.zte-mesh:30` 往 `$RUNDIR/actions/.$id.tmp` 写，`actions/` 是共享目录的**子目录**，沙箱能把它整个换成指向别处的
  符号链接，宿主 root 这次写入就跟着出去。一度改成兄弟目录 `/run/zte-mesh-actions`，**那是错的并已还原**：消费它的
  `action_reconcile()` 在 `controller_service()` 里，而这个进程跑在 chroot 内（`zte-mesh.init:987` → `ns-run.c:161` →
  `db-engine-probe.c:253`），唯一挂进来的宿主路径就是 `/run/zte-mesh`，搬出去之后沙箱侧 `mkdir` 得 EROFS、`opendir` 失败，
  页面三个按钮全部返回 `null`。正确方向：**取消 `actions/` 子目录**，请求文件直接叫 `/run/zte-mesh/<id>.act`，宿主用
  `mktemp` 建在 `/run` 再 `mv -fT`；消费者 `readdir` 已按 `.act` 后缀与长度过滤，改成扫交换目录本身即可。
  **主机测试表达不出这一条的判据**：`test_rpcd.sh` 的服务是 shell 假替身，只验插件侧，不跑真消费者——上一轮那三条回归
  在假替身上是绿的，端到端却是坏的。改完要在测试环境真起一次服务，按页面按钮读 `state.json` 的 `actions`。
- **29 待排（H）· 装机验证认不出"运行包里换进一枚旧 probe"**（2026-11-07 批末 repack 时发现的）：
  `verify-install.sh` 的 `packed_copy()` 只 grep `bss_detail` 与按 BSSID 那个文件名两个标记，`tests/test_runtime.py`
  的标记同理——**这四个标记 r23 里全都有**，所以 tar.gz 里换进上一批的 `db-engine-probe`，两处照样绿。本轮是靠
  `.note.gnu.build-id` 判出来的：OpenWrt 构建时 strip 包里的 ELF，装上的字节数与 tar.gz 成员对不上（probe 189 155
  对 211 660），但 build ID 是 allocated 段、strip 不动它。修法分两侧：**主机侧**比 tar.gz 成员的 sha256 与
  `prebuilt/db-engine-probe.sha256`（repack 换进去的就是那枚，精确相等）；**装机侧**只能问 build ID（strip 之后
  sha256 必然不同），而 `ax3600` 上没有 `readelf`，所以要自带一个从 ELF 里取 note 的读法（`hexdump` 就够）。
  主机可测：`test_runtime.py` 拿一份故意换成旧 probe 的 tar.gz 当反证，必须红。

- **30 已完成（A28）· 跨节点设备明细**：页面「设备」一节原先只有本机视角——控制器从 AP Metrics 只学到每 BSS
  的终端**数**，学不到终端 MAC（见 [ARCHITECTURE](ARCHITECTURE.md) 那一节的边界）。本轮没有取证窗口（H-21 仍未做），
  按本条原定的第二条路先做：子节点把自己那张表的终端 MAC、BSSID 与 SSID 塞进 `0x800c` 的自定 vendor 子 TLV `0x70`，
  主控解码后写 `/run/zte-mesh/peers/<AL MAC>.assoc`，页面在 A27 那一节加一列「节点」，只有上报、没有本机关联的设备
  位置改成「无线」（来源「子路由上报」）。判据与边界写在 [FEATURE_STATUS](FEATURE_STATUS.md) A28；上机 ①②③④
  2026-11-09 装 r27 已读到（H-22）。取证那条（原厂子节点发不发 Client Association、原厂主控认不认）仍是 H-21——
  结论决定要不要把这段自定载荷换成标准字段，这一条不因它作废。

- **31 已完成（A26/A28 的应答器）· 应答器只答发给自己的查询**：`agent_ap_metrics_answer()` 以前把查询的**目的**
  地址当应答的**源**地址，于是任何被当成查询目的的地址都会成为一个节点身份——主控按帧源 MAC 落 `peers/`，节点表
  因此多出没有型号的行（`02:00:00:00:19:cd`，页面把空名称兜底成 `router`）。现在函数多收一个本机 AL 参数：目的
  地址既不是本机 AL 也不是广播就不答，应答的源固定写本机 AL；`controller-trace.c` 用 `SIOCGIFHWADDR` 从 `br0`
  读一次身份，读不到就不答。判据：`tests/agent-ap-metrics-codec.c` 106 条（新增"别的单播 → 0""广播 → 应答且源是
  本机 AL"），ARM32 库在 `ubuntu` 上交叉编译通过，运行包已按 A28 流程换进这份新库（`repack-runtime.py` +
  `test_runtime.py`）。**上机**：r28（CI run 42705586146）两台 `verify-install.sh` 各 PASS（24 / 23 查，0 败）；
  先升子节点后主控上 `peers/…:cd.*` 即停止刷新、`…:e3.*` 照常，再升主控重启后 `state.json` 只剩 `…:e3`、
  `zmesh0` 抓 26 秒无 `…:cd` 帧——读数见 `docs/journal/2026-11-10.md`。**只堵新的幽灵行**：启动时 `br-lan` 还没
  挂端口就取 AL 末字节那条（另一条根因）本轮没改，已经写进 `state.json` 的旧行要等它自己过期或重启才消失。

需要上机才能验的项不阻塞排期，全部登记在第 5 节（H-01 … H-22）；主机侧做完、结论要等上机窗口的写在那里。

## 4. 主机测试的判据范围

命令与条数基线归 [DEVELOPMENT](DEVELOPMENT.md) 第 4 节，这里不重复。

- **规则：本机能跑通的判据 = 结论的一半，另一半必须上机。** 批末判定以全部行为准，本机能跑的先跑，
  C 那几行在批末那一次 CI 或 Ubuntu 上补。
- 要 `out/analysis/cspd` 的只有 `cspd` 证据那几组：`test_schema.py`（4 条）、`test_policies.py`（5 条）、
  `test_profiles.py`（6 条）、`test_startup.py`（整组卡在 `setUpClass`）。`cspd` 只从固定哈希的原厂镜像提取
  （`evidence/` 不入库），所以这几组只在有镜像的机器（mac 上的容器）绿；`ubuntu`（`192.168.1.191`）没有镜像，
  `make test-host` 会停在第一组就退出——那是环境事实，不是回归。
- 本机（Windows + Git Bash）的两个环境坑，不是代码问题：`python3` 常解析成"去商店装 Python"的占位程序
  （不报错也不输出），`test_rpcd.sh` 的 jsonfilter 桩会整批变成 `{"result":-2}`——放一个指向真 Python 的垫片；
  `test_wlan_sync.sh` 在 Git Bash 上跑不完，去 ubuntu 或 owrt。
- `test_verify_install.sh` 判的是 busybox ash 的语义，本机 `sh` 过了以后在 owrt 上再跑一遍更稳。

## 5. 上机待测清单（OpenWrt 路由器 / 中兴原厂路由器，登记，不阻塞）

编号 **H-xx**。每条写清"验什么、前置、几步、判据"。到上机窗口时按编号取。

| 编号 | 验什么 | 前置 | 步骤 | 判据 | 风险 |
|---|---|---|---|---|---|
| **H-01** | S3：无线回程关联（B13c） | 一台能发 Wi-Fi 的 OpenWrt 路由器当子节点（现在的 VM 没有无线）；中兴原厂路由器（当主控）已开回程 BSS；子节点装**完整** `wpad`（ax3600 实装机是 `wpad-openssl` + `hostapd-common`，够用） | ①子节点 `uci set zte_mesh.main.backhaul=multi_ap` 并 `mesh_role=slave` ②插上主控的配置后确认 `uci show wireless` 里有 `zte_backhaul_sta_5g`：`mode=sta`、`multi_ap=1`、**`wds=1`** ③`iw dev <sta接口> info` 看 `4addr on`，`ip -d link` 同处，`brctl show` 与 `ubus call network.interface.wan status` 确认它挂在 `br-lan` ④拔网线 ⑤`iw station dump` 看 `Authorized`，`ping` 上游 ⑥无线侧抓 1905 帧。**①–⑤ 从 r19 起是一条命令**：`verify-install.sh` 的 `smoke_backhaul` 判我们写的那三行（`backhaul_mode_5g`/`backhaul_multi_ap_5g`/`backhaul_wds_5g`，缺 `wds` 直接 FAIL），报射频答的那六行（`backhaul_iface_5g`/`backhaul_assoc_5g`/`backhaul_4addr_iw_5g`/`backhaul_4addr_ip_5g`/`backhaul_brport_5g`/`backhaul_station_5g`）——`4addr` 那两条 grep **顺带也是一次实测**：本仓库一直写着"`iw dev info` 打 `4addr on`"却没有实测出处，哪台机器的哪个构建会这么说，读一次就知道 | STA 关联成功且 `4addr` 打开；拔线后仍能 ping；1905 CMDU 在无线侧可见。**已知会踩的点**：没有 `wds` 时 netifd 直接 `BRIDGE_NOT_ALLOWED`（`supplicant.uc:278`），`4addr` 也只看 `wds`（`:305`）——所以这一条只验端到端，不验映射。**2026-11-04 装 r19/r11 那一次没验到**：两台都是 `backhaul_asked=off`、`backhaul_section=none`，`smoke_backhaul` 的三行判据与四条 4 地址 grep 一个都没跑——`iw dev info` 到底打不打 `4addr on` 这条实测还没有读数，H-01 照旧等窗口 B（要先把 ax3600 设成 `backhaul=multi_ap`，再拔网线） | 换 `wpad` 可能挤爆 flash；4 地址要求两侧都开，主控是原厂不可控 |
| **H-02** | B11 在中兴原厂路由器上不炸：额外网络写成多个 BSS | 小米 AX3600 + 中兴原厂路由器（当主控）或 OpenWrt 路由器（当主控） | ①`extra_slots=on` ②主控配一个访客网络（序号 1） ③`iw dev` 与 `uci show wireless` ④客户端连访客 SSID | 多出一个 BSS、名字密钥对、能拿到 IP；关掉后 BSS 消失且 `wlan-sync.status` 有记录 | 信道/带宽被多 BSS 摊薄；`wpad` 版本不支持多 BSS 时会静默失败 |
| **H-03** | A17 踢终端/封锁真的生效 | S1 拓扑 + 一台测试手机（MAC 已知）；页面「封禁与踢除设备」里各一条 `ban`（可选时长）与 `kick` | ①封禁该 MAC，`state.json` 的 `clients[]` 应显示 `sent` ②终端重连看是否被拒 ③删除记录，看是否恢复 ④加一条 `kick`，抓 1905 看是否真有 `0x8014` 出门、有无 `0x8015` 回报，`clients[]` 变 `done`/`failed` ⑤必要时开 `EnBtmKickoff` 再试 | 封禁后拒绝关联；解封后恢复；`0x8014` 真的发出；BTM 发出后终端切换（不保证） | 会踢掉用户自己的设备；`EnBtmKickoff` 出厂是关的，可能根本不发；`0x8014` 的 BSSID 目前用广播（还没有"终端挂在哪个 BSS"的表），要确认子节点接受广播 |
| **H-04** | 多子节点（A21 的落盘 + A23 的拓扑） | 两台子路由同时在线 | 各自动作一次（`kick` 或 `ban`），看落盘与路由 | 拓扑页两个节点同时出现、动作按 MAC 不串；`/run/zte-mesh/peers/` 两个 MAC 各自的文件不互覆盖；`state.json` 汇总两节点 | 1905 域 ID 冲突、AL MAC 冲突（`al_last`） |
| **H-05** | A18 RSSI/终端数的真值来源 | S1 + 一台已关联终端 | 抓 1905 后与 `iw station dump` 对照。**已有负结果**（[CAPTURE_1905](research/CAPTURE_1905.md)）：有线链路的 107 帧里没有任何 RSSI，链路度量应答是 ZTE 定长模板；要看 RSSI 必须走无线回程 | 页面上的数值与真实路由器读数的偏差在 ±3 dB 内，或明确标注"来自控制器上报" | 原厂 `StaRssiRptThr` 出厂 -110（等于不上报），可能根本没有值 |
| **H-06** | B15 `CmWLANMapSsidCfgSync2Repeater` | 中兴原厂路由器 + strace | 逆向或动态跟踪 | 沙箱里替身不再需要 | 需要停机窗口；前序项没做完不值得动 |
| **H-07** | D5 VLAN / D6 绑定 / D7 BTM 在 E2631 上的实际行为 | 一台市售 E2631/AX3000，停机窗口 | **先确认命令面再抓包**：用它自己的 `sendcmd` / 帮助输出核实 `pktdump` 在不在，再 `pktdump 1` 抓线上。[MULTIAPD](research/MULTIAPD.md) §5 那张命令表出自**未 strip 的 SR1010 版**，E2631 原厂那份已 strip、字符串里只剩 `sendcmd multiapd..master` 外壳（[BLOB_SHIM](research/BLOB_SHIM.md) §1），表里每一条在这台上存在与否都没核对过。命令面不在就走退路：`logcat` 里过滤 `AGENT RECV`，或 static 编 armv7l musl 抓包程序 + 限时 promisc | 三处配置入口能被厂商自己的 web 打开（说明运行时真的读） | 动的是别人的家用路由器 |
| **H-08** | 出厂 `VersionMode` 与"兼容模式"文案的对应 | E2631 页面 | 改页面开关，看 `MultiAPCfg.VersionMode` 与 `P1905ComCfg.Enable` | 文案与字段一一对应 | 已有一半结论（[FINDINGS](research/FINDINGS.md)），缺厂商侧确认 |
| **H-09** | 长时间稳定性（A22 的真实路由器版） | 两台测试机 + 一台中兴原厂路由器 | 装 rXX 后放 24 小时，看 `daemon_restarts`、内存、`dmesg`；**外加中兴原厂路由器推送时的日志占比**（改前实测：一次推送 1012 行 / 7 分钟，超过整圈 128 KiB，四小时后仍占 94.5%） | 重启计数 0、无 OOM；`logread` 里 `ZTE_TRACE_POLL` 不超过每分钟 4 行、每个超额窗口一条 `log_budget=poll sampled=1` 且带 `dropped_total=`，用户的行（入网、封禁、告警）还在。**当前读数**：每分钟 4 行与超额说明两台测试机已读到；缺 24 小时与中兴原厂路由器那一次推送 | 会占着机器 |
| **H-10** | **已验**：A24 有线口掉线自愈——宿主这头的 `zmesh0` 没了，沙箱自己长回来 | 测试环境控制器（owrt VM），页面能看到「有线接口」 | 宿主侧 `ip link del zmesh0`，观察服务与链路重建 | **25 秒自愈，没有人重启主控**（GRACE=20 加一个 tick 的生效时间，和脚本里的常数对得上）；重启的是沙箱所属的 controller 实例，`attach` 自己的 pid 没动——这正是"看门狗没被一并删掉"的证据；`daemon_restarts` 仍为 0，节点重新 `online` | 只验了"主动删线"这一种丢失；`attach` 自己也被杀时自愈停摆；连续重启会短暂（约 1 秒）断开子路由的 1905 链路 |
| **H-11** | A13′ 角色改成 `master` 后中兴原厂路由器照样入网；也是 C8 那部分（"只读文档能走到 S1/S2"）的入口 | ax3600（或 Z506）+ OpenWrt 主控，装机验证 | ①主控 `mesh_role=master`（不再发 `auto`）②子路由开机入网 ③`uci show zte_mesh` 与页面拓扑确认 ④主控改成 `apply_local=1` 再看本机 SSID 与子路由 SSID 同步 ⑤子路由重启一次，等 5 分钟内被 `search` 重新发现（A25 之后才可能） | 入网、下发配置、拓扑填充与 `auto` 时代一致；关键是**没有**出现子路由自当选主控（角色那几列 `Mode`/`SetRoleEnable`/`EnMasterOnly` 在 `MultiAPCfg` 那一行——不是 `MultiAPMaster`，那张表装的是 steering 与漫游的开关）；页面节点表显示的是真实型号，不是插件自己的字符串 | 改动只在主控写库的 `SetRoleEnable` 与 `Mode`，子路由侧一行没动 |
| **H-12** | A18c 子路由固件版本显示到页面——布局是代码级推的，真实路由器上没读到过 | 一台中兴原厂路由器（ZXHN Z506，页面写着 `V1.0.0.2B2.8000`）+ 主控已装机；`tcpdump` 抓 `zmesh0` | ①抓主控收到的 CMDU，找 `0x8002`（AP Capability Report）②在它的 TLV 里找 `0xd4`（厂商 Device Inventory Type），按 `[len][text]` 手工切三段，看顺序是否 序列号 → 软件版本 → 硬件版本 ③`cat /run/zte-mesh/peers/<MAC>.software` 看文件是否生成、内容是否与页面同源 ④刷新拓扑页，看 `nodes[].software` 是否显示版本号而不是 `-`。**r19 起这些读数在一条命令里**：`verify-install.sh` 报 `peers_seen`（`peers/` 里出现过几台 MAC）、`peer_software_bytes`（`.software` 一共几个字节，0 = 一台都没发过）、`nodes_rows` / `nodes_online`（`state.json` 里节点行数与在线数，页面那一侧的配平）。**只报不判**，因为"0"要配 `peers_seen>0` 才是负结果 | ②切出来的第二段与子路由「关于」页逐字符相同；③④有值即通路全绿。**负结果同样有用**：若 `0x8002` 里根本没有 `0xd4`，说明这个 Z506 不发库存字段，解码器留着但页面无数据，要另找来源。**别拿测试环境那台当负结果**：那台 peer 从不发带 `0xd4` 的 `0x8002`（读回 `peers_software=0`），只说明没收到过，不说明 Z506 也不发。**2026-11-04 装 r19/r11 后的第一次连读**（两台测试环境机器，配的正是这两个条件）：`owrt` `peers_seen=1 peer_software_bytes=0`、`ax3600` `peers_seen=1 peer_software_bytes=0`——两台都见过一台 peer 的 MAC，`.software` 加起来 0 字节，就是"peer 在、库存字段没发过"这种情况；`0xd4` 那一段照旧要 Z506 那台。 | 只读抓包，不推配置；子路由侧一行没动 |
| **H-13** | A18d 的前提：原厂路由器当子节点时回 `0x800c` 那几帧**一帧列全所有 BSS** 还是**一射频一帧**（测试环境抓的包里没有 `0x800c`） | 一台中兴原厂路由器 + 主控已装机；`tcpdump -i zmesh0` 抓 1905；页面上这个节点已能看到终端数 | ①2.4G 和 5G 各挂一个终端 ②抓 `0x800c` AP Metrics Response，数每条消息里 `0x94` 的个数与各自 BSSID ③`ls /run/zte-mesh/peers/` 看是否出现 `<MAC>.<BSSID>.sta`（几路 BSS 就几个）④断开 5G 那个终端，看页面数字是否只降这一路 ⑤关掉子节点一个 BSS，30 秒后看它是否从总和里退出。**r19 起这一段也是一条命令**：`peers_sta`（`.sta` 文件几个）、`sta_file_sum`（这些文件里的数字之和）、`bss_detail_rows`（`state.json` 里 `"bssid":"` 出现的次数，页面那一列的行数）、`peer_reports_stations`（`.metrics` 有没有 `stations=`）。四个数只报不判：`peers_sta=0` 在 `peer_reports_stations=no` 的机器上不说明任何事，`bss_detail_rows` 与 `sta_file_sum` 对得上才是"逐路进页面"成立 | **先确认这一步有没有输入**：`.metrics` 里必须有 `stations=`（读回打印 `peer_reports_stations=yes`）——测试环境那台只有 `hops=1`，即从没收到带终端数的 `0x94`，在那种机器上"没有 `.sta`"什么也不说明。②③ 对得上即通路成立：**一帧一 BSS** ⇒ 每个 `.sta` 各自有时间戳、页面读"还活着的几路之和"；**一帧列全** ⇒ 每次上报同时刷新全部文件，两者等价，`bss=` 应等于条目数。失败的样子是只有 `.metrics` 没有 `.sta`，页面退回摘要（数字仍是最后一帧的总数，不算错但不准确）。**2026-11-04 装 r19/r11 后的第一次连读**：`owrt` `peer_reports_stations=no peers_sta=0 state_bss_detail=present bss_detail_rows=0`，`ax3600` 同前三个键，另加 `nodes_rows=0 nodes_online=0`。**先按这一行自己那条前提读**：两台都是 `peer_reports_stations=no`，那 `peers_sta=0` 与 `bss_detail_rows=0` 什么都不说明——"一帧列全还是一射频一帧"这条判据**一个字节都没推进**，照旧等 Z506。读到的一件是结构事实：子节点跑 `slave` 时 `state.json` 里**根本没有** `bss_detail` 那把键（`absent`），主控有键但零行——用户问的"子路由页面上不显示设备、主路由显示子路由的设备"就是这个结构，`nodes_rows=0` 与"子节点没有自己的 agent"对得上。**2026-11-05 批 18 补了替身**：OpenWrt 子节点从此会答 `0x800c`（宿主采集 + 沙箱应答 + 主控定时查询，见 H-15），所以"**没有 `.sta`**"这一句今后只对**没装批 18 包的机器**和**中兴原厂子节点**成立——替身答的是 OpenWrt 那一侧，Z506 当子节点时发不答、应答的格式是不是一帧列全，照旧只能上机读。**同日装 r20/r11 后补一条更正**：上面那句"只对没装批 18 包的机器和中兴原厂子节点成立"暗示装了包的 OpenWrt 子节点会读成 `yes`，实测不成立——那把键来自 `0x92` 摘要（原厂 agent 的路径），替身回的是 `0x94` 逐路，而且 `.metrics` 只存最后到的那一帧，一帧不带终端数的状态帧会把 `stations=` 抹成只剩 `hops=1`（八次连读到一次 `no`）。本行"先确认这一步有没有输入"的判据因此换成 `peers_sta>0`（`.sta` 文件在不在），装载体的 OpenWrt 子节点现在是 `peers_sta=2`。**第 19 批把这句判据做成了读数**：装机脚本现在对 `peers/` 做多次快照（只在"这一趟哪儿都没数"时重采，默认最多 3 趟），并直接打印 `peer_input=yes|no`（`.sta` 文件在 **或** 摘要带 `stations=`）与 `peer_reads=`（读了几趟）——本行那句"先确认有没有输入"以后看 `peer_input`，`peer_reports_stations` 只是结束连读那一趟的快照，装载体的子节点照样可以是 `2` 配 `no` | 只读抓包，不推配置；子路由侧一行没动 |
| **H-14** | B8b 的最后一步：子节点**真重启一次**，看服务重启时不再改本机广播的 SSID。批 10 只证到配置面（仓库版的 init 与 `wlan-sync` 在 ax3600 上是**对着 `/etc/config/wireless` 的副本**跑的，射频一下没碰）。**判据已固化成一条命令**（批 11 的 C11）。**①②③④⑤ 与 ⑦ 前半部分已结案，只剩 ⑥ 与 ⑦ 后半部分** | ax3600（`mesh_role=slave`，挂在 owrt 主控下面）＋ **r18 及以后的包**（r17 仍是旧的初始配置）；家里没人连着的时候做 | ①`rssh.py ax3600 put tools/lab/verify-install.sh /tmp/` ②`RESTART_NOW=1 sh /tmp/verify-install.sh` ③读 `air_matched_plan` 与 `applier_writes` ④`logread` 里 `wlan-sync` 不许出现 `several 5g radios`（那是 `radio0.disabled` 被改动的信号）⑤对照 `/run/zte-mesh/config`：应写主控下发的两个名字、`wifi.dual_band_sync=0`（r17 那份写的是页面填的 `ImmortalWrt` 一对）⑥**只剩这一条掉线窗口**：只从**主控侧**改一次 SSID，确认该翻的那一次照翻（20 秒内）且翻完没有多余的第二次 ⑦**只剩开一次窗口读那一次读取**（开关本身在批 13 已经有了，是一个文件不是代码）：owrt 上 `printf 'MultiAPCfg 0 CurHop 5\n' > /run/zte-mesh/db-profile`，重启服务，`verify-install.sh` 读回 `db_profile_cur_hop=5` **且** `master_cur_hop=5` 才说明这个 5 真进了主控自己那一行（`profile_apply` 在角色写列之前跑，而主控启动路径不写 `CurHop`，所以它活过一次重启）；然后 ax3600 只读跑一次，看同一行里的 `cur_hop=` 与 `max_hop=`——**跟随 5＝采用主控下发的 TLV `0x43`，仍是 2＝本机 `MaxHop`（`slave_init` 自己设的）** | ③ 是 `air_matched_plan=yes` 且 `applier_writes=0` ⇒ **核心判定结案**：一次重启＝零次无线配置写入。**当前读数**：装 r18 那一次自带的重启窗口就是这个值（`sync_changed=0`、`/etc/config/wireless` md5 未变、`several 5g radios` 0 次、`/run/zte-mesh/config` 已带上主控的两个名字）。**对照组是看得见的**：r17 重启时先广播页面填的那对，约 21 秒后 M2 才把名字改回去——两次掉线，而这一条现在会被判 FAIL（`applier_writes` 非 0），这正是它值得自动化的地方。若 `air_matched_plan` 不是 `yes`，脚本只报数不作判定，那一次窗口要重跑，不算结论。⑦ 前半部分已不用等窗口：脚本按 `epoch` 报了年龄，环里那两行（`-2` 与更新的值相差 27 秒）就是这一步的读数。**⑦ 后半部分现在是两条读数**：主控 `master_cur_hop=5`（开关进了那一行）＋子节点 `cur_hop=`（5 或 2）——**先看到前者再解释后者**，否则主控把字段拒了也会让子节点停在 2，看起来像一个结论。r10 装机那一趟正好把"还不能解释"的情况走了一遍：没打开开关（`db_profile_cur_hop=absent`）、子节点刚重启完读到 `cur_hop=-2` 年龄 12 秒，脚本按年龄只报告数值、不作判定——那一次读取不产生结论。测完删掉 `db-profile` 里那一行并重启服务 | **⑥ 会让 ax3600 上的终端掉线**（改主控 SSID 是唯一还没自动化的一半），红线里"先征得同意"包括这一条；测前留 `iw dev` 基线，测后确认名称回到主控下发的那对。② 单独用也会掉线，所以默认关。⑦ 只动主控沙箱里那一列，不碰射频，但重启主控会让子节点短暂（实测 8–58 秒）掉出拓扑；测这一条期间**不要在页面上切角色**——那处换角色的写列会把 `CurHop` 写回 2，看起来像开关没生效 |

| **H-15** | 批 18 那条替身链在真实路由器上的三项：①**组合 ③**（OpenWrt 主控 + OpenWrt 子节点）页面第一次出现真数；②**组合 ②**（中兴原厂主控 + OpenWrt 子节点）原厂主控收不收我们回的 `0x94`；③**组合 ①**（OpenWrt 主控 + 中兴子节点）原厂 agent 答不答空列表的 `0x800b` | 两台装好批 18 包的 OpenWrt 路由器（owrt 当主控 + ax3600 `mesh_role=slave`），子节点要真能广播 AP 且至少挂着一个终端——虚拟机没有射频，这一条在它上面测不出来；中兴原厂路由器（ZXHN Z506）那两部分要它分别当一次主控、一次子节点 | ①子节点 `cat /run/zte-mesh/ap-sta`：`epoch=` 在 30 秒内、每个 AP 一行 `BSSID 数`，且**回程 sta 接口不许在表里**（在表里就是把主控数成本机的一个终端）②主控 `logread` 里 `ZTE_MESH_SERVICE ap_metric_query mac=… status=0`（`status=0` 才叫真发出去了）③主控 `ls /run/zte-mesh/peers/` 出现 `<MAC>.<BSSID>.sta`，`cat` 同 MAC 的 `.metrics` 有 `stations=` ④刷拓扑页：「终端数」不是 `-`、「BSS 明细」一路一行；把一个终端连上去看数字跟着涨 ⑤断开它，10–30 秒内退回去 ⑥子节点停掉采集（r21 起 `touch /run/zte-mesh/collect-pause`：只停这一条循环，表留着等它过期；批 18 那两台没这个开关，只能删 procd 实例，而恢复它要重启服务；`ifreload -a` 也算），30 秒后页面退回 `-`——**过期即沉默，不是报 0** ⑦原厂那部分：`tcpdump -i zmesh0` 抓主控发的 `0x800b`，数 TLV `0x93` 里那个计数是 0（空列表）还是列全 BSSID，再看我们回的 `0x800c` 之后原厂页面/数据库有没有这台子节点的终端数 | ①–④ 齐 ⇒ 组合 ③ 结案，H-13 那句"先确认这一步有没有输入"第一次能读到 `peer_reports_stations=yes`（**2026-11-05 更正：这句话不作数**，见下面那次连读）；⑤⑥ ⇒ 增减与"过期即沉默"在设备上成立；⑦ 的计数是 0 ⇒ 我们的应答格式正好对着原厂主控的问法，列全 BSSID 也答得上（应答按指定的路数生成，问几条答几条）。**原厂收不收值长 10、不带 ESP 那一段的 `0x94`，只有这一条读得出来**——主机测试里读它的是我们自己的解码器，那只证明字节自洽。**2026-11-05 装 r20/r11 后的第一次连读**：①②③ 都读到（表两行 `BSSID 0`、主控 `status=0` 每 10 秒、子节点 `ap_metrics_answer bytes=60 sent=60`、主控 `.sta` 两个文件与 `bss_detail_rows=2`），④读到一半——数不是 `-`、明细一路一行，但"把一个终端连上去看数字跟着涨"没做：**三台测试机都没有可当终端的射频**（`ubuntu` 只有 `ens33`/`docker0`/`lo`，`owrt` 是虚拟机），这部分要人拿一台手机连上子节点的 SSID。⑤⑥⑦ 照旧没动。**③那条判据里"`cat` 同 MAC 的 `.metrics` 有 `stations=`"当场判不了**：`.metrics` 只存最后到的那一帧，一帧不带终端数的状态帧会把 `stations=` 抹成只剩 `hops=1`（八次连读到一次 `no`），所以"有没有输入"看 `.sta` 文件在不在，不看这个单次采样的证据行；页面读的是 `.sta` 之和，不受它影响。替身答的本来就只有 `0x94` 逐路，`stations=` 那把键属于 `0x92` 摘要，是原厂 agent 的路径。**同日 ⑥ 用 procd 那两条命令读了一次**（子节点 `ax3600`）：`ubus call service delete '{"name":"zte-mesh","instance":"sta-collect"}'` 之后采集进程没了、`ap-sta` 停在那儿不再更新时间戳，主控约 45 秒后 `state.json` 里这个节点 `stations` 从 `0` 退回 `-1`、`bss_detail` 零行，**而节点行还在**（拓扑没动）——"过期即沉默，不是报 0"这部分照预期成立。恢复用的是 `/etc/init.d/zte-mesh start`，数字回来了，但那一下把守护进程也换了 pid（16814 → 22636），子节点为此掉出拓扑十几秒：**这趟读回来的是"重启了一次"，不是"采集停了"**。第 19 批因此给 `sta-collect` 加了它自己的开关（`touch /run/zte-mesh/collect-pause` 停、`rm -f` 起，5 秒内恢复更新时间戳），⑥ 要在装了 r21 的机器上照新开关重读一次才算结。**同日装了 r21 重读一次**（`ax3600` 创建/删除开关文件，`owrt` 当主控）：开关的两侧都照设计出现——打开后 `epoch` 停在 `1793860300`、`paused by /run/zte-mesh/collect-pause` 一行、子节点 `ap_metrics_answer` 不再出现；删掉后下一轮（5 秒）`collecting 2 access point(s) into /run/zte-mesh/ap-sta`、`epoch` 跳到 `1793860505` 并跟着时钟更新、主控那头的 `ap_metric_query … epoch=` 照旧每 10 秒递增——那枚 `epoch` 是主控自己写的、暂停开关打开期间也没停，所以它判不了"表停没停"，那部分要看子节点的 `ap-sta`（批 19 的 `ap_sta_age=` 就是为这个加的）；而且 daemon 没换 pid、拓扑没动——这一次读回来的确实是"采集停了"。**这一行那句"45 秒内退回 `stations:-1`"这一次判不了**：暂停约 3 分 15 秒，中途 `14:33:10` 读到的是两个 `.sta` 各一个 `0` 与 `state.json` `"stations":0`。按子节点那 30 秒应答时限推算，此刻主控自己的新鲜窗（`src/controller-service.c:965` 的 `controller_peer_bss_load(mac, 30, …)`）早该关了，照批 18 procd 那次的情形该是 `-1`；**但那次抄的是分钟级 mtime**（`ls -l`），带秒的 `metrics_age` / `bss_detail[].age` 没抄，主控查询日志与它自己 `date` 的对应关系也没核对——这三项任一项都能把"窗关没关"翻过来，所以这部分**不记成结论、记成待复测**。复测要抄的就是那三个秒级的数：`date +%s` 减 `.sta` 的 `%Y-%m-%d %H:%M:%S`、`state.json` 里 `"metrics_age"` 与 `bss_detail` 每条的 `"age"`；动作照旧 `touch` → 等 90 秒 → 抄 → `rm -f`，不动射频、不掉线，不用先征得同意。**2026-11-05 装 r22 那一轮把这三个数抄齐了，⑥ 结案**（`ax3600` 打开暂停开关、`owrt` 当主控）：打开暂停开关那一刻两台的 `date +%s` 都是 `1793872925`（先排掉时钟差），暂停前读到的是 `"stations":0`、`"metrics_age":6`；**100 秒后**（`18:03:45`，`date +%s=1793873025`）主控 `state.json` 这个节点 `"stations":-1`、`"metrics_age":30`、`"bss_detail":[]`，同一时刻 `peers/*.sta` 两个文件还在原地（mtime 停在 `18:02`、内容仍是 `0`）、主控那头的 `ap_metric_query … status=0 epoch=` 每 10 秒照旧递增——**"过期即沉默，不是报 0"到这一刻有秒级证据了**：那枚 `-1` 来自主鲜的 30 秒窗（`controller-service.c:954` 的 `read_peer(…, 30, &metrics_age)`），窗关了但节点行还在，拓扑没动。`rm -f` 之后 `18:04:09` 子节点恢复更新时间戳（`ap-sta` 的 `epoch=1793873049`、日志 `collecting 2 access point(s)`），44 秒后主控回到 `"stations":0`、`"metrics_age":3`、`bss_detail` 两行各 `"age":3`。这一趟 daemon 没换 pid、没动射频、没掉线。**④⑤ 照旧还缺一台手机、⑦ 照旧还缺 Z506**。**同日再装 `main` 上复现构建的那份 r22（run #60）把 ①–③ 的情形重读一遍，链子没断**：子节点 `ap_sta_rows=2 ap_sta_age=3`、表两行 `8c:de:f9:b3:d9:e4 0` / `…e3 0`，主控 `peers_sta=2` `peer_input=yes` `bss_detail_rows=2`，`peers/02:00:00:00:19:e3.<那两路 BSSID>.sta` 两个文件里的数与子节点自己表上的数一字不差——**launcher 并进 `zte-mesh` 之后，"查询 → 应答 → 按 BSSID 落盘"仍是同一条路**。数字是 0 因为这两台都没有终端连着，"**0**"和"`-`"分别是哪一半，还要 ④ 那台手机 | ①②③④⑦ 只读；⑤ 会动本机射频（`ifreload -a` 让子节点上的终端掉线几十秒），按红线先征得同意；⑥ 创建/删除 `collect-pause` 那一下不动射频、不掉线，不用先征得同意，但批 18 的机器上只能删 procd 实例并用 `/etc/init.d/zte-mesh start` 恢复——那会把守护进程换掉、子节点掉出拓扑十几秒，算要先征得同意的动作；⑦ 要 Z506 当主控，入网那一下是它主动发的，我们没法替它问一台它不认识的机器 |
| **H-16** | 批 20 那个"一个包三种架构"在真实路由器上的两段：**这台机器的 `uname -m` 能不能在包里找到自己的二进制**，以及**找不到时那句拒绝怎么说**。装装机时那道架构门（`DEPENDS:=@(TARGET_zte\|\|TARGET_qualcommax)`）已经拿掉，"这台不行"改由运行时的入口脚本与 init 的预检来说，而这两句在主机测试里只是桩出来的 `uname -m` | 两台测试机各装 launcher **r2**（`ax3600` 是 aarch64、`owrt` 是 x86_64，正好两种架构）；`armv7` 那种架构**目前没有机器**——三台测试环境的 CPU 是 aarch64 / x86_64 / x86_64，Z506 是中兴原厂固件、不吃 apk。ax3600 跑沙箱要靠私有根里的 `qemu-arm-static`，那是 ARM32 用户态的事，与 launcher 这种架构的 aarch64 二进制无关 | ①`sha256sum` 比对 artifact 里的 `SHA256SUMS.txt` ②`verify-install.sh` 读四行：`launcher_dispatch=` / `launcher_second_name=` / `launcher_arch_present=`（这三条是判据）与 `launcher_arch=`（入口自己说的那一行，是证据）③`/usr/libexec/zte-mesh/ns-run --launcher-check` 手工跑一次，看它报的是不是 `arch/<这种架构>/ns-run`，并确认 `mesh-search` 那个名字答的是同一种架构目录 ④`/etc/init.d/zte-mesh start` 之后 `ps` 里没有每 5 秒重开的 127 ⑤**对照组一次**：`mv /usr/libexec/zte-mesh/arch/x86_64 /usr/libexec/zte-mesh/arch/x86_64.bak`（这台机器瞬间"不在包里"），看 init 日志里那句 `zte-mesh-launcher has no build for`、`verify-install.sh` 判 `launcher_arch_present=no`，然后 `mv` 回来 | ②三行 yes 且 ③那一行指向本机那种架构 ⇒ 打包 + dispatcher 在真实路由器上成立；⑤ 那句出现且服务没被 procd 反复重开 ⇒ 运行时拒绝真的替代了装机时那句门；`launcher_arch=` 那行会进装机证据，`--launcher-check` 与人直接跑必须给出同一句第一行。**armv7 这种架构在拿到机器之前只能靠 CI**：`ZTE_MESH_REQUIRE_ARCHES` 让少编一种架构直接红，构建后用 SDK 编出来的 `apk extract` 解出成员、再问每个二进制是哪种架构 CPU 确认三个目录真在包里（`.apk` 是 libadb 容器，`tar -t` 一个成员也列不出来）——那是编译与打包级的证据，不是运行时的。**2026-11-05 装了 r2 / r21，①–⑤ 都读到**：两台三行 `yes`、`launcher_arch=` 各指 `arch/x86_64/ns-run` 与 `arch/aarch64/ns-run`、入口那两个名字 `cmp` 同一个文件（换程序不换 pid）、⑤ 那句完整（`… carries: aarch64 armv7 x86_64.bak`）且 `verify-install.sh` 当场 `FAIL launcher_arch_present`；`/tmp/log/apk.log` 另外给了这道门以前的样子——launcher r1 在这台 x86_64 上四次 `error: uninstallable`。**这一行读出来一处要改的代码**：`zte-mesh.init` 那句预检把 `--launcher-check` 的任何非零都说成"这台没带构建"，而架构只有 127 一个答案（先升 `zte-mesh` 后装 launcher 的那四秒里，入口名字上还是手工放的旧 ELF，回 64 打 usage，日志记了一句假诊断）——修法与用例见第 3 节，不在本行的判据里。**2026-11-06 再读一次（`ci/batch23`，包字节与批 22 逐字节相同，两台没重装）**：三行判据仍 `yes`、`launcher_arch=` 仍各指自己那种架构，`verify-install.sh` 两台 **18 查 0 败**；这一趟新增的不是运行时读数，而是把"打包级证据"与"设备上那几个字节"对上——CI 的 job log 现在自己报六行重量，与两台 `/usr/libexec/zte-mesh/arch/*/*` 的 `wc -c` 一字不差（aarch64 61 703／66 039、x86_64 61 719／61 975、armv7 356 373／389 117）。armv7 那种架构照旧没有机器 | 只读的那几步不动射频、不掉线。⑤ 动的是包自己在 `/usr/libexec` 下的文件，改名期间服务起不来，`mv` 回来即恢复（重装 apk 也行）；**不要拿 `ns-run` 这个入口去改**，`apk add` 会覆盖它，手工放进 `/usr/libexec/zte-mesh/` 的旧二进制也会被这份入口脚本盖掉（见 [INSTALL](INSTALL.md) 第 8 节） |
| **H-17** | 批 21 把 `zte-mesh-launcher` 并进 `zte-mesh` 之后，**升级路径上两个包同时声称拥有同一批文件**会发生什么。`/usr/libexec/zte-mesh/{ns-run,mesh-search,arch/**}` 以前只归 launcher，现在 `zte-mesh` 的安装规则也摆它们——两台测试机都还装着合并前的 launcher（`owrt`：`zte-mesh` r21 + launcher r2；`ax3600` 同），正是这个叠加态；`apk` 的数据库里 `world` 那条 launcher 记录也不会因为我们改了打包就消失 | 两台测试机各装一次合并后的 `zte-mesh`（`apk add --allow-untrusted zte-mesh-[0-9]*.apk`，**别用 `zte-mesh*.apk`**，那条 glob 会把目录里残留的旧 launcher apk 一并匹配进来） | ①`verify-install.sh` 的 `legacy_launcher_package=` 在装之前是不是版本号（不是就说明这台其实早就是合并态，本行不用读）②`apk add` 那一步的原文：报不报 file conflict、有没有 `Overlapping files`/`ERROR: … wants to install …`、`rc` 是多少 ③装完之后 `ls -l /usr/libexec/zte-mesh/` 与 `ls /usr/libexec/zte-mesh/arch/`：入口两个名字还在不在、`arch/` 三种架构齐不齐，`ns-run --launcher-check` 回什么 ④`apk list --installed \| grep zte` 里 launcher 还留不留着；**留着就再 `apk del zte-mesh-launcher` 一次**，看它会不会把 `zte-mesh` 现在拥有的那些文件一起删掉（`ns-run` 没了 = 服务起不来，这是这行唯一会让人等在哪里的地方）⑤`/etc/init.d/zte-mesh restart` 之后 `ubus call luci.zte-mesh status` 与 `logread \| tail` ⑥顺带读 `apk info -s zte-mesh`（合并后的占用，旧读数 8 007 KiB 只覆盖运行环境那部分） | ②③④ 三条合起来就是"合并这个动作在真实机器上是怎么发生的"：要么 apk 自己处置干净（覆盖 + 保留 launcher 直到用户 `del`），要么它把两部分都删坏——那就要在 INSTALL 第 2/8 节写明"这一批要先 `apk del zte-mesh-launcher` 再装"。**读之前不要写结论**，那是求解器的行为不是我们的。**2026-11-05 两台各读一次，答案是"先删 launcher、再装合并包"**：`owrt` 先走了另一头——`apk add` 合并包时逐条拒覆盖（八行 `ERROR: zte-mesh-0.1.0-r22: trying to overwrite usr/libexec/zte-mesh/ns-run owned by zte-mesh-launcher-0.1.0-r2.`，两个入口名字 × 三种架构），**`rc=1`，可同一趟把 `zte-mesh` 记成已安装、跑了 `post-upgrade`、也把新脚本装上了**，`apk info -L zte-mesh` 只列 4 个 `libexec` 成员（入口与六份二进制仍归 launcher）；这台当场还能跑，因为包里那份入口和 launcher 那份是同一段脚本。**这种状态再 `apk del zte-mesh-launcher`，入口就被它一并删掉**（`ls /usr/libexec/zte-mesh/` 只剩 `arch attach sta-collect wlan-sync`，`--launcher-check` 回 127，那是 ash 的 `not found`），而"再 `apk add` 一次这个包"这条恢复动作**不成立**——包已经"装着"，那条命令是空操作，`--force-overwrite` 也一样；`apk del zte-mesh` 单发还被拒（`not removed due to: zte-mesh: luci-app-zte-mesh / luci-i18n-zte-mesh-zh-cn`，那两个包依赖它）。`owrt` 最后是**三个包一起 `apk del` 再一起 `apk add`** 才回到干净态（rc=0、12 个 `libexec` 成员、`attached=true`、`RESULT=PASS` 18 查 0 败）。`ax3600` 走干净那头：**先 `apk del zte-mesh-launcher`（rc=0，`OK: 41.8 MiB in 224 packages`）再 `apk add` 合并包（`Upgrading zte-mesh (0.1.0-r21 -> 0.1.0-r22)`，rc=0）**，一次到位，`RESULT=PASS` 19 查 0 败，且 `air_matched_plan=yes` + `applier_writes=0`——这一趟升级没动本机当前广播的 SSID。⑥ `apk info -s zte-mesh` 两台都读 **10 MiB**（合并前 `zte-mesh` r21 是 8 034 KiB、launcher r2 是 2 413 KiB，相加约 10.2 MiB；那个旧的 8 007 KiB 读数果然只覆盖运行环境那部分）。**顺带一处判据要改**：`apk info -e` 在这两台的 `apk-tools 3.0.5` 上回 **0 行**，看这台装了什么只能 `apk list --installed` | ①–③⑤⑥ 只读，装包与重启服务会让子节点掉出拓扑十几秒（一次正常升级的量），按红线先征得同意；④ 的 `apk del` 是**卸载动作**，而且读完之后要知道它不是一句"再装回来"就能完事——合并包已经"装着"时 `apk add` 是空操作，要连 `luci-*` 一起删才装得动（见判据那格） |

| **H-18** | 批 26 的 P2-11：**热改服务角色之后，主循环跟不跟着换**。`/run/zte-mesh/config` 里的 `mesh.role` 被就地改掉时，`apply_new_config` 写原厂那三行、换掉原厂守护进程，可循环里的 `slave` 以前只在进程启动时算一次——切过去的机器继续拿控制器的功能对着已经不存在的 agent 跑，也不再轮询 M2 行去生成 Wi-Fi 计划；切回来反过来。**页面那条路碰不到它**：`mesh_role` 进重启签名，改了就 `restart`，跑的是冷启动；所以这条判据要直接改运行期那份配置文件（下面 ①），这条潜在的不一致也只有这样才照得出来。主机侧 `tests/test_service_role.sh` 只钉得住这段决定的形状（顺序、两个一次性动作跟着角色重开），跑不了这段循环——它只编进 ARM32 的 `db-engine-probe`。**①–⑤ 已结案（2026-11-07 装 r25，读数在判据那格）** | 一台 OpenWrt 路由器当主控 + 一台子节点（S1 拓扑；测试环境里就是 `owrt` 主控 + `ax3600` 子节点），装本批的包；**改的是 OpenWrt 路由器自己的服务角色，不动中兴原厂路由器的 `MultiAPCfg`** | ①记下 `logread` 位置，**直接改运行期那份配置文件**：`sed -i 's/^mesh.role=master$/mesh.role=slave/' /run/zte-mesh/config`，UCI 一个字不动。**不能走 `uci set … + reload`**：`mesh_role` 以 `role=master/slave` 进重启签名（`zte-mesh.init:803`），签名一变 `reload_service` 就 `restart`（`:1098`），跑的是冷启动路径——那条路本批与旧版一字不差，照不出这个修复。配置文件是 tmpfs 上的运行期文件，`apply_new_config` 每秒按 dev/inode/size/mtime 折一次（`controller-service.c:680`），改它就等于热改 ②等一轮，读日志里那句 `ZTE_MESH_SERVICE role_loop role=slave` ③`ls /run/zte-mesh/` 看有没有开始生成给本机用的 Wi-Fi 计划（`wlan-plan` 出现＝子节点那条 M2 轮询真的跑起来了）。**页面那一行不作判据**：`ubus call luci.zte-mesh status` 的 `role` 读的是 UCI（`luci.zte-mesh:81`），而这条判据故意一个字不动 UCI，所以它整段都还答 `master`；`state.json` 里没有 `role` 这个键（`write_state`，`controller-service.c:890`）——循环换没换边，只有它自己那句 `role_loop` 与计划文件说得清 ④还原走 `/etc/init.d/zte-mesh reload`：UCI 仍是 `master`，`write_config` 把配置文件写回原值，签名没变所以不重启，服务折一次就该读到 `role_loop role=master` ⑤切过去之后**在第 5 秒与第 8 秒附近**各读一次日志：`renew-acd` 与选举触发应当从这次切换重新计时，不是从服务启动那一刻算 | ②④ 两句都出现且 ③ 的计划文件跟着出现／消失 ⇒ 循环真的换了边；⑤ 两个一次性动作在切换后才发 ⇒ 计时跟着角色走。**没切之前的读数不算**：这段代码在冷启动路径上与旧版一字不差，只有热改能把它照出来。**2026-11-07 装 r25 在 `owrt` 上读到了，①–⑤ 结案**：`sed` 那一刻 `T0=1794044019`，同一秒 `daemon_respawn old=116367 new=116679 status=0` 与 `role_loop role=slave epoch=1794044019`；**+5 秒** `startup_renew=skipped role=slave epoch=1794044024`、**+8 秒** `agent_election attempt=1 status=0 epoch=1794044027`、**+20 秒** `attempt=2 epoch=1794044039`——两个门都从这次切换重新计时，不是从服务启动那一刻算；`/run/zte-mesh/` 多出 `wlan-plan`。`reload` 还原（UCI 仍是 `master`，`write_config` 把运行期那份写回原值）同一秒 `daemon_respawn old=116679 new=116812` 与 `role_loop role=master epoch=1794044047`，**+4 秒** `startup_renew=0 epoch=1794044051`（门限量的是 `CLOCK_MONOTONIC` 上的 5 秒、打印的 epoch 是 `time(NULL)`，两者差一秒时墙上时钟就读成 4），`wlan-plan` 消失、拓扑 `online=true`。**整个来回 `ns-run` 的 pid 一直是 115294**、三个实例没被 procd 重启过，换掉的只有沙箱里那个原厂守护进程——照出来的确实是热路径。这一趟没动射频，切换窗口共 28 秒（`17:33:39`→`17:34:07`） | 改角色会让这台机器退出／重进拓扑，子节点掉线十几秒到一分钟，按红线**先征得同意**，读完立刻切回原角色并确认拓扑恢复；不要在中兴原厂路由器上做这一条 |

| **H-19** | 批 26 的 P2-4／P2-6 在板子上的两条读数：**页面关掉服务，实例真的停**；**计划不再点名的回程频段，接口真的撤下来**。两条的主机判据都在（`test_init.sh` 8p、`test_wlan_sync.sh` 的 prune 那组），但一条走的是真 procd、一条走的是真 netifd，桩答不出这两层。P2-5 的 `owed` 重试只有主机判据——要在板子上造一次`wifi reload` 失败得动无线栈本身，不值得。**① 已结案（2026-11-07 装 r25，读数在判据那格）；② 这一轮没做**，仍只有主机判据 | 一台 OpenWrt 路由器当主控 + 一台 OpenWrt 子节点（S1 拓扑），装本批的包。**②要先搭出回程行**：测试环境现在两台都是 `backhaul=off`、`extra_slots=0`，子节点计划里两行都是 `slot=0 access=32`，一个 `zte_backhaul*` 段也没有——不先搭就无段可撤，"没报错"不等于验过 | ①页面「启用」关掉并保存（走的是 `reload` 不是 `start`），`ubus call service list '{"name":"zte-mesh"}'` 看实例还在不在、`pgrep -f db-engine-probe` 有没有；再打开并确认回来 ②子节点侧：先让计划里**有**一行回程——主控加 `zte_mesh_bss` 的 5g `slot=3`（`enabled/ssid/key` 三项给齐；`slot=3` 要先有 `slot=2`、`slot=2` 要先有 `slot=1`，`zte-mesh.init:746`/`:749`），子节点 `zte_mesh.main.backhaul=multi_ap` 而 `extra_slots` 保持 0，这样 slot 1/2 只记一行日志不上射频，只有 `access=64` 那行变成 `zte_backhaul_sta_5g`；等段与接口出现后，**删掉主控那个 slot=3 段**让计划少掉这一行，读子节点 `uci show wireless` 里那个段在不在、`iw dev` 里那个接口在不在、`/run/zte-mesh/wlan-sync.status` 的 `result` 与日志里那句 `the plan no longer carries the backhaul of …`。**改主控的 `backhaul` 不是这个杠杆**：那个选项只决定主控自己那一端，计划里的行来自主控的原厂 `WLANCfg` profile（slot 3 才带 `MapAccessAttr=64`，`wlan-profiles.c:305`） | ① 关掉之后实例集合为空、打开之后回到角色应有的那一套 ⇒ `reload` 认了 `enabled`；② 段与接口一起消失且状态仍是 `ok` ⇒ 撤下来的是计划不要的那一行，不是把应用失败当成删除。**① 的读数（2026-11-07，`owrt` 主控）**：关之前 `enabled=1`、三个实例 `running:true`、一个 probe 进程；`uci set zte_mesh.main.enabled=0` + `uci commit` + `reload` 回 `rc=0`，`ubus call service list` 答**空对象**、probe 与 `multiapd` 都没了、`zmesh0` 答 `Device "zmesh0" does not exist.`、`/run/zte-mesh/` 只剩 `actions`/`peers`/`version_mode.host`。开回来三个实例全 `running:true`，`status` 答 `attached=true`、`role=master`、`local=false`，签名尾部读到 `bridge=br-lan al_last= emulator=`（P2-4 补的那三个输入在真 procd 侧有读数了）。走的是 UCI + `reload`，与页面保存进同一个 `reload_service`；表单那一层仍由 rpcd 与 `test_luci.js` 守。**② 没做**：不先搭出回程行就无段可撤，而搭建那一步会掉线（见风险那格），这一轮只做了 ①。**两段都要还原**：读完把 `enabled` 与回程配置改回原值，确认子节点重新关联 | ① 关服务会让这台机器退出拓扑，子节点掉线；② **搭建那一步也动无线栈**：子节点 `backhaul=multi_ap` 会给主 BSS 加 `multi_ap 1`（`wlan-sync:573`）并触发 `wifi reload`，那一下走这块射频的终端掉一次；撤掉回程接口再掉一次。主控是虚拟机、没有射频，加 slot 1/2/3 只进 profile 与计划，不上它自己的射频。两条都按红线**先征得同意**，测完立即还原：删掉主控那三个段、子节点 `backhaul` 改回 `off`，并确认主 BSS 上那个 `multi_ap` 被清掉（`clear_backhaul` 只撤它自己写的 1）；不动中兴原厂路由器 |

| **H-20** | 批 27 的 A27 只读设备页：**页面在真实路由器上长什么样**，以及那份 JSON 经真 rpcd 转发有没有丢字节 | 一台装了本批包的 OpenWrt 路由器，**有射频、挂着至少一台无线终端和一台有线设备**即可；不需要子节点，也不需要主控（测试环境里就是 `ax3600`，它现在挂着手机与 PC） | ①`ubus call luci.zte-mesh devices` 直接读一次：应有那台 PC（`sources` 含 `lease neigh`）与手机（`lease neigh wlan`，`ssid` 是本机当前广播的那个、`iface` 是它关联的那路 AP）、`epoch` 当轮 ②`sh /usr/libexec/zte-mesh/device-list` 的输出与 ① **语义相同**（`ubus call` 会把结果格式化，去掉空白后 `cmp` 一等；插件不重写字段） ③浏览器打开「网络 → ZTE Mesh」，滚到「设备」：设备名 / MAC / IP / 位置 / 来源 / 租期 / 备注 七列，IP 可点、位置是 SSID 且 tooltip 是接口名，5 秒后表格自己刷新 ④让那台手机断开关联但不要断开电源：租约还在这段时间里，那一行应该还在、只是「来源」少了"无线关联"、位置变"有线"→（`ssid` 为空后页面落到 `iface`，再落到「有线」，这一格要看实际渲染对不对），**不是整行消失** ⑤`uci show dhcp` 与租约页一个字没被我们改 | ①② 相等 ⇒ 转发链在真 rpcd 上没有丢字节；③ 七列与刷新 ⇒ 渲染成立（**这是唯一只有浏览器能读的判据**，主机侧 `test_luci.js` 只钉得住行的内容）；④ 留下的行与「来源」的差异 ⇒"租约比设备活得久"在页面上说得清，不是靠猜；⑤ ⇒ 我们没有去接管 DHCP 页。**2026-11-09 装 CI 的 r26（run 42559747625）后复读 ①②**：`ax3600` 4 台设备（有线 PC `lease neigh`、手机 `lease neigh wlan`、`ssid=FRESHR10-5G`、`iface=phy1-ap0`、`.20`/`.191` 仅 `neigh`）；`ubus` 那份是 rpcd 格式化后的 JSON（803 字节），直跑脚本是紧凑 JSON（610 字节），去掉空白后两边 md5 都是 `ffadad22f6095880a79256504c9a08a3`——**"逐字节相同"这条判据要改成语义相同**，转发链没有丢字段。③ 仍是白天浏览器那次读数；④⑤ 照旧没做 | 只读，不动射频、不掉线。装包会重启服务（子节点退出拓扑十几秒），按红线先征得同意；④ 要让一台手机断关联，用手机自己关 Wi-Fi 或走开着的那台 AP 即可，别在路由器上关主 SSID |

| **H-21** | A28 的第一步取证：**子节点能不能把终端 MAC 列表报上来**（1905 的 Client Association 一类消息），以及原厂主控收不收这个 | 一台有射频、挂着一个终端的 OpenWrt 子节点 + 一台主控（先 OpenWrt，再中兴原厂 Z506） | ①子节点 `tcpdump -i zmesh0 -s0 -w /tmp/assoc.pcap` 抓 15 分钟，找有没有区别于 `0x8001`/`0x8002`/`0x800b`/`0x800c` 的 CMDU 出门 ②把抓到的几条按 Multi-AP 规范正文逐字段解出来（消息类、TLV、MAC、BSSID），记下是哪一版规范 ③原厂主控当对照：看它自己的页面/数据库里这台子节点的终端明细有没有出现 ④拿 ①②③ 的结论定落盘格式（`/run/zte-mesh/peers/<AL MAC>.assoc`）与页面上"挂在哪个节点"那一列的写法，编解码先做成主机可测 + 夹具 | ①有帧且 ③原厂主控认 ⇒ 这条路存在，照它的字段写；**①②③ 任一为空** ⇒ 写"这条数据在这套固件上不存在"，A28 改成自造：子节点把 `ip neigh` 与 `stations` 的列表塞进我们自己的应答。② 解不出来就不动 A28 的代码。**本轮没有抓包窗口**，所以先按自造那条路做了 A28（判据已绿，上机归 H-22）；这一步仍要做，结论决定要不要把 `0x70` 换回标准字段 | 只读抓包，不踢终端、不改 SSID、不动 Z506 的配置；③ 那一步在 Z506 上是读它自己的页面/数据库 |

| **H-22** | A28 自造路线的上机读数：**主控页面上那台挂在子节点无线上的手机，位置是「无线」、节点列写着子节点** | `ax3600` 当子节点（有射频、挂一台手机）+ `owrt` 当主控（无射频），两台装本批包 | ①`ax3600` 的 `/run/zte-mesh/ap-sta` 有 `b <BSSID> <数> <SSID>` 与 `s <BSSID> <手机 MAC>`，5 秒一轮刷新 ②`owrt` 上 `ls /run/zte-mesh/peers/*.assoc`：文件名是子节点 AL MAC，首行 `epoch=` 与 `date +%s` 同源且 30 秒内，每行 `<手机 MAC>` 制表符 `<BSSID>` 制表符 `<SSID>` ③`ubus call luci.zte-mesh devices`：手机那行 `via` 是子节点 AL MAC、`sources` 含 `assoc`，本机三张表里没有它 ④浏览器打开 `owrt` 的「设备」：手机那行位置是本机当前广播的 SSID（读到空时是 `Wireless`）、「节点」列是子节点（别名 > 名称 > 型号 > MAC），5 秒刷新后不跳；同一页上 A27 那三台本机设备仍是「有线」、节点列 `-` ⑤手机在子节点两路 AP 之间漫游一次，行不消失、节点列不变 ⑥换中兴原厂 Z506 当子节点那一侧：它的设备**不出现**（`0x70` 不被认），是诚实降级不是错标 | ①③④ 同时成立 ⇒ 跨节点那一路在真 procd + 真 rpcd 上通了；② 的 `epoch` 与主控时钟同源 ⇒ 新鲜度判的是主控自己的钟（不是子节点的）；⑤ ⇒ 短时重复上报不会把行抖成两台；⑥ ⇒ 边界与文档一致。读数抄进 [FEATURE_STATUS](FEATURE_STATUS.md) A28 | 都是读文件与页面，不动射频、不掉线；① 只需要那台手机挂在子节点的 AP 上（现在就是）。装包会重启服务、子节点退出拓扑十几秒，按红线先征得同意；⑤ 用手机自己关/开 Wi-Fi 时掉线的是那台手机，不是改路由器配置。**2026-11-09 已验 ①②③④（r27，CI run 42587771304）**：① `ax3600` 的 `ap_sta_rows=2 ap_sta_clients=1 ap_sta_age=3`，表是 `b 8c:de:f9:b3:d9:e4 1 FRESHR10-5G` / `s 8c:de:f9:b3:d9:e4 0a:95:b7:16:e2:39` / `b 8c:de:f9:b3:d9:e3 0 FRESHR10` ② `owrt` 上有 `peers/02:00:00:00:19:e3.assoc`（`peers_assoc=1`），`epoch=1794215440` 对着同一刻 `date +%s=1794215442`，一行 `0a:95:b7:16:e2:39 → 8c:de:f9:b3:d9:e4 → FRESHR10-5G` ③ `devices` 该行 `via=02:00:00:00:19:e3`、`sources="neigh assoc"`、`ssid=FRESHR10-5G` ④ 浏览器里八列表格，该行位置 `FRESHR10-5G`、节点 `zte-shim-public-lab`、控制台无报错，同页 A27 那两台本机设备仍是「有线」、节点 `-`；两台 `verify-install.sh` 均 PASS。⑤ 真实漫游与 ⑥ 原厂 Z506 那格未做 |

## 6. CI 与装机节奏

### 6.1 触发原则

- **不按项触发**。改完一项只在本地跑测试，不烧 CI；**攒够一批（3–5 项）批末触发一次**。
- **批末触发一次**：`gh workflow run build-apk.yml`（手动 `workflow_dispatch`），产物是 `zte-mesh-apk`
  artifact（三个 apk + `SHA256SUMS.txt`）。
- **一轮要多久：19–25 分钟是常态，57.5 分钟出现过一次**。多出来的时间不在构建（`Build packages` 1.6 分钟），
  在 "Install host dependencies"——apt 从 `azure.archive.ubuntu.com` 拖宿主交叉编译器，慢的时候四个空档能到几百秒。
  三个成本的分项、改动与判据见 [DEVELOPMENT](DEVELOPMENT.md) 第 8 节。
  **轮询窗口按 90 分钟留**，别按 25 分钟留一轮就断定构建失败了。
- **缓存命中不等于没重编**：`tools/install` 在构建缓存命中的那一轮照样走满 772 个目录访问、约 1000 秒。
  判它有没有少做事看**目录访问计数**，不看秒数——同一批目录走两遍，秒数差的是镜像与磁盘的快慢。
- **run summary 里的东西 REST 读不到**：`Summarise the batch` 那一步是 `cat` 进 `$GITHUB_STEP_SUMMARY`，
  API 不返回 step summary，那条 HTML 路径（`…/checks/<run>/summary`）回 404。job log 里有脚本自身的回显与判据失败时那句
  `>&2`，**唯独绿的时候的读数没有**——所以要在线上读的数必须同时 `echo` 到 stdout（读包那一步的六行重量就是这么加的）。
- **走临时分支，不推 `main`**：`main` 直连 ImmortalWrt feed，推上去构建机立刻吃到。批末把这一批提交推成
  `ci/batchN`，在该 ref 上触发，**装机读完就删**。`workflow_dispatch` 跑的是**选中的那个 ref**，
  点错在旧提交上会"构建成功但产物不含本批"，只能靠装机读回的 `PKG_RELEASE` 发现——**发起前核对 ref 的 SHA 与本地一致。**
  清场那句只对 origin 成立，**本地那份分支不会跟着没**：`git ls-remote --heads` 读不到 `ci/*` 不等于清完，
  `git branch --list 'ci/*'` 也要空。删之前确认它的 tip 是 `main` 的祖先、`main..ci/batchN` 为空，一个提交都没丢。
- **CI 不创建 GitHub Release**。发布是人的决定：确认某一版要对外，再手工
  `gh release create <tag> dist/*.apk dist/SHA256SUMS.txt`。流水线按批跑，跑出来的字节是"这一批的中间态"，
  不该自动变成对外承诺。本机没有 `gh` 时的三次 API 调用、`target_commitish` 那个提交必须在远端存在这条前置
  （走临时分支时要先把它推出去）、以及"发布成没成只看匿名重下 `sha256sum -c`"都写在
  [DEVELOPMENT](DEVELOPMENT.md) 第 8 节。
- **同一棵树重编出来的 apk，sha 不一定相同**：`zte-mesh` 那份的时间戳钳在包目录的最后一次提交上，能逐字节相同；
  两个 LuCI 包跟着构建时钟走，容器哈希每轮都换。所以"sha 不同"不等于"内容变了"，判 LuCI 那两个包是不是同一批要解成员，
  办法见 [DEVELOPMENT](DEVELOPMENT.md) 第 8 节。

### 6.2 装机（批末一次）

| 机器 | 装什么 | 验收 |
|---|---|---|
| `192.168.1.20`（VM，x86_64，无无线） | `zte-mesh`（`arch: all`，包里就有 x86_64 那种架构启动器，不再需要本地编的 `ns-run`）+ 两个 LuCI 包 | `ubus call luci.zte-mesh status` 返回角色；`/run/zte-mesh/wlan-sync.status` 有结果 |
| `192.168.1.1`（ax3600，aarch64，有无线） | 三个包（`--allow-untrusted`） | 同上；`slave` 角色下 `iw dev` 跟着主控改 SSID |

装完先跑 `sh tools/lab/verify-install.sh /tmp/zte-apk`（装包 + 读回版本 + 冒烟 + 装机读回，输出 `KEY=VALUE`
证据行与 `RESULT`），再跑本批新增的用例（页面开关、`uci` 选项）。判定逻辑由主机上的 `test_verify_install.sh`
守着，不接机器也能跑。

- **装机复验只能按标记字符串认，不能比哈希**——apk 打包会剥掉 ELF 节，设备上的 `.so`
  与仓库 tarball 成员大小不同；根目录没解包时读回打印 `unreadable`、不作判定失败。
- **同版本 `apk add` 是空操作**：这一批的字节与设备上那份相同时不会重装，那一趟能读的只有传输完整性
  （`sha256sum -c`）与一次 `verify-install.sh` 没退化。CI 在 job log 报的六个重量字节数与设备上
  `/usr/libexec/zte-mesh/arch/*/*` 一字不差，所以读重量不必上机复核。读完删掉 `/tmp` 里那份 apk 与校验和
  （两台各自的旧批次残留不在本批动，见 [LAB](LAB.md)）。
- **升级顺序**：这台还装着合并前的 `zte-mesh-launcher` 时，**先 `apk del zte-mesh-launcher` 再 `apk add` 合并后的
  `zte-mesh`**。反着走会读出八行 `trying to overwrite … owned by …`、`rc=1`，可包照样记成 `[installed]`；那种状态下
  `apk add` 是空操作、`apk del zte-mesh-launcher` 会把入口一并删掉，只有三个包一起删再一起装才回干净态。
  全文见 H-17 与 [INSTALL](INSTALL.md) 第 2 节。

### 6.3 版本号

`make-runtime.py` / 包 `PKG_RELEASE` 在批末递增一位，让"这台机器上装的是哪一批"能从 `apk list` 读出来。
**三个包的 `r` 号对齐，批末一起加一**（不是"只给这一批改过的包加一"）——不管那一批动没动某个包的内容，
`zte-mesh`、`luci-app-zte-mesh`、`luci-i18n-zte-mesh-zh-cn` 都升到同一个数，于是一批只有一个版本号，
`apk list | grep zte` 三行末尾那个数一样就是"这一套是同一批"。不做自动版本号。对齐那一次只改 Makefile 一行：
文档以外没有第二处写死旧的 `r` 号（`git grep` 过 `*.sh` / `*.py` / `*.yml` / `*.js` / `Makefile`）。

**release 的 tag 写完整版本串**（`v<PKG_VERSION>-r<对齐后的号>`，如 `v0.1.0-r23`），和三个 apk 的文件名一字不差；
`v0.x.y` 那套和包无关的序号会让 tag 与 apk 里的数对不上，正文第一段还得专门解释"两个身份"。旧的 `v0.1.0`、`v0.2.0`
两个 tag 已从 origin 删除（对应的树是 `feaac56`、`d9ce19d`，要复现那两批字节按 SHA 取）。发布步骤见第 6.1 节与
[DEVELOPMENT](DEVELOPMENT.md) 第 8 节。

**改过包里的内容就要升版本，哪怕那一批不跑 CI**——同一个 `PKG_RELEASE` 在 CI 上只能有一种内容。反例出在一次没跑 CI、
因此没升版本的文档轮：同名 `r10` 的 `luci-app-zte-mesh` apk 前后是两份字节，差在 `po/zh_Hans/zte-mesh.po` 的 4 行中文翻译。

## 7. 本项目的判据纪律

- 不许用"编译通过"当验收；每项都要留一条**能重复跑**的判据。
- 桩的**失败语义和成功语义一样重要**：`no-cfg-line`、`wrapped`、`no-logread`、"证据还太年轻"这一类
  "问得太早"的情况必须显式不作判定，否则一台健康的机器会被判 FAIL。
- "没有读到 X"在这种环境里往往什么也不说明（表可以行空、文件可以按需生成、peer 可以从不发那个 TLV）——
  判据要设在能产生输入的那一侧。
- 结论已经有了、缺的只是可重复判据时，**去补判据，别把结论删掉**。
- 过程叙事（哪一天跑了什么、装到第几版顺带读到什么）只写在 [docs/journal/](journal/)，不进这份表。
