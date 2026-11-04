# 开发计划：让 OpenWrt 与中兴路由器组网

> 这份文件是**唯一排期表**：先做哪几件、每件怎么算做完、哪些必须等上机。
> 功能状态与证据只在 [FEATURE_STATUS](FEATURE_STATUS.md)（唯一总账）；协议与逆向在 [research/](research/)；
> 构建与测试命令、条数基线在 [DEVELOPMENT](DEVELOPMENT.md) 第 4 节；装机在 [INSTALL](INSTALL.md)；
> 机器清单在 [LAB](LAB.md) §2.1；中兴原厂路由器的语法基线在 [REAL_CHILD](REAL_CHILD.md)；过程叙事在 [journal/](journal/)。
> 别处出现的"已完成/未做"都不算数。最后更新：2026-11-04。

## 1. 终点：四个可验收的场景

每个场景一条判据，判据都是**看得到的现象**，不是日志里的 "status=0"。

| # | 场景 | 拓扑 | 现在 | 判据（怎么算过） |
|---|---|---|---|---|
| **S1** | 主控 | OpenWrt 有线网关（`master`）→ 中兴原厂路由器（原厂固件，有线回程） | ✅ 已在 ZXHN Z506 上过 | 中兴原厂路由器的 SSID/密码/信道/ACL 跟着页面变；节点动作（重启、指示灯）生效 |
| **S2** | 子节点（有线回程） | OpenWrt 主控 → OpenWrt 子节点（`slave`），一根网线 | ☑️ 两台测试机打通（中兴原厂主控那一半还没上过） | 主控改 SSID，20 秒内子节点的 `iw dev` 跟着变，且改回来能回退 |
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

类型列：**H** = 主机可测，**D** = 需上机（登记在第 5 节）。批 1–13 已全部走完并装机读过，每批一行：项 + 结论 + 判据 + 上机尾巴。

