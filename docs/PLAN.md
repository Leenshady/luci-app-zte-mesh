# 开发计划：让 OpenWrt 与中兴路由器组网

> 这份文件是**唯一排期表**：先做哪几件、每件怎么算做完、哪些必须等真机。
> 功能本身的状态与证据在 [FEATURE_STATUS](FEATURE_STATUS.md)（唯一总账），协议与逆向在
> [research/](research/)，装机与踩坑在 [LAB](LAB.md)。三处不重复列同一件事。
>
> 最后更新：2026-11-01（批次 3 完成：A17a/A17b；A17c 等真机）

## 1. 终点：四个可验收的场景

插件要交付的不是"能跑起来"，是下面四个场景。每个场景一条判据，判据都是**看得到的现象**，不是日志里的
"status=0"。

| # | 场景 | 拓扑 | 现在 | 判据（怎么算过） |
|---|---|---|---|---|
| **S1** | 主控 | OpenWrt 有线网关（`master`）→ 中兴子路由（原厂固件，有线回程） | ✅ 已在真机 Z506 上过 | 中兴子路由的 SSID/密码/信道/ACL 跟着页面变；节点动作（重启、指示灯）生效 |
| **S2** | 子节点（有线回程） | 中兴主控 → OpenWrt（`slave`），一根网线 | ☑️ 两台实验机打通 | 中兴主控改 SSID，20 秒内 OpenWrt 的 `iw dev` 跟着变，且改回来能回退 |
| **S3** | 子节点（**无线**回程） | 中兴主控 ⇝ 无线 ⇝ OpenWrt（`slave` + `backhaul=multi_ap`） | ⬜ 只有 AP 半边 | 拔掉网线，OpenWrt 仍能拿到配置、能上网（4 地址回程 BSS 关联成功） |
| **S4** | 多子节点 + 可见 | S1/S2 加第二台子路由、页面显示拓扑与终端 | ⬜ 单节点 | 页面列出两个及以上节点、每个节点的回程 RSSI 与终端数，与真机对得上 |

S1 是"能用"，S2 是"能当子路由"，S3 是"不用网线"，S4 是"能运维"。批次计划围绕这四个场景排。

## 2. 这一轮定下的开发规则

1. **主机可测的项先做**。判定标准是：这件事的结论能不能只靠仓库里的代码 + 测试桩 + 构造的帧/配置得出。
   能 → 直接做、直接测；不能 → 只登记到第 5 节，不占当前批次的时间。
2. **需真机项不阻塞**。登记要写到"谁来验、几步、判据是什么"，这样等真机到位时不用重新想。
3. **CI 按批跑，不按项跑**。一批（3–5 项）做完才手动触发一次 `build-apk.yml`，然后装机验一次。
   单改一个文件就烧一次 20–30 分钟的构建、再装一次机，是把时间花在等上面。
4. **一批的收尾固定三件事**：本机能跑的测试全绿（第 4 节的命令）→ 文档同步（FEATURE_STATUS 的
   状态列 + 本文件批次表 + `README.md` 索引）→ `commit`（不推送，攒到批末一起推）。
5. **不许用"编译通过"当验收**。这个仓库有过三类假通过：`(void)write` 在 GCC 11 下编不过、
   `tcpdump` 流式输出切错列把 771 字节的 M1 读成"最大 68 字节"、`state[0]:3` 被当成故障。
   每项都要留一条能重复跑的判据。

## 3. 批次计划

状态列：✅ 已完成 / 🔄 进行中 / ⬜ 未开始 / ⏸ 等真机。类型列：**H** = 主机可测，**D** = 需真机（登记在第 5 节）。

### 批 1 · 子路由侧落地保真（H）

子路由把控制器下发的档案**照原样**落到本机。现在只落主 BSS，额外槽位（访客/备用/隐藏回程）只写日志。

