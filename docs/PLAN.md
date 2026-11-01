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
| A17b | **已完成**。状态机 `controller-actions.c`（KICK 5 秒一次最多 3 次、BAN 每 120 秒重发、到期或规则被删时发"放行"、KICK 收到回报即结案）；`controller_client_action()` 把 `0x8014`/`0x8016` 发给每个在线子节点；`0x8015`/`0x8017` 经 trace 落成 `peers/<client>.btm`。配置链：`zte_mesh_client` 区段（`mac` + `action`，最多 8 条，同一地址不许重复）→ `ban.mac`/`kick.mac`，`ban_minutes`（0..1440）→ `ban.minutes`；页面有「封禁 / 踢一次」列表与时长字段，状态框显示每个动作的进度。**顺带修掉两个只在真节奏下才现形的 bug**：配置每秒重读一次会把封禁到期时间每秒往后推（一分钟的封禁永不结束），以及规则撤回后一秒内又加回来会先放行再封 | H | `tests/controller-actions-test.c` 215 断言：假时钟跑完重发、去抖、到期解锁、规则删除的撤销、失败上报、每秒重配置不滑期、改时长重新计时、撤回又加回取消撤销；`sh tests/test_init.sh` 新增 7 项（重复地址、坏动作、多播 MAC、超上限、时长越界）；`node tests/test_luci.js` 新增 2 组 |
| A17c | 真机确认：一台手机关联后被踢、被拒重连 | D | 见第 5 节 H-03 |

### 批 4 · 无线回程（H 配置半边 / D 数据面）

S3 的前半步。netifd 侧要生成回程 STA 的 `wifi-iface`（`mode=sta`、`multi_ap`、WPA3 与 SAE 的差异），
这一步不看射频也能断言生成的配置；关联成功与否只能真机。

| 项 | 内容 | 类型 | 验收 |
|---|---|---|---|
| B13a | `wlan-sync` 生成 `zte_backhaul_sta_<band>`：由 plan 的回程行驱动，`mode=sta`，`multi_ap` 与加密方式对齐，缺少 SSID/密钥时明确报错 | H | `sh tests/test_wlan_sync.sh` 新增 8+ 条 |
| B13b | 4 地址模式的开关与 `wpad` 变体检查：`wpad-basic` 上直接拒绝并说清楚 | H | 同上；`test_init.sh` 一条告警 |
| B13c | 真机关联、断网线仍在线、1905 帧过无线 | D | 见第 5 节 H-01 |

### 批 5 · 多子节点与稳定性（H + D）

| 项 | 内容 | 类型 | 验收 |
|---|---|---|---|
| A21 | 多个 peer 并存：`peers/` 目录、`state.json` 汇总、动作按 MAC 路由 | H | 用两段构造的 CMDU 喂进 trace，断言两个节点都出现 |
| A22 | 长时间稳定性：服务重启计数、plan 抖动抑制、日志轮转 | H | 实验室跑 30 分钟无 `daemon_restarts`（`dev-service-smoke.py`） |
| A23 | 真机：两台子路由同时在线 | D | 见第 5 节 H-04 |

### 批 6 · 发布与工程化（H）

| 项 | 内容 | 类型 | 验收 |
|---|---|---|---|
| C8 | 安装文档：三种角色的最短上手路径（装什么包、改哪两项、在页面点哪里） | H | 按文档从裸机走到 S1/S2，中途不查别的文档 |
| C9 | `build-apk.yml` 去掉 release 发布，只留 artifact + 校验和；release 改为手工 | H | workflow 里 `gh release` 零出现 |
| C10 | 装机验收脚本：一条命令装四个包 + 读回版本 + 跑三条冒烟 | H | 在两台实验机上各跑一次，输出可贴进证据 |

### 状态速览

| 批次 | 主题 | 状态 | 批末 CI |
|---|---|---|---|
| 1 | 子路由侧落地保真 | ✅ B11（210 项主机测试）/ C3 / D0 全做完 | **待触发**（批末一次） |
| 2 | 主控侧可见性 | ✅ A18a / A18b / A19b 全做完（回程 RSSI 无线索，见 H-05） | **待触发**（批末一次） |
| 3 | 控制面 | 🔄 A17a/A17b 全做完（能编能解、能发能停、页面能配能看）；**A17c 效果确认等真机**（H-03） | H-03 |
| 4 | 无线回程 | ⬜ | — |
| 5 | 多子节点与稳定性 | ⬜ | — |
| 6 | 发布与工程化 | ⬜（C9 已随本轮下令先做） | — |

## 4. 主机测试口径

一台**没有任何真机**的开发机要能给出结论，靠的是这些（第一条是本轮实测的基线）：

| 命令 | 覆盖 | 基线 |
|---|---|---|
| `sh tests/test_wlan_sync.sh` | plan → `/etc/config/wireless` 的全部映射（假 uci） | 130 passed（批 2 加 5 条；Windows 上约 8 分钟，Linux 上快得多） |
| `sh tests/test_init.sh` | init 的配置校验分支与本机 plan | 94 项（批 3 加 7 条） |
| `sh tests/test_rpcd.sh` | rpcd `status`/`apply` 的输出字段 | 18 项 |
| `node tests/test_luci.js` | LuCI 表单与节点表：字段、角色可见性、槽位校验、链路列、设备动作 | `33 main / 10 slot / 4 ACL / 2 client / child 5`（批 2 加节点表断言，批 3 加动作列表与状态框） |
| `tests/controller-1905-codec.c`（`make test-host`） | 1905 解码：头、TLV 走查、厂商子 TLV、`0x83`/`0x94` | 101 断言 + 10 万次有界变异（需要一台能编 C 的机器） |
| `tests/controller-steer-codec.c`（`make test-host`） | 终端动作消息的编码与两种回报的解码 | 整帧字节比对 + 10 万次有界变异（同上） |
| `sh tests/test_steer_tshark.sh` | 把上面那几帧交给 tshark，逐字段读回 | 20 项；没有 tshark 时输出 SKIP 并退出 0 |
| `tests/controller-actions-test.c`（`make test-host`） | 终端动作的去抖、重发、超时、回滚、每秒重配置 | 215 断言，假时钟 |
| `python tests/test_runtime.py`、`test_i18n.py`、`test_schema.py out` 等 | 运行包完整性、翻译、表结构 | 各自脚本末尾 |
| `sh tests/test_frames.py`（`python tests/test_frames.py`） | pcap 帧长/配对/周期 | 4 项 |
| `sh tools/build/container-make.sh test-host` | C 编解码（含 10 万次变异）+ 沙箱 | 需要容器或 Ubuntu（`192.168.1.191`） |