| 批 | 项 | 结论（做了什么） | 判据 | 上机尾巴 |
|---|---|---|---|---|
| 1 | B11 · C3 · D0（H） | `wlan-sync` 应用额外网络 1–3：每（频段,序号）一个 `zte_extra_<band>_<slot>` 段，字段与主 BSS 一致，plan 里没有的序号删掉，`main` 角色的本机 plan 也带 `zte_mesh_bss` 行，`main.extra_slots` 默认 **off**；`.gitattributes` 全库 LF；DEVELOPMENT §1 旧机器表不再保留（清单只在 LAB 2.1） | `test_wlan_sync.sh` + `test_init.sh` + `node test_luci.js` 绿；`git ls-files --eol` | 中兴原厂路由器上不炸 → H-02 |
| 2 | A18a · A18b · A19b（H） | 1905 CMDU 头 + TLV 走查 + ZTE 厂商子 TLV（`OUI 00:19:c6`）解码，`state.json` 每节点加 `stations`/`bss`/`hops`/`metrics_age`，**`-1` 表示该节点没上报（与 0 区分）**；rpcd 本来就透传 `state.json`，节点表加「链路」列（悬停看 BSS 数与新鲜度）；`wlan-sync` 读包库判断 `wpad` 变体，把原因写进日志与 `status.message`，三种正常情况不误报 | 1905 codec 断言 + 10 万次有界变异，含一段在测试主机上抓到的原始帧；`0x83`/`0x94` 布局对照 tshark dissector 核过 | 有线链路没有 RSSI → H-05 |
| 3 | A17a · A17b（H），A17c（D） | `0x8014` 引导 / `0x8016` 封禁编码、`0x8015`/`0x8017` 解码，字段顺序以 Wireshark dissector 为准（见 [STEER_1905](research/STEER_1905.md)）；状态机 `controller-actions.c`：KICK 5 秒一次最多 3 次、BAN 每 120 秒重发、KICK 有回报即结案，**到期改成 `expired` 并留着这一行**（删掉会被下一秒的重读建回来，客户端每分钟循环一次封禁/放行），时钟收进 `action_table`（`time(NULL)` 减 `CLOCK_MONOTONIC` 会报出负数）；配置链 `zte_mesh_client`（`mac` + `action`，最多 8 条，同一地址不重复）→ `ban.mac`/`kick.mac`/`ban_minutes`（0..1440），页面有列表与进度 | steer codec 整帧字节比对 + 越界/容量/参数拒绝 + tshark 逐字段读回（无 malformed、无多余 TLV）；actions 测试用假时钟跑完重发、去抖、到期解锁、规则删除的撤销、每秒重配置不滑期、撤回又加回取消撤销；测试环境控制器上实跑过整条链 | 对端是否照做 → H-03 |
| 4 | B13a/b/d/e/f/g（H），B13c（D） | `wlan-sync` 生成 `zte_backhaul_sta_<band>`：`mode=sta` + `multi_ap 1` + `wds 1`，名称/密钥/加密取 plan 那行（不写 `hidden`，那是 AP 的概念），换角色时把对端段还回去，行里声称加密却没密钥时两端都拒绝应用。**装机实测纠正的前提**：客户端的 4 地址与入桥只看 `wds`，`multi_ap` 在 sta 模式只是翻译成 `multi_ap_backhaul_sta`；只写前者会卡在 `BRIDGE_NOT_ALLOWED`（[AGENT_PROBE](research/AGENT_PROBE.md) §15）。**applier 的四个输入一起进 `restart.sig`**（`backhaul`/`extra_slots`/`wifi_2g`/`wifi_5g`）——watcher 比的是 plan 的内容摘要，从 UCI 读又不改 plan 也不改实例集合的东西，改了必须重启才看得见；最坏的不是回程而是 `extra_slots`（关掉却不重启，子路由会继续广播一个已不该存在的 guest）。四个字段按 child 或 local 渲染（纯主控都不显示，标题与描述分两端），`wifi_2g`/`wifi_5g` 是"一个频段两块同类型射频"时指定用哪一块射频（ax3600 正吃这一条），保存前校验段名格式；`backhaul` 只接受 `off`/`multi_ap`——`multi-ap` 这类写法会变成"页面开关设过、射频上什么都没有" | `test_wlan_sync.sh` 回程客户端区块（映射、幂等、两端互换、缺密钥、禁用行）；`test_init.sh` 的 `sig-*`（两端 off/on、显式默认与不设等价、纯主控四种写法签名相同且不带）+ 坏取值 + 源码针；`test_luci.js` 三种角色可见性 + 情况校验 + i18n | 端到端关联 → H-01 |
| 5 | A21 · A22（H），A23（D） | peer 落盘从 `controller-trace.c` 拆成 `controller-peer.c/.h`，对外只留 `controller_peer_note`，目录听 `ZTE_MESH_PEERS_DIR`，主机测试够得着整条链；ARM32 `libzte_controller_trace.so` 重编并 `repack-runtime.py` 只换成员。稳定性：**判据不能写成"pid 不许变"**——`search` 每 300 秒按设计退出一次被 respawn 拉回，要判的是"设计性退出的周期"和"崩溃"分不分得清；重启允许几次发布也要按这个写。日志轮转：服务不写会长大的文件（根只读、`/tmp` 是 8 MiB tmpfs、`/run` 下小文件原子替换），唯一落点是 logd 的 128 KiB 环；真问题是原厂 daemon 一次配置推送重复打 1012 行/7 分钟（超过整圈），改法是 `controller-log.c` 分类预算 + 两条重复状态行每 60 秒采样 4 行（四条一次性证据行不打折），采样说明带 `dropped_total=`（否则读者分不出"4 留 5 丢"和"4 留 1000 丢"） | peer codec（两节点不互覆盖、一帧多值多行、碎帧与 `0x0004` 不落盘、MAC 洪水预算）；log codec（含有界变异与"采样那句必须报出丢了几个"那条规则）；`test_runtime.py` 守住"成员与仓库文件一致" | 两个 peer 并存 → H-04；中兴原厂路由器推送时的日志占比与 24 小时 → H-09 |
| 6 | C8 · C9 · C10（H） | `INSTALL.md` 九节写完**并按文档从头走通**（现在"手册写完"和"按文档走通"是同一件事）。走查改口的结论：这台机器上没有 `opkg`；四个 apk 一条 glob 在 x86_64 上一条都不装；`/usr/lib/zte-mesh` 会跟着 `apk del` 一起删；`multi-ap` 不是"读成 off"而是整个配置被拒、服务不起；`verify-install.sh` 原先在健康子节点上是假 FAIL（重启后 12 秒就断言 SSID，而子节点约 21 秒才把主控的 SSID 改成）——改成先等主控的 SSID 写进本机再判。CI 去掉 release 发布，只留 artifact + 校验和；`verify-install.sh` = 一条命令装四个包（架构不合算 `uninstallable` 不算失败）+ 重启 rpcd 与服务 + 读回版本 + 冒烟 + 装机读回，输出 `KEY=VALUE` 证据行与 `RESULT` | `test_verify_install.sh`（桩 `ubus`/`uci`/`apk`/`logread`/`date` 加 fixture；`date` 也钉死，否则秒级的年龄窗口取决于什么时候跑这套）；两台测试机各跑一次都 PASS | 只读文档也能走到 S1/S2 的那半 → H-11（真型号）、H-01/H-02（原厂当主控） |
| 插队 | A24 · A25 · A13′（H） | 有线口掉线自愈：`attach` 兼任看门狗；`wlan-sync`/`search` 的 respawn 预算写成显式 `3600 5 0`（实测裸 `respawn` 半小时后永久放弃，retry 0 才是无限次）；角色拆成 `mesh_role` + `apply_local`，线上不再发 `auto` | 主机测试（init / rpcd / luci / i18n） | H-10 已验；角色对照 → H-11 |
| 7 | A18c（H） | 子节点固件版本的来源是 **CMDU `0x8002` AP Capability Report 的厂商 TLV `0xd4`**（`I5TlvDeviceInventoryTypeExtract`）——`IGD.P1905DeviceInfo` 的写手 `topoAdd1905DevInfoToDB` 只取设备记录的 `+24/+60/+124/+584`，`SoftwareVer`（+247）从来不解包（[CAPTURE_1905](research/CAPTURE_1905.md) §5）。`device_inventory_software()` 按 `[长度][文本]` 三段取第二段（超 64 与不可打印都算没说），只在 `0x8002` 里认，写成 `peers/<MAC>.software` 文件，数据库那列为空时填进 `nodes[].software`；**页面一行没改**（本来就有这一列） | 1905 codec（整段、截到每一刀、空字段、控制字节、64/65 边界、小缓冲区截断）+ peer codec（版本单独一个文件、别的消息类型里同号码不落盘、没有清单的 capability report 不落盘、空版本不覆盖已有文件、只有报的那台变） | 测试环境那台 peer 从没发过带 `0xd4` 的 `0x8002` → H-12 |
| 8 | A18d（H） | 终端数不再被"最后一帧"顶掉：原先 `peers/<MAC>.metrics` 是一份摘要、谁最后到谁写整份，子节点按射频分别回 `0x800c` 时页面就在 2.4G 与 5G 的数字之间来回换。`ap_metrics_by_bss()` 把 `0x94` 逐条解成 `{BSSID, 终端数}`，每路落一个 `peers/<MAC>.<BSSID>.sta`（各自时间戳，摘要照旧留着），`controller_peer_bss_load()` 把没过期的求和、`load.age` 报最老的一条。**同一 BSSID 出现两次或路数超过 16 一律返回 0** 回落摘要——宁可回落，也不能让一个"偏小的和"看起来像一次测量。读侧对文件名和文本都不信任 | 1905 codec（两路保序、20 字节带"估计服务时长"、0 终端是真条目、没有 metrics TLV、残帧、切在计数中间、重复 BSSID、`max` 比路数小、`max=0`/NULL）+ peer codec（空目录不等于 0、一帧两路落三个文件、单射频帧后摘要变小而总和含两边、单独让一路过期、重复 BSSID 不落 per-BSS、坏文件都不读） | 原厂路由器是一帧列全还是每射频一帧 → H-13 |
| 9 | A18e（H） | `bss_detail`（`{bssid, stations, age}`，按 BSSID 升序——目录顺序会随重写变，一秒换一次行的页面看着像网络在动）进 `state.json`，拓扑页「终端数」悬停改成一行一路；满 16 路时**总数照旧全加、行只留最小的 16 个**，页面那个数不会因为一行放不下就变小。缓冲 16 KiB → 32 KiB、每节点预算 1200 → 2400，节点条数上限没降。没加新文案。**遗留：按 BSSID 的那一列还没显示到页面**（要动表单与翻译） | peer codec（两路的两行、升序、`age` 对得上；二十路时 `entries=20`、`listed=16`、总数照加）+ `test_luci.js` 悬停整串与"没有指标就没有悬停" + i18n 无新增 | 随 H-13 一起看（测试环境那台 peer 只有一路） |
| 10 | B8b（H） | 子角色的初始配置**逐频段从本机当前广播的配置读回**：`live_ap_iface` 复刻 `wlan-sync` 的两条解析（指定了射频 / 只有一个候选 / 只有一块在用，跳过回程段与 `mode` 不是 ap，优先 `default_*`），`borrow_profile` 拿那一段的名称/密钥/加密/隐藏/开关填进初始配置（广播已关的射频不借——applier 为了让配置生效会 `enable_radio`，那是另一次全量 reload）。**旧行为是每次重启服务把本机广播的 SSID 改两次＝家里有人连着时两次掉线**，根因是初始配置来自 `/etc/config/zte_mesh` 而不是来自本机当前广播的配置。`wifi.dual_band_sync` 变成**对本机广播状态的断言**（两频段同一对才写 1，否则写 0 并把 5G 逐字段写全——守护进程按 2.4G 派生 5G，写错就要改名）；`wlan-sync` 补 `set_flag`：UCI 里没写过的 `disabled`/`hidden` 含义就是 0，**按含义比而不是按拼写比**，否则一次"补一个 0"的空写就把所有频段踢掉 | init 用 ax3600 真实配置做 fixture（关掉的空卡挂着 `ZTEMESHCTRL`，配置里必须零次出现；两端同名/同名派生/只一路同步/两卡活着/指定/射频关/读不到/主控不读本机广播）；wlan-sync 断言"初始配置等于本机广播 ⇒ `changed=0`、无 commit、桩 `wifi` 未被调用"，改一个字符作反证仍要 commit；两份都在 busybox ash 上过；设备配置面：新的初始配置与中兴原厂路由器上的 `/etc/config/wireless` 逐字段相同、md5 一致 | 重启窗口已判（H-14 ③）；只剩 ⑥ 与 ⑦ 后半 |
| 11 | C11（H） | `verify-install.sh` 第四条冒烟 `applier_writes`：数日志里 `applied; wireless committed` 的**增量**，窗口就是脚本自己要求的那次重启。**比文件不行**——旧的初始配置改了名、M2 又改回来，文件前后一样可家里已经掉了两次线，只有 applier 那行留了痕。**判 0 要有前提**：先读 `air_matched_plan`（本机广播是否已带着 plan 每个启用行的名字），是才判 0，否则只报数不作判定（把一次正常收敛说成故障就是那个假 FAIL 的重演）；`no-restart`/`no-applier`/`no-logread`/`wrapped` 四种情况都不作判定。新增 `RESTART_NOW=1`：不装机也驱动这一次重启——**会真掉线，所以默认关** | `test_verify_install.sh`（安静重启 PASS、`writes=2` 必须 FAIL 且 `RESULT=FAIL`、收敛中只报告数值、不作判定、纯主控 `no-applier` 且不打印 `air_matched_plan`、窗口翻圈不作判定；init 桩按 fixture 往日志里长那几行或整份换掉日志）；busybox ash 同跑 | 判据③已读到；⑥ 要用这个开关 |
| 12 | B16（H） | 读回守护进程自己那行 `ZTE_MESH_SERVICE cfg` 里的 `cur_hop=`。子角色判"不是 `-2`"——`-2` 是我们的启动路径写的"未接入"，子节点方向再没有第二处写它，所以别的值只能是守护进程更新的（`slave_init` 的 `CfMultiapSetCurHop` 或主控 TLV `0x43` 采用的值）；主控那一列本来就是我们自己写的，只当证据。不作判定的四种情况：`no-cfg-line`（日志环把那一行翻出去了）、`-98`（读不到那一列）、刚重启过、以及**那一行还太年轻**——按行末 `epoch=` 算 `cur_hop_age`，小于 `CUR_HOP_GRACE`（默认 120 秒）只报告数值、不作判定（子节点重启后要在"未接入"上待半分钟） | `test_verify_install.sh`（`-2` 必须 FAIL、算不出年龄照判、含"把窗口设成 0 就必须仍 FAIL"那条反证）；busybox ash 同跑；两台只读跑 PASS | "设成 MaxHop"还是"采用主控下发的值"分不开 → H-14 ⑦ 后半 |