| 项 | 内容 | 类型 | 验收 |
|---|---|---|---|
| B11 | **已完成**。`wlan-sync` 应用额外槽位 1–3：每（频段,槽位）一个 `zte_extra_<band>_<slot>` 段，SSID/密钥/加密/开关/隐藏/网络归属都跟主 BSS 一致；plan 里没有的槽位删掉；`main` 角色的本机 plan 也带上 `zte_mesh_bss` 的行；`zte_mesh.main.extra_slots` 默认 **off** | H | `sh tests/test_wlan_sync.sh` 123、`sh tests/test_init.sh` 87、`node tests/test_luci.js` ✓ |
| C3 | **已完成**。`.gitattributes` 已存在且规则正确（`* text=auto eol=lf` + 二进制白名单），索引里 210 个文件全是 LF；只剩 Windows 工作区的旧 CRLF 副本 | H | `git ls-files --eol`（2026-11-01） |
| D0 | **已完成**。文档漂移：`DEVELOPMENT.md` §1 的老机器表（mac/SR1010/Z506）与 `LAB.md` §2.1 的三台台账冲突 | H | 现役台账只在 LAB 2.1，DEVELOPMENT §1 明确标成"2026-10 之前的旧环境，脚本默认值还指向它" |

### 批 2 · 主控侧可见性（H）

S4 的一半：页面能看见拓扑。数据面不碰无线，只把已经收得到的 1905 帧翻译成页面能读的字段。

| 项 | 内容 | 类型 | 验收 |
|---|---|---|---|
| A18a | **已完成**。`controller-1905.c`：CMDU 头 + TLV 走查 + ZTE 厂商子 TLV（`OUI 00:19:c6` + 3 字节 + `[类型 长度 值]`）。`state.json` 每个节点加 `stations`/`bss`/`hops`/`metrics_age`，`-1` = 该节点没上报（与 0 区分） | H | `tests/controller-1905-codec.c`：101 断言 + 10 万次有界变异，**含一段实验台抓包的原文帧**；`0x83`/`0x94` 的布局对照 tshark dissector 核过 |
| A18b | **已完成**。rpcd 的 `status` 本来就把整个 `state.json` 透传，字段自动可见；LuCI 节点表加「链路」列（直连/经中继，鼠标悬停看 BSS 数与上报新鲜度） | H | `node tests/test_luci.js` 新增 2 组断言（有指标 / 无指标）；`sh tests/test_rpcd.sh` 18 项 |
| A19b | **已完成**。`wlan-sync` 读包库判断 `wpad` 变体：`-basic` 上开 `backhaul=multi_ap` 时把原因写进日志与 `wlan-sync.status` 的 `message`；完整 `wpad`、回程关、包库读不到三种情况都不误报 | H | `sh tests/test_wlan_sync.sh` 新增 5 项 |

### 批 3 · 控制面（H 建模 + D 效果确认）

踢终端、封锁、BTM 引导。协议侧能构造、能断言线上字节；**对端是否照做只能真机看**。