本机（Windows + Git Bash + Python 3.13 + Node 22）能跑前六行；C 那行必须借容器或 Ubuntu 机器。
**批末判定以全部行为准**，本机能跑的行先跑，C 那行在批末那一次 CI/Ubuntu 上补。

## 5. 真机待测台账（登记，不阻塞）

编号 **H-xx**。每条写清"验什么、前置、几步、判据"。到真机窗口时按编号取。

| 编号 | 验什么 | 前置 | 步骤 | 判据 | 风险 |
|---|---|---|---|---|---|
| **H-01** | S3：无线回程关联（B13c） | 一台能发 Wi-Fi 的 OpenWrt 当子节点（现在的 VM 没有无线）；中兴主控已开回程 BSS；子节点装 `wpad`（非 basic） | ①`backhaul=multi_ap` ②插上控制器档案后拔网线 ③`iw dev` 看 STA 接口 `4addr`/`Authorized` ④`ping` 上游 ⑤抓 1905 帧 | STA 关联成功且 `4addr` 打开；拔线后仍能 ping；1905 CMDU 在无线侧可见 | 换 `wpad` 可能挤爆 flash；4 地址要求两侧都开，主控是原厂不可控 |
| **H-02** | B11 在真子路由上不炸：额外槽位落成多个 BSS | ax3600 + 中兴主控（或 OpenWrt 主控） | ①`extra_slots=on` ②主控配一个访客槽位 ③`iw dev`/`uci show wireless` ④客户端连访客 SSID | 多出一个 BSS、名字密钥对、能拿到 IP；关掉后 BSS 消失且 `wlan-sync.status` 有记录 | 信道/带宽被多 BSS 摊薄；`wpad` 版本不支持多 BSS 时会静默失败 |
| **H-03** | A17 踢终端/封锁真的生效 | S1 拓扑 + 一台测试手机（MAC 已知）；页面「封禁与踢除设备」里各一条 `ban`（可选时长）与 `kick` | ①封禁该 MAC，`state.json` 的 `clients[]` 应显示 `sent` ②终端重连看是否被拒 ③删除记录，看是否恢复 ④加一条 `kick`，抓 1905 看是否真有 `0x8014` 出门、有无 `0x8015` 回报，`clients[]` 变 `done`/`failed` ⑤必要时开 `EnBtmKickoff` 再试 | 封禁后拒绝关联；解封后恢复；`0x8014` 真的发出；BTM 发出后终端切换（不保证） | 会踢掉用户自己的设备；`EnBtmKickoff` 出厂是关的，可能根本不发；`0x8014` 的 BSSID 目前用广播（还没有"终端挂在哪个 BSS"的表），要确认子节点接受广播 |
| **H-04** | 多子节点 | 两台子路由 | A23 | 两个节点同时出现在拓扑页，动作按 MAC 不串 | 1905 域 ID 冲突、AL MAC 冲突（`al_last`） |
| **H-05** | A18 RSSI/终端数的真值来源 | S1 + 一台已关联终端 | 抓 1905 后与 `iw station dump` 对照。**已有负结果**（[CAPTURE_1905](research/CAPTURE_1905.md)）：有线链路的 107 帧里没有任何 RSSI，链路度量应答是 ZTE 定长模板；要看 RSSI 必须走无线回程 | 页面上的数值与真机读数的偏差在 ±3 dB 内或明确标注"来自控制器上报" | 原厂 `StaRssiRptThr` 出厂 -110（等于不上报），可能根本没有值 |
| **H-06** | B15 `CmWLANMapSsidCfgSync2Repeater` | 真机 + strace | 逆向或动态跟踪 | 沙箱里替身不再需要 | 需要停机窗口；前序项没做完不值得动 |
| **H-07** | D5 VLAN / D6 绑定 / D7 BTM 在 E2631 上的实际行为 | 一台市售 E2631/AX3000，停机窗口 | 用它自带 `pktdump 1` 抓线上，按 [MULTIAPD](research/MULTIAPD.md) §5 的命令表 | 三处配置入口能被厂商自己的 web 打开（说明运行时真的读） | 动的是别人的家用路由器 |
| **H-08** | 出厂 `VersionMode` 与"兼容模式"文案的对应 | E2631 页面 | 改页面开关，看 `MultiAPCfg.VersionMode` 与 `P1905ComCfg.Enable` | 文案 ↔ 字段一一对应 | 已有一半结论（[FINDINGS](FINDINGS.md)），缺厂商侧确认 |
| **H-09** | 长时间稳定性（A22 的真机版） | 两台实验机 | 装 rXX 后放 24 小时，看 `daemon_restarts`、内存、`dmesg` | 重启计数 0、无 OOM | 会占着机器 |

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