| 13 | A18f · B16b（H） | 按 BSSID 的终端数从悬停改成**一列**（`zte-mesh.js` 节点表 11 格 → 12 格，`_('BSS detail')`／「BSS 明细」一路一行 `BSSID 数`，每行悬停是那一路的年龄；「终端数」那格的悬停退回只说"报了几路"，不再把同一串数字写两遍。**0 终端是一行 `02:…:01 0`，不是 `-`**——`-` 是"从没上报"的情况）。主控 `CurHop` 的测试环境专用开关：**不改 C**，用已有的 `/run/zte-mesh/db-profile`（一行 `MultiAPCfg 0 CurHop 5`）——`profile_apply` 在角色写列之前跑，而主控启动路径根本不写 `CurHop`，所以它活过一次重启；`verify-install.sh` 加 `db_profile_cur_hop=` 与 `master_cur_hop=`（相等才判 PASS，`absent`／`not-master`／`no-cfg-line`／`not-read`／太年轻五种情况不作判定），另加 `max_hop=` 那一条读数。这个文件不在任何页面、UCI 或 apk 里，用户的机器上不存在这条路径。包版本：`luci-app-zte-mesh` r9 → **r10**（含 i18n），`zte-mesh` 与 launcher 不动 | `node tests/test_luci.js`（12 格、两路两行、总数那格不再重复 BSSID、没按 BSSID 的条目时是 `-`）+ `python tests/test_i18n.py`（新增一条 `BSS detail`）；`test_verify_install.sh`（开关进了行 PASS、没进行必须 FAIL、`CUR_HOP_GRACE=0` 的反证、无文件不作判定、子角色 `not-master`、`no-cfg-line` 与 `-98` 只报）；本机 sh 与 owrt busybox ash 同跑 173 条；**装机读回**：两台 `RESULT=PASS`（owrt `pass=13`、ax3600 `pass=14`，`version_luci-app-zte-mesh=0.1.0-r10`、`zte-mesh=0.1.0-r18`、ax3600 上 launcher `0.1.0-r1`），新加的三行读数都在——`max_hop=2`、`db_profile_cur_hop=absent`（没摆开关就不作判定，符合设计），子节点那一趟 `cur_hop=-2` 年龄 12 秒 → 只报告数值、不作判定 | 开关摆好后只差一次窗口去读子节点的 `cur_hop` 与 `max_hop` → H-14 ⑦ 后半；⑥ 仍要改主控 SSID（会掉线，先征得同意）；页面那一列**装上去了但没在浏览器里看过**，主控的 `bss_detail` 当时是空数组——有内容的行还没有真实路由器上的读数 |