| 项 | 内容 | 类型 | 验收 |
|---|---|---|---|
| A17a | **已完成**。`controller-steer.c`：`0x8014` 引导请求（flags/window/timer/STA 列表/目标 BSS 列表）、`0x8016` 封禁（封/放行 + 有效期 + 多 MAC）、`0x8015` BTM 结果与 `0x8017` 引导完成的解码。字段顺序以 Wireshark dissector 为准（flags 在窗口之前、每个列表的计数贴着列表），见 [STEER_1905](research/STEER_1905.md) | H | `tests/controller-steer-codec.c`：整帧字节比对 + 越界/容量/参数拒绝 + 10 万次有界变异；`tests/test_steer_tshark.sh`：tshark 逐字段读回 20 项，且无 malformed/多余 TLV 数据 |
| A17b | **已完成**。状态机 `controller-actions.c`（KICK 5 秒一次最多 3 次、BAN 每 120 秒重发、到期或规则被删时发"放行"、KICK 收到回报即结案）；`controller_client_action()` 把 `0x8014`/`0x8016` 发给每个在线子节点；`0x8015`/`0x8017` 经 trace 落成 `peers/<client>.btm`。配置链：`zte_mesh_client` 区段（`mac` + `action`，最多 8 条，同一地址不许重复）→ `ban.mac`/`kick.mac`，`ban_minutes`（0..1440）→ `ban.minutes`；页面有「封禁 / 踢一次」列表与时长字段，状态框显示每个动作的进度。**装机后又抓到两个只在真节奏下才现形的 bug**：①`clients[].left` 用 `time(NULL)` 减 `CLOCK_MONOTONIC` 的截止时间，报出 `-1793371222`（已把时钟放进 `action_table`，调用方再也传不进来）；②定时封禁到期后条目被删掉，下一秒配置又把同一条规则建回来——客户端被"封禁 / 放行 / 封禁"每分钟循环，现在到期改成 `expired` 并留着这一行。此前还有两个：配置每秒重读会把封禁到期时间每秒往后推，以及规则撤回后一秒内又加回来会先放行再封 | H | `tests/controller-actions-test.c` 215 断言：假时钟跑完重发、去抖、到期解锁、规则删除的撤销、失败上报、每秒重配置不滑期、改时长重新计时、撤回又加回取消撤销；`sh tests/test_init.sh` 新增 7 项（重复地址、坏动作、多播 MAC、超上限、时长越界）；`node tests/test_luci.js` 新增 2 组。**实验室控制器（owrt r13/r14）上实跑过**：`ban.mac` 进配置文件 → 日志 `client_actions rules=1` → `clients[]` 从 `sent` 倒计时到 `expired`、删规则后立刻清空 |
| A17c | 真机确认：一台手机关联后被踢、被拒重连 | D | 见第 5 节 H-03 |

### 批 4 · 无线回程（H 配置半边 / D 数据面）

S3 的前半步。netifd 侧要生成回程 STA 的 `wifi-iface`（`mode=sta`、`multi_ap`、`wds`），
这一步不看射频也能断言生成的配置；关联成功与否只能真机。
**装机实测（2026-11-02，ax3600 的 ucode 版 wireless）纠正了一个前提**：客户端的 4 地址与入桥只看 `wds`，
`multi_ap` 在 sta 模式只是翻译成 wpa_supplicant 的 `multi_ap_backhaul_sta`（决定关联能不能被回程 BSS 接受）。
所以一段回程 STA 必须同时写 `multi_ap 1` 和 `wds 1`，只写前者会卡在 `BRIDGE_NOT_ALLOWED`。证据见
[AGENT_PROBE](research/AGENT_PROBE.md) §15。

