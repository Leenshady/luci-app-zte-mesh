# 中兴 mesh 功能清单、完成度与推进顺序

> 本表是**唯一的功能总账**：每完成一项就更新"状态"列与判据，别处不再另起一份清单。
> **排期、批次、真机待办（H-01 … H-14）只在 [PLAN](PLAN.md)**（第 3 节批次、第 5 节真机台账）；开发过程叙事在 [journal](journal/)。本表只记状态与"怎么算成立"。
> 协议与逆向细节见 [CONTROLLER_CAPABILITIES](CONTROLLER_CAPABILITIES.md)、[REAL_CHILD](REAL_CHILD.md)、[research/AGENT_PROBE](research/AGENT_PROBE.md)。
> **最新批末数字（`zte-mesh` r18 已装机；`luci-app-zte-mesh` 升到 r10，批 13 还没走 CI）**：装机验收 **173** 条（r18 那一批是 149，批 13 加 24 条，还没在设备上跑过）、init **130** 条、wlan-sync **182** 条、rpcd 24 条。各项测试的条数基线见 [DEVELOPMENT](DEVELOPMENT.md) 第 4 节，本表不再逐条复述测试计数与它的增长史。
> **实验机**：三台机器（`ax3600` / `owrt` / `ubuntu` 192.168.1.191）的地址、角色、架构与机型限制以 [LAB](LAB.md) 第 2.1 节为唯一权威，本表不维护机器名单；登实验机一律走 `tools/lab/rssh.py`（dropbear 没有 sftp 时它自己退回 `cat` 管道）。

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