| 14 | 目录整理 A 档（H，插队） | C 侧 51 个文件（27 `.c` + 14 `.h` + 10 `.def`/`.inc`）从仓库根进 `src/`，跟着改 23 个：`Makefile` 逐文件列的源码、`tests/*.c` 上跳的 include、`schema.py`／`profile_schema.py`／`bridge_evidence.py` 的表描述路径、`tests/test_runtime.py` 与 `tools/build/runtime.sh` 的仓库侧路径、`build-lab-tools.yml` 里照抄 Makefile 的两条规则。**tar 成员名一个没动**（`ns-run.c`／`controller-net.c`／`enroller.c` 在包里还是这个名字，`zte-mesh-launcher/Makefile:61` 与 `test_runtime.py` 判的就是成员名）。根目录 17 个 `.py` 与零引用的 `kernel.fragment` 当时没动（`.py` 见第 15 批） | ubuntu 上编译与基线同绿（`all database` rc=0，日志 `warning`/`error` 各 0；`test-host` 仍停在缺 `out/analysis/cspd` 的 `test_schema`，是环境事实）；**产物哈希与移动前逐条相同**，其中三个 x86 工具要按 `ROUTERCC=cc` 重推才复现——基线里那三份是 11-01 留下的陈旧文件，`out/` 不受版本控制、`make` 只比 mtime，所以**判"改动不该影响产物"不能拿 `out/` 里的历史文件当基线**；CI 从 `ci/batch14` 出包，`zte-mesh` 与 launcher **与上一批逐字节相同**、两个 LuCI 包差在 4 行翻译；两台 `RESULT=PASS`（owrt `pass=13`、ax3600 `pass=14`，版本 r18/r1/r10/r10 不变），ax3600 那趟 `air_matched_plan=yes` + `applier_writes=0` + `sync_changed=0` | 无新增——尾巴还是 H-14 的 ⑥ 与 ⑦ 后半，页面「BSS 明细」有内容的行仍等 H-13 |

**排给下一批（主机可测）**：**没有了**。剩下的每一条都卡在上机窗口——H-01（无线回程端到端）、H-02（中兴原厂路由器多 BSS）、H-03（踢人/封锁对端照不照做）、H-04（第二台子路由）、H-09（24 小时与真推送的日志占比）、H-11（真型号与角色对照）、H-12（Z506 发不发 `0xd4`）、H-13（一帧列全还是一射频一帧）、H-14 的 ⑥ 与 ⑦ 后半。下一轮的时间应该花在窗口上，不是代码上。

| 15 | 目录整理 B 档（H，插队） | 根目录 17 个 `.py` 进 `tools/dev/`，跟着改 12 个文件：`build.sh` 四条提取/审计命令 + 容器里那条 `smoke.py`、`Makefile` 的 `--verify` 两条、按模块名 import 它们的 8 个（`openwrt/make-runtime.py`、`tests/` 六个、`tools/lab/bootstrap-out.py` 与 `syms.py`）。脚本里 10 处"仓库根 = 脚本目录"改成 `parents[2]`（`schema.py` 的 `TABLE_DEFS`、`bridge_evidence.py` 读 `src/mesh-bridge.h`，以及七个 `repo = …`）。`tests/test_evidence.py` 补上显式插入——它以前是**靠 `python3 -m unittest` 把 cwd 塞进 `sys.path` 才 import 得上的**。**顺带修掉 A 档漏的一处**：`tests/test_profiles.py` 用 `profile_schema.__file__` 的父目录找 `.def`，C 侧进 `src/` 之后那里没有 `wlan_base.def`，本机 13 条用例静悄悄报错（构建机因为 `make` 提前停在 `test_schema` 没暴露），改成取 `profiles.TABLE_DEFS` | 本机 `unittest discover -s tests` 126 条，报错 **46 → 33**（差的正好是那 13 条）；构建机先 `touch src/*.c …` 全量重编 `all database` **rc=0、`warning:`/`error:` 各 0、15 次 gcc**，七个产物哈希与第 9 轮基线**逐字相同**（`zte-meshctl d3b3d5c0…`、`db-engine-probe f488ba48…`、`zte-debugcmd b1dfd50c…`，`ROUTERCC=cc` 现编的 `ns-run f043437c…`／`ns-run-svctest 0e5a4beb…`／`exec-check 53a0dcde…`／`config-bridge-probe dcc9b84f…`）；测试单独跑齐：`test_evidence` 17 过、`test_device_workspace` 14 过、`test_cmapi_tables` 4 过、`test_i18n`／`test_runtime`（0 failure）／`test_frames` 过，shell 四组 130／24／182／173 全过，`test_profiles` 只剩 6 条缺 `cspd`、`test_policies` 5 条、`test_startup` 整组、`test_schema` 4 条（环境事实）；`tools/dev/cm-profile-evidence.py --verify src/cm-profile-layout.inc` 44 处 payload／2496 字节、`bridge_evidence.py` 出图正常，七个脚本 `--help` 全 rc=0 | **不上机，也不触发 CI**：判据是 `git diff --name-only b3ef7f8..HEAD -- openwrt/` 只有 `make-runtime.py`，而它不在任何 apk 里（三个包 `PKG_RELEASE` 18／1／10 不动，`zte-mesh-runtime.tar.gz` 仍是 `a36f9637…`）。剩下的还是 H-14 ⑥ 与 ⑦ 后半、页面「BSS 明细」等 H-13 |

