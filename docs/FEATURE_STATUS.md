# 中兴 mesh 功能清单、完成度与推进顺序

> **状态（2026-10-31）：** 主控侧在真子路由（ZXHN Z506，原厂固件）上验证；子路由侧在实验室与两台实验机上验证。
> 本表是唯一的功能总账：每完成一项就在"状态"列打勾并把证据补上，**不要**在别的文档里另起一份清单。
> 协议与逆向细节见 [CONTROLLER_CAPABILITIES](CONTROLLER_CAPABILITIES.md)、[REAL_CHILD](REAL_CHILD.md)、
> [research/AGENT_PROBE](research/AGENT_PROBE.md)；部署与测试见 [DEVELOPMENT](DEVELOPMENT.md)。

## 实验机

| 代号 | 机器 | 架构 / 系统 | 角色 |
|---|---|---|---|
| **VM** | `192.168.1.20`，VMware 里的 ImmortalWrt | x86_64，25.12.2 r38135，内核 6.12.103，**没有无线网卡** | `master`（测 `main` 时临时切） |
| **ax3600** | `192.168.1.1`，小米 AX3600 刷 ImmortalWrt（非中兴硬件） | aarch64 cortex-a53，同上 | `slave`（`radio1`=5G、`radio2`=2.4G、`radio0` 空卡） |

两台都在 `192.168.1.0/24`，root 密码 `root`，公钥已装；都没有 `tcpdump`（`apk add tcpdump-mini` 可装），
dropbear 没有 sftp（传文件用 `ssh A 'cat f' | ssh B 'cat > f'`）。

**当前轮次的产物**：`main` 上 run 41181993285，`zte-mesh-apk` 3.86 MB，拆包后四个包。**这一轮还没装上两台机器**——
下表"实验机"的证据来自上一次装机（拆包前三个包 + 手工补的 `ns-run`/`db-engine-probe`）；r10 装机验收待做。
装法：ax3600 装 `zte-mesh` + `zte-mesh-launcher`；VM 是 x86_64，只能装 `arch: all` 的 `zte-mesh`
（`launcher` 在它上面报 `uninstallable`，属预期）。都加 `--allow-untrusted`，`/etc/config/zte_mesh` 不会被覆盖。

## 图例

| 记号 | 含义 |
|---|---|
| ✅ | 已实现，**真机**验证过 |
| 🔄 | 做了一半：主机侧（配置、编解码、测试）完成，剩下的那半只能真机验 |
| ☑️ | 已实现，只在实验室 / 测试桩上验证（未上真机，或对端固件不支持） |
| ⬜ | 未做 |
| ➖ | 做不到：对端没有接收代码或没有下发通道（不是我们没写） |

难度：**1** 改几行、**2** 一天内可验证、**3** 需要新逻辑或新测试面、**4** 需要真机驱动侧配合、**5** 需要逆向或换硬件。

## A. 主控（本机是主路由）