| # | 功能 | 状态 | 证据（怎么算成立） | 难度 |
|---|---|---|---|---|
| A1 | 原厂 `multiapd` 在私有命名空间里跑（IPC/net/mount、veth 到 `br-lan`） | ✅ | `ns-run` + `attach` 在真路由器 + 真 procd 上跑起，拓扑页非空 | — |
| A2 | 发现 / 拓扑 / 链路指标 / 高层数据（CMDU 0、1、2、3、5、6、7、13、14） | ✅ | 真子路由上逐类读回，见 [REAL_CHILD](REAL_CHILD.md) §1 | — |
| A3 | AP-Autoconfig + WSC M1/M2（真 DH、HMAC、Key Wrap） | ✅ | 子路由 `config M2[0]` 读回 | — |
| A4 | 配置变更后 Renew（热更新，不重启服务） | ✅ | 改配置约 4 秒生效 | — |
| A5 | 主 SSID / 密码 / 加密 / 隐藏 | ✅ | 2.4G、5G 均读回 | — |
| A6 | 多 BSS（访客、备用、隐藏回程，槽位 1–3）名称 / 密码 / 开关 | ✅ | [REAL_CHILD](REAL_CHILD.md) §1 | — |
| A7 | BeaconInterval（含热更新） | ✅ | 发 120 → 读回 120 | — |
| A8 | ACL 名单 + 每 BSS 策略（关 / 拦 / 只放行） | ✅ | 一加手机被踢并拒绝重连；`allow` 与 5G 驱动读回未验证 | — |
| A9 | 信道与发射功率（`0x8006`） | ✅ | 两个频段验证并还原 | — |
| A10 | 重启子节点 / 指示灯开关 | ✅ | [REPLAY_RESULTS](research/REPLAY_RESULTS.md) 16.1 | — |
| A11 | 访客限时、Wi-Fi 定时关闭 | ✅ | `scope=guest` 真机；`scope=all` 仅实验室 | — |
| A12 | 自组网 V2（`mesh.v2`）与市场码门槛 | ✅ | 两种取值都正常入网 | — |
| A13 | 控制器角色：只发 `master`，`auto`（自适应选举替身）不再出现在配置里 | ✅ | `main`/`auto` 在 init 折算成 `master` + `apply_local=1`；`SetRoleEnable=0`/`Mode 2` 从第一帧生效（沙箱里没有 cspd，选举没有对手，反而给真中兴子路由留了三秒当选主控的窗口）。C 侧 `CONTROLLER_ROLE_AUTO` 分支只服务升级前写好的 `/run/zte-mesh/config`。真机对照见 H-11 | — |
| A14 | MLO 开关下发（`0200fe`） | ☑️ | 线上帧逐字节一致；Z506 无该字段，等于无操作 | — |
| A15 | Wi-Fi 5 兼容标志（`ForceWiFi5`） | ☑️ | 线上值正确；Z506 不认 | — |
| A16 | 漫游 / 频段引导阈值下发 | ☑️ | 能发；Z506 固件没有解析代码 | — |
| A17 | 引导（BTM）/ 封锁 / 踢终端 | 🔄 | `controller-steer.c` 编解码 `0x8014`/`0x8015`/`0x8016`/`0x8017`（tshark 逐字段核对）；`controller-actions.c` 做去抖 / 重发 / 超时 / 回滚与回执回灌。**配置链已通**：`zte_mesh_client` 区段 → `ban.mac`/`kick.mac`，`ban_minutes` → `ban.minutes`，页面有列表与时长、状态框显示进度，加删规则不需要重启。判据：实验室控制器上封禁倒计时到 `expired`、删规则即撤销。**对端是否照做未验**（出厂 `EnBtmKickoff=0`），见 [STEER_1905](research/STEER_1905.md) 与 H-03 | 3 |
| A18 | 拓扑页：节点清单 + **每节点的终端数（按 BSSID 记账）/ BSS 数 / 直连还是经中继 / 上报新鲜度 / 固件版本**；缺回程 RSSI、树状图、每 AP 终端列表 | 🔄 | 四块各有判据：① `0x83`/`0x94`/头部 relay 位解码，`state.json` 每节点带 `bss`/`hops`/`metrics_age`（-1 = 没上报，与 0 区分）。② **固件版本**走 `0x8002` AP Capability Report 里的厂商 TLV `0xd4`（Device Inventory Type）——原厂收帧后把它抽进设备结构 +247 却**从不写进 `IGD.P1905DeviceInfo` 的 `SoftwareVer`**，所以页面原本读不到（[CAPTURE_1905 §5](research/CAPTURE_1905.md)）；现在 `device_inventory_software()` 切序列号后取软件版本（>64 字节、含控制字节一律不要），落成 `peers/<MAC>.software`，数据库列为空时改读这个文件，**页面一行没改**；真机缺（这颗 Z506 到底发不发 `0xd4`）→ H-12。③ **终端数按 BSSID 记账**：`0x94` 逐路解成 `{BSSID, 终端数}`（同一 BSSID 出现两次、或路数超过 16 就整体不拆、宁可回落摘要，也不能让一个偏小的和看起来像一次测量），每路落一个 `peers/<MAC>.<BSSID>.sta`（各自时间戳），服务把 30 秒内还活着的几路求和填进 `stations`，读不到才退回摘要；真机缺（一帧列全还是一射频一帧）→ H-13。④ 逐路的数**上页成一列**（`state.json` 的 `bss_detail`，按 BSSID 排序、一路一行 `BSSID 数`，每行悬停是那一路的年龄；「终端数」那格仍是总数、悬停只说"报了几路"）。一批只改脚本时页面上没有新东西可读回——判据是 `test_luci.js` 的 12 格与"0 终端是一行、不是杠"。已知边界：终端数/BSS 数没有 AL MAC 可跟，**中继转发的计数帧记在中继自己名下**（`hops=2`）；每 AP 的**终端列表**（这些终端是谁）与树状图仍缺。回程 RSSI 无线索（[CAPTURE_1905](research/CAPTURE_1905.md)）→ H-05；多 peer 并存要第二台子路由 → H-04 | 3 |
| A19 | 服务配置校验：`zte-mesh-timer` / ACL / 槽位连续性 / `extra_slots` / **`backhaul` 的取值（只 `off`/`multi_ap`）** / **`wpad` 变体** | ☑️ | 判据：`-basic` 变体上开回程会把原因写进日志与 `wlan-sync.status` 的 `message`（子节点那一端同样告警，因为 `wds`/4 地址也来自完整 wpad），完整 `wpad` 不告警、回程关时不告警、包库读不到时既不言过也不失败；`backhaul=multi_ap` 落在不本地生效的主控上时启动说清无事可做 | — |
| A20 | **界面中英双语**：文案全走 `_()`，`po/zh_Hans` 与源码逐条对齐 | ✅ | i18n 测试全绿；两台随包装出 `zte-mesh.zh-cn.lmo`，`/cgi-bin/luci/admin/translations/zh-cn` 返回中文 | — |
| A22 | **谁有资格进设备的日志**：长运行不被自己控制器的状态轮询挤掉用户那一行 | 🔄 | 唯一落点是 logd 的 `log_size=128`（KiB）环——服务不写会长大的文件（沙箱根只读、`/tmp` 与 `/var/tmp` 各 8 MiB tmpfs、`/run/zte-mesh` 全是小文件原子替换），所以这里管的是**打进环的比例**，不是轮转（量与预算表见 [LAB](LAB.md) 3.1）。判据：`controller-log.c` 按格式文本分类给预算（ordinary 160 / protocol 64 / device、renew 各 48），前两类打光各发一条固定提示；两条重复状态行改打 **`ZTE_TRACE_POLL` 每 60 秒 4 行**，每个超额窗口一句 `log_budget=poll sampled=1 limit=4 window=60s dropped_total=N`（进程内累计丢弃数，相邻两句之差就是上一分钟丢的）；四条一次性证据行不采样；**带密钥/SSID/BSS 的格式永不展开值**（值行打印的是格式化结果，所以否决在 `controller_log_shape()` 第一步）；时钟由调用方给（`CLOCK_MONOTONIC`）。主机测试 `tests/controller-log-codec.c`：同一次真子路由推送的 1012 行压成 28 行 + 7 条说明。装机已验（重启控制器后每分钟恰好 4 行、`ZTE_TRACE_VALUE text=acdc current state` 归零、随包 `.so` 含 `dropped_total=`）。**还欠**：24 小时长跑，以及真子路由那种 145 行/分钟的推送下行数到底降多少（实验环境只能把它顶到上限）→ H-09 | 2 |
| A24 | **有线口掉线自愈**：`attach` 兼任看门狗，线没了就重启沙箱实例 | ✅ | 机制：`veth-lab`/`zmesh0` 是一对 veth，删一端两个一起消失，而沙箱里没有 `/proc`、`/sys`、`ip`、`CAP_NET_ADMIN`，daemon 又把 ifindex 绑死在 `bind()` 上——只有重启实例才能重建。`attach` 连续 20 秒看不到两端而沙箱进程仍在就 `SIGTERM` 它，procd（`respawn 3600 5 0`，retry 0 = 不限）拉起后由同一个循环改名入桥；冷却 60 秒；纯 host 侧 shell，不动 C。判据（已实测）：`ip link del zmesh0` 后 **25 秒自愈**——日志 `no zmesh0 for 20 s ...: restarting it to rebuild the wire`、回读 `attached=true`、拓扑里节点重新 `online=true`，且 `attach` 自己的 pid 没动（看门狗没被一起带走）。见 H-10 | 2 |
| A25 | **`search` 实例不再半小时后永久死掉** | ✅ | 裸 `respawn` 被 procd 填成 `3600 5 5`：每 300 秒按设计退出一次的 `search` 半小时烧完预算，之后开机的子路由再也找不到主控。改成 `wlan-sync`、`search` 显式 `3600 5 0`。判据：`search` 的 pid 恰好每 5 分钟换一个而实例始终 `running`（**按设计退出 ≠ 崩溃**，判据要能把两者分开），`service list` 读出 `threshold=3600 retry=0`；`test_init.sh` 断言三个实例都不再用默认值 | — |