另有一条不用上机、也不要新判据的小账：`openwrt/luci-app-zte-mesh/po/zh_Hans/zte-mesh.po` 还剩 **3 处写"档案"**
（141、150、174 行，都是页面描述），那是称呼清理漏的一层。改它必须连着升 luci 的 `PKG_RELEASE`（见 §6.3），
所以攒到下一批真动页面时一起做，不为三行字单开一轮 CI。

### 状态速览

| 批次 | 主题 | 状态 | 等上机 |
|---|---|---|---|
| 1 | 子路由侧写入准确 | 做完 | H-02 |
| 2 | 主控侧可见性 | 做完 | H-05 |
| 3 | 控制面 | 做完（能编能解、能发能停、页面能配能看） | H-03（对端是否照做） |
| 4 | 无线回程 | 主机侧做完 | H-01（端到端关联） |
| 5 | 多子节点与稳定性 | 主机侧做完 | H-04、H-09 |
| 6 | 发布与工程化 | 做完 | H-11 |
| 插队 | 自愈 / respawn 预算 / 角色拆分 | 做完 | H-10 已验；H-11 |
| 7 | 拓扑页缺的那一格 | 主机侧做完 | H-12 |
| 8 | 终端数按 BSSID 记账 | 主机侧做完 | H-13 |
| 9 | 按 BSSID 终端数显示到页面面 | 主机侧做完 | 随 H-13 |
| 10 | 子节点重启不再把 SSID 改两次 | 做完（OpenWrt 路由器的重启窗口已判） | H-14 的 ⑥ 与 ⑦ 后半 |
| 11 | 把 H-14 变成一条命令 | 做完 | 判据已读到，不用再单独开窗口 |
| 12 | 把子节点的跳数读回来 | 做完 | H-14 ⑦ 后半 |
| 13 | 按 BSSID 终端数上列 + 主控跳数的测试环境专用开关 | 做完（两台测试机 `RESULT=PASS`） | H-14 的 ⑥ 与 ⑦ 后半（开关已就位，只差读子节点那一次读取） |
| 14 | C 侧进 `src/`（目录整理 A 档） | 做完（两台测试机 `RESULT=PASS`，`zte-mesh` 与 launcher 两个包逐字节未变） | 无新增 |
| 15 | Python 进 `tools/dev/`（目录整理 B 档） | 做完（七个产物哈希与基线逐字相同，包内容没变所以没触发 CI） | 无新增 |

最近一次 CI 从临时分支 `ci/batch14` 触发，跑的是 A 档目录整理那一轮（C 侧进 `src/`，见 [journal](journal/2026-11-04.md) 第七、八轮），
包版本一个没升（`zte-mesh` r18、launcher r1、`luci-app-zte-mesh` 与 i18n r10），两台测试机 `RESULT=PASS`。
这一趟多读到一条证据：**源码从根目录挪进 `src/` 之后，`zte-mesh` 与 `zte-mesh-launcher` 两个 apk 与上一批逐字节相同**——
"产物与源码位置无关"以前只有主机侧的哈希比对，现在是包级的。两个 LuCI 包不同，差在 4 行中文翻译，
出处是那次没跑 CI 的称呼清理（见 §6.3 加的那条）。
**第 15 批（Python 进 `tools/dev/`）没有触发 CI，也没有装机**：这一批进不了任何包——判据是
`git diff --name-only <上批 CI 的 commit>..HEAD -- openwrt/` 只有 `make-runtime.py`，而它不在任何 apk 里（`grep` 过三个包的 `Makefile` 与两个 workflow），
运行包 `zte-mesh-runtime.tar.gz` 的哈希还是 `a36f9637…`。按 §6"攒够几项才跑一次 CI"的节奏，这种改动不值得单开一轮。
逐项状态、测试条数与证据只在 [FEATURE_STATUS](FEATURE_STATUS.md)（唯一总账）。

## 4. 主机测试口径

命令与条数基线归 [DEVELOPMENT](DEVELOPMENT.md) 第 4 节，这里不重复。

- **规则：本机能跑绿的判据 = 结论的一半，另一半必须上机。** 批末判定以全部行为准，本机能跑的先跑，
  C 那几行在批末那一次 CI 或 Ubuntu 上补。