| # | 功能 | 状态 | 证据 | 难度 |
|---|---|---|---|---|
| A1 | 原厂 `multiapd` 在私有命名空间里跑（IPC/net/mount、veth 到 `br-lan`） | ✅ | `ns-run` + `attach`，真路由器 + 真 procd | — |
| A2 | 发现 / 拓扑 / 链路指标 / 高层数据（CMDU 0、1、2、3、5、6、7、13、14） | ✅ | REAL_CHILD §1 | — |
| A3 | AP-Autoconfig + WSC M1/M2（真 DH、HMAC、Key Wrap） | ✅ | 子路由 `config M2[0]` | — |
| A4 | 配置变更后 Renew（热更新，不重启服务） | ✅ | 改配置约 4 秒生效 | — |
| A5 | 主 SSID / 密码 / 加密 / 隐藏 | ✅ | 2.4G、5G 均读回 | — |
| A6 | 多 BSS（访客、备用、隐藏回程，槽位 1–3）名称 / 密码 / 开关 | ✅ | REAL_CHILD §1 | — |
| A7 | BeaconInterval（含热更新） | ✅ | 120 → 读回 120 | — |
| A8 | ACL 名单 + 每 BSS 策略（关 / 拦 / 只放行） | ✅ | 一加手机被踢并拒绝重连；`allow` 与 5G 驱动读回未验证 | — |
| A9 | 信道与发射功率（`0x8006`） | ✅ | 两个频段验证并还原 | — |
| A10 | 重启子节点 / 指示灯开关 | ✅ | REPLAY_RESULTS 16.1 | — |
| A11 | 访客限时、Wi-Fi 定时关闭 | ✅ | `scope=guest` 真机；`scope=all` 仅实验室 | — |
| A12 | 自组网 V2（`mesh.v2`）与市场码门槛 | ✅ | 两种取值都正常入网 | — |
| A13 | 控制器角色：只发 `master`，`auto`（自适应选举替身）不再出现在配置里 | ✅ | 第 40 轮；2026-11-02 起 `main`/`auto` 在 init 折算成 `master` + `apply_local=1`，`SetRoleEnable=0`/`Mode 2` 从第一帧生效（沙箱里没有 cspd，选举没有对手，反而给真中兴子路由留了三秒当选主控的窗口）。C 侧 `CONTROLLER_ROLE_AUTO` 分支留着只服务升级前写好的 `/run/zte-mesh/config` | — |
| A14 | MLO 开关下发（`0200fe`） | ☑️ | 线上帧逐字节一致；Z506 无该字段，等于无操作 | — |
| A15 | Wi-Fi 5 兼容标志（`ForceWiFi5`） | ☑️ | 线上值正确；Z506 不认 | — |
| A16 | 漫游 / 频段引导阈值下发 | ☑️ | 能发；Z506 固件没有解析代码 | — |
| A17 | 引导（BTM）/ 封锁 / 踢终端 | 🔄 | A17a：`controller-steer.c` 编解码 `0x8014`/`0x8015`/`0x8016`/`0x8017`，tshark 逐字段核对 20 项。A17b：`controller-actions.c` 去抖/重发/超时/回滚 + 发送 + `0x8015`/`0x8017` 回灌 + `state.json` 的 `clients[]`；**配置链已通**（`zte_mesh_client` 区段 → `ban.mac`/`kick.mac`，`ban_minutes` → `ban.minutes`，页面有列表与时长，状态框显示进度），加删规则不需要重启；在实验室控制器上实跑过（封禁倒计时到 `expired`、删规则即撤销）。**对端是否照做未验**（出厂 `EnBtmKickoff=0`），见 [STEER_1905](research/STEER_1905.md) 与 H-03 | 3 |
| A18 | 拓扑页：节点清单 + **每节点的终端数（按 BSSID 记账）/ BSS 数 / 直连还是经中继 / 上报新鲜度 / 固件版本**；缺回程 RSSI、树状图、每 AP 终端列表 | 🔄 | A18a：`controller-1905.c` 解码 `0x83`/`0x94`/头部 relay 位，`state.json` 每节点加 `bss`/`hops`/`metrics_age`（-1 = 没上报，与 0 区分）；C 测试 101 断言 + 10 万次变异，含一帧抓包原文。A18b：LuCI 加「链路」列。**A21（2026-11-02）：落盘那一环拆成 `controller-peer.c`/`.h`，主机测试 58 项**——两个子节点各自一个文件、后者不覆盖前者、一帧多值写多行、灯跟着 sub-TLV 里的 AL MAC 而不是以太网源、动作回执记在终端 MAC 下、`0x0004` 那种碎帧不落盘、32 个 MAC 的洪水预算用满即拒。已知边界：终端数/BSS 数没有 AL MAC 可跟，**中继转发的计数帧记在中继自己名下**（`hops=2`），这是原有行为、A21 没改。**装机复验（2026-11-03，r16 两台实验机 PASS）**：新落盘在真机上是 `/run/zte-mesh/peers/<mac>.light` + `.metrics` 一对文件，主控（owrt）看子端 `02:00:00:00:19:e3`、子端（ax3600）反过来看主控 `02:00:00:00:19:03`，`state.json` 的 `nodes[]` 与 `light`/`hops`/`metrics_age` 都对得上——但**两台各带一个 peer 只能证明格式没错，多 peer 并存这一条要第二台子路由**（H-04）。**回程 RSSI 无线索**，见 [CAPTURE_1905](research/CAPTURE_1905.md) 与 H-05。**A18c（2026-11-03，主机侧完成）：固件版本那一格不再是空的**——反汇编查明版本走 `0x8002` AP Capability Report 里的厂商 TLV `0xd4`（Device Inventory Type），原厂收帧后把它抽进设备结构 +247，却**从不写进 `IGD.P1905DeviceInfo` 的 `SoftwareVer`**，所以页面一直读不到（[CAPTURE_1905 §5](research/CAPTURE_1905.md)）。现在 `device_inventory_software()` 切序列号后取软件版本（>64 字节、含控制字节一律不要），`controller-peer.c` 落成 `peers/<MAC>.software`，`controller-service.c` 在数据库列为空时改读这个文件；**页面一行没改**（早就在渲染 `node.software`）。C 测试 192 断言 + 10 万次变异、peer 落盘 71 项。**真机缺**：这颗 Z506 到底发不发 `0xd4`，见 H-12。**A18d（2026-11-03，主机侧完成）：终端数不再被"最后一帧"顶掉**——`peers/<MAC>.metrics` 是一份摘要，谁最后到谁写整份，子节点按射频分别回 `0x800c` 时页面就在 2.4G 的数字和 5G 的数字之间来回换，夹进一条不带计数的消息 `stations=` 干脆没了。现在 `0x94` 逐路解成 `{BSSID, 终端数}`（`ap_metrics_by_bss()`；**同一 BSSID 出现两次、或路数超过 16 就整体不拆**，宁可回落摘要，也不能让一个偏小的和看起来像一次测量），每路落一个 `peers/<MAC>.<BSSID>.sta`（各自时间戳），服务用 `controller_peer_bss_load()` 把 30 秒内还活着的那几路加起来填进 `state.json` 的 `stations`，读不到才退回摘要；读侧对文件名和文本都不信任（半段反写、尾巴多一字符、值不是数字都不算条目）。C 测试 212 断言 + 10 万次变异、peer 落盘 103 项。**真机缺**：这颗子路由到底一帧列全所有 BSS 还是一射频一帧，见 H-13。**A18e（2026-11-03，主机侧完成）：逐路的数上了页面**——`controller_peer_bss_load()` 把活着的条目本身带出来（`detail`，按 BSSID 排序：目录顺序会随重写变，拼出来的 JSON 不能跟着变），`state.json` 每个节点多一个 `bss_detail: [{bssid, stations, age}]`，拓扑页「终端数」那一格的悬停改成一行一路、后面仍跟着原来那句 BSS 数；满 16 路时总数照旧全加、只留最小的 16 行，页面那个数不会因为放不下就变小。没有新增界面文案（拼的都是 BSSID 与已有短句），`test_i18n.py` 12 项仍绿；peer 主机测试 103 → **162** 项，`node tests/test_luci.js` 加一条整串悬停断言。每 AP 的**终端列表**（这些终端是谁）仍缺。**装机读回（2026-11-03，r17 两台实验机 PASS）**：`.so` 里逐路文件名的格式串、`software` 后缀与 probe 里的 `bss_detail` 键都在两台上都读到了，主控的 `state.json` 每个节点也确带 `"bss_detail":[]`；但 `peers/` 里没有 `.sta`、没有 `.software`——**这不是判据**：那台 peer 的 `.metrics` 只有 `hops=1`，即主控从没收到过带终端数的 `0x94`，也从没收到过带 `0xd4` 的 `0x8002`（读回现在把这两件事各打印一行：`peer_reports_stations=no`、`peers_software=0`）。H-12/H-13 仍挂在真 Z506 上 | 3 |
| A19 | 服务配置校验：`zte-mesh-timer` / ACL / 槽位连续性 / `extra_slots` / **`backhaul` 的取值（只 `off`/`multi_ap`）** / **`wpad` 变体** | ☑️ | init 122 项 + wlan-sync 的变体区块：`-basic` 变体上开回程会把原因写进日志与 `wlan-sync.status` 的 `message`（客户端那一端同样告警，因为 `wds`/4 地址也来自完整 wpad），完整 `wpad` 不告警，回程关时不告警，包库读不到时既不言过也不失败；`backhaul=multi_ap` 落在不本地生效的主控上时启动说清无事可做 | — |
| A20 | **界面中英双语**：文案全走 `_()`，`po/zh_Hans` 与源码逐条对齐 | ✅ | i18n 12 项；两台随包装出 `zte-mesh.zh-cn.lmo`（11632 B），`/cgi-bin/luci/admin/translations/zh-cn` 返回中文 | — |
| A22 | **谁有资格进设备的日志**：长运行不被自己控制器的状态轮询挤掉用户那一行 | 🔄 | 不是"轮转"——先量落点：沙箱根只读、`/tmp`/`/var/tmp` 各 8 MiB tmpfs、`/run/zte-mesh` 下全是小文件原子替换，**没有会长大的文件**；唯一落点是 logd 的 `log_size=128`（KiB）环，实测 `logread` 当下 1179 行。挤占是真的：一次真子路由配置推送里原厂 daemon 重复打 `acdc current state[…]` 与 `WLAN_ASSERT_RET` **1012 行 / 7 分钟**（比整圈还多），四小时后仍占 94.5%，而这一路原来挂在火管 2 万行预算上、打光不吭声。改法在拆出来的 `controller-log.c`：按格式文本分类给预算（ordinary 160 / protocol 64 / device·renew 各 48，前两类打光各发一条固定提示），四条一次性证据行不采样，两条重复行改打 **`ZTE_TRACE_POLL` 每 60 秒 4 行**、每个超额窗口一句 `log_budget=poll sampled=1 limit=4 window=60s dropped_total=N`；**带密钥/SSID/BSS 的格式永不展开值**（值行打印的是格式化结果，所以否决在 `controller_log_shape()` 第一步）。时钟由调用方给（`CLOCK_MONOTONIC`，同 `controller-actions.c` 的时钟域教训）。主机测试 `tests/controller-log-codec.c` **655 649 项断言**（含 2^17 次有界变异）：同一次推送 1012 行 → 28 行 + 7 条说明。ARM32 `libzte_controller_trace.so` 已在实验室虚拟机重编并换进随包 tarball（逐成员 diff 只变该 `.so` 与 `SHA256SUMS`；新引用 `clock_gettime@GLIBC_2.17` 逐个对着包里 `root/lib/libc.so.6` 验过）。**装机已验（2026-11-03，r16 两台实验机 PASS）**：ax3600 上重启一次控制器，`ZTE_TRACE_POLL` 每分钟恰好 4 行、四个超额窗口各一句说明，`ZTE_TRACE_VALUE text=acdc current state` 归零（环里那种格式剩下的 122 行都是旧进程 pid 23126 在 11-02 18:49 留下的）；owrt 上ordinary/packet 两条"打光了"的提示也第一次真的落到设备日志里。**还欠 H-09**：24 小时长跑，以及真子路由那种 145 行/分钟的推送下行数到底降多少（实验环境只能把它顶到上限，测不出"从 1012 降到 28"这一步）；另记一条可读性缺口**当天就补了**：采样说明原来只说这个窗口采过样，现在带 `dropped_total=`——进程内累计丢弃数，相邻两句之差就是上一分钟丢的（`tests/controller-log-codec.c` 多一条规则守住，655 671 项断言；ARM32 shim 重编换包，`PKG_RELEASE` 随批 7/8/9 的批末升到 r17；**r17 装机读回两台的 `.so` 都含 `dropped_total=`**（`verify-install.sh` 现在把这类标记算成断言），但那一行日志本身要等超额窗口真的出现才读得到——这一轮没有） | 2 |
| A24 | **有线口掉线自愈**：`attach` 兼任看门狗，线没了就重启沙箱实例 | ✅ | 机制：`veth-lab`/`zmesh0` 是一对 veth，删一端两个一起消失，沙箱里没有 `/proc`、`/sys`、`ip`、`CAP_NET_ADMIN`，daemon 又把 ifindex 绑死在 `bind()` 上——只有重启实例才能重建。`attach` 连续 20 秒看不到两端而沙箱进程仍在，就 `SIGTERM` 它，procd（`respawn 3600 5 0`，retry 0 = 不限，实测 13 次退出仍在拉起）重新拉起，新线由同一个循环改名入桥；冷却 60 秒。纯 host 侧 shell，不动 C。**2026-11-02 在 owrt 上实测通过**（H-10）：`ip link del zmesh0` 之后 16:06:53 日志 `no zmesh0 for 20 s ...: restarting it to rebuild the wire`、16:06:58 新沙箱起来（controller 实例 pid 23117→27545，`attach` 自己的 pid 23118 没动）、16:06:59 `attached zmesh0 to br-lan`，回读 `attached=true`、拓扑里节点重新 `online=true`——25 秒自愈，没有人动手 | 2 |
| A25 | **`search` 实例不再半小时后永久死掉** | ✅ | 裸 `respawn` 被 procd 填成 `3600 5 5`：每 300 秒按设计退出一次的 `search` 半小时烧完预算，日志 `crash loop 6 crashes, 300 seconds since last crash`，之后开机的子路由再也找不到主控。实测 `owrt`：`status → running (2/3)`，只剩 ns-run + attach。`wlan-sync`、`search` 改成显式 `3600 5 0`；`test_init.sh` 断言三个实例都不再用默认值。**装机复验（2026-11-02，owrt 装 r15 之后 30 分钟、每分钟一次采样）**：`search` 的 pid 恰好每 5 分钟换一个（30 分钟 6 次，与命令里那个 300 秒的寿命对上），实例始终 `running`，`service list` 读出 `threshold=3600 retry=0`；同一窗口里 `controller`/`attach` 的 pid 30462/30463 一次没变，`daemon_restarts` 30 次全是 0——按设计退出不再烧掉预算，且这不是"没退过"而是"退了 6 次还活着" | — |

## B. 子路由（本机是子节点）