## B. 子路由（本机是子节点）

| # | 功能 | 状态 | 证据（怎么算成立） | 难度 |
|---|---|---|---|---|
| B1 | agent 模式启动（`Mode=1` / `SetRoleEnable=0` / `EnMasterOnly=0`） | ☑️ | 实验室：三个值均读回 | — |
| B2 | 发 M1、收 M2、凭据落 `WLANCfg` / `WLANPSK` | ☑️ | 两个频段各一份，`m2_match=1` | — |
| B3 | setter 兜底（配置管理器返回 -3 时读回校验再报 0） | ☑️ | `libzte_agent_mm.so`，沙箱专用 | — |
| B4 | 库 → 计划的发布（`agent-sync` → `/run/zte-mesh/wlan-plan`） | ☑️ | `published=1 rows=2` | — |
| B5 | 计划 → `/etc/config/wireless`（`wlan-sync`，幂等、只写真变化；plan 启用某 band 时把关着的 radio 打开） | ✅ | 幂等与"只写真变化"由主机测试守；判据：ax3600 人为 `disabled=1` 后重启服务，日志 `switched on ... applied`，空卡 `radio0` 未被碰 | — |
| B6 | 回程 BSS（主控那一端）：`MapAccessAttr&64` → `multi_ap=2` 独立段，主 SSID `multi_ap=1` | ☑️ | 开关双向复原在主机测试里；这一段只在 `mesh_role` 不是 `slave` 时生成，子节点那一端见 B13 | — |
| B7 | UCI / init / LuCI 暴露 `mesh_role`（`master`/`slave`）+ `apply_local` + `backhaul`；旧名 `main`/`auto` 仍被读 | ✅ | init 的角色区块（`role-local-sig`、`role-slave-local-keeps-plan`、`role-main-local-off`、两个 legacy 别名各出一份 plan）+ rpcd 24 项；映射表见 [SERVICE](SERVICE.md) | — |
| B8 | **子路由的种子来自空中**：本机是子节点时，`write_config` 不再拿页面上填的 `wifi_24g_ssid` / `wifi_24g_key` / `wifi_24g_security` 当起始档案，而是**逐频段读回本机正在广播的那一段**（名称、密钥、加密、隐藏、开关）当种子——于是主控档案落地时 `wlan-sync` 逐字段一比发现没有要改的，不 `commit`、不 `wifi reload`。`wifi reload` 是全局的：任一个频段上的一次空写会把所有频段的终端一起踢掉，所以"种子等于空中"要的是**零写入**，不是"只改名字"。四个边界：①两频段广播的不是同一对 ⇒ 各自写并把 `wifi_dual_band_sync` 落成 0（同步是**关于空中的断言**，不是页面留没留那一位；守护进程按 2.4G 派生 5G，写错就把 5G 改名）②只有 2.4G 读得到且同步开着 ⇒ 5G 那一行跟 2.4G 的种子走 ③广播已关的射频不借（applier 为了落地会 `enable_radio`，那是另一次全量 reload）④名称/密钥读不出（空、超长、不可打印、加密认不出）就退回原来的检查。`live_ap_iface` 与 `wlan-sync` 的 `radio_section_of` + `ap_iface_of` 是同一条解析（点名 `wifi_2g`/`wifi_5g`、单候选、单活着、跳过 `zte_backhaul_*` / `mode != ap` / `multi_ap=2`、优先 `default_*`），两边必须指到同一段。页面上填的名称与空中不同时长日志说明以空中为准 | ☑️ | 主机侧判据：逐字段相同 ⇒ `changed=0`、无 commit、`wifi` 未被调用；把名称改一个字符作反证仍要 commit；`disabled`/`hidden` **没写过**而含义相同也不算变化；`role-slave-borrow` 带 ax3600 的真实形状（空卡 `radio0` 关掉却还挂着工厂测试名，配置里必须一次都不出现）。**总账判据（子节点重启零写入）**：`verify-install.sh` 自己那一次重启窗口读回 `air_matched_plan=yes` + `applier_writes=0` + `sync_changed=0`，且 `/etc/config/wireless` 装前后 md5 不变——装 r18 那次就是这个读数（为什么不能比文件、五种不判形状见 [INSTALL](INSTALL.md) 第 3 节与 C11；反面是装 r17 的同一台机器先广播页面填的那对、约 21 秒后才被 M2 翻过去＝两次掉线）。**剩下的两半还在真机窗口**：⑥只从主控侧改一次 SSID（该翻的那一次照翻、且没有多余的第二次）、⑦把主控 `CurHop` 摆成不等于子节点 `MaxHop` 的值（B16 分不开的那种）→ [PLAN](PLAN.md) H-14 | — |
| B9 | **LuCI 按角色只显示有效设置**（子路由隐藏 Wi-Fi/漫游/额外网络/子路由清单；**applier 的四个输入只留给"会动射频的角色"**：`backhaul`、`extra_slots` 和两个射频点名 `wifi_2g` / `wifi_5g` 在子路由和"主控 + `apply_local=1`"上显示，在纯主控上都不显示；两个点名填 `wifi-device` 段名，形状非法保存前就拒） | ☑️ | `node tests/test_luci.js` 的三种角色可见性 + 段名形状的取值枚举；"每个 init 选项都要有表单项"这一遍在纯主控上按设计就少几个字段 | — |
| B10 | rpcd `status` 返回 `role` 与 `local` 两个轴 | ✅ | rpcd 24 项；`main`/`auto` 报成 `master` + `local:true`，子路由永远 `local:false`；页面标题由这两值决定 | — |
| B11 | **额外槽位（slot 1–3）落到本机**：`extra_slots=1` 时每（频段,槽位）一个 `zte_extra_<band>_<slot>` 段，plan 不再下发的槽位被删除，`extra_slots=0`（默认）与旧行为逐字节相同；`main` 角色的本机 plan 也带上 `zte_mesh_bss` 的行 | ☑️ | 判据：默认关 / 开 / 幂等 / 删槽位 / 坏密钥 / 频段二选一 / 槽位 4 越界 / 无名行都在主机测试里；init 断言 `main` 角色 plan 的行数与 `extra_slots=2` 被拒。**真机效果**见 [PLAN](PLAN.md) H-02 | 3 |
| B12 | **应用结果回传**：`wlan-sync.status` + `status.sync` + 状态页行 | ☑️ | 判据：状态文件与 rpcd 的 `status` 是同一份，状态页那一行由它渲染（主机测试守两侧） | — |
| B13 | 无线回程的数据面：本机作为 backhaul STA 去关联主控的回程 BSS | 🔄 | 主机侧完成：`mesh_role=slave` 时 plan 里 `MapAccessAttr&64` 的那行落成 `zte_backhaul_sta_<band>`（`mode sta` + **`multi_ap 1`** + **`wds 1`**，名称/密钥/加密取 plan，不写 `hidden`）；换角色时把对端那段还回去；行里声称加密却没有密钥两端都拒绝；该频段在 plan 里没有主 BSS 时段照建、但日志明说它不通桥；`wpad-basic` 的告警扩到客户端；**取值只有 `off`/`multi_ap`**（`multi-ap` 这类写法现在由 `write_config` 拒掉，以前被静默读成 off）；`backhaul=multi_ap` 落在主控 + `apply_local=0` 时启动说清这个开关此刻无事可做；**applier 的四个输入（含 `wifi_2g`/`wifi_5g`，它们也从"只能手写 UCI"变成页面字段）一起进 `restart.sig`**——它们都不改 plan、也不改实例集合，只看 plan 摘要的 watcher 不重启就看不见（最坏的是 `extra_slots`：子路由会继续广播一个已经关掉的 guest）；带值的只有跑 applier 的两个角色，与 procd 起 `wlan-sync` 实例共用 `sync_runs`。**关键结论**：装机实测 `supplicant.uc` 里 4 地址与入桥只看 `wds`（`:305`/`:313`），`multi_ap` 在 sta 模式只译成 `multi_ap_backhaul_sta`（`:83`），只写它会被 netifd 判 `BRIDGE_NOT_ALLOWED`（`:278`）——这纠正了 [AGENT_PROBE](research/AGENT_PROBE.md) §11.6 "4addr 跟 multi_ap 走"的推断。**线索留着**：`MapCooperatorCfg.Addr4EffectDelayTime=10` 可能就是 4 地址模式的生效延迟（歧义未定，[E2631_DB_SNAPSHOT](research/E2631_DB_SNAPSHOT.md) §10.8），真机要等它才看得出"关联上但短时间不通"。**端到端（真关联、拔网线仍在线、1905 过无线）待真机** → H-01 | 4 |
| B14 | 真机：本机当子路由，接真主控（有线回程） | ✅ | 两台实验机打通。抓包确认**子路由自己会发** `0x0007` Search、主控回 `0x0008` 带 WSC（原判断"子路由被 cspd 门控"是错的，真因是 C6 那条打包漂移）；主控改 SSID 后子路由 `wlan-plan` 与 `iw dev` 跟着变 | — |
| B15 | `CmWLANMapSsidCfgSync2Repeater` 对齐（libcmapi 内部调用，替身命中不到） | ⬜ | 沙箱里是 no-op；要逆向或真机 strace → H-06 | 5 |
| B16 | **子节点的 `CurHop` 是守护进程自己更新的**：我们的启动路径只写 `-2`（注册表里的"未接入"），子节点方向再没有第二处写它 | ☑️ | 判据：子角色（ax3600，跑我们沙箱里那颗原厂守护进程）的日志里守护进程自己那行 cfg 读到 `mode=1 … cur_hop=2 max_hop=2`，而 `-2` 出自 `controller-run.c` 的子角色分支（换角色时 `controller-service.c` 往子节点方向写的也是 `-2`）⇒ 这个 2 只能来自守护进程：`slave_init` 的 `CfMultiapSetCurHop`，或主控 TLV `0x43` 采用的值（[FINDINGS](FINDINGS.md) 第 7 节两条路径）。**读数是一条命令**：`verify-install.sh` 的 `cur_hop=`；**四种形状不判**——主控那一列本来是我们自己写的（只当证据）、`no-cfg-line`（日志环已把那一行翻出去，实验台跑久就是这一种）、`-98`（守护进程自己读不到那一列）、**那一行还太年轻**（脚本按行末 `epoch=` 报 `cur_hop_age=<秒>`，小于 `CUR_HOP_GRACE`＝默认 120 秒只报不判；算不出年龄照判，不放过——子节点重启后要在"还没接入"上待约半分钟）。逐条见 [INSTALL](INSTALL.md) 第 3 节。**还分不开的一种**：主控 `CurHop` 恰等于本机 `MaxHop` ⇒ "设成 MaxHop"与"采用主控下发的值"读数相同——**开关已经有了**（B16b）：往 `/run/zte-mesh/db-profile` 写一行 `MultiAPCfg 0 CurHop 5` 重启服务，主控启动路径本来不写这一列所以它能活下来，`verify-install.sh` 用 `db_profile_cur_hop` + `master_cur_hop` 证明这个 5 真进了主控自己的行（相等才判，`absent`/`not-master`/`no-cfg-line`/`not-read`/太年轻五种形状不判）；剩下的只差开一次真机窗口去读子节点的 `cur_hop` 与 `max_hop` → [PLAN](PLAN.md) H-14 第 ⑦ 步。原厂侧同一结论：E2631 实测 `CurHop=2=MaxHop` 且 `EnMasterOnly=0`（[E2631_DB_SNAPSHOT](research/E2631_DB_SNAPSHOT.md) §6.3 与本文末尾的结案注） | — |
| B17 | **两个开关**：`mesh_role` 决定本机是主控还是子节点，`apply_local` 决定主控要不要把同一份档案落到本机；旧名 `main`/`auto` = 主控 + 落地 | ✅ | 判据：`role-master` 不写 plan、`role-local-sig` 写 plan 且 `restart.sig` 变、`role-slave-local-keeps-plan` 不碰子路由 plan、两个 legacy 别名各出一份；VM 上切角色后 `wlan-plan` 与 `wlan-sync` 实例一起出现/消失（**一个 plan 只有一个生产者**）。完整语义见 [SERVICE](SERVICE.md) | — |
| B18 | **cspd 替身**：控制器侧自己发 `0x0007` Search、并代答子路由的 Search（`0x0008`） | ✅ | `tools/lab/enroller.c` → `/usr/libexec/zte-mesh/mesh-search`，init 在非 `slave` 角色下多起一个 `search` 实例，带 `--al-mac 02:00:00:00:19:<al_last>`（子路由的 M1 发给控制器 AL MAC）。`--search` 每次启动只发**一帧**，所以周期就是"多久收一次新子节点"（已从 `86400` 改成 `300`，最坏等 5 分钟）；实测不抖（`plan applied nothing new`、`daemon_restarts=0`）。判据：装包后真机自动入网，不再需要手工 enroller | — |
| B19 | **控制器地址末字节可配**（UCI `al_last` / 页面"控制器地址末字节"）：两台设备 LAN MAC 相同时手工错开 | ✅ | init 的 `al_last` 分支（空 / 大小写 / 非法值，VM 上 busybox ash 实测）；只暴露末字节，前五字节固定为子路由准入检查认的形状。两台**必须**不同，见 [LAB](LAB.md) 4.1 | 1 |
| B20 | 页面上本机型号取系统型号（`/tmp/sysinfo/model`）、名称取主机名，取不到都显示 `router` | ✅ | `controller_self_identity()`（`controller-run.c`）→ `state.json` 的 `self`；子路由行缺型号/名称时同样显示 `router` | 1 |
| B21 | `mesh-search` 的 `0x0007` 补 End of Message TLV；线上帧用 Wireshark `ieee1905` 逐字段核对过一遍 | ✅ | 沙箱 90 秒抓 34 帧五类消息：改前 12 个 `0x0007` 全被标 `[Malformed Packet]`，补三个 0 后 0 malformed 且对端照样回 `0x0008`；`0x8006` 的 TLV 编码与 dissector 解析一致；`0x0008` 少的 EOM/`0xb3` 故意不改（Z506 验过的形状）。见 [WIRESHARK_1905](research/WIRESHARK_1905.md) | 1 |
| B22 | 查证原厂内核（E2631 2024-09 `vmlinux`）**不含 mesh 协议实现**，也不含 rootfs 解密 | ✅（查证，无代码改动） | 1908 条 `__bug_table` + 6393 个导出符号恢复出函数级地图：`zx_denali_nand.c` 读页路径只有 Denali 寄存器序列与 DMA、无 crypto 调用；内核无 `ecb(aes)`/`cbc(aes)` 模板实例、`"aes"` 字符串零引用；`wlan_get_wlan_multiap_mode()` 全函数只有"读一个 u32"；`br_ieee1905` 钩子对 1905 组播帧在**转发**路径返回 DROP（本地投递走 `br_ieee1905_local_in_handle` 放行）；`CBC decrypt OK!` 位于 Bootloader 分区。见 [MESH_INTEROP](research/MESH_INTEROP.md) §4.1 与 §5 | 0 |
| B23 | 摸清**原厂 mesh 的 1905 协议实体是 `multiapd`，不是 cspd**；它未 strip，可直接按函数名调试与逆向 | ✅（查证，无代码改动） | 第三方同型机 `ps` 显示 `cspd` 与 `multiapd` 是两个进程——我们此前 22 个成员**漏取了 `multiapd`**。该二进制 988 140 B / 6835 符号 / 2184 具名函数：`I5Tlv*` 360 个（TLV 编解码全在此，不在任何 `.so`）、M1/M2 两端对称（`_acdcSendApAutoConfigWSCM1` / `_acdsRcvApAutoConfigWSCM1` / `_acdcRcvApAutoConfigWSCM2`）、`g_ieee1905_sockfd` + `bind socket to br0` 证明 1905 走用户态 raw socket。自带 `pktdump 1`（落 `/var/tmp`）、`setrole`、`querydev`、`showMsgStatistics`，**设备无 tcpdump 也能抓 1905**。见 [MULTIAPD](research/MULTIAPD.md) | 0 |