| 项 | 内容 | 类型 | 验收 |
|---|---|---|---|
| B13a | **已完成**。`wlan-sync` 生成 `zte_backhaul_sta_<band>`：由 plan 的回程行驱动，`mode=sta` + `multi_ap 1` + `wds 1`，名称/密钥/加密取 plan 那行（不写 `hidden`，那是 AP 的概念）；由 `mesh_role` 决定写哪一端，换角色时把对端那段还回去；行里声称加密却没有密钥时两端都拒绝应用 | H | `sh tests/test_wlan_sync.sh` 新增回程客户端区块（映射 15 项、幂等、两端互换、缺密钥、禁用行、`role main` 仍走主控端） |
| B13b | **已完成**。4 地址模式的开关就是 `wds`（见上），`wpad` 变体检查扩到客户端：`wpad-basic` + 回程开 + 本机是子节点时同样把原因写进日志与 `status.message`；`zte-mesh.init` 在 `backhaul=multi_ap` 但本机是主控且 `apply_local=0`（这个开关无事可做）时启动说一句 | H | 同上 + `sh tests/test_init.sh` 新增 3 条源码针 |
| B13c | 真机关联、断网线仍在线、1905 帧过无线 | D | 见第 5 节 H-01 |
| B13d | **已完成**。页面的 `backhaul` 以前只在子节点角色渲染（`if (child)`），可它在"主控 + `apply_local=1`"上同样有作用（那一端建回程 BSS）。现在和 `extra_slots` 一样按 `child || local` 渲染，标题与描述分两端（子节点=连过去 / 主控=放上天），纯主控两个开关都不显示 | H | `node tests/test_luci.js` 三种角色组合的可见性断言 + i18n 12 项（三条新文案已进 `po/zh_Hans`） |
| B13e | **已完成**。`backhaul` 的取值校验：`write_config` 只接受 `off` / `multi_ap`。`wlan-sync` 认不出的值当 `off`（应用侧不该为一个拼写停掉整份档案，这条留着），但 `multi-ap`（中兴自己文档里的写法）、`on` 这类会变成"页面开关设过、射频上什么都没有"，而只有 init 还能说清为什么 | H | `sh tests/test_init.sh` 坏取值两条（`multi-ap`、`on`），空值按默认 `off` 收 |
| B13f | **已完成**。**applier 的四个输入一起进 `restart.sig`**（`backhaul` / `extra_slots` / `wifi_2g` / `wifi_5g`）。判据一条：`wlan-sync` 直接从 UCI 读、又不改 plan 也不改实例集合的东西，改了必须重启才看得见——那个 watcher 比的是 plan 的**内容摘要**（`stamp()`）。最坏的一条不是回程而是 `extra_slots`：子节点的 plan 里槽位行本来就有，关掉开关却不重启，子路由会继续广播一个已经不该存在的 guest。带值的是 `sync_runs` 的两个角色（子节点 / 会落地的主控），与 procd 起 `wlan-sync` 实例那一行共用同一个判据（以前两处各写一遍，抽成 `sync_runs`）；纯主控一个都不带 | H | `sh tests/test_init.sh` 122 项：`sig-*` 十一条（两端各自 off/on、显式默认与不设等价、纯主控四种写法签名相同且不带）+ 三条源码针 |
| B13g | **已完成**。`wifi_2g` / `wifi_5g` 以前在包里只有注释掉的一行 UCI，页面上没有字段——可 `wlan-sync` 在"一个频段两块同类型射频"时明确让用户去设它（ax3600 正吃这一条：SoC 的 5G 旁边还有一块 PCIe 5G）。现在和 `backhaul`、`extra_slots` 一样按 `child \|\| local` 渲染两个 `wifi-device` 段名输入，共用一条描述（含"填了会重启 applier"），保存前校验段名形状（字母/数字/下划线；写错的设备名只会被 `wlan-sync` 忽略并留一行日志，那是另一种"设了却不动"） | H | `node tests/test_luci.js`：三处可见性（子节点 7 项含两个 pin、会落地的主控 38 项、纯主控四个都不给）+ 形状校验 7 个取值（空 / `radio1` / `radio_hwmode11g` 收，`radio-1` / `radio 1` / `radio.1` / `"radio1"` 拒）；`test_init.sh` 的"每个可选项都写一遍"用例带上两个 pin，`restart.sig` 期望随之变成 `pin2g=radio2 pin5g=radio1`；i18n 12 项（四条新文案进 `po/zh_Hans`） |

### 批 5 · 多子节点与稳定性（H + D）

| 项 | 内容 | 类型 | 验收 |
|---|---|---|---|
| A21 | 多个 peer 并存：`peers/` 目录、`state.json` 汇总、动作按 MAC 路由 | H | 用两段构造的 CMDU 喂进 trace，断言两个节点都出现 |
| A22 | 长时间稳定性：服务重启计数、plan 抖动抑制、日志轮转 | H | 实验室跑 30 分钟无 `daemon_restarts`（`dev-service-smoke.py`） |
| A23 | 真机：两台子路由同时在线 | D | 见第 5 节 H-04 |