| # | 功能 | 状态 | 证据 | 难度 |
|---|---|---|---|---|
| B1 | agent 模式启动（`Mode=1` / `SetRoleEnable=0` / `EnMasterOnly=0`） | ☑️ | 实验室：三个值均读回 | — |
| B2 | 发 M1、收 M2、凭据落 `WLANCfg` / `WLANPSK` | ☑️ | 两个频段各一份，`m2_match=1` | — |
| B3 | setter 兜底（配置管理器返回 -3 时读回校验再报 0） | ☑️ | `libzte_agent_mm.so`，沙箱专用 | — |
| B4 | 库 → 计划的发布（`agent-sync` → `/run/zte-mesh/wlan-plan`） | ☑️ | `published=1 rows=2` | — |
| B5 | 计划 → `/etc/config/wireless`（`wlan-sync`，幂等、只写真变化；plan 启用某 band 时把关着的 radio 打开） | ✅ | 86 项测试；ax3600 人为 `disabled=1` 后重启服务，日志 `switched on ... applied`，空卡 `radio0` 未被碰 | — |
| B6 | 回程 BSS（主控那一端）：`MapAccessAttr&64` → `multi_ap=2` 独立段，主 SSID `multi_ap=1` | ☑️ | 86 项测试含开关双向复原；2026-11-02 起这一段只在 `mesh_role` 不是 `slave` 时生成，子节点那一端见 B13 | — |
| B7 | UCI / init / LuCI 暴露 `mesh_role`（`master`/`slave`）+ `apply_local` + `backhaul`；旧名 `main`/`auto` 仍被读 | ✅ | init 122 项（`role-local-sig`、`role-slave-local-keeps-plan`、`role-main-local-off`、两个 legacy 别名各出一份 plan）、rpcd 24 项 | — |
| B8 | **子路由的种子来自空中**：本机是子节点时，`write_config` 不再拿页面上填的 `wifi_24g_ssid` / `wifi_24g_key` / `wifi_24g_security` 当起始档案，而是**逐频段读回本机正在广播的那一段**（名称、密钥、加密、隐藏、开关）当种子——于是主控档案落地时 `wlan-sync` 逐字段一比发现没有要改的，不 `commit`、不 `wifi reload`。`wifi reload` 是全局的：任一个频段上的一次空写会把所有频段的终端一起踢掉，所以"种子等于空中"要的是**零写入**，不是"只改名字"。四个边界：①两频段广播的不是同一对 ⇒ 各自写并把 `wifi_dual_band_sync` 落成 0（同步是**关于空中的断言**，不是页面留没留那一位；守护进程按 2.4G 派生 5G，写错就把 5G 改名）②只有 2.4G 读得到且同步开着 ⇒ 5G 那一行跟 2.4G 的种子走 ③广播已关的射频不借（applier 为了落地会 `enable_radio`，那是另一次全量 reload）④名称/密钥读不出（空、超长、不可打印、加密认不出）就退回原来的检查。`live_ap_iface` 与 `wlan-sync` 的 `radio_section_of` + `ap_iface_of` 是同一条解析（点名 `wifi_2g`/`wifi_5g`、单候选、单活着、跳过 `zte_backhaul_*` / `mode != ap` / `multi_ap=2`、优先 `default_*`），两边必须指到同一段。页面上填的名称与空中不同时长日志说明以空中为准 | ☑️ | init **130** 项（`role-slave-borrow` 带 ax3600 的真实形状：空卡 `radio0` 关掉却还挂着工厂测试名 `ZTEMESHCTRL`，配置里必须一次都不出现；`-same-pair` / `-same-pair-derived` / `-synced-one-band` / `-two-live` / `-pinned` / `-radio-off` / `-unreadable` / `master-ignores-air`，另更新 `role-slave-no-profile`）；wlan-sync **182** 项（新"子节点种子等于空中"区块：逐字段相同 ⇒ `changed=0`、无 commit、`wifi` 未被调用；把名称改一个字符作反证仍要 commit；`disabled` / `hidden` **没写过**而含义相同也不算变化）。**ax3600 上配置面实测**：新种子与 `/etc/config/wireless` 逐字段相同，拿它喂 `wlan-sync --once`（`UCI_CONFIG_DIR=/tmp/b17etc`）报 `result=ok changed=0 bands=`，副本与真文件 md5 一致（`59094b96b83f8f74655f5268b95be2be`），被桩掉的 `wifi` 一次没跑；对照组是装机的 r17，它同一台机器写出 `radio0.ssid=ImmortalWrt` / `key=labbpass123` / `beacon_type=12`——正是那次"翻两次"的源头。**装 r17 的机器仍会翻两次**。**r18 那次批末装机给出了判决**：装四个包自带的重启里，ax3600 读到
`air_matched_plan=yes` 且 `applier_writes=0`、`sync_changed=0`，`/etc/config/wireless` 装前后 md5 都是
`59094b96b83f8f74655f5268b95be2be`——新种子加重启＝零写入，H-14 的判据③由 C11 自动读到，不用另开一个掉线窗口。
剩下的两半还在真机窗口里：⑥只从主控侧改一次 SSID（确认该翻的那一次照翻、且没有多余的第二次），⑦把主控的
`CurHop` 摆成不等于子节点 `MaxHop` 的值（B16 分不开的那种） | — |
| B9 | **LuCI 按角色只显示有效设置**（子路由隐藏 Wi-Fi/漫游/额外网络/子路由清单；**applier 的四个输入只留给"会动射频的角色"**：`backhaul`、`extra_slots` 和两个射频点名 `wifi_2g` / `wifi_5g` 在子路由和"主控 + `apply_local=1`"上显示，在纯主控上都不显示；两个点名填 `wifi-device` 段名，形状非法保存前就拒） | ☑️ | `test_luci.js`（会落地的主控 38 项 / 子路由 7 项，另加"纯主控不给这四个"一条和段名形状的七个取值；"每个 init 选项都要有表单项"这一遍渲染前者，因为纯主控按设计就少几个字段） | — |
| B10 | rpcd `status` 返回 `role` 与 `local` 两个轴 | ✅ | rpcd 24 项；`main`/`auto` 报成 `master` + `local:true`，子路由永远 `local:false`；页面标题由这两值决定（`test_luci.js`） | — |
| B11 | **额外槽位（slot 1–3）落到本机**：`extra_slots=1` 时每（频段,槽位）一个 `zte_extra_<band>_<slot>` 段，plan 不再下发的槽位被删除，`extra_slots=0`（默认）与旧行为逐字节相同；`main` 角色的本机 plan 也带上 `zte_mesh_bss` 的行 | ☑️ | `wlan-sync` 123 项（含默认关/开/幂等/删槽位/坏密钥/频段二选一/槽位 4 越界/无名行）、init 87 项（`role-main-slots` 五行的 plan、`role-main-no-slots` 只两行、`role-slave-slots-keeps-plan` 不动子路由 plan、`extra_slots=2` 被拒）。**默认关**，真机效果见 [PLAN](PLAN.md) H-02 | 3 |
| B12 | **应用结果回传**：`wlan-sync.status` + `status.sync` + 状态页行 | ☑️ | wlan-sync 86 项、rpcd 18 项 | — |
| B13 | 无线回程的数据面：本机作为 backhaul STA 去关联主控的回程 BSS | 🔄 | B13a/B13b 主机侧完成：`mesh_role=slave` 时 plan 里 `MapAccessAttr&64` 的那行落成 `zte_backhaul_sta_<band>`（`mode sta` + **`multi_ap 1`** + **`wds 1`**，名称/密钥/加密取 plan，不写 `hidden`）。**关键是 `wds`**：装机实测 `supplicant.uc` 里 `4addr` 与入桥只看 `wds`（`:305`/`:313`），`multi_ap` 在 sta 模式只是译成 `multi_ap_backhaul_sta`（`:83`，决定关联能不能被回程 BSS 接受），只写它会被 netifd 判 `BRIDGE_NOT_ALLOWED`（`:278`）——这纠正了 [AGENT_PROBE](research/AGENT_PROBE.md) §11.6 "4addr 跟 multi_ap 走"的推断。换角色时把对端那段还回去；行里声称加密却没有密钥两端都拒绝；该频段在 plan 里没有主 BSS（客户端没有桥可挂）时段照建、但日志明说它不通桥；`wpad-basic` 的告警扩到客户端；`backhaul=multi_ap` 落在主控 + `apply_local=0` 时启动说清这个开关此刻无事可做；**取值只有 `off`/`multi_ap`**（`multi-ap` 这类写法以前被 `wlan-sync` 静默读成 off，现在 `write_config` 拒掉），并且**取值与 `wlan-sync` 从 UCI 直接读的另外三项（`extra_slots`、`wifi_2g`、`wifi_5g`）一起进 `restart.sig`**——它们都不改 plan、也不改实例集合，只改 applier 拿同一份 plan 写出什么，而那个 watcher 只看 plan 摘要，不重启就要等下一次别的原因的重启才生效（`extra_slots` 那条最难发现：子路由会继续广播一个已经关掉的 guest）。带值的只有跑 applier 的两个角色，与 procd 起 `wlan-sync` 实例的条件共用 `sync_runs`；`wifi_2g` / `wifi_5g` 也从"只能手写 UCI"变成页面上两个字段（B13g，同一批角色）。**端到端（真关联、拔网线仍在线、1905 过无线）待真机**，见 [PLAN](PLAN.md) H-01 | 4 |
| B14 | 真机：本机当子路由，接真主控（有线回程） | ✅ | 两台实验机打通。抓包确认**子路由自己会发** `0x0007` Search、主控回 `0x0008` 带 WSC；原判断"子路由被 cspd 门控"是错的，真因是 C6。r7 后主控改 SSID，子路由 `wlan-plan` 与 `iw dev` 跟着变成 `MASTERTEST`/`MASTERTEST-5G` | — |
| B15 | `CmWLANMapSsidCfgSync2Repeater` 对齐（libcmapi 内部调用，替身命中不到） | ⬜ | 沙箱里是 no-op | 5 |
| B16 | **子节点的 `CurHop` 是守护进程自己更新的**：我们的启动路径只写 `-2`（注册表里的"未接入"），子节点方向再没有第二处写它 | ☑️ | ax3600（`mesh_role=slave`，跑我们沙箱里那颗原厂守护进程）上守护进程自己那行 cfg 读到 `mode=1 … cur_hop=2 max_hop=2`——`-2` 出自 `controller-run.c` 的子角色那一支（换角色时 `controller-service.c` 往子节点方向写的也是 `-2`），所以这个 2 只能来自守护进程：`slave_init` 的 `CfMultiapSetCurHop`，或主控 TLV `0x43` 采用的值（[FINDINGS](FINDINGS.md) 第 7 节的两条路径）。原厂那一侧同一个结论：E2631 实测 `CurHop=2=MaxHop` 且 `EnMasterOnly=0`，见 [E2631_ROOTFS](research/E2631_ROOTFS.md) 与本文末尾那条"已结案"注。**读数现在是一条命令**：`verify-install.sh` 的 `cur_hop=`——子角色判"不是 `-2`"，主控只当证据（那一列本来就是我们自己写的）；`no-cfg-line`（日志环已经把那行翻出去了，实验台跑久就是这一种）、`-98`（守护进程自己读不到那一列）、这一次运行刚重启过服务、以及**那一行还太年轻**（脚本顺带报 `cur_hop_age=<秒>`，比 `CUR_HOP_GRACE`＝默认 120 秒还小就只报不判），四种形状都不判。年轻这一种是 r18 装机那一趟当场教出来的：ax3600 环里 `13:26:51 … cur_hop=-2` 与 `13:27:18 … cur_hop=2` 相差 27 秒，只读复跑落在那半分钟里把健康机器判成 `RESULT=FAIL`（一分钟后同命令 `RESULT=PASS`）——**和 SSID 那一项等档案落地是同一件事**，见 [INSTALL](INSTALL.md) 第 3 节；算不出年龄（老日志没有 `epoch=`）照判，不放过。**还分不开的一种**：实验台上主控的 `CurHop` 与本机 `MaxHop` 都是 2，"设成 MaxHop"和"采用主控下发的值"读数相同——要分开得让主控那一侧不等于 2，登记在 [PLAN](PLAN.md) H-14 的第 ⑦ 步，跟真重启窗口一起做 | — |
| B17 | **两个开关**：`mesh_role` 决定本机是主控还是子节点，`apply_local` 决定主控要不要把同一份档案落到本机；旧名 `main`/`auto` = 主控 + 落地 | ✅ | init 122（`role-master` 不写 plan、`role-local-sig` 写 plan 且 `restart.sig` 变、`role-slave-local-keeps-plan` 不碰子路由 plan、两个 legacy 别名各出一份、`sig-*` 七条把 `backhaul` 钉成 `restart.sig` 的第三个取值，且 `apply_local=0` 的主控不带它）；VM 上切角色后 `wlan-plan` 与 `wlan-sync` 实例一起出现/消失（一个 plan 只有一个生产者） | — |
| B18 | **cspd 替身**：控制器侧自己发 `0x0007` Search、并代答子路由的 Search（`0x0008`） | ✅ | `tools/lab/enroller.c` → `/usr/libexec/zte-mesh/mesh-search`，init 在非 `slave` 角色下多起一个 `search` 实例，带 `--al-mac 02:00:00:00:19:<al_last>`（子路由的 M1 发给控制器 AL MAC）。`--search` 每次启动只发**一帧**，所以周期就是"多久收一次新子节点"，已从 `86400` 改成 `300`（最坏等 5 分钟）；实测不抖：`plan applied nothing new`、`daemon_restarts=0`。装 r6 后真机自动入网，不再需要手工 enroller | — |

