# 中兴 mesh 功能清单、完成度与推进顺序

> **状态（2026-10-31）：** 主控侧在真子路由（ZXHN Z506，原厂固件）上验证；子路由侧在实验室（合成主控）验证。
> 本表是唯一的功能总账：每完成一项就在“状态”列打勾并把证据补上，**不要**在别的文档里另起一份清单。
> 协议与逆向细节见 [CONTROLLER_CAPABILITIES.md](CONTROLLER_CAPABILITIES.md)、[REAL_CHILD.md](REAL_CHILD.md)、
> [research/AGENT_PROBE.md](research/AGENT_PROBE.md)；部署与测试见 [DEVELOPMENT.md](DEVELOPMENT.md)。

## 实验机

证据列里写的“实验机”指下面两台，它们与本轮 CI 产物（`main` 上 `dca90cd` 之后的 run 41151243794）一一对应。
两台都在 `192.168.1.0/24`，同一 L2；root 密码 `root`，登录用 `ssh root@192.168.1.X`（公钥已装）。

| 代号 | 机器 | 架构 / 系统 | 角色（本轮） | 说明 |
|---|---|---|---|---|
| **VM** | `192.168.1.20`，VMware 里的 ImmortalWrt | x86_64，25.12.2 r38135，内核 6.12.103 | `master`（测 `main` 时临时切过去，测完切回） | **没有一张无线网卡**，所以 `main` 角色下 `wlan-sync` 只会记“无射频可管” |
| **ax3600** | `192.168.1.1`，小米 AX3600 刷 ImmortalWrt（**非中兴硬件**） | aarch64 cortex-a53，25.12.2 r38135，内核 6.12.103 | `slave` | 三张 radio：`radio1`=5G、`radio2`=2.4G、`radio0` 空卡（`phy0` 无设备） |

两台的 `/tmp` 里放的 apk 与 CI 产物 sha256 逐字节一致（`zte-mesh` r5、`luci-app-zte-mesh` r6、
`luci-i18n-zte-mesh-zh-cn` r6），所以“装上新包”等于“装上 `main` 上那次构建”。

装法（2026-10-31）：ax3600 上 `apk del` 三个包再 `apk add --allow-untrusted /tmp/*.apk`，
`/etc/config/zte_mesh` 被保留（新配置落成 `zte_mesh.apk-new`，没有覆盖）。VM 装不了：包是
`aarch64_cortex-a53`，VM 是 x86_64，所以改成把 ax3600 上新装出来的那批文件（`init.d`、
`wlan-sync`、`attach`、`rpcd/luci.zte-mesh`、`root/{bin,lib}`、LuCI 视图与菜单、`lmo`）
`tar` 过去解到 `/`，**故意不含 `ns-run`**，保住 VM 上 x86_64 的那一份。

三个当时踩到的点：

- dropbear **没有 sftp**，`scp` 报 `Connection closed`；两台之间传文件用
  `ssh A 'cat f' | ssh B 'cat > f'`，本机也没有 `timeout` / `nohup`
- 包里 `usr/libexec/zte-mesh/ns-run` 曾是 aarch64 旧版，见 C5（已修）
- 两台 root 密码都是 `root`；公钥已写进 `/etc/dropbear/authorized_keys`，以后 `ssh root@192.168.1.X` 直接进
- 两台**都没有 `tcpdump`**，`apk add tcpdump-mini` 可以装（有源）

## 图例

| 记号 | 含义 |
|---|---|
| ✅ | 已实现，**真机**验证过 |
| ☑️ | 已实现，只在实验室 / 测试桩上验证（未上真机，或对端固件不支持） |
| ⬜ | 未做 |
| ➖ | 做不到：对端没有接收代码或没有下发通道（不是我们没写） |

难度：**1** 改几行、**2** 一天内可验证、**3** 需要新逻辑或新测试面、**4** 需要真机驱动侧配合、**5** 需要逆向或换硬件。

## A. 主控（Controller，本机是主路由）