## C. 打包与分发

| # | 功能 | 状态 | 证据（怎么算成立 / 为什么这么做） | 难度 |
|---|---|---|---|---|
| C1 | 单个 apk（runtime + init + wlan-sync + rpcd + LuCI，压缩约 3.9 MB；装机读数只看 `apk info -s`，[INSTALL](INSTALL.md) 第 1 节） | ✅ | 真路由器上 procd/rpcd 全通 | — |
| C2 | 拆包：`zte-mesh`（`arch: all`）+ `zte-mesh-launcher`（aarch64，只装 `ns-run`/`mesh-search`） | ✅ | **只做了"让 x86_64 能直装"这一半**。**为什么必须两个目录**：`package-pack.mk` 的 `Architecture` 读 Makefile 级全局 `PKGARCH`，一个 Makefile 给不了两个架构。判据：VM 上 `apk add zte-mesh` 直装且不覆盖自编的 `ns-run`（`launcher` 报 `uninstallable` 属预期）。原方案的 controller/agent 拆分仍未做 | 1 |
| C3 | 统一行尾（`.gitattributes`，`* text=auto eol=lf`） | ✅ | 判据：`git ls-files --eol` 索引全 `i/lf`、二进制标 `-text`；只有 Windows 工作区的旧副本是 CRLF（重新检出即消失），Linux/CI 上一直是对的 | — |
| C4 | **中文翻译包随 CI 构建**：`luci-i18n-zte-mesh-zh-cn` 之前只配 `=m` 没编译，中文从不产出 | ✅ | 判据：两台装完后 `zte-mesh.zh-cn.lmo` 就是包带来的 | — |
| C5 | **包里的 `ns-run` 与 init 不同步**：tarball 里是旧版，不认 `ZTE_MESH_QEMU`/`ZTE_MESH_EMULATED_LAB`，无 `CONFIG_COMPAT` 的 aarch64 上 `execv` 直接 `errno=8` | ✅ | 修法是**消除漂移本身**：tarball 额外带 `ns-run.c`/`controller-net.c`/`enroller.c`，`Build/Compile` 用 `$(TARGET_CC)` 现编。判据：`test_runtime.py` 比对源码副本，重打包漏了就在本机失败 | — |
| C6 | **tarball 里的 `db-engine-probe` 冻结在打包那天**：ARM32 半边只能 `-nostdlib` 对着原厂 glibc 2.26 编、包里编不了；冻住的那份不认 `mesh.role=slave` → 子路由的 controller 实例每 5 秒退一次 | ✅ | 判据：`test_runtime.py` 断言包里那份必须带 `config-reject`、`wlan-plan`、`bss_detail`，且 `libzte_controller_trace.so` 必须带写侧才有的逐路文件名格式串 `%02x:….sta`——**副本落后表现为"回落到摘要"这种不像故障的错法**，所以守卫只能按字符串钉死。重打走 `openwrt/repack-runtime.py`，随后补跑 `make-runtime.py` 跳过的 GNU 符号版本审计（[LAB](LAB.md) 3.0） | 1 |
| C7 | **全新装包后服务起不来**：`qemu-arm-static` 不在包里，而 `CONFIG_COMPAT` 探测在官方固件上必然误判（不导出 `/proc/config.gz` → `grep -c` 返回 0 → 一律走模拟器） | ✅ | 判据：读不到 config 就 exec 一次 `$ROOT/lib/ld-linux.so.3` 看它有没有打印 usage（**"没有答案"不等于"不支持"**），全新装包的服务能起。**ARM32 模拟器不入包是取舍、不是待办**（OpenWrt 不编它、必须与宿主架构匹配、约 11 MB 比整个运行环境包还大、只有 chroot 里的 probe 需要它）：判定表与三条出路见 [LAB](LAB.md) 第 4.2 节 | 1 |
| C8 | 装机手册：装哪四个包、页面点哪里、`mesh_role` / `apply_local` 两个开关怎么摆出 S1/S2/S3、怎么确认成功、卸载与"设了没动静" | ✅ | 判据是**照着走一遍走得通**：[INSTALL](INSTALL.md) 九节在两台实验机上原样跑过、输出与原话逐条对，改口八处（`opkg` 这台机器上没有、四个 apk 一条 glob 在 x86_64 上一条都不装、`apply_local` 没写时一行都不出、`multi-ap` 不是"读成 off"而是整个配置被拒、`/usr/lib/zte-mesh` 会随 `apk del` 一起删、走查中发现的用户可见行为"子节点每次重启服务把空中名称翻两次"＝两次掉线 等）。八处逐条与剩下那半（S1/S2 要真中兴设备）见 [PLAN](PLAN.md) 批 6 | 1 |
| C9 | CI 不再发布 release：只构建 + 校验和 + artifact，发布改手工 | ✅ | 判据：workflow 里 `gh release` 不再是任何一步（只剩注释与 summary 那一句）、`permissions: contents: read`；`DEVELOPMENT.md` 第 8 节是"只构建 + 三条手工发布步骤"。CI **全手动触发**，push 不发起构建 | — |
| C10 | 装机验收脚本：一条命令装四个包 + 读回版本 + 跑三条冒烟 + **读回装机标记** | ✅ | 判据：`tools/lab/verify-install.sh`（busybox ash）输出 `KEY=VALUE` 证据行 + `RESULT=PASS/FAIL`——`apk add` 每包一条（架构不合算 `uninstallable`、文件不在算 `skip`，都不算失败）→ 重启 rpcd 与 `zte-mesh` → 读回四个版本 → 三条冒烟（服务与角色；角色应有的实例集合：controller/attach 恒有、wlan-sync 看 `apply_local`、search 看 `mesh_role`；档案落地：`wlan-sync.status` 的 `result` 加 plan 里每个启用行的 SSID 落在哪个 `wifi-iface` 上）。**装机标记只能按字符串认**：apk 打包会剥掉 ELF 节，设备副本比不了哈希——`trace_per_bss_name`、`probe_bss_detail`、`init_seed_from_air`、`sync_flag_by_meaning` 算断言，`peers_sta` / `peers_software` / `peer_reports_stations` / `state_bss_detail` 只算证据（`.metrics` 里没有 `stations=` 时"没有 `.sta`"什么也不说明，不是解码器的失败）；这台机器没解过包时打印 `unreadable`、不判失败——环境事实不该变成批次判决。判定逻辑由 `sh tests/test_verify_install.sh` 在主机上守（owrt 上 busybox ash 同样条数）。**装 r18 两台都 `RESULT=PASS`**：owrt（主控）13 项、ax3600（子节点）14 项 | 2 |
| C11 | **装机验收读回"这一次重启写了几次无线"**（B8b 的判据入口）：`verify-install.sh` 第四条冒烟 `applier_writes` | ✅ | 数的是日志里 `applied; wireless committed` 的行数**增量**，窗口就是脚本自己要求的那次重启。**为什么不能比文件**：旧种子先把空中改名、主控的 M2 再翻回来，前后 `/etc/config/wireless` 一模一样，可家里已经掉了两次线——只有落地器自己那行留了痕。**判 0 的前提**是 `air_matched_plan=yes`（还在收敛就只报数不判，否则把一次正常收敛说成故障）；另有四种不判形状 `no-restart` / `no-applier`（纯主控本来没有落地器）/ `no-logread`（读不出来当成 0 行，而 `0 - 0` 正像一次安静重启）/ `wrapped`（窗口被截断，增量不是个数），逐条见 [INSTALL](INSTALL.md) 第 3 节。`RESTART_NOW=1` 让**不装机也能驱动这一次重启**（会真掉线，所以默认关），这就是 [PLAN](PLAN.md) H-14 的一条命令。**第一个真判决**：装 r18 的重启窗口里 ax3600 给出 `air_matched_plan=yes` + `applier_writes=0`（判据成立，见 B8），owrt 给出 `applier_writes=no-applier` | 1 |