| B19 | **控制器地址末字节可配**（UCI `al_last` / 页面"控制器地址末字节"）：两台设备 LAN MAC 相同时手工错开 | ✅ | init 83 条（含 `al_last` 空/大小写/非法值四个分支，VM 上 busybox ash 实测）；只暴露末字节，前五字节固定为子路由准入检查认的形状 | 1 |
| B20 | 页面上本机型号取系统型号（`/tmp/sysinfo/model`）、名称取主机名，取不到都显示 `router` | ✅ | `controller_self_identity()`（`controller-run.c`），`state.json` 的 `self`；子路由行缺型号/名称时同样显示 `router`。runtime 包里的 probe 已按这份源码重打（2026-11-01），设备上才会有 `self` | 1 |
| B21 | `mesh-search` 的 `0x0007` 补 End of Message TLV；线上帧用 Wireshark `ieee1905` 逐字段核对过一遍 | ✅ | 沙箱 90 秒抓 34 帧五类消息：改前 12 个 `0x0007` 全被标 `[Malformed Packet]`，补三个 0 后 0 malformed 且对端照样回 `0x0008`；`0x8006` 的 TLV 编码与 dissector 解析一致（静态构造核对）；`0x0008` 少的 EOM/`0xb3` 故意不改（Z506 验过的形状）。见 [WIRESHARK_1905](research/WIRESHARK_1905.md) | 1 |
| B22 | 查证原厂内核（E2631 2024-09 `vmlinux`）**不含 mesh 协议实现**，也不含 rootfs 解密 | ✅（查证，无代码改动） | 1908 条 `__bug_table` + 6393 个导出符号恢复出函数级地图：`zx_denali_nand.c` 读页路径只有 Denali 寄存器序列与 DMA、无 crypto 调用；内核无 `ecb(aes)`/`cbc(aes)` 模板实例、`"aes"` 字符串零引用；`wlan_get_wlan_multiap_mode()` 全函数只有"读一个 u32"；`br_ieee1905` 钩子对 1905 组播帧在转发路径返回 DROP（**§5.5 修正：丢的是转发，本地投递走 `br_ieee1905_local_in_handle` 放行**）；`CBC decrypt OK!` 位于 Bootloader 分区（`0x619E`）。见 [MESH_INTEROP](research/MESH_INTEROP.md) §4.1 与 §5 | 0 |
| B23 | 摸清**原厂 mesh 的 1905 协议实体是 `multiapd`，不是 cspd**；它未 strip，可直接按函数名调试与逆向 | ✅（查证，无代码改动） | 第三方同型机 `ps` 显示 `cspd`(597) 与 `multiapd`(1293) 是两个进程——我们此前 22 个成员**漏取了 `multiapd`**。该二进制 988 140 B / 6835 符号 / 2184 具名函数：`I5Tlv*` 360 个（TLV 编解码全在此，不在任何 `.so`）、M1/M2 两端对称（`_acdcSendApAutoConfigWSCM1` / `_acdsRcvApAutoConfigWSCM1` 2388 B / `_acdcRcvApAutoConfigWSCM2`）、`g_ieee1905_sockfd` + `bind socket to br0` 证明 1905 走用户态 raw socket。自带 `pktdump 1`（落 `/var/tmp`）、`setrole`、`querydev`、`showMsgStatistics`，**设备无 tcpdump 也能抓 1905**。见 [MULTIAPD](research/MULTIAPD.md) | 0 |

## C. 打包与分发