| # | 功能 | 状态 | 证据 | 难度 |
|---|---|---|---|---|
| A1 | 原厂 `multiapd` 在私有命名空间里跑（IPC/net/mount、veth 到 `br-lan`） | ✅ | `ns-run` + `attach`，装包后真路由器、真 procd | — |
| A2 | 发现 / 拓扑 / 链路指标 / 高层数据（CMDU 0、1、2、3、5、6、7、13、14） | ✅ | REAL_CHILD §1 | — |
| A3 | AP-Autoconfig + WSC M1/M2（真 DH、HMAC、Key Wrap） | ✅ | 子路由 `config M2[0]` | — |
| A4 | 配置变更后 Renew（热更新，无需重启） | ✅ | 改配置约 4 秒生效 | — |
| A5 | 主 SSID / 密码 / 加密 / 隐藏 | ✅ | 2.4G、5G 均读回 | — |
| A6 | 多 BSS（访客、备用、隐藏回程，槽位 1–3）名称 / 密码 / 开关 | ✅ | REAL_CHILD §1 | — |
| A7 | BeaconInterval（含热更新） | ✅ | 120 → 读回 120 | — |
| A8 | ACL 名单 + 每 BSS 策略（关 / 拦 / 只放行） | ✅ | 一加被踢并拒绝重连；`allow` 与 5G 驱动读回未验证 | — |
| A9 | 信道与发射功率（`0x8006`） | ✅ | 两个频段验证并还原 | — |
| A10 | 重启子节点 / 指示灯开关 | ✅ | REPLAY_RESULTS 16.1 | — |
| A11 | 访客限时、Wi-Fi 定时关闭 | ✅ | `scope=guest` 真机；`scope=all` 仅实验室 | — |
| A12 | 自组网 V2（`mesh.v2`）与市场码门槛 | ✅ | 两种取值都正常入网 | — |
| A13 | 控制器角色 `master` / `auto`（含自适应选举替身） | ✅ | 第 40 轮 | — |
| A14 | MLO 开关下发（`0200fe`） | ☑️ | 线上帧逐字节一致；Z506 无该字段，等于无操作 | — |
| A15 | Wi-Fi 5 兼容标志（`ForceWiFi5`） | ☑️ | 线上值正确；Z506 不认 | — |
| A16 | 漫游 / 频段引导阈值下发 | ☑️ | 能发；Z506 固件没有解析代码 | — |
| A17 | 引导（BTM）/ 封锁 / 踢终端 | ⬜ | `0x8014` 等，接口已在 libcmapi | 3 |
| A18 | 拓扑页：回程 RSSI、树状图、每 AP 终端列表、固件版本 | ⬜ | 现在只有节点清单与动作 | 3 |
| A19 | 服务配置校验：`zte-mesh-timer` / ACL / 槽位连续性的错误定位 | ☑️ | init 69 项 | — |
| A20 | **界面中英双语**：全部文案走 `_()`，`po/zh_Hans` 与源码逐条对齐，菜单标题也在目录里 | ✅ | i18n 12 项；**实验机**：两台都随包装出 `zte-mesh.zh-cn.lmo`（11632 B），`/cgi-bin/luci/admin/translations/zh-cn` 返回中文 `window.TR` | — |

## B. 子路由（Agent，本机是子节点）