- 要 `out/analysis/cspd` 的只有 `cspd` 证据那几组：`test_schema.py`（4 条）、`test_policies.py`（5 条）、
  `test_profiles.py`（6 条）、`test_startup.py`（整组卡在 `setUpClass`）。`cspd` 只从哈希钉住的原厂镜像提取
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
| **H-01** | S3：无线回程关联（B13c） | 一台能发 Wi-Fi 的 OpenWrt 路由器当子节点（现在的 VM 没有无线）；中兴原厂路由器（当主控）已开回程 BSS；子节点装**完整** `wpad`（ax3600 实装机是 `wpad-openssl` + `hostapd-common`，够用） | ①子节点 `uci set zte_mesh.main.backhaul=multi_ap` 并 `mesh_role=slave` ②插上主控的配置后确认 `uci show wireless` 里有 `zte_backhaul_sta_5g`：`mode=sta`、`multi_ap=1`、**`wds=1`** ③`iw dev <sta接口> info` 看 `4addr on`，`ip -d link` 同处，`brctl show` 与 `ubus call network.interface.wan status` 确认它挂在 `br-lan` ④拔网线 ⑤`iw station dump` 看 `Authorized`，`ping` 上游 ⑥无线侧抓 1905 帧 | STA 关联成功且 `4addr` 打开；拔线后仍能 ping；1905 CMDU 在无线侧可见。**已知会踩的点**：没有 `wds` 时 netifd 直接 `BRIDGE_NOT_ALLOWED`（`supplicant.uc:278`），`4addr` 也只看 `wds`（`:305`）——所以这一条只验端到端，不验映射 | 换 `wpad` 可能挤爆 flash；4 地址要求两侧都开，主控是原厂不可控 |
| **H-02** | B11 在中兴原厂路由器上不炸：额外网络写成多个 BSS | 小米 AX3600 + 中兴原厂路由器（当主控）或 OpenWrt 路由器（当主控） | ①`extra_slots=on` ②主控配一个访客网络（序号 1） ③`iw dev` 与 `uci show wireless` ④客户端连访客 SSID | 多出一个 BSS、名字密钥对、能拿到 IP；关掉后 BSS 消失且 `wlan-sync.status` 有记录 | 信道/带宽被多 BSS 摊薄；`wpad` 版本不支持多 BSS 时会静默失败 |
| **H-03** | A17 踢终端/封锁真的生效 | S1 拓扑 + 一台测试手机（MAC 已知）；页面「封禁与踢除设备」里各一条 `ban`（可选时长）与 `kick` | ①封禁该 MAC，`state.json` 的 `clients[]` 应显示 `sent` ②终端重连看是否被拒 ③删除记录，看是否恢复 ④加一条 `kick`，抓 1905 看是否真有 `0x8014` 出门、有无 `0x8015` 回报，`clients[]` 变 `done`/`failed` ⑤必要时开 `EnBtmKickoff` 再试 | 封禁后拒绝关联；解封后恢复；`0x8014` 真的发出；BTM 发出后终端切换（不保证） | 会踢掉用户自己的设备；`EnBtmKickoff` 出厂是关的，可能根本不发；`0x8014` 的 BSSID 目前用广播（还没有"终端挂在哪个 BSS"的表），要确认子节点接受广播 |
| **H-04** | 多子节点（A21 的落盘 + A23 的拓扑） | 两台子路由同时在线 | 各自动作一次（`kick` 或 `ban`），看落盘与路由 | 拓扑页两个节点同时出现、动作按 MAC 不串；`/run/zte-mesh/peers/` 两个 MAC 各自的文件不互覆盖；`state.json` 汇总两节点 | 1905 域 ID 冲突、AL MAC 冲突（`al_last`） |
| **H-05** | A18 RSSI/终端数的真值来源 | S1 + 一台已关联终端 | 抓 1905 后与 `iw station dump` 对照。**已有负结果**（[CAPTURE_1905](research/CAPTURE_1905.md)）：有线链路的 107 帧里没有任何 RSSI，链路度量应答是 ZTE 定长模板；要看 RSSI 必须走无线回程 | 页面上的数值与真实路由器读数的偏差在 ±3 dB 内，或明确标注"来自控制器上报" | 原厂 `StaRssiRptThr` 出厂 -110（等于不上报），可能根本没有值 |
| **H-06** | B15 `CmWLANMapSsidCfgSync2Repeater` | 中兴原厂路由器 + strace | 逆向或动态跟踪 | 沙箱里替身不再需要 | 需要停机窗口；前序项没做完不值得动 |
| **H-07** | D5 VLAN / D6 绑定 / D7 BTM 在 E2631 上的实际行为 | 一台市售 E2631/AX3000，停机窗口 | **先确认命令面再抓包**：用它自己的 `sendcmd` / 帮助输出核实 `pktdump` 在不在，再 `pktdump 1` 抓线上。[MULTIAPD](research/MULTIAPD.md) §5 那张命令表出自**未 strip 的 SR1010 版**，E2631 原厂那份已 strip、字符串里只剩 `sendcmd multiapd..master` 外壳（[BLOB_SHIM](research/BLOB_SHIM.md) §1），表里每一条在这台上存在与否都没核对过。命令面不在就走退路：`logcat` 里过滤 `AGENT RECV`，或 static 编 armv7l musl 抓包程序 + 限时 promisc | 三处配置入口能被厂商自己的 web 打开（说明运行时真的读） | 动的是别人的家用路由器 |
| **H-08** | 出厂 `VersionMode` 与"兼容模式"文案的对应 | E2631 页面 | 改页面开关，看 `MultiAPCfg.VersionMode` 与 `P1905ComCfg.Enable` | 文案与字段一一对应 | 已有一半结论（[FINDINGS](FINDINGS.md)），缺厂商侧确认 |
| **H-09** | 长时间稳定性（A22 的真实路由器版） | 两台测试机 + 一台中兴原厂路由器 | 装 rXX 后放 24 小时，看 `daemon_restarts`、内存、`dmesg`；**外加中兴原厂路由器推送时的日志占比**（改前实测：一次推送 1012 行 / 7 分钟，超过整圈 128 KiB，四小时后仍占 94.5%） | 重启计数 0、无 OOM；`logread` 里 `ZTE_TRACE_POLL` 不超过每分钟 4 行、每个超额窗口一条 `log_budget=poll sampled=1` 且带 `dropped_total=`，用户的行（入网、封禁、告警）还在。**当前读数**：每分钟 4 行与超额说明两台测试机已读到；缺 24 小时与中兴原厂路由器那一次推送 | 会占着机器 |
| **H-10** | **已验**：A24 有线口掉线自愈——宿主这头的 `zmesh0` 没了，沙箱自己长回来 | 测试环境控制器（owrt VM），页面能看到「有线接口」 | 宿主侧 `ip link del zmesh0`，观察服务与链路重建 | **25 秒自愈，没有人重启主控**（GRACE=20 加一个 tick 的生效时间，和脚本里的常数对得上）；重启的是沙箱所属的 controller 实例，`attach` 自己的 pid 没动——这正是"看门狗没被一起带走"的证据；`daemon_restarts` 仍为 0，节点重新 `online` | 只验了"主动删线"这一种丢失；`attach` 自己也被杀时自愈停摆；连续重启会短暂（约 1 秒）断开子路由的 1905 链路 |
| **H-11** | A13′ 角色改成 `master` 后中兴原厂路由器照样入网；也是 C8 那半（"只读文档能走到 S1/S2"）的入口 | ax3600（或 Z506）+ OpenWrt 主控，装机验证 | ①主控 `mesh_role=master`（不再发 `auto`）②子路由开机入网 ③`uci show zte_mesh` 与页面拓扑确认 ④主控改成 `apply_local=1` 再看本机 SSID 与子路由 SSID 同步 ⑤子路由重启一次，等 5 分钟内被 `search` 重新发现（A25 之后才可能） | 入网、下发配置、拓扑填充与 `auto` 时代一致；关键是**没有**出现子路由自当选主控（`MultiAPMaster` 的角色列仍是子）；页面节点表显示的是真型号，不是插件自己的字符串 | 改动只在主控写库的 `SetRoleEnable` 与 `Mode`，子路由侧一行没动 |
| **H-12** | A18c 子路由固件版本显示到页面——布局是代码级推的，真实路由器上没读到过 | 一台中兴原厂路由器（ZXHN Z506，页面写着 `V1.0.0.2B2.8000`）+ 主控已装机；`tcpdump` 抓 `zmesh0` | ①抓主控收到的 CMDU，找 `0x8002`（AP Capability Report）②在它的 TLV 里找 `0xd4`（厂商 Device Inventory Type），按 `[len][text]` 手工切三段，看顺序是否 序列号 → 软件版本 → 硬件版本 ③`cat /run/zte-mesh/peers/<MAC>.software` 看文件是否生成、内容是否与页面同源 ④刷新拓扑页，看 `nodes[].software` 是否显示版本号而不是 `-` | ②切出来的第二段与子路由「关于」页逐字符相同；③④有值即通路全绿。**负结果同样有用**：若 `0x8002` 里根本没有 `0xd4`，说明这个 Z506 不发库存字段，解码器留着但页面无数据，要另找来源。**别拿测试环境那台当负结果**：那台 peer 从不发带 `0xd4` 的 `0x8002`（读回 `peers_software=0`），只说明没收到过，不说明 Z506 也不发 | 只读抓包，不推配置；子路由侧一行没动 |
| **H-13** | A18d 的前提：原厂路由器当子节点时回 `0x800c` 那几帧**一帧列全所有 BSS** 还是**一射频一帧**（测试环境抓的包里没有 `0x800c`） | 一台中兴原厂路由器 + 主控已装机；`tcpdump -i zmesh0` 抓 1905；页面上这个节点已能看到终端数 | ①2.4G 和 5G 各挂一个终端 ②抓 `0x800c` AP Metrics Response，数每条消息里 `0x94` 的个数与各自 BSSID ③`ls /run/zte-mesh/peers/` 看是否出现 `<MAC>.<BSSID>.sta`（几路 BSS 就几个）④断开 5G 那个终端，看页面数字是否只降这一路 ⑤关掉子节点一个 BSS，30 秒后看它是否从总和里退出 | **先确认这一步有没有输入**：`.metrics` 里必须有 `stations=`（读回打印 `peer_reports_stations=yes`）——测试环境那台只有 `hops=1`，即从没收到带终端数的 `0x94`，在那种机器上"没有 `.sta`"什么也不说明。②③ 对得上即通路成立：**一帧一 BSS** ⇒ 每个 `.sta` 各自有时间戳、页面读"还活着的几路之和"；**一帧列全** ⇒ 每次上报同时刷新全部文件，两者等价，`bss=` 应等于条目数。失败的样子是只有 `.metrics` 没有 `.sta`，页面退回摘要（数字仍是最后一帧的总数，不算错但不准确） | 只读抓包，不推配置；子路由侧一行没动 |
| **H-14** | B8b 的最后一步：子节点**真重启一次**，看服务重启时不再改本机广播的 SSID。批 10 只证到配置面（仓库版的 init 与 `wlan-sync` 在 ax3600 上是**对着 `/etc/config/wireless` 的副本**跑的，射频一下没碰）。**判据已固化成一条命令**（批 11 的 C11）。**①②③④⑤ 与 ⑦ 前半已结案，只剩 ⑥ 与 ⑦ 后半** | ax3600（`mesh_role=slave`，挂在 owrt 主控下面）＋ **r18 及以后的包**（r17 仍是旧的初始配置）；家里没人连着的时候做 | ①`rssh.py ax3600 put tools/lab/verify-install.sh /tmp/` ②`RESTART_NOW=1 sh /tmp/verify-install.sh` ③读 `air_matched_plan` 与 `applier_writes` ④`logread` 里 `wlan-sync` 不许出现 `several 5g radios`（那是 `radio0.disabled` 被改动的信号）⑤对照 `/run/zte-mesh/config`：应写主控下发的两个名字、`wifi.dual_band_sync=0`（r17 那份写的是页面填的 `ImmortalWrt` 一对）⑥**只剩这一条掉线窗口**：只从**主控侧**改一次 SSID，确认该翻的那一次照翻（20 秒内）且翻完没有多余的第二次 ⑦**只剩开一次窗口读那一次读取**（开关本身在批 13 已经有了，是一个文件不是代码）：owrt 上 `printf 'MultiAPCfg 0 CurHop 5\n' > /run/zte-mesh/db-profile`，重启服务，`verify-install.sh` 读回 `db_profile_cur_hop=5` **且** `master_cur_hop=5` 才说明这个 5 真进了主控自己那一行（`profile_apply` 在角色写列之前跑，而主控启动路径不写 `CurHop`，所以它活过一次重启）；然后 ax3600 只读跑一次，看同一行里的 `cur_hop=` 与 `max_hop=`——**跟随 5＝采用主控下发的 TLV `0x43`，仍是 2＝本机 `MaxHop`（`slave_init` 自己设的）** | ③ 是 `air_matched_plan=yes` 且 `applier_writes=0` ⇒ **核心判决结案**：一次重启＝零次无线配置写入。**当前读数**：装 r18 那一次自带的重启窗口就是这个值（`sync_changed=0`、`/etc/config/wireless` md5 未变、`several 5g radios` 0 次、`/run/zte-mesh/config` 已带上主控的两个名字）。**反面是看得见的**：r17 重启时先广播页面填的那对，约 21 秒后 M2 才把名字翻过去——两次掉线，而这一条现在会被判 FAIL（`applier_writes` 非 0），这正是它值得自动化的地方。若 `air_matched_plan` 不是 `yes`，脚本只报数不作判定，那一次窗口要重跑，不算结论。⑦ 前半已不用等窗口：脚本按 `epoch` 报了年龄，环里那两行（`-2` 与更新的值相差 27 秒）就是这一步的读数。**⑦ 后半现在是两条读数**：主控 `master_cur_hop=5`（开关进了那一行）＋子节点 `cur_hop=`（5 或 2）——**先看到前者再解释后者**，否则主控把字段拒了也会让子节点停在 2，看起来像一个结论。r10 装机那一趟正好把"还不能解释"的情况走了一遍：没摆开关（`db_profile_cur_hop=absent`）、子节点刚重启完读到 `cur_hop=-2` 年龄 12 秒，脚本按年龄只报告数值、不作判定——那一次读取不产生结论。测完删掉 `db-profile` 里那一行并重启服务 | **⑥ 会让 ax3600 上的终端掉线**（改主控 SSID 是唯一还没自动化的一半），红线里"先征得同意"包括这一条；测前留 `iw dev` 基线，测后确认名称回到主控下发的那对。② 单独用也会掉线，所以默认关。⑦ 只动主控沙箱里那一列，不碰射频，但重启主控会让子节点短暂（实测 8–58 秒）掉出拓扑；测这一条期间**不要在页面上切角色**——那处换角色的写列会把 `CurHop` 写回 2，看起来像开关没生效 |