| # | 功能 | 状态 | 证据 | 难度 |
|---|---|---|---|---|
| C1 | 单个 apk（runtime + init + wlan-sync + rpcd + LuCI），4.1 MB | ✅ | 真路由器上 procd/rpcd 全通 | — |
| C2 | 拆包：`zte-mesh`（`arch: all`）+ `zte-mesh-launcher`（aarch64，只装 `ns-run`/`mesh-search`） | ✅ | **只做了"让 x86_64 能直装"这一半**。必须两个目录：`package-pack.mk` 的 `Architecture` 读 Makefile 级全局 `PKGARCH`，一个 Makefile 给不了两个架构。VM 现在 `apk add zte-mesh` 直装且不覆盖自编的 `ns-run`。原方案的 controller/agent 拆分仍未做 | 1 |
| C3 | 统一行尾（`.gitattributes`，`* text=auto eol=lf`） | ✅ | 规则早在仓库里，2026-11-01 核过：`git ls-files --eol` 210 个文件索引全 `i/lf`，6 个二进制标 `-text`；只剩 Windows 工作区的旧副本是 CRLF（重新检出即消失），Linux/CI 上一直是对的 | — |
| C4 | **中文翻译包随 CI 构建**：`luci-i18n-zte-mesh-zh-cn` 之前只配 `=m` 没编译，中文从不产出 | ✅ | 两台装完后 `zte-mesh.zh-cn.lmo` 就是包带来的 | — |
| C5 | **包里的 `ns-run` 与 init 不同步**：tarball 里是旧版，不认 `ZTE_MESH_QEMU`/`ZTE_MESH_EMULATED_LAB`，无 `CONFIG_COMPAT` 的 aarch64 上 `execv` 直接 `errno=8` | ✅ | 修法是消除漂移本身：tarball 额外带 `ns-run.c`/`controller-net.c`/`enroller.c`，`Build/Compile` 用 `$(TARGET_CC)` 现编；`test_runtime.py` 比对源码副本，重打包漏了就在本机失败 | — |
| C6 | **tarball 里的 `db-engine-probe` 冻结在打包那天**：ARM32 半边只能 `-nostdlib` 对着原厂 glibc 2.26 编，包里编不了；冻住的那份不认 `mesh.role=slave` → 子路由的 controller 实例每 5 秒退一次 | ✅ | 换重编副本（193364 B，sha256 `7ff631a7…`）后 slave 角色 3/3 实例全起；`test_runtime.py` 断言包里那份必须带 `config-reject` 与 `wlan-plan`。**这一条又犯了一次**（2026-11-03）：批 7/8/9 三个批次只改 C，谁都没看 tarball——页面读 `bss_detail`、trace 库却不写逐路文件，是"回落到摘要"这种不像故障的错法。现在守卫多两条：probe 必须带 `bss_detail`，`libzte_controller_trace.so` 必须带写侧才有的文件名格式 `%02x:….sta`（旧包两个都是 0 次命中，所以它们真会失败）。重打走 `openwrt/repack-runtime.py`，随后补跑 `make-runtime.py` 跳过的 GNU 符号版本审计（见 [LAB](LAB.md) 3.0）；`PKG_RELEASE` 到 r17/r9 | — |
| C7 | **全新装包后服务起不来**：`qemu-arm-static` 不在包里，而 `CONFIG_COMPAT` 探测在官方固件上必然误判（不导出 `/proc/config.gz` → `grep -c` 返回 0 → 一律走模拟器） | ✅ | 改成"没有答案不等于不支持"：读不到 config 就 exec 一次 `$ROOT/lib/ld-linux.so.3` 看它有没有打印 usage。模拟器**仍不入包**（取舍，不是待办），判定表与三条出路见 [LAB](LAB.md) 第 4.2 节 | 1 |
| C8 | 装机手册：装哪四个包、页面点哪里、`mesh_role` / `apply_local` 两个开关怎么摆出 S1/S2/S3、怎么确认成功、卸载与"设了没动静" | ✅ | [INSTALL](INSTALL.md) 九节，**2026-11-03 按文档从头走了一遍**：两台实验机（owrt 主控、ax3600 子节点）上把每一节里的命令原样跑一次、输出与原话逐条对，改口八处——①§1 那条 `opkg` 检查在 25.12 上根本不存在这个命令，且 `/proc/config.gz` 两台都没导出（"看配置文件"这条判据改成"看服务起没起来、日志有没有 `errno=8`"）；②§1 的容量数字是 r5 那批的，实测换成 3 872 842 B / 8 007 KiB / 释放 7.8 MiB，并点明占地方的是手工放的 `qemu-arm-static`（10.7 MB）不是包；③§2 那条四个 apk 一条 glob 在 x86_64 上**一条都不装**（launcher 架构不合 → 求解全败），另加"一个目录只放一批"（r17 与 r9 同目录时 `apk` 报 `Downgrading (0.1.0-r17 -> 0.1.0-r9)`，而目录里没有 r9 的文件）和 `scp` 目标目录不存在会把文件写成那个名字；④§3 补上配置被拒绝时三条检查各长什么样（`state=null`／`age=-1`／`wlan-sync.status` 不存在），以及 `unix_bind errno=98/111` 不是故障；⑤§4 的 `apply_local` 默认值读不到——`uci show \| grep apply_local` 一行都不出，判据拆成"实验台能测的"与"要有真子路由的"两半；⑥§5 补实测拒绝原文 `24g slot 2 needs slot 1 (slots must be 1..k); not starting` 与 `uci add` 后必须带 `zte_mesh.` 段前缀；⑦§6 写下走查中才发现的用户可见行为：**子节点每次重启服务把空中名称翻两次**（本地档案 → 约 21 秒后主控 M2），家里有人连着就是两次掉线，§9 的"先确认你能接受设备掉线"因此包括重启服务；⑧§7 的 `multi-ap` 那条原话"读成 off"是错的——init 取值表不认它，整个配置被拒、服务不起；§8 的 `/usr/lib/zte-mesh` 原话"apk 清不掉"也错，那棵树是包装进去的，`apk del` 连它一起删。走查同时暴露 `verify-install.sh` 在健康机器上的假 FAIL（重启后 12 秒就断言 SSID，实测档案约 4 秒、子节点重播约 21 秒才落地），已改成等落地并补到 85 条断言（见 C10） | 1 |
| C9 | CI 不再发布 release：只构建 + 校验和 + artifact，发布改手工 | ✅ | `.github/workflows/build-apk.yml` 里 `gh release` 只剩注释与 summary 那一句；`permissions: contents: read`；`DEVELOPMENT.md` 第 8 节改成"只构建 + 三条手工发布步骤"（原来写的 `release_tag` 输入早已不存在）。触发方式是手动，push 不发起 CI | — |
| C10 | 装机验收脚本：一条命令装四个包 + 读回版本 + 跑三条冒烟 + **读回装机标记** | ✅ | `tools/lab/verify-install.sh`（busybox ash）：`apk add` 每包一条（架构不合算 `uninstallable`、文件不在算 `skip`，都不算失败）→ 装过就重启 rpcd 与 `zte-mesh` → `apk list` 读回四个版本 → 三条冒烟：服务与角色、角色应有的实例集合（controller/attach 恒有，wlan-sync 看 `apply_local`，search 看 `mesh_role`）、档案落地（`wlan-sync.status` 的 `result` 加 plan 里每个启用行的 SSID 落在哪个 `wifi-iface` 上）。输出 `KEY=VALUE` 证据行 + `RESULT=PASS/FAIL`。**两台实验机各跑一次都 PASS**，判定逻辑由 `sh tests/test_verify_install.sh`（85 条，桩 ubus/uci/apk/logread + fixture，已登记进 `make test-host`；busybox ash 在 owrt 上跑同样 85 条）在主机上守。**补上批末版本号后又装验了第二遍**（CI run 41410028326 的 r15/r8/r1）：owrt 读回 `0.1.0-r15` / `0.1.0-r8`（launcher 在 x86_64 上 `uninstallable`、版本 `absent`，正是预期），ax3600 四个包全装上、两条 SSID 仍跟着主控，两台仍 `RESULT=PASS`——"这台机器装的是哪一批"现在能用一条命令读出来。**第四遍（2026-11-03，run 41556872802 的 r17/r9/r1）加装机读回一段**：apk 剥 ELF 节，设备副本比不了哈希，所以按字符串认——`trace_per_bss_name`（`.so` 里那个逐路文件名的格式串）与 `probe_bss_detail` 算断言，`peers_sta` / `peers_software` / `peer_reports_stations` / `state_bss_detail` 只算证据（`peers_sta=0` 且 `peer_reports_stations=no` 说明这颗 peer 从没报过终端数，不是解码器的失败）；这台机器压根没解过包时打印 `unreadable`，不判失败——环境事实不该变成批次判决。两台都 `RESULT=PASS`（owrt 11 项、ax3600 11 项）。**C8 走查抓到一个假 FAIL**（2026-11-03，ax3600）：SSID 那一项在脚本自己重启服务后 12 秒就断言，而实测 `/run/zte-mesh/wlan-plan` 约 4 秒才写到这一批的名字、子节点还要约 21 秒才把主控下发的名字翻上去——一台健康的机器被判 `RESULT=FAIL`。改成先等落地再判（`SSID_WAIT` 默认 60 秒、每 `SSID_POLL`=3 秒看一次），超时才 FAIL 并把没落地的行号写成 `ssid_pending=`；判定加到 **85 条**（新守：`ssid_wait`/`ssid_pending` 的读回，和"uci 桩第三次才返回新名字也必须 PASS"），owrt 上 busybox ash 同样 85 条、两台重跑仍 `RESULT=PASS`。**r18 那次批末又补了两个标记**：这一批只改包里的两个 shell 文件（init 的 `live_ap_iface`、wlan-sync 的 `set_flag`），rootfs 一个字没变，只读那两个库会把"没装上"读成"装上了"——现在 `init_seed_from_air` 与 `sync_flag_by_meaning` 也算断言，机器上没有那份文件时同样打印 `unreadable` 不判；实测装 r17 的两台上这两个字符串都是 0 次命中，所以这条真有区分度。判定守到 **136 条**（busybox ash 同样 136）。**r18 装机那一趟又还了一条债**：只读复跑在 ax3600 上把健康的子节点判成 `RESULT=FAIL`——那一行 `ZTE_MESH_SERVICE cfg` 距守护进程写下它才 27 秒（环里 `13:26:51 cur_hop=-2` → `13:27:18 cur_hop=2`），B16 缺"证据太年轻"这第四种形状。现在按行末的 `epoch=` 算 `cur_hop_age=<秒>`，小于 `CUR_HOP_GRACE`（默认 120 秒）只报不判；算不出年龄（没有 `epoch=`）照判。判定守到 **149 条**（busybox ash 同样 149；新增 13 条＝年龄报数 1 + 年轻不判 5 + 把窗口关掉仍判 3 + 老日志没时刻照判 4）。**r18 两台装完都 PASS**：owrt（主控）13 项、四个标记全 `yes`、`applier_writes=no-applier`；ax3600（子节点）14 项、`air_matched_plan=yes` 且 `applier_writes=0`、`sync_changed=0`、`/etc/config/wireless` 装前后 md5 没变、`version_zte-mesh=0.1.0-r18` | 2 |
| C11 | **装机验收读回"这一次重启写了几次无线"**（B8b 的判据入口）：`verify-install.sh` 第四条冒烟 `applier_writes` | ✅ | 数的是日志里 `applied; wireless committed` 的行数**增量**——窗口就是脚本自己要求的那次重启。**为什么不能比文件**：旧种子会把空中改名、主控的 M2 再改回来，前后 `/etc/config/wireless` 一模一样，可家里已经掉了两次线；只有 applier 自己那行留了痕。**判 0 的前提**：重启前先读一次"空中是否已经带着 plan 里每个启用行的名字"（`air_matched_plan`）——是，才判 `applier_writes=0`；不是（刚装完、还在收敛），只报数不判，否则把一次正常的收敛说成故障。另外四种不判的形状：没重启（`no-restart`）、这个角色没有 applier（`no-applier`，纯主控）、`logread` 读不出来（`no-logread`，读不出来当成 0 行，而 `0 - 0` 恰好像一次安静重启）、日志环在窗口里翻了圈（`wrapped`，被截断的窗口不是个数）。`RESTART_NOW=1` 让**不装机也能驱动这一次重启**，这就是 [PLAN](PLAN.md) H-14 的一条命令；它会真掉线，所以默认关。**主机测试 85 → 108 条**（安静重启 PASS、`writes=2` 必须 FAIL、收敛中只报不判、纯主控不判、没重启不算、读不到日志不算、窗口翻圈不算——`zte-mesh` 的 init 桩现在按 fixture 往日志里长那几行，也能整份换掉日志）；owrt 上 busybox ash 同样 108 条；两台只读跑各 `RESULT=PASS` 并打印 `applier_writes=no-restart`。**r18 那次批末装机就是它的第一个真判决**：
装包自带的重启窗口里 ax3600 给出 `air_matched_plan=yes` + `applier_writes=0`（判据成立，见 B8），owrt 作为纯主控
给出 `applier_writes=no-applier`（该角色本来没有落地器，不判） | 1 |