### 批 6 · 发布与工程化（H）

| 项 | 内容 | 类型 | 验收 |
|---|---|---|---|
| C8 | 安装文档：两个开关（`mesh_role` / `apply_local`）的最短上手路径（装什么包、改哪两项、在页面点哪里） | H | 按文档从裸机走到 S1/S2，中途不查别的文档 |
| C9 | **已完成**。`build-apk.yml` 去掉 release 发布，只留 artifact + 校验和；release 改为手工 | H | workflow 里 `gh release` 不再是任何一步（只剩注释与 summary 里那句"要发布就手工跑这条"），`permissions: contents: read`；`DEVELOPMENT.md` 第 8 节的发布段同步改成"只构建 + 三条手工发布步骤"（以前写的 `release_tag` 输入已经不存在了） |
| C10 | 装机验收脚本：一条命令装四个包 + 读回版本 + 跑三条冒烟 | H | 在两台实验机上各跑一次，输出可贴进证据 |

### 插队 · 2026-11-02 装机反馈

| 项 | 内容 | 类型 | 验收 |
|---|---|---|---|
| A24 | 有线口掉线自愈（`attach` 兼任看门狗） | H | 主机测试 3 项守住判据；真机复现见 H-10 |
| A25 | `wlan-sync` / `search` 的 respawn 预算写成显式 `3600 5 0` | H | ✅ 主机测试；实测 retry 0 = 不限次，裸 `respawn` 半小时后永久放弃 |
| A13′ | 角色拆成 `mesh_role` + `apply_local`，线上不再发 `auto` | H | ✅ init 103 项 / rpcd 24 项 / luci / i18n；真机对照见 H-11 |

### 状态速览

| 批次 | 主题 | 状态 | 批末 CI |
|---|---|---|---|
| 1 | 子路由侧落地保真 | ✅ B11（210 项主机测试）/ C3 / D0 全做完 | **待触发**（批末一次） |
| 2 | 主控侧可见性 | ✅ A18a / A18b / A19b 全做完（回程 RSSI 无线索，见 H-05） | **待触发**（批末一次） |
| 3 | 控制面 | 🔄 A17a/A17b 全做完（能编能解、能发能停、页面能配能看）；**A17c 效果确认等真机**（H-03） | H-03 |
| 4 | 无线回程 | ✅ 主机侧做完（B13a/b/d/e/f/g：回程 STA 的 `wifi-iface` 两端都生成、`wds` 才是 4 地址的开关、换角色会回收对端段、applier 的四个输入都进签名且页面上都有字段）；**B13c 端到端等真机**（H-01） | H-01 |
| 5 | 多子节点与稳定性 | ⬜ | — |
| 6 | 发布与工程化 | 🔄 C9 做完（CI 只构建、发布改成手工三步，`DEVELOPMENT.md` 的发布段跟着改对了）；C8 装机文档、C10 装机验收脚本未做 | 装机 |

## 4. 主机测试口径

一台**没有任何真机**的开发机要能给出结论，靠的是这些（第一条是本轮实测的基线）：