## 6. CI 与装机节奏

### 6.1 触发原则

- **不按项触发**。改完一项只在本地跑测试，不烧 CI；**攒够一批（3–5 项）批末触发一次**。
- **批末触发一次**：`gh workflow run build-apk.yml`（手动 `workflow_dispatch`），产物是 `zte-mesh-apk`
  artifact（四个 apk + `SHA256SUMS.txt`）。
- **走临时分支，不推 `main`**：`main` 直连 ImmortalWrt feed，推上去构建机立刻吃到。批末把这一批提交推成
  `ci/batchN`，在该 ref 上触发，**装机读完就删**。`workflow_dispatch` 跑的是**选中的那个 ref**，
  点错在旧提交上会"构建成功但产物不含本批"，只能靠装机读回的 `PKG_RELEASE` 发现——
  **发起前核对 ref 的 SHA 与本地一致。**
- **CI 不创建 GitHub Release**。发布是人的决定：确认某一版要对外，再手工
  `gh release create <tag> dist/*.apk dist/SHA256SUMS.txt`。流水线按批跑，跑出来的字节是"这一批的中间态"，
  不该自动变成对外承诺。

### 6.2 装机（批末一次）

| 机器 | 装什么 | 验收 |
|---|---|---|
| `192.168.1.20`（VM，x86_64，无无线） | `zte-mesh`（`arch: all`）+ 本地编的 `ns-run` | `ubus call luci.zte-mesh status` 返回角色；`/run/zte-mesh/wlan-sync.status` 有结果 |
| `192.168.1.1`（ax3600，aarch64，有无线） | 四个包（`--allow-untrusted`） | 同上；`slave` 角色下 `iw dev` 跟着主控改 SSID |