| # | 功能 | 状态 | 证据 | 难度 |
|---|---|---|---|---|
| B1 | agent 模式启动（`Mode=1` / `SetRoleEnable=0` / `EnMasterOnly=0`） | ☑️ | 实验室：`role=slave mode=1 set_role_enable=0 en_master_only=0` | — |
| B2 | 发 M1、收 M2、凭据落 `WLANCfg` / `WLANPSK` | ☑️ | 两个频段各一份，`m2_match=1` | — |
| B3 | setter 兜底（配置管理器返回 -3 时读回校验再报 0） | ☑️ | `libzte_agent_mm.so`，沙箱专用 | — |
| B4 | 库 → 计划的发布（`agent-sync` → `/run/zte-mesh/wlan-plan`） | ☑️ | `published=1 rows=2` | — |
| B5 | 计划 → `/etc/config/wireless`（`wlan-sync`，幂等、只写真变化；plan 启用某 band 时把关着的 radio 设备打开，只写 0） | ✅ | 86 项测试；**ax3600**：人为 `uci set wireless.radio2.disabled=1` 后重启服务，日志 `radio radio2 (2g) was switched off; switched on ...`、`applied; wireless committed and reloaded`，`disabled` 回到 0；空卡 `radio0` 全程 `disabled=1` 没被动 | — |
| B6 | 回程 BSS：`MapAccessAttr&64` → `multi_ap=2` 独立段，主 SSID `multi_ap=1` | ☑️ | 86 项测试含开关双向复原 | — |
| B7 | UCI / init / LuCI 暴露 `mesh_role` + `backhaul` | ✅ | init 74、rpcd 18；**实验机**：两台 `uci` 写的角色都被 init 接受，`ubus call luci.zte-mesh status` 读到 `master` / `slave` / `main` 三个值 | — |
| B8 | **子路由免本地 profile**：没有配置时沿用本机正在广播的网络 | ☑️ | 本轮；init `role-slave-borrow` | — |
| B9 | **LuCI 按角色只显示有效设置**（子路由隐藏 Wi-Fi/漫游/额外网络/子路由清单） | ☑️ | 本轮；`test_luci.js`（主控 31 项 / 子路由 4 项） | — |
| B10 | rpcd `status` 返回角色 | ✅ | 本轮；rpcd 18 项；**实验机**：VM `"role":"master"`→`"main"`→`"master"`、ax3600 `"role":"slave"` | — |
| B11 | 子路由侧应用额外槽位（slot 1–3 现在只记日志） | ⬜ | `wlan-sync` 明确跳过 | 3 |
| B12 | **应用结果回传**：`wlan-sync.status` + `status.sync` + 状态页行 | ☑️ | 本轮；wlan-sync 86 项、rpcd 18 项 | — |
| B17 | **三角色**：`master` 只控制 / `main` = 主控 + 本机落地 / `slave` 只被控；`main` 的 plan 由 init 写 | ✅ | init 74（`role-main` / `role-main-sync` / `role-master` / `role-auto-legacy`）；LuCI 角色表 3 项断言；**VM**：`main` 时 init 写出 `wlan-plan`（316 B、0600、`rows=2`、`ZTEMESHCTRL`/`Ztemesh2631!`/`access=32`/`beacon_type=12`）、`wlan-sync` 实例在跑、日志 `no usable 2g radio; left alone` / `no usable 5g radio; left alone`；切回 `master` 后 plan 与 `wlan-sync` 实例一起消失（一个 plan 只有一个生产者） | — |
| B13 | 无线回程的数据面：本机作为 backhaul STA 去关联主控的回程 BSS | ⬜ | 只做了 AP 侧 | 4 |
| B14 | 真机：本机当子路由，接真主控（有线回程） | ✅ | **2026-10-31 11:01 两台实验机打通**。抓包先否掉了"子路由在发 Search"：ax3600 的 `br-lan` 上 30 秒 **0 帧 `0x893a`**，两端日志都是 `SSEND socket Message[0X%04X] to [%s] failed`。真正原因是 **C6**：那份冻住的 `db-engine-probe` 让子路由的 controller 实例每 5 秒崩一次，它根本没机会开口。**抓包否掉了「子路由被 cspd 门控所以不发 Search」这个判断** —— 修好 C6 之后在 ax3600 上抓到子路由（`02:00:00:00:19:03`）朝 1905 组播 `01:80:c2:00:00:13` 发 `0x0007` Search，主控回 `0x0008` 并把 WSC 档案带回去。cspd 门控只在**控制器**那一侧成立：沙箱里的 vendor daemon 确实从不为自己发 Search，那一拍仍要靠 B18。VM 上 `enroller br-lan 100 --al-mac 02:00:00:00:19:03 --search` 补一拍后立刻通：`0x0002/0x0003` 拓扑发现、`0x0004/0x0005` 查询/响应、`0x0006` 链路度量、子路由自己发出 `0x0007`（band 0/1）并收到 `0x0008`、`M1/M2` 往返，`heard=352 replies=2`；VM 的 `state.json` 出现子节点（`mac=02:00:00:00:19:e3` `model=ZXSLC SR1010` `zte=true` `online=true`）。补的这一拍已作为 B18 进包。**r7 装到两台后复测（12:31）**：把主控 SSID 改成 `MASTERTEST`，子路由的 `wlan-plan` 跟着变成 `0.ssid=MASTERTEST` / `1.ssid=MASTERTEST-5G`，wlan-sync 应用到 radio2(2g)/radio1(5g) 并 `wireless committed and reloaded`，`iw dev` 确认空中就是 `MASTERTEST` / `MASTERTEST-5G`，密码同为 `Ztemesh2631!`。**用户报的「子路由 WiFi 名没按主控改」就是 C6**：冻住的那份 `db-engine-probe` 里压根没有 `wlan-plan` 这段发布逻辑，子路由收到 M2 也没人把档案落成 plan | — |
| B18 | **cspd 替身**：控制器侧自己发 `0x0007` Search、并代答子路由的 Search（`0x0008`），补上沙箱里不存在的 cspd | ✅ | `tools/lab/enroller.c` 编成 `/usr/libexec/zte-mesh/mesh-search`，init 在控制器角色（非 `slave`）下多起一个 `search` 实例，带 `--al-mac 02:00:00:00:19:<al_last>`（子路由的 M1 发给控制器 AL MAC，不是出接口 MAC），生命周期就是「多久重发一次 Search」，所以必须短：`--search` 每次启动只发**一帧**，控制器比子路由先起来的话，晚开机的子路由要等到下一次才被收进来。`86400` 实测就是这个下场（子路由重启后只能靠手工重启主控才入网），已改成 `300`，最坏等 5 分钟。**实测不抖**：每 5 分钟重发一次，wlan-sync 一直报 `plan applied nothing new`，`daemon_restarts=0`，没有反复重载 Wi-Fi。**实验机**：机制就是上面 B14 那次实测（同一份代码，手工跑的 `enroller --search`）；**装 r6 后真机自动入网已验证**：ax3600 切 master（控制器）、VM 切 slave（子路由），全程不跑手工 enroller，ax3600 的 `state.json` 自动出现 `mac=02:00:00:00:19:03 online=true`，日志 `topology dev_num=2`；`ubus call service list` 里 search 实例命令行确认为 `mesh-search br-lan 86400 --al-mac 02:00:00:00:19:e3 --search --quiet` | — |
| B15 | `CmWLANMapSsidCfgSync2Repeater` 对齐（libcmapi 内部调用，替身命中不到） | ⬜ | 沙箱里是 no-op | 5 |
| B16 | `CurHop` 在子路由侧的真实更新 | ⬜ | 实验室用 `-2` 顶替 | 2 |