| 命令 | 覆盖 | 基线 |
|---|---|---|
| `sh tests/test_wlan_sync.sh` | plan → `/etc/config/wireless` 的全部映射（假 uci），含回程两端 | 169 项（批 2 加 5 条、批 4 加回程客户端 39 条；Windows 上约 8 分钟，Linux 上快得多） |
| `sh tests/test_init.sh` | init 的配置校验分支与本机 plan | 122 项（批 3 加 7 条、批 4 加回程告警 3 条 + `backhaul` 取值 2 条 + applier 四个输入的签名 11 条 + 共用判据 3 条源码针） |
| `sh tests/test_rpcd.sh` | rpcd `status`/`apply` 的输出字段 | 24 项（批 3 加六个 role/`apply_local` 组合；Windows 上要先解决上面那条 `python3` 占位程序） |
| `node tests/test_luci.js` | LuCI 表单与节点表：字段、角色可见性、槽位校验、链路列、设备动作 | `38 main / 10 slot / 4 ACL / 2 client / child 7`（批 2 加节点表断言，批 3 加动作列表与状态框，批 4 加回程开关的两端可见性与两个射频点名——纯主控 `apply_local=0` 时 `backhaul`、`extra_slots`、`wifi_2g`、`wifi_5g` 都不渲染；"每个选项都要有表单项"这一遍现在渲染**会落地的**主控，因为纯主控按设计就少几个字段） |
| `tests/controller-1905-codec.c`（`make test-host`） | 1905 解码：头、TLV 走查、厂商子 TLV、`0x83`/`0x94` | 101 断言 + 10 万次有界变异（需要一台能编 C 的机器） |
| `tests/controller-steer-codec.c`（`make test-host`） | 终端动作消息的编码与两种回报的解码 | 整帧字节比对 + 10 万次有界变异（同上） |
| `sh tests/test_steer_tshark.sh` | 把上面那几帧交给 tshark，逐字段读回 | 20 项；没有 tshark 时输出 SKIP 并退出 0 |
| `tests/controller-actions-test.c`（`make test-host`） | 终端动作的去抖、重发、超时、回滚、每秒重配置、到期不复发、时钟域 | 239 断言，假时钟 |
| `python tests/test_runtime.py`、`test_i18n.py`、`test_schema.py out` 等 | 运行包完整性、翻译、表结构 | 各自脚本末尾 |
| `sh tests/test_frames.py`（`python tests/test_frames.py`） | pcap 帧长/配对/周期 | 4 项 |
| `sh tools/build/container-make.sh test-host` | C 编解码（含 10 万次变异）+ 沙箱 | 需要容器或 Ubuntu（`192.168.1.191`） |

本机（Windows + Git Bash + Python 3.13 + Node 22）能跑前六行；C 那行必须借容器或 Ubuntu 机器。
**批末判定以全部行为准**，本机能跑的行先跑，C 那行在批末那一次 CI/Ubuntu 上补。

> Windows 上的一个坑：Git Bash 里 `python3` 常解析成 `WindowsApps\python3.exe` 那个"去商店装 Python"的占位程序，
> 它不报错也不输出。`test_rpcd.sh` 的 jsonfilter 桩走 `python3`，于是七条断言全成 `{"result":-2}`——**这是本机环境，
> 不是代码**。用一个指向真 Python 的 `python3` 垫片放进 `PATH` 就恢复 24 项全过（2026-11-02 实测）。

## 5. 真机待测台账（登记，不阻塞）

编号 **H-xx**。每条写清"验什么、前置、几步、判据"。到真机窗口时按编号取。