装完先跑 `sh tools/lab/verify-install.sh /tmp/zte-apk`（装包 + 读回版本 + 冒烟 + 装机读回，输出 `KEY=VALUE`
证据行与 `RESULT`），再跑本批新增的用例（页面开关、`uci` 选项）。判定逻辑由主机上的 `test_verify_install.sh`
守着，不接机器也能跑。**装机复验只能按标记字符串认，不能比哈希**——apk 打包会剥掉 ELF 节，设备上的 `.so`
与仓库 tarball 成员大小不同；根目录没解包时读回打印 `unreadable`、不作判定失败。

### 6.3 版本号

`make-runtime.py` / 包 `PKG_RELEASE` 在批末递增一位，让"这台机器上装的是哪一批"能从 `apk list` 读出来；
**只给这一批改过的包加一**。不做自动版本号。

**改过包里的内容就要升，哪怕那一批不跑 CI**——同一个 `PKG_RELEASE` 在 CI 上只能有一种内容。2026-11-04 的实例：
`ci/batch14` 出的 `luci-app-zte-mesh-0.1.0-r10.apk` 与 `ci/batch13` 的同名包**不同字节**，差在 `po/zh_Hans/zte-mesh.po`
的 4 行中文翻译，而那 4 行出自一次没跑 CI、因此没升版本的文档轮。机器上现在装的是新那份（翻译目录里
「BSS 明细」「广播的是同一个网络」在、旧写法不在）；下一次动页面文案时按 r10 → r11 升，别再让版本号同名两样内容。

## 7. 本项目的判据纪律

- 不许用"编译通过"当验收；每项都要留一条**能重复跑**的判据。
- 桩的**失败语义和成功语义一样重要**：`no-cfg-line`、`wrapped`、`no-logread`、"证据还太年轻"这一类
  "问得太早"的情况必须显式不作判定，否则一台健康的机器会被判 FAIL。
- "没有读到 X"在这种环境里往往什么也不说明（表可以行空、文件可以按需生成、peer 可以从不发那个 TLV）——
  判据要挂在能产生输入的那一侧。
- 结论已经有了、缺的只是可重复判据时，**去补判据，别把结论删掉**。
- 过程叙事（哪一天跑了什么、装到第几版顺带读到什么）只写在 [docs/journal/](journal/)，不进这份表。