## C. 打包与分发

| # | 功能 | 状态 | 证据 | 难度 |
|---|---|---|---|---|
| C1 | 单个 apk（runtime + init + wlan-sync + rpcd + LuCI），4.1 MB | ✅ | 装在真路由器上，procd/rpcd 全通；**实验机**：ax3600 走完整 `apk del` → `apk add --allow-untrusted /tmp/*.apk`（三个包全在、旧配置 `/etc/config/zte_mesh` 保留、新文件替换），VM 因为包是 `aarch64_cortex-a53` 架构装不进去，改用同一批产物解包覆盖（见 C5） | — |
| C2 | 拆包：`zte-mesh-runtime` + `zte-mesh-controller` + `zte-mesh-agent` | ⬜ | 方案已定（见 AGENT_PROBE §11.7），未动手 | 2 |
| C3 | 统一行尾（`.gitattributes`，`* text=auto eol=lf`） | ⬜ | 现在工作区 CRLF，测试要转 LF 才能跑 | 1 |
| C4 | **中文翻译包随 CI 构建**：`luci-i18n-zte-mesh-zh-cn` 之前只配了 `=m`、没编译，中文目录从不产出 | ✅ | 本轮；CI 断言三个 apk 都在；**实验机**：两台装完后 `/usr/lib/lua/luci/i18n/zte-mesh.zh-cn.lmo` 就是包带来的（11632 B），端点读得出中文 | — |
| C5 | **包里的 `ns-run` 与 init 脚本不同步**：`zte-mesh-runtime.tar.gz` 里的 aarch64 `ns-run` 还是旧版，不认 `ZTE_MESH_QEMU` / `ZTE_MESH_EMULATED_LAB`，在内核没有 `CONFIG_COMPAT` 的 aarch64 上 `execv` 直接 `errno=8`（ENOEXEC），controller 实例反复退 80 | ✅ | **ax3600 实测**：包里的 ns-run（strip 后 598083 B）`strings` 里没有 `emulated_lab`；换成现编的版本后三个实例全起、`errno=8` 消失。**修法是消除这类漂移本身**，不是换一个二进制：tarball 现在额外带 `ns-run.c` / `controller-net.c` / `enroller.c`（`repack-runtime.py --add`，41 个成员，`SHA256SUMS` 重算），`Build/Compile` 用 `$(TARGET_CC)` 现编，包再也发不出过期的 ns-run。`tests/test_runtime.py`（20 项，已进 test-host）比对 tarball 里的副本与仓库文件，重打包漏了它们在本机就失败 | — |
| C6 | **tarball 里的 `db-engine-probe` 冻结在打包那天**：ARM32 那一半（`parse_config`、服务循环）只能挂在 lab-tools 分支上、对着原厂 glibc 2.26 用 `-nostdlib` 编，包里编不了。三角色上线后 init 已经喂 `mesh.role=slave`，可那份副本还不认这个值 → `controller_service()` 直接 `-1`，子路由角色的 controller 实例每 5 秒退一次（`fail=config-missing-or-invalid`，连 `config-reject` 详情都没有），两台都是 2/3 | ✅ | **ax3600 实测**：`strings` 里 `config-reject`、`wlan-plan` 都是 0；换成 lab-tools 重编的副本（193364 B，sha256 `7ff631a7…`）后 slave 角色 `agent_election attempt=1`、`cfg cur_hop=2 max_hop=2`、3/3 实例全起。`tests/test_runtime.py` 现在断言包里那份必须带 `config-reject` 与 `wlan-plan`，冻住的副本在本机就会被拦下 | — |