| 编号 | 验什么 | 前置 | 步骤 | 判据 | 风险 |
|---|---|---|---|---|---|
| **H-01** | S3：无线回程关联（B13c） | 一台能发 Wi-Fi 的 OpenWrt 当子节点（现在的 VM 没有无线）；中兴主控已开回程 BSS；子节点装**完整** `wpad`（ax3600 实装机是 `wpad-openssl` + `hostapd-common`，够用） | ①子节点 `uci set zte_mesh.main.backhaul=multi_ap` 并 `mesh_role=slave` ②插上控制器档案后确认 `uci show wireless` 里有 `zte_backhaul_sta_5g`：`mode=sta`、`multi_ap=1`、**`wds=1`** ③`iw dev <sta接口> info` 看 `4addr on`、`ip -d link` 同处，`brctl show`/`ubus call network.interface.wan status` 确认它挂在 `br-lan` ④拔网线 ⑤`iw station dump` 看 `Authorized`，`ping` 上游 ⑥无线侧抓 1905 帧 | STA 关联成功且 `4addr` 打开；拔线后仍能 ping；1905 CMDU 在无线侧可见。**已知会踩的点**：没有 `wds` 时 netifd 直接 `BRIDGE_NOT_ALLOWED`（`supplicant.uc:278`），`4addr` 也只看 `wds`（`:305`）——所以这一条只验端到端，不验映射 | 换 `wpad` 可能挤爆 flash；4 地址要求两侧都开，主控是原厂不可控 |
| **H-02** | B11 在真子路由上不炸：额外槽位落成多个 BSS | ax3600 + 中兴主控（或 OpenWrt 主控） | ①`extra_slots=on` ②主控配一个访客槽位 ③`iw dev`/`uci show wireless` ④客户端连访客 SSID | 多出一个 BSS、名字密钥对、能拿到 IP；关掉后 BSS 消失且 `wlan-sync.status` 有记录 | 信道/带宽被多 BSS 摊薄；`wpad` 版本不支持多 BSS 时会静默失败 |
| **H-03** | A17 踢终端/封锁真的生效 | S1 拓扑 + 一台测试手机（MAC 已知）；页面「封禁与踢除设备」里各一条 `ban`（可选时长）与 `kick` | ①封禁该 MAC，`state.json` 的 `clients[]` 应显示 `sent` ②终端重连看是否被拒 ③删除记录，看是否恢复 ④加一条 `kick`，抓 1905 看是否真有 `0x8014` 出门、有无 `0x8015` 回报，`clients[]` 变 `done`/`failed` ⑤必要时开 `EnBtmKickoff` 再试 | 封禁后拒绝关联；解封后恢复；`0x8014` 真的发出；BTM 发出后终端切换（不保证） | 会踢掉用户自己的设备；`EnBtmKickoff` 出厂是关的，可能根本不发；`0x8014` 的 BSSID 目前用广播（还没有"终端挂在哪个 BSS"的表），要确认子节点接受广播 |
| **H-04** | 多子节点 | 两台子路由 | A23 | 两个节点同时出现在拓扑页，动作按 MAC 不串 | 1905 域 ID 冲突、AL MAC 冲突（`al_last`） |
| **H-05** | A18 RSSI/终端数的真值来源 | S1 + 一台已关联终端 | 抓 1905 后与 `iw station dump` 对照。**已有负结果**（[CAPTURE_1905](research/CAPTURE_1905.md)）：有线链路的 107 帧里没有任何 RSSI，链路度量应答是 ZTE 定长模板；要看 RSSI 必须走无线回程 | 页面上的数值与真机读数的偏差在 ±3 dB 内或明确标注"来自控制器上报" | 原厂 `StaRssiRptThr` 出厂 -110（等于不上报），可能根本没有值 |
| **H-06** | B15 `CmWLANMapSsidCfgSync2Repeater` | 真机 + strace | 逆向或动态跟踪 | 沙箱里替身不再需要 | 需要停机窗口；前序项没做完不值得动 |
| **H-07** | D5 VLAN / D6 绑定 / D7 BTM 在 E2631 上的实际行为 | 一台市售 E2631/AX3000，停机窗口 | 用它自带 `pktdump 1` 抓线上，按 [MULTIAPD](research/MULTIAPD.md) §5 的命令表 | 三处配置入口能被厂商自己的 web 打开（说明运行时真的读） | 动的是别人的家用路由器 |
| **H-08** | 出厂 `VersionMode` 与"兼容模式"文案的对应 | E2631 页面 | 改页面开关，看 `MultiAPCfg.VersionMode` 与 `P1905ComCfg.Enable` | 文案 ↔ 字段一一对应 | 已有一半结论（[FINDINGS](FINDINGS.md)），缺厂商侧确认 |
| **H-09** | 长时间稳定性（A22 的真机版） | 两台实验机 | 装 rXX 后放 24 小时，看 `daemon_restarts`、内存、`dmesg` | 重启计数 0、无 OOM | 会占着机器 |
| **H-10** | A24 有线口自愈：删掉宿主这头的 `zmesh0` 后自己长回来 | 实验室控制器（owrt VM），页面能看到「有线接口」 | ①`zte-mesh status` 三项都在跑 ②`ip link del zmesh0` ③等 25 秒：页面先「未接入」再恢复「已挂到 LAN 桥」 ④`logread` 里 zte-mesh 先有 `restarting it to rebuild the wire`、随后 `attached zmesh0 to br-lan` ⑤`service list` 里 controller 实例换了 pid ⑥子路由重新出现在拓扑页 | 不需要人重启主控：两分钟内线自愈、拓扑重新填上，`multiapd` 的绑定跟着新 ifindex 走（这正是重启实例的原因） | `attach` 自己也被杀时自愈停摆——它的 respawn 同样是不限次；连续重启会短暂断开子路由的 1905 链路 |
| **H-11** | A13 角色改成 `master` 后，真中兴子路由照样入网 | ax3600（或 Z506）+ OpenWrt 主控，装机验证 | ①主控 `mesh_role=master`（不再发 `auto`）②子路由开机入网 ③`uci show zte_mesh` 与页面拓扑确认 ④主控改成 `apply_local=1` 再看本机 SSID 与子路由 SSID 同步 ⑤子路由重启一次，等 5 分钟内被 `search` 重新发现（A25 之后才可能） | 入网、下发档案、拓扑填充与 `auto` 时代一致；关键是**没有**出现子路由自当选主控（`MultiAPMaster` 的角色列仍是子） | 改动只在主控写库的 `SetRoleEnable` 与 `Mode`，子路由侧一行没动 |