## D. 换子路由固件才谈得上的（原判据只有 Z506）

判据原来只有 Z506（Wi-Fi 5 老固件）。2026-11-01 从一台市售 AX3000（E2631，2026-08 固件）拿出明文用户态后，D5/D6/D7 从"没有通道"变成"厂商有入口"，其中 **D7 的接收侧处理路径是齐的**；表名与列名从 `cspd` 的 schema 字符串表 + web 模块 `.lua` 两条独立路径交叉得到，**D3–D7 各自"该改哪个字段"不再是猜测**；强度分级（强/中/弱）见 [VENDOR_MESH_STACK](research/VENDOR_MESH_STACK.md) 第 5 节。

| # | 功能 | Z506（原判据） | E2631 / AX3000（2026-11-01 补） | **配置库入口（表.列）** |
|---|---|---|---|---|
| D1 | 带宽 20/40/80/160 | 不写 `BandWidth`，也不关"自动信道" | 未找到新证据，不变 | — |
| D2 | 访客限速 | 1905 里没有这个字段 | 协议外的东西，不变 | — |
| D3 | 信道扫描 / CAC | 无接收代码 | 有 `SendCAC` / `ChannelScan` **发送**入口，接收侧没查（弱） | `MultiAPMaster.InChannelScanRptFlag=0` + **`MultiAPCfg.EnReportCenterChannel=1`**（均实测，`En1905Msg=1` 也开着）。**上报本来就是开的**，缺口在接收侧 |
| D4 | VBSS（虚拟 BSS 创建/销毁/迁移） | 无 | `cspd` 有 `EnVbss` / `ToVbssMode` 配置位，但**没有任何 CM/CF 导出**（中） | **`MultiAPMaster.EnVbss=0` / `.ToVbssMode=1`**（实测，与 `EnBtmKickoff` 同表） |
| D5 | VLAN / 流量分离 / Profile-2 | 无 | `CfMultiAPSetVlanCfg`（152 B 实函数，`ASEND 0xa94e`）——有下发入口（中） | 编译期 `CONFIG_CSP_WLAN_VLAN` + `EBTABLES_{VLAN,FFE_T}` + 下发入口三条都在；**配置层实测为零**，三处落点全关：`EthVlan`(#79) **0 行**、`Brport`(#20) **16 口 × 8 个 VLAN 列全默认**（⇒ 出厂是一张单广播域、零划分的网）、**`WLANCfg` 每行 7 个 per-VAP VLAN 列全 0**。**入口已定位到两侧：VAP 打/剥 tag 在 `WLANCfg`（`Vlan`/`VlanPriority`/`UpTagEnable`/`DownTagStripEnable`/`McastVlan*`），端口默认 VLAN 在 `Brport` 那 8 列**。厂商启用时写哪侧仍未解（web `.lua`）。另：`MultiapCapability` 只有 2 列（`SupportBSSync=1`、`SupportPackageProduct=0`，后者与 EasyMesh packaged-product 的对应**仅命名联想，弱**） |
| D6 | STA 绑定 | 无 | `CmMultiApSetStaBind`，走 `0x2401` wlan 本地命令，**不是 1905 通道**（中） | **五张准入/绑定表全部 0 行**：`P1905StaBind`(#168)、`MultiAPStaCfg`、`MultiAPCacheMac`、`WlanRoamingSta`(#271)、`AclCfg`(#5)。**入口存在、出厂未配**。另：`WLANSetting.ACLIsFrom1905Slave=0` ⇒ 本机**不收** 1905 侧同步的 ACL，同向证据 |
| D7 | 802.11r/BTM 踢人（部分） | 固件不支持 FT | `handle_wlan_multiap_steering_req`、`wnm_send_btm_req_raw`、`En11rSteer` 都在（**强**）。内核侧 `br_ieee1905_local_in_handle` 放行本地投递，帧能到 `multiapd`；而它自带 `pktdump`，线上取证不必再自带抓包程序 | **`MultiAPMaster.{EnBtmKickoff=0,EnRoamKick=0,En11rSteer=0,11rSsidSync=1,EnActiveDeauth=1}`**（**出厂踢人走 `EnActiveDeauth=1`、BTM 那条是关的** ⇒ 与厂商线上行为比对时该预期 deauth/disassoc，而不是 11v 的 BTM Request）+ `MultiAPDomain` 的 106 列状态机参数 + `MultiAPRoamCatonCfg.BtmCacheEn=0`（全部实测）+ `WlanRoamingCfg`（实测，**与 11r 无关**：六列是客户端弱信号重关联参数 ⇒"固件不支持 FT"有编译期（`CONFIG_CSP_WLAN_SUPPORT_80211KV` 只有 11k/11v）+ 配置层两条独立证据，不必再找 11r 的配置入口） |

### D 节的取证结论在哪、哪些只在总账里

八轮只读取证（22 张表 + `DB all` 的 276 张表名全集）的**过程、逐轮读数与五处自纠**都在存档里，本表不再复述：
[E2631_DB_SNAPSHOT](research/E2631_DB_SNAPSHOT.md)（逐轮结果，含 §13"配置库解决不了的那几件"）、
[E2631_DB_TABLES](research/E2631_DB_TABLES.md)（mesh 相关的表是 **19 张**（10 `MultiAP*` + 9 `P1905*`）而不是 5 张；`MESH`/`Netsphere`/`EASYMESH` 不在 276 张里，只当 web 层名称映射）、
[E2631_ROOTFS](research/E2631_ROOTFS.md)（整块 rootfs 与 `/etc/autouserconf` 的 612 个编译期开关）、
[VENDOR_MESH_STACK](research/VENDOR_MESH_STACK.md) 第 5 节（分级）。上表右列就是那八轮的结论。

结掉的几条线（读数与推证过程在存档，这里只留结论）：**角色是静态配置出来的 Slave**（`MapCooperatorCfg` 与 `MultiAPCfg` 六个开关同向，不选举、不自动开局、重启也不改主意）；**band 归属** `Radio.1`=2.4G、`Radio.2`=5G（`WLANBase` 两行同报 MT7916，与 [FINDINGS](FINDINGS.md) §10 记的 Z506 射频 MT7592/MT7613 不是同一套，"不支持 160"那句不能套到本机）；**`PortState=4` = 在跑**（配置侧 `WLANCfg.Enable` + 物理侧"只有 LAN2 插着网线"双证闭合）；**真实 SSID 名在 `MultiAPDomain.ESSID`（明文），`WLANCfg.ESSID` 与 `WLANPSK` 的密钥都被遮蔽成 `***`——名字可还原、密钥不行**（遮蔽按表/列做，不是全局策略）。

只有总账需要留下的三条（它们管的是本表怎么被读，不是读数）：

- **`sendcmd 1 DB p` 这条线已到头**：D3–D7 加角色、band、端口拓扑的**库侧入口**都是实测级，但**功能本身一项都没实测**，所以 D 节一项都不改成 ✅；换手段才动得了（位图/枚举语义 → 反汇编或 Multi-AP 规范正文；厂商启用 VLAN 写哪侧 → web `.lua`；1905 实际跑在哪几个接口 → `pktdump 1` / `/proc/net/dev`）。真机行为见 [PLAN](PLAN.md) H-07。
- **表名不许猜、行名不是表名**：先 `DB all` 列全集（`DB l` 不存在）；`IGD.P1905CFGn` 是 `P1905Cfg` 的**行名**；`RowCount=0` 只说明出厂没配，**不否证落点**；`P1905Cfg` 只能证明"允许跑在哪些接口"，不能证明"在跑"。
- **`isSave=0` 的列只能读状态、不能当配置比对**（写 DB 不落盘；`WLANSSID` 整表、`WLANCfg.IfName`、`MultiAPDomain.ESSID` 都是）——拿它当配置依据会判错，反过来"改了不生效"也别急着说固件不认。同一台设备上"支持某能力"一律指"编进去了"，不指"开着"。

> **B16 已结案（一句话）**：`MultiAPCfg.CurHop=2=MaxHop` 且 `EnMasterOnly=0`，与 [SCHEMA](milestones/SCHEMA.md) 的"`EnMasterOnly=1` 时 `slave_init` 跳过、`CurHop` 不会被设成 `MaxHop`"条件句三者自洽——不是"读到一个值"式的结案。存档见 [E2631_DB_SNAPSHOT](research/E2631_DB_SNAPSHOT.md) §6.3。