## D. 换子路由固件才谈得上的（原判据只有 Z506）

| # | 功能 | Z506（原判据） | E2631 / AX3000（2026-11-01 补） | **配置库入口（表.列）** |
|---|---|---|---|---|
| D1 | 带宽 20/40/80/160 | 不写 `BandWidth`，也不关"自动信道" | 未找到新证据，不变 | — |
| D2 | 访客限速 | 1905 里没有这个字段 | 协议外的东西，不变 | — |
| D3 | 信道扫描 / CAC | 无接收代码 | 有 `SendCAC` / `ChannelScan` **发送**入口，接收侧没查（弱） | `MultiAPMaster.InChannelScanRptFlag=0` + **`MultiAPCfg.EnReportCenterChannel=1`**（均实测）。**上报本来就是开的**，缺口在接收侧 |
| D4 | VBSS（虚拟 BSS 创建/销毁/迁移） | 无 | `cspd` 有 `EnVbss` / `ToVbssMode` 配置位，但**没有任何 CM/CF 导出**（中） | **`MultiAPMaster.EnVbss=0` / `.ToVbssMode=1`**（实测，与 `EnBtmKickoff` 同表） |
| D5 | VLAN / 流量分离 / Profile-2 | 无 | `CfMultiAPSetVlanCfg`（152 B 实函数，`ASEND 0xa94e`）——有下发入口（中） | 编译期 `CONFIG_CSP_WLAN_VLAN` + `EBTABLES_{VLAN,FFE_T}` + 下发入口三条都在；**配置层实测为零**，三处落点全关：`EthVlan`(#79) **0 行**、`Brport`(#20) **16 口 × 8 个 VLAN 列全默认**、**`WLANCfg` 每行 7 个 per-VAP VLAN 列全 0**。**入口已定位到两侧：VAP 打/剥 tag 在 `WLANCfg`（`Vlan`/`VlanPriority`/`UpTagEnable`/`DownTagStripEnable`/`McastVlan*`），端口默认 VLAN 在 `Brport` 那 8 列**。厂商启用时写哪侧仍未解（web `.lua`）。另：`MultiapCapability.SupportPackageProduct=0`（若对应 EasyMesh packaged-product 概念则是直接否定，**仅命名联想，弱**） |
| D6 | STA 绑定 | 无 | `CmMultiApSetStaBind`，走 `0x2401` wlan 本地命令，**不是 1905 通道**（中） | **五张准入/绑定表全部 0 行**：`P1905StaBind`(#168)、`MultiAPStaCfg`、`MultiAPCacheMac`、`WlanRoamingSta`(#271)、`AclCfg`(#5)。**入口存在、出厂未配**。另：`WLANSetting.ACLIsFrom1905Slave=0` ⇒ 本机**不收** 1905 侧同步的 ACL，同向证据 |
| D7 | 802.11r/BTM 踢人（部分） | 固件不支持 FT | `handle_wlan_multiap_steering_req`、`wnm_send_btm_req_raw`、`En11rSteer` 都在（**强**）。内核侧 `br_ieee1905_local_in_handle` 放行本地投递（§5.5），帧能到 `multiapd`；而它自带 `pktdump`，线上取证不必再自带抓包程序 | **`MultiAPMaster.{EnBtmKickoff=0,EnRoamKick=0,En11rSteer=0,11rSsidSync=1,EnActiveDeauth=1}`** + `MultiAPDomain` 的 106 列状态机参数 + `MultiAPRoamCatonCfg.BtmCacheEn=0`（全部实测）+ `WlanRoamingCfg`（实测，**与 11r 无关**） |

这一节的判据原来只有 Z506（Wi-Fi 5 老固件）。2026-11-01 从一台市售 AX3000（E2631，2026-08 固件）
拿出明文用户态之后，D5/D6/D7 从"没有通道"变成"厂商有入口"，其中 **D7 的接收侧处理路径是齐的**。
强度分级（强/中/弱）与反汇编证据见 [VENDOR_MESH_STACK](research/VENDOR_MESH_STACK.md) 第 5 节。

**右列是 2026-11-01 新增的**：把整块 rootfs（[E2631_ROOTFS](research/E2631_ROOTFS.md)）解出来后，
从 `cspd` 的 schema 字符串表 + web 模块 `.lua` 两条独立路径交叉得到 mesh 配置库的表名与列名，
**D3–D7 各自"该改哪个字段"不再是猜测**。

当晚又用五轮只读命令把推断核对了一遍（原文整理在
[E2631_DB_SNAPSHOT](research/E2631_DB_SNAPSHOT.md) 与 [E2631_DB_TABLES](research/E2631_DB_TABLES.md)），
**前四轮的结局是表归属错了三处**：

- `MultiAPMaster`（55 列）、`MultiAPDomain`（8 实例 × 106 列）、`MultiAPRoamCatonCfg`（7 列）、
  `MultiAPCfg`（50 列）、`P1905Cfg`（12 行 × 3 列）、`WlanRoamingCfg`（1 行 × 6 列）**切对了**，
  `MultiAPMaster` 实测还补上 12 个原先漏掉的列；
- `P1905ComCfg` **切错了表**——它其实只有 8 列（`Enable/EnableIsSync/Policy/DropNum/MarketConfEn/MarketSign/DropRoam/A1Enable`），
  `SetRoleEnable`/`En1905Msg`/`STAMac` 那些名字属于别的表，**上一版右列里引用过它们的写法已作废**；
  更糟的是本仓 [SCHEMA](milestones/SCHEMA.md) 2026-10-12 的补充节早就写了这 8 个字段的偏移与字节数，
  重新推断时没先查；
- `MultiAPCacheMac` **也是切错的**：那 7 个帧缓存列实际在 `MultiAPRoamCatonCfg`，且列名没有 `t` 前缀
  （`BtmCacheEn`，不是 `tBtmCacheEn`）；`MultiAPCacheMac` 本身是空表；
- 我上轮猜的 `IGD.P1905CFGn` **作为表名不存在**，角色开关的正解是 **`MultiAPCfg`**；
  但**它作为行名是对的**——见下。

第三轮 `sendcmd 1 DB all` 拿到 **276 张表名全集**，把上面错误的根源（从字符串表推断表名）彻底堵上：

- 又有三个"列名"其实是**表名**：`MultiAPPTK`(#147)、`P1905StaBind`(#168)、`P1905Cfg`(#164)；
- 库里**没有 `IGD` 这张表**，但 `IGD.` 是**行名前缀**（`TimePolicy` 的行名就叫 `IGD.TpCfg0..2`，见
  [FEATURE_MAP](research/FEATURE_MAP.md) §15）。所以 `IGD.P1905CFGn` 应读成"某表第 n 行"，
  不是表名——`DB p` 只收表名，报 not find 只证明它不是表；
- `MESH`/`Netsphere`/`EASYMESH` 也**不在 276 张表里**，[E2631_ROOTFS](research/E2631_ROOTFS.md) §6.4
  把它们写成"DB 视图别名"没有依据，已降级为 web 层名称映射；
- mesh 相关的表不是 5 张而是 **19 张**（10 `MultiAP*` + 9 `P1905*`）。

**第四轮把上面那条行名推断坐实了**：dump `P1905Cfg`(#164) 后，`ViewName` 逐字就是
`IGD.P1905CFG1`…`IGD.P1905CFG6`，对应 `eth1`–`eth4`、`wlan0`、`wlan1` 六个接口，全 `EnableP1905=1`，
后 6 行是空壳（12 行是编译期容量）。所以我最初在 D5 右列写的
`IGD.P1905CFGn.{P1905InterfaceName, EnableP1905}` **内容全对**，只是把「表 `P1905Cfg` 的行
`IGD.P1905CFG<n>`」压缩成了「表 `IGD.P1905CFGn`」——上一轮一刀切作废属于**过度纠正**，现已恢复成正确层级。

`WlanRoamingCfg`(#270) 这一轮也 dump 了，**方向是负的**：`RoamingEnable=0`、`Interval=60`、
`TimeStart=120`、`RssiThresh=-50`、`RssiStart=-30`，是客户端弱信号重关联参数，**六列里没有 11r 开关**。
⇒ "固件不支持 FT"现在有**两条独立证据**（编译期 + 配置层），不必再找 11r 的配置入口。

`MultiAPCfg` 实测还结掉两条待办：**`CurHop=2=MaxHop` 且 `EnMasterOnly=0`**（B16 结案，与 SCHEMA 的
条件句三者自洽）；**`En1905Msg=1`、`EnReportCenterChannel=1`** 都是开的——所以 1905 报文取证那条路
没有落空，D3 的缺口也不在"要不要上报"。

实测顺带纠了一个方向性判断：**出厂踢人走 `EnActiveDeauth=1`，BTM 那条（`EnBtmKickoff`）是关的**。
所以 D7 与厂商线上行为比对时，该预期看到 deauth/disassoc，而不是 11v 的 BTM Request。

另外两条编译期证据（`/etc/autouserconf`，612 个开关）：`CONFIG_CSP_WLAN_SUPPORT_80211KV`
**只有 11k/11v、没有 11r**——与 D7"固件不支持 FT"是同一件事的两面，实测 `En11rSteer=0`、`SupportRrm=1` 与之吻合；
`CONFIG_CSP_WLAN_VLAN` + `CONFIG_CSPCORE_EBTABLES_{VLAN,FFE_T}` 说明 WLAN 侧 VLAN 是编进去的。

**第五轮（`MultiapCapability` / `EthVlan` / `Brport`）把 D5 从"本体未定位"推到"入口明确、出厂关着"**，
同时纠掉一处方法论错误：

- **`EthVlan`(#79) `RowCount=0`**：出厂没有任何以太 VLAN 配置。0 行只证明"没配"，
  **不能**证明"落点不在这张表"（per-STA 那三张表也是 0 行，是按需建行）——候选未被否证，只是没被启用。
- **`Brport`(#20) 16 行 × 23 列，一次读全端口拓扑**：`BRPORT1` 是桥自身（`LowerLayers` 列出
  `BRPORT2`–`BRPORT12` 十一个成员），`BRPORT2`=`LAN1`(`DEV.ETH.IF2`)、`BRPORT3`–`BRPORT10`=
  `SSID1`–`SSID8`、`BRPORT11/12`=`LAN2`/`LAN3`(`IF3`/`IF4`)、`BRPORT13/14`=`APCLI1`/`APCLI5G1`
  （无线回程 STA，指向 `STAPROFILE1/2`）、`BRPORT15`=`DEV.ETH.IF1`（WAN 侧，`ServList=1`+`WanName=Bridge`）、
  `BRPORT16`=`ETH_WAN`(`IF99`)。**16 口的 8 个 VLAN 列全默认**（`PVID` 全 1，`IngressFiltering`/
  `AcceptableFrameTypes`/`PriorityTagging`/`WanPortVlan{Enable,Mode,ID,Pri}` 全 0），`ServList` 只有两个
  WAN 口为 1 ⇒ **单广播域、零划分**，两条独立证据坐实。
- **`MultiapCapability`(#154) 只有 2 列**（`SupportBSSync=1`、`SupportPackageProduct=0`）。
  我上一轮把它排成"与 `SupportStrategies=251` 位图对照，可能一次解开"，这是**第四处自纠**：
  ① 名字相似被当成结构证据；② 更根本的是——**位图的值早就在 `MultiAPDomain` 里实测到了，缺的是语义而不是数据**，
  dump 再多表也解不出来。这条待办已从 dump 清单里撤掉，改为"只能靠反汇编或 Multi-AP 规范正文"。

顺带读出两条：

- `SupportBSSync=1` 而 `MultiAPCfg.SyncBhBssEnable=0`/`SSIDSyncRadio=0` ⇒ **能力在、配置关**，
  与 D4 的 `EnVbss=0`、D7 的 `EnBtmKickoff=0` 同一个模式。**这台设备上"支持某能力"一律指"编进去了"，不指"开着"。**
- 端口编号解掉了一半旧疑问：以太口是 `IF1`–`IF4` + `IF99`（**没有 `IF0`**），`IF1` 在 WAN 侧、
  `IF2/3/4` 恰好是 `LAN1/LAN2/LAN3`，与 AX3000"1 WAN + 3 LAN"吻合。但 `DEV.ETH.IFn ↔ ethn` 同号
  **本轮没有证到**，只是间接证据；要定得靠 `ip link`。

**第六轮（`MapCooperatorCfg` / `WLANSSID` / `PortBinding`）结掉两条线，也给"dump 到头"画了边界**：

- **`MapCooperatorCfg` 1 行 5 列，字段名与顺序与 [SCHEMA](milestones/SCHEMA.md) 的还原逐字一致**——
  本仓第三次「离线还原被真机命中」（前两次：`MultiAPCfg` 的 `36+32n`、`IGD.` 是行名前缀）。
  `AutoSetup=0`、`EnRebootElect=0`，加上 `Mode=1`/`SetRoleEnable=0`/`RoleElect=0`/`EnMasterOnly=0`，
  **六个开关同向** ⇒ 本机是静态配置出来的 Slave，不选举、不自动开局、重启也不改主意。**角色这条线结案。**
- **`WLANSSID` 8 行 × 8 列，band 分组第一次明确**：`SSID1`–`SSID4` 挂 `Device.WiFi.Radio.1`，
  `SSID5`–`SSID8` 挂 `Radio.2`，**4 + 4**（哪个是 2.4G/5G 仍未定，第七轮 `WLANBase` 结掉）。`MULTIAPDOMAINn ↔ SSIDn` 的同号关系
  由此升到**强**，三方证据：8 行的数量与编号、`Brport` 端口指向 `DEV.WIFI.SSIDn`、以及生效 domain
  `1/4/5/8` 正好是「每个 radio 两个」的对称分布。
- **但 `WLANSSID` 8 列全 `isSave=0`，且没有任何 ESSID/加密/密钥字段 ⇒ 它是运行时接口视图，不是配置表。**
  按 §8.5 的判据，**别去写 `WLANSSID`**（整表连一个可写字段都没有）。真正的 SSID 配置在
  `WLANCfg`(#259)/`WLANBase`(#258)/`WLANSetting`(#263)/`WLANPSK`(#260)（第七轮结掉前三张，只剩 `WLANPSK`）。
- `PortBinding` 0 行 ⇒ D5 的三个候选（`EthVlan`/`Brport`/`PortBinding`）**只剩 `Brport` 有内容**。
- 上一轮的 `PortState` 张力**没消掉，反而多了一条反证**：`WLANSSID.Enable` 8 个全是 1，而
  `PortState` 只有 5 个是 4；且 `WLANSSID.Status`/`LastChange` 8 行全 0 更像"字段没被填充"而不是
  「0 = Up」。⇒ `PortState` 的 0/4 语义**仍未定**，要从 `brctl show`/`iw dev` 取，不再排进 dump。（这两句**第七轮推翻**，见下）

**第七轮（`WLANBase` / `WLANCfg` / `WLANSetting` / `WlanRoamingSta`）把无线配置本体一次读全，也带出第五处自纠**：

- **`WLANBase` 2 行 × 68 列，band 归属结案**：`DEV.WIFI.RD1` `Band=0`/`Channel=4`、`RD2` `Band=1`/`Channel=40`
  ⇒ **`Device.WiFi.Radio.1` = 2.4G、`Radio.2` = 5G**，且 `Radio.n` ↔ `RDn` 同号。两行同报 Mediatek **MT7916**
  （一颗双频 2×2 芯片，不是两颗独立射频）。与 [FINDINGS](FINDINGS.md) §10 记的 Z506 射频（MT7592/MT7613）
  **不是同一套**，"不支持 160"那句不能套到本机。
- **`WLANCfg` 8 行 × 49 列是真正的 SSID 配置**，`IfName` 与 [FINDINGS](FINDINGS.md) §9 的还原**逐字一致**
  （AP1–4 = `wlan0`–`wlan3`、AP5–8 = `wlan5g0`–`wlan5g3`）——本仓**第四次**「离线还原被真机命中」
  （前三次：`36+32n`、`IGD.` 行名、`MapCooperatorCfg` 五字段）。八个槽位「0 主 / 1 访客 / 2 备用 / 3 回程隐藏」
  也对上了：槽 1 是唯二 `GuestSsid=1`，槽 3 是唯二隐藏 + `ACLPolicy=0` + `MapAccessAttr=64`（`ZMESH-` 回程 SSID）。
  **真实 SSID 名在 `MultiAPDomain.ESSID`（明文），`WLANCfg.ESSID` 被遮蔽成 `***`**——想看名字 dump 前者。
- **第五处自纠（把 `isSave` 判据用反了）**：第六轮拿 `WLANSSID.Enable`（8 行全 1）去反证 `PortState=4` 只有 5 个，
  这个反证立不住——`WLANSSID` 整表 `isSave=0`，它的 `Enable` 只是"对象存在"的运行时标记，不是配置开关。
  真正的开关 `WLANCfg.Enable` 与 `PortState=4` 的五个 BSS **逐个相同** ⇒ **`PortState=4` = 在跑，
  从"推断"升到"两表交叉证实"**。`SSID3`「开着但 `DomainUsed=0`」也不矛盾：一个 BSS 可以在广播、
  却不对应任何生效的 mesh 域（两个值量的是不同的事）。
- **D5 的落点从桥侧一处扩成两侧**：`WLANCfg` 每行 **7 个 per-VAP VLAN 列**（`Vlan`/`VlanPriority`/
  `UpTagEnable`/`DownTagStripEnable`/`McastVlan`/`McastVlanPriority`/`McastDownTagStripEnable`）**全 0**——
  这才是"按 SSID 做流量分离"的直接落点。加上 `Brport` 八列全默认、`EthVlan`/`PortBinding` 0 行，**三处全空**：
  出厂是一张单广播域、零 VLAN 的网；但入口是明确的两侧，不再是"未定位"。
- `WLANSetting` 1 行 9 列：**`ACLIsFrom1905Slave=0`**（不收 1905 侧 ACL，D6 同向）、`SSIDSync=1`/`DualBandSync=0`、
  扫描节律 `BootScanDuration=21600`/`ScanInterval=86400`（秒）。`WlanRoamingSta` 0 行——**第四张 per-STA 空表**。
- **一条新问题**：`P1905Cfg` 的 `wlan0`/`wlan1` 现在知道是谁了（AP1 主 2.4G + AP2 访客 2.4G，**后者还关着**），
  两个都在 2.4G、5G 一个没列——与 [REPLAY_RESULTS](research/REPLAY_RESULTS.md) §5 在 Z506 抓到的
  「1905/ACD 实际走 `wlan0`+`wlan5g0`（两频段各一）」**对不上**。`P1905Cfg` 可能只是"允许跑"的静态声明
  而非运行时清单 ⇒ **凡拿它当"1905 跑在哪些接口"的证据都要降级成"允许跑在哪些接口"**。

**第八轮（`WLANPSK` / `AclCfg` + 现场状态）配置库取证收口**：

- **`WLANPSK` 8 行 × 4 列**，行名 `DEV.WIFI.APn.PSK1` 与 [FEATURE_MAP](research/FEATURE_MAP.md) 的
  `OBJ_WLANPSK_ID(DEV.WIFI.AP1.PSK1)` **逐字对上**——本仓第五次「离线还原被真机命中」。
  `PreSharedKey`/`KeyPassphrase` **被遮蔽成 `***`**：密钥无泄露路径；但 `MultiAPDomain.ESSID` 是明文，
  **SSID 名可还原、密钥不行**（遮蔽按表/列做，不是全局策略）。8 个 AP 各有且仅有 `PSK1` 一行，
  含关着的与隐藏的槽位——密钥出厂全配。
- **`AclCfg` 0 行**：MAC ACL 一条没有（`FINDINGS` §9 的结构在，数据空）。至此**准入/绑定数据面五张全空**
  （per-STA 四张 + `AclCfg`），"机制在、数据空"坐实。
- **「只有 LAN2 插着网线」——预测命中**：用户确认现场 LAN 口只插了 LAN2，与 `Brport` 逐口吻合
  （`BRPORT11`(LAN2)=4、`BRPORT2`(LAN1)=0、`BRPORT12`(LAN3)=0）。接上一轮 `WLANCfg.Enable` 的配置侧对照，
  **`PortState=4` = 在跑，配置侧 + 物理侧双证闭合**。WAN 侧补记：用户确认 **WAN 口也插着线**，
  `BRPORT15`(`IF1`)=4 与物理一致、`BRPORT16`(`ETH_WAN`/`IF99`)=0 说明 `IF99` 是逻辑 WAN 抽象
  而非物理链路镜像——物理侧三口全对（LAN2 插=4、LAN1/3 没插=0、WAN 插=4）。

至此**八轮、22 张表 + 276 张表名全集，`sendcmd 1 DB p` 这条线全部收口**——D3–D7 加角色、band、
端口拓扑的库侧入口都是实测级。dump 解决不了、要换手段的只剩三件：位图/枚举语义（反汇编）、
厂商启用 VLAN 时写哪侧（web `.lua`）、1905 实际跑在哪几个接口（`pktdump`/`/proc/net/dev`）。
（WAN 口插线状态的尾巴已随 `PortState` 一并闭合。）

一条通用判据：**`isSave=0` 的列是运行时/派生值，写 DB 不落盘**（`WLANSSID` 整表、`WLANCfg.IfName`、
`MultiAPDomain.ESSID` 都是）。改配置前先看这一列，否则"改了不生效"会被误判成"固件不认"；
**反过来拿它当配置依据也会判错**——第七轮的第五处自纠就是这么来的：`isSave=0` 的列只能读状态，
不能当配置比对。

**七项的"入口位置"现在是实测级，但"功能本身"一项都没实测**，所以仍然没有一项改成 ✅。

> **排期在 [PLAN](PLAN.md)**：分批、每批的判据、哪些等真机、CI 什么时候跑。本表只记状态与证据。

## 推进顺序（只做下一项，做完打勾再往下）

| 顺序 | 项 | 难度 | 说明 |
|---|---|---|---|
| — | B8 / B9 / B10 / B12 / A20 / B5 / B7 / B17 / C4 / C5 / C6 / C7 | — | **已完成**（2026-10-31） |
| — | `sendcmd 1 DB p` 配置库 schema 取证（八轮、二十二张表 + 表名全集） | — | **已完成**（2026-11-01）：`MultiAPMaster` 55 列、`MultiAPDomain` 8×106 列、`MultiAPRoamCatonCfg` 7 列、`MultiAPCfg` 50 列、`P1905Cfg` 12×3 列（`ViewName` = `IGD.P1905CFG1..6`）、`WlanRoamingCfg` 1×6 列、`MultiapCapability` 1×2 列、`Brport` 16×23 列（端口拓扑 + 8 个 VLAN 列全默认）、`MapCooperatorCfg` 1×5 列（**与 SCHEMA 还原逐字同序**）、`WLANSSID` 8×8 列（视图，全列 `isSave=0`）、`WLANBase` 2×68 列（**RD1=2.4G/RD2=5G，MT7916**）、`WLANCfg` 8×49 列（**真 SSID 配置，`IfName`/7 个 VLAN 列**）、`WLANSetting` 1×9 列（**`ACLIsFrom1905Slave=0`**）、`WLANPSK` 8×4 列（**密钥被遮蔽，`APn.PSK1` 与 FEATURE_MAP 对上**）切对；`P1905ComCfg` 只有 8 列、`MultiAPCacheMac` 那 7 列其实在 `MultiAPRoamCatonCfg`，两处切错已自纠；`EthVlan`/`PortBinding`/`AclCfg` 与四张 per-STA 表（`MultiAPStaCfg`/`P1905StaBind`/`MultiAPCacheMac`/`WlanRoamingSta`）表在行空；`DB all` 拿到 276 张表名全集。**结案**：11r 为负、D5 配置层为零（三处落点全关、入口定在 `WLANCfg`+`Brport` 两侧）、角色为静态 Slave（六开关同向）、band 归属（RD1=2.4G/RD2=5G）、`PortState=4`=在跑（配置侧 `WLANCfg.Enable` + 物理侧「只有 LAN2 插线」双证）。见 [E2631_DB_SNAPSHOT](research/E2631_DB_SNAPSHOT.md)、[E2631_DB_TABLES](research/E2631_DB_TABLES.md) |
| 1 | r10 四包装机验收 | 0 | 产物已在手，两台换上即可 |
| 2 | ax3600 刷 `CONFIG_COMPAT=y` 固件 | 1 | 彻底摆脱手工模拟器，`.github/workflows/build-firmware.yml` |
| 3 | C3 统一行尾 | 1 | 影响整个仓库，**需要单独确认** |
| 4 | ~~B11 额外槽位在子路由侧应用~~ | — | **已完成**（2026-11-01，批 1）：`extra_slots` 默认关，主机测试 210 项；真机验收见 [PLAN](PLAN.md) H-02。后续批次见 [PLAN](PLAN.md) |
| 5 | ~~A18 拓扑页~~ | — | **已做一半**（2026-11-01，批 2）：终端数 / BSS 数 / 直连与中继 / 新鲜度已上线；**固件版本主机侧完成**（2026-11-03 批 7：`0x8002` 里的厂商 TLV `0xd4` → `peers/<MAC>.software` → `state.json`，真机待验 H-12）；**终端数改成按 BSSID 记账**（2026-11-03 批 8：`0x94` 逐路 → `peers/<MAC>.<BSSID>.sta` → 服务求和，真机待验 H-13）；**逐路的数上了悬停**（批 9：`state.json` 的 `bss_detail`，页面一格仍是总数）；回程 RSSI 要无线回程真机（H-05），树状图与每 AP 终端列表待排 |
| 6 | A17 引导 / 封锁 / 踢终端 | 3 | 接口已存在；策略与 UI 已接上，缺真机确认（H-03） |
| 7 | ~~B13 无线回程 STA~~（主机侧） | 4 | **配置半边已完成**（2026-11-02，批 4）：`4addr` 的开关实测是 netifd 的 `wds` 不是 `multi_ap`（[AGENT_PROBE](research/AGENT_PROBE.md) §15），STA 段两个都写；与 wpad 变体绑定，客户端也进告警。**剩端到端**（H-01）。**线索留着**：`MapCooperatorCfg.Addr4EffectDelayTime=10` 可能就是 4 地址模式的生效延迟（歧义未定，见 [SNAPSHOT](research/E2631_DB_SNAPSHOT.md) §10.8），真机要等它才看得出"关联上但短时间不通" |
| 8 | B15 `CmWLANMapSsidCfgSync2Repeater` | 5 | 逆向或真机 strace，前 7 项都过了才值得动 |
| 9 | 用 AX3000 当子路由验 D5 / D6 / D7 | 3 | **要停机窗口**；取证改用它自带的 `pktdump 1`（落 `/var/tmp`）或 `logcat \| grep "AGENT RECV"`，**不必再自带抓包程序**。停机前先做零成本只读项：`sendcmd 1 DB p`（读 mesh 配置库）、`cat /proc/kallsyms \| grep 1905`、取 `/kmodule/bin/multiapd` 原件。见 [MULTIAPD](research/MULTIAPD.md) §5 与 [E2633_TELNET_BOOT](research/E2633_TELNET_BOOT.md) §11 |

> **B16 `CurHop` 已结案**（原第 5 项）：`MultiAPCfg.CurHop=2=MaxHop`，且 `EnMasterOnly=0`，
> 与 [SCHEMA](milestones/SCHEMA.md) "`EnMasterOnly=1` 时 `slave_init` 跳过、`CurHop` 不会被设成 `MaxHop`"
> 的条件句三者自洽。不是"读到一个值"式的结案。原表 5–11 项已顺次前移。