## 6. CI 与装机节奏

### 6.1 触发原则

- **不按项触发**。改完一项只在本地跑测试，不烧 CI。
- **批末触发一次**：`gh workflow run build-apk.yml`（手动 `workflow_dispatch`），产物是
  `zte-mesh-apk` artifact（四个 apk + `SHA256SUMS.txt`）。
- **CI 不再创建 GitHub Release**。发布是人的决定：确认某一版要对外，再手工
  `gh release create <tag> dist/*.apk dist/SHA256SUMS.txt`。理由：流水线按批跑，跑出来的字节是
  "这一批的中间态"，不该自动变成对外承诺。

### 6.2 装机（批末一次）

| 机器 | 装什么 | 验收 |
|---|---|---|
| `192.168.1.20`（VM，x86_64，无无线） | `zte-mesh`（`arch: all`）+ 本地编的 `ns-run` | `ubus call luci.zte-mesh status` 返回角色；`/run/zte-mesh/wlan-sync.status` 有结果 |
| `192.168.1.1`（ax3600，aarch64，有无线） | 四个包（`--allow-untrusted`） | 同上；`slave` 角色下 `iw dev` 跟着主控改 SSID |

装完先跑 [LAB](LAB.md) 的三条冒烟，再跑本批新增的用例（页面开关、`uci` 选项）。

### 6.3 版本号

`make-runtime.py` / 包 `PKG_RELEASE` 在批末递增一位（r11、r12…），让"这台机器上装的是哪一批"
能从 `apk list` 读出来。不做自动版本号。

## 7. 已知的文档漂移（顺手收）

- `DEVELOPMENT.md` §1 的机器表是老的（mac `192.168.167.93`、SR1010 `192.168.5.1`），与 `LAB.md` §2.1
  的三台台账冲突。合并到 `LAB.md`，`DEVELOPMENT.md` 只留指针。（批 1 的 D0）
- `README.md` "没做或没验证"那段列的四件事已经过时两件（装包验收、LuCI 浏览器效果）。
- `FEATURE_STATUS.md` 的 C3 写成 ⬜，实际 `.gitattributes` 早已存在（批 1 的 C3 已核对）。