## D. 没有通道的（换子路由固件才谈得上）

| # | 功能 | 原因 |
|---|---|---|
| D1 | 带宽 20/40/80/160 | Z506 不写 `BandWidth`，也不关“自动信道” |
| D2 | 访客限速 | 1905 里没有这个字段 |
| D3 | 信道扫描 / CAC | Z506 无接收代码 |
| D4 | VBSS（虚拟 BSS 创建/销毁/迁移） | Z506 无 |
| D5 | VLAN / 流量分离 / Profile-2 | Z506 无 |
| D6 | STA 绑定 | Z506 无 |
| D7 | 802.11r/BTM 踢人（部分） | Z506 固件不支持 FT |

## 推进顺序（只做下一项，做完打勾再往下）

按难度从 1 到 5，同难度按“能立刻验证”优先：

| 顺序 | 项 | 难度 | 为什么排这里 |
|---|---|---|---|
| 1 | ~~B8 / B9 / B10~~ | — | **2026-10-29 已完成** |
| 2 | ~~B12 应用结果回传~~ | — | **2026-10-29 已完成** |
| 3 | ~~A20 / B5 / B7 / B10 / B17 / C4~~ | — | **2026-10-31 两台实验机装新包后已全部升到 ✅**（中文、打开关着的 radio、角色暴露与三角色落地、翻译包随包产出） |
| 4 | ~~C5 包里的 ns-run 与 init 同步~~ | — | **2026-10-31 已修**：tarball 带源码、包里现编，并有 test_runtime 守着漂移 |
| 5 | C3 统一行尾 | 1 | 一次改动，之后所有测试不用再转 LF；影响整个仓库，**需要单独确认**（本轮已把两台设备上出过问题的脚本都归一为 LF） |
| 6 | ~~B14 子路由接真主控（有线）~~ | — | **2026-10-31 已打通**：缺的是 cspd 那一拍，已作为 B18 进包 |
| 7 | C2 拆包 | 2 | 方案已定，属于分发形态，不阻塞功能。**顺带解决 x86_64 装不上包**：`zte-mesh` 是 `PKGARCH:=aarch64_cortex-a53` 且 `DEPENDS` 带 `@(TARGET_zte\|\|TARGET_qualcommax)`，VM 装不进去；`luci-app-zte-mesh` 虽是 `LUCI_PKGARCH:=all`，但 `LUCI_DEPENDS` 里有 `+zte-mesh`，一样被拖住。把 init/wlan-sync/rpcd/LuCI 拆成 `arch: all` 的包，VM 就能直接装 |
| 8 | B16 `CurHop` 真机确认 | 2 | 需要真机，可并入 B14 一起看 |
| 9 | B11 额外槽位在子路由侧应用 | 3 | 要先把 plan 的槽位与 hostapd 段对上，且有 A6 真机作参照 |
| 10 | A18 拓扑页 | 3 | 需要状态字段（RSSI、终端数）先落到 `state.json` |
| 11 | A17 引导 / 封锁 / 踢终端 | 3 | 接口已存在，缺的是策略与 UI |
| 12 | B13 无线回程 STA | 4 | 要真机看 `4addr`，且与 wpad 变体绑定 |
| 13 | B15 `CmWLANMapSsidCfgSync2Repeater` | 5 | 逆向或真机 strace，只有前 6 项都过了才值得动 |
