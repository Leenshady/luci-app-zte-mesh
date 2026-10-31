# 回放对比结果：shim 控制器 vs 原厂控制器（真子节点 Z506）

> **路径说明（2026-10-18 迁入 sr1010-mesh 仓库）：** 本文写于迁移之前，文中路径按当时的布局：mac 上的 `zx279133/port/mesh-shim/` 即现在的仓库根目录，`out/mesh-shim/` 即 `out/`；`session/…`、pcap、`db-baseline-s000.txt`、`vendor-log-debug.txt` 在 `evidence/`（只在 mac 上，不入库）；`tools/<脚本>` 即 `tools/capture/<脚本>`；`/tmp/buildNN.sh` 由 `tools/build/container-make.sh` 取代；`/tmp/shim-bak-*` 备份已删除（由 git 历史取代）；当时手工编的 `ns-run-al` 即现在 Makefile 的 `ns-run-svctest` 目标（运行包内容逐字节相同），当时说的旧 `ns-run-svctest` 文件已不存在。

日期 2026-10-16 晚。方法与工具见 `REPLAY_PLAN.md`。基准：`vendor-master-coldjoin-eth4.pcap` 的 12:33:37.5 起（原厂最初基线配置）。
shim 在主线 f66c96d8（首次冷启动成功）上，由 `tools/mreplay.py` 在 mac 上驱动，子节点在 `lan1`。

## 1. 运行记录

| 轮 | 配置 | 结果 |
|---|---|---|
| 1 | 无 profile，无市场码（旧 `ns-run-svctest` 不转发），AL `…19:06` | **子节点发了 Search，完整入网**（M1×2、M2×2、12 对厂商查询）；节点 `ZXHN Z506`、`zte:true` |
| 2 | + profile（Master、Domain、P1905ComCfg），仍无市场码 | 无 Search，无 M2；稳态流量 |
| 3 | + 市场码生效（换用 `ns-run`） | 同上 |
| 4 | AL 改 07，但只改了入口没改 `br0` | 无有效数据（`zmesh0` 没抓到） |
| 5 | AL 07，`br0` 与入口一致 | 服务启动失败：`verify_route_adapter` 里写死了 `02:00:00:00:19:06` |
| 6 | AL 06 对照（新二进制，无链路抖动） | 服务正常，确认失败由 AL MAC 引起 |
| 7 | 去掉写死的自检，AL 07，profile + 市场码 | 服务正常、AL `…19:07`；**仍无 Search** |
| 8 | AL 08，**无 profile**，有市场码 | 仍无 Search，且第一条策略消息仍是残缺的 |
| 9 | profile + `MultiAPCfg`（34 项），AL 09 | 服务正常；第一条策略消息仍为空体 |
| 10–12 | profile2，AL 0b/0c/0d，无触发；同时用子节点 telnet 采它自己的日志 | 无 Search（10、11 的子节点日志因管道缓冲为空，12 才采到 1.1 MB） |
| 13 | profile2，AL 0e，`--trigger manual` | 窗口里**没有**拔插（用户拔插发生在窗口结束之后），无 Search |
| 14 | profile2，AL 0f，无触发，启动于那次物理拔插后约 2 分钟 | **子节点发了 Search，完整入网**（见第 6 节）；节点 `ZXHN Z506`、`zte:true` |

**子节点只在第 1 轮发了 AP-autoconfig Search。** 观察到的事实：
- 第 1 轮的 Search 发生在控制器启动后约 1 秒，不是在 12 秒后的 `lan1` down/up 之后；之后每一轮，`lan1` down/up 都没有让子节点重新入网。主线 DSA 端口管理性 down/up 与原厂的 `ip link set eth4 down` 一样，**不能让子节点看到链路掉线**。
- 控制器换新的 AL MAC（07、08、09）也没有让子节点再次 Search，所以不是"按 AL MAC 记住控制器"。
- 第 1 轮是原厂控制器下线（原厂重启）约 3 分钟后的第一个控制器；之后的几轮之间间隔都更短或者子节点已被 shim 配置过。**触发 Search 的条件目前不明**，需要看子节点自己的日志（见第 5 节）。

## 2. 已对齐（逐字节一致）的字段

对比第 7、9 轮的第二条 `PolicyConfigReq` 与原厂冷启动里的同类消息：

| 字段 | 原厂 | shim（有 profile + 市场码） |
|---|---|---|
| `MetricRptPolicy`（0x8a） | `00 02 4d415035474c 000000c0 4d4150323447 000000c0` | **一致**（第 1 轮无 profile 时是间隔 `0x1e`、标志 `0x00`） |
| 厂商子 TLV `4a`（漫游阈值） | `64 50 50 50 50 50 0a 00 00 00 01 2c` | **一致**（第 1 轮已一致） |
| `4c[2]` | `0001` | **一致**（无 profile 时 `0000`，来自 `Master.11rSsidSync=1`） |
| `5a[1]` | `00` | **一致** |
| `4d[1]` | `01` | **一致** |
| `60[2]`（`[P1905ComCfg.Enable, MarketConfEn]`） | `0101` | **一致**（无 profile 时整个 `60` 缺失，来自 `EnableIsSync=1`；无市场码时第二字节为 0） |
| 12 对厂商查询中的频段配置、槽位 0 | `07=100 19=2 55=55`；槽位 0 `0c=1 46=0 11=1 0f=0 1e=0 0d=0` | **一致**（第 1 轮） |
| M2 里槽位 0 的 WSC TLV 长度 | 640 | **一致**（第 1 轮） |

## 3. 剩余的差异

| # | 差异 | 判断 |
|---|---|---|
| 1 | 第一条 `PolicyConfigReq` 的厂商体：原厂 `4d=01 4f=<16 字节> 60=0101`，shim 为空（无 profile 时更糟，只有 2 字节 `0019`） | 加了 profile 和 MultiAPCfg 后仍为空。推测取决于控制器**已记住该子节点是中兴设备**（`P1905DeviceInfo.ZTEDevFlag`，原厂库里是历次入网留下的持久值；shim 每次启动库是全新的，收到 M1 前为 0）。未验证 |
| 2 | M2：shim 单分片 857 字节（WSC 640 + APRadioId + 厂商 TLV），原厂三分片 1324+1353+209，含 4 个 WSC | 槽位 1 到 3 未配置（预期）；原厂另有 `0xe0(31)` TLV，shim 没有 |
| 3 | AP-autoconfig 响应：原厂多一个 `0xdd(1)` TLV，厂商 TLV 175 字节（shim 171） | 未知 TLV；设备身份长度差异 |
| 4 | `TopoResponse`：原厂多 `0xe0(1)`，`DevInfo` 34 字节（shim 16） | 原厂上报虚拟射频接口；未知 TLV |
| 5 | 12 对厂商查询中槽位 1 到 3 的回复：shim 回空 | 预期（未配置槽位） |
| 6 | `0200fe` 跨设备模块消息：入网序列里 3 条（值 = `MLOEnable`=1） | shim 没有 cspd，预期 |
| 7 | 厂商 TLV `0x40`（设备信息）：序列号、设备名、版本 | shim 有意不带序列号，预期 |
| 8 | 稳态流量：shim 多发 `BeaconMetricsQuery`（有限次的一小串，不是每 5 秒；见第 15.2 节），原厂 1.5 小时的抓包里一条也没有；`AssocSTALinkQuery` 的频率和原厂一样（每个终端每 5 秒一条，原厂 12:32 到 12:54 每分钟 24 条 = 2 个终端） | 第 15.2 节：机制已查明（`master_sta_get_dualband_by_rrm` 的 7 个条件），原厂为何不满足仍未查明 |
| 9 | 空的 `ChanSelReq`/`ChanSelResp`（只有头，无 TLV） | 无害，原厂没有；来源待查 |

## 4. 本轮对 shim 源码的改动（都在 mac `port/mesh-shim/`，已备份 `/tmp/shim-bak-20261016/`，未入库）

1. **profile 加载器**（`mesh-policies.c/.h`、`controller-run.c`）：启动时读 `/run/zte-mesh/db-profile`（行 `表 行 字段 值`），覆盖 `MultiAPMaster`、`MultiAPDomain`（行 0 到 7 → 内部行 2 到 9）、`P1905ComCfg`、`MultiAPCfg`；显式服务配置仍然优先。`tools/mkprofile.py` 从原厂快照生成。
2. **AL MAC 可配置**（`controller-net.c`）：环境变量 `ZTE_MESH_AL_LAST`（十六进制末字节，默认 06）同时决定 `br0` 与 `veth-lab`。后续改为整串 `ZTE_MESH_AL_MAC`（`02:00:00:HH:19:LL`，见 [FINDINGS](../FINDINGS.md) 第 4 节），`ZTE_MESH_AL_LAST` 保留为只改末字节。
3. **去掉写死的自检**（`controller-run.c` `verify_route_adapter`）：只校验前缀 `02:00:00:00:19`。
4. 构建：用 `/tmp/build10.sh`（容器内删旧产物）；`ns-run` 用 `ns-run-al`（带市场码、`SERVICE_ROOT`、AL MAC 可配）。

## 5. 下一步

- 要拿到"带 profile 的完整入网"（Search、M1、M2、12 对厂商查询）与原厂逐项对比，需要子节点重新走一遍入网。已排除链路 down/up 与换 AL MAC。**最干净的办法：给子节点断电再通电一次**，同时让控制器持续运行（`--trigger manual --seconds 420`）。
- 更好的办法是读子节点自己的日志与状态（`logctrl -r -g 1 -n`、`sendcmd multiapd.map.slave dev`、`sendcmd 1 DB p MultiAPCfg`），能直接看到它为什么不发 Search，也能验证每次 M2 是否被应用（`config M2[0/1]`）并澄清纯 WPA3 的 auth=0 问题。需要子节点的 telnet 账号口令。
- 之后再对 `0xe0`、`0xdd` 两个未知 TLV 做反汇编，确认它们的来源和门控条件。

## 6. 第 10–14 轮：子节点日志与完整入网（带 profile + 市场码）

### 6.1 子节点侧事实（telnet 读取，口令不入文档）
- 子节点 `ZXHN Z506`（ZE50X）：内核 4.1.25 armv7l，进程 `cspd`、`multiapd`。库里 `MultiAPCfg`：`Mode=1`（从）、`Enable=1`、`SyncEnable=1`、`SetRoleEnable=1`、`RoleElect=0`、`MaxHop=2`、`CurHop=2`、`P1905DevNum=1`；`P1905_ACD`：`ACDRole=1`、`ACDVersion=3`、`ACDSyncEnable=1`；`P1905ComCfg`：`Enable=0`、`MarketConfEn=1`、`MarketSign=8000`。
- `sendcmd multiapd.map.slave dev` 看到的从节点 `uMode=2`，两个射频：2.4G 信道 8、5G 信道 48，各 `rssithr=-110`，`BssNum=4`。
- 子节点的 ACD 客户端在 **`wlan0`、`wlan5g0`** 上每 60 秒重发一次 AP-autoconfig Search（计数器 `retrytimes` 一直在涨，两个频段各一条）。这些走的是它自己的无线口，和有线控制器无关；有线口 `eth0`（`wanlantype:1`）在这段日志里没有 Search。
- 子节点日志里能看到它收到新控制器 AL MAC 后新建 1905 设备，并带出 `ManufacturerName=ZTE`、`ManufacturerModel=ZXSLC SR1010`（来自我们注入的真实身份）。

### 6.2 为什么前面几轮没有 Search（修正第 1 节的"触发条件不明"）
- 第 13 轮窗口内没有拔插；用户的物理拔插发生在窗口结束之后（路由器日志：`lan1` Link Down 24 秒后 Up），那时没有控制器在运行。
- 第 14 轮在拔插后约 2 分钟启动，子节点随后发 Search 并完整入网。
- 第 14 轮的第一帧是 03:02:48；物理拔插的链路恢复约在 02:59:32（路由器 dmesg 4412 秒），所以间隔约 3 分钟。子节点是在处理完控制器的 `TopoNotification` 后约 1 秒发的 Search。
- **能排除的**：第 12 轮和第 13 轮之间子节点有约 4 分钟没有控制器，之后新控制器出现，它仍然没有 Search。所以"控制器缺席够久"单独不是触发条件。
- **目前唯一同时出现在第 1 轮和第 14 轮之前的事件**是物理链路丢失（第 1 轮前路由器重启，PHY 掉线）。管理性 `lan1` down/up 和换 AL MAC 都没有触发。解释为"物理链路丢失后，子节点才会重新在有线口 Search"，但**没有直接证实**（第 14 轮没有采它有线口的日志）。
- 注意：第 14 轮前我曾试图用 telnet 重启子节点，但命令在 mac 上用了不存在的 `timeout`，**没有发出**（检查时子节点运行时间为 379735 秒）。第 14 轮的入网与重启无关。

### 6.3 第 14 轮对比（原厂冷启动 12:33:37.5–55 vs shim 03:02:48–03:03:00，去掉周期性消息）
**逐项一致：** Search×2、AutoconfResponse×2、M1×2、M2×2 条数；12 对厂商查询的往返数（12/12）；子节点侧的 `TopoQuery`、`HigherLayerQuery`、`ClientCapQuery` 往返；第二条 `PolicyConfigReq` 的 `MetricRptPolicy` 与厂商子 TLV（`4a/4c/5a/4d/60`）。

**仍有差异：**

| # | 差异 | 判断 |
|---|---|---|
| 1 | 第一条 `PolicyConfigReq` 厂商体：原厂 `4d=01 4f=<16 字节> 60=0101`，shim 为空 | 与第 3 节 #1 相同；加了 profile 和 `MultiAPCfg` 后仍为空，门控条件未明 |
| 2 | M2：shim 单分片 861 字节（WSC(640)+APRadioId+设备信息），原厂三分片 1324/1383/209（4 个 WSC：640+656×3，另有 `0xe0(31)`） | 槽位 1–3 未配置（预期）；`0xe0` 未知 |
| 3 | 12 对厂商查询里槽位 1–3 的应答：原厂有真实内容（如槽 3 `0c=1 0d=1`），shim 回空 `V[010001 ]`/`V[010101 ]` | 预期：shim 服务配置只配了槽位 0 |
| 4 | 子节点自己的 `TopoResponse`：shim 里 `APOperBSS(33)`，原厂 `(77)` | 同上，因槽位 1–3 没配，子节点只上报更少 BSS |
| 5 | 原厂入网后推 **`CombinedInfraMetrics`**（窗口内 3 条，带厂商 TLV `020017/02002f/020047`、`030001`），shim 整轮 300 秒 0 条 | **真实缺口**。原厂 19 条分布在 14:05 之前的长抓包里，间隔 1–1160 秒不等，每条都紧跟 `HigherLayerResponse`/`APMetricsResp`/`LinkMetricResponse`，是事件触发而不是周期性。shim 用的是同一个 `multiapd`，所以更像是触发条件没满足，而不是缺功能。可能的门控有两类，都还没验证：库字段或 cspd 通知；控制器本机无线数据（shim 的控制器 `DevInfo` 只有 16 字节、一个本地接口，原厂 34 字节、约三个，厂商 TLV 长度 26/50/74 恰好符合 2+12·n，n=2/4/6，需要查 n 是否随 BSS 数变化） |
| 6 | 原厂控制器侧 `TopoResponse`：`DevInfo(34)`+`0xe0(1)`，shim `DevInfo(16)` | 与第 3 节 #4 相同 |
| 7 | AutoconfResponse：原厂多 `0xdd(1)` | 与第 3 节 #3 相同 |
| 8 | 原厂入网后子节点连发 6 条 `TopoNotification`（含 `ClientAssocEvent`），并重做 3 轮 `APCapQuery/ChanPrefQuery/OperChanReport`；shim 只做 1 轮、子节点只发 1 条 | 很可能只是因为这个子节点已经带着正确配置（上次入网留下），M2 没有造成变化、不用重启 Wi-Fi；原厂冷启动时子节点配置与 M2 不同。**未验证** |
| 9 | 稳态：shim 对子节点下的终端发 `BeaconMetricsQuery`（0x8011，有限次，见第 15.2 节），原厂整个抓包里没有 | 与第 3 节 #8 相同；子节点日志显示它对 STA `56:87:79:2b:97:30` 回 `not support beacon report`，所以这些查询在子节点那边是徒劳的 |
| 10 | 原厂 3 条 `0200fe`（`cspd.wlan_manage.wlan_mgr`，值 1） | shim 没有 cspd，预期 |

### 6.3b shim 入网后，子节点的 Wi-Fi 配置与原厂基线不同（需要用户决定是否恢复）
用子节点 telnet 只读比较 `WLANCfg`（密钥被遮成 `***`，没有读取）：

| 项 | 原厂基线（原厂控制器配过） | 子节点现在 |
|---|---|---|
| 槽位 3（`wlan3`/`wlan5g3`，隐藏 SSID，ACL 0，BeaconType 6） | `Enable=1`，隐藏 | **`Enable=0`**（隐藏、ACL 0、BeaconType 6 仍在） |
| 槽位 0、4（主 SSID） | `Enable=1` | `Enable=1`，一致 |
| 槽位 1、2、5、6 | `Enable=0` | `Enable=0`，一致 |
| BeaconType（槽位 0/1/4/5） | 12（WPA3+WPA2） | 3（WPA2）；这个子节点固件没有 `WPA3AuthMode` 字段，M1 也不声明 SAE，所以不能归因于 shim |

- 原因：shim 的 M2 只带槽位 0，原厂的 M2 带 4 个槽位（含启用的隐藏槽位 3）。子节点按 M2 应用，槽位 3 被关掉了。第 1 轮、第 14 轮的 M2 都是这样，所以不是第 14 轮才发生。
- 我没有"shim 之前"的子节点状态，这一条是由原厂控制器当时的应答（槽位 3 `0c=1 0d=1`）和子节点现在的值推断的。
- 恢复需要槽位 3 的密码。子节点的 `DB p` 遮蔽了密钥，原厂路由器里的密码（刷机前的 Wi-Fi5 `AP3/AP7`）此前也记录为无法恢复。**没有对子节点做任何写入。**

### 6.4 本节之后的状态
- 子节点日志级别已恢复为 Info，临时日志文件已删；mac 上带 Wi-Fi 密码的服务配置、各轮运行目录、临时脚本已删；路由器上没有残留（`/tmp/zte-mesh-svc`、`/run/zte-mesh`、`/tmp/zmc`、`zmesh0` 都不存在）。`db-profile`/`db-profile2` 只含策略字段（已核对字段名，无口令）。
- 第 14 轮证据：`session/replay-run14/zmesh.pcap`、`state.json`。
- **子节点的槽位 3 仍是关闭状态（见 6.3b），没有恢复；这一项不属于"已清理"。**

### 6.5 下一步（按价值排序）
1. 查 `CombinedInfraMetrics` 的触发条件（反汇编 `multiapd` 里发 0x8013 的函数，看它依赖哪个库字段或哪条 cspd 通知）。顺带解释第一条 `PolicyConfigReq` 的 `4d/4f/60`，很可能是同一类门控（"是否已知为中兴设备"）。
2. 在 shim 服务配置里补槽位 1–3（访客/隐藏 SSID），再做一次完整入网，确认 M2 分片、`APOperBSS` 和 12 对查询的槽位应答与原厂一致。
3. `0xe0`、`0xdd` 两个未知 TLV 的来源。
4. 要验证第 6.2 节的触发解释：下次在有控制器时拔插网线并同时采子节点的有线口日志。

## 7. 第 15、16 轮：对照实验与反汇编结论（2026-10-17）

用户授权随时重启子节点（只限子节点）。两轮都是"先起控制器，再用 telnet 重启子节点"，子节点启动后自己 Search，完整入网。
第 15 轮 `version_mode=128`（原厂基线 `MultiAPCfg.VersionMode` 就是 128，我之前的服务配置一直写 0）；第 16 轮 `version_mode=0`，其余完全相同。
证据：`session/replay-run15-vm128/`、`session/replay-run16-vm0/`（只留 `zmesh.pcap` 与 `state.json`）。

### 7.1 对照结果

| 现象 | 第 14 轮（VM 0，子节点未重启） | 第 15 轮（VM 128，重启） | 第 16 轮（VM 0，重启） | 归因 |
|---|---|---|---|---|
| `CombinedInfraMetrics` | 0 | 4 | 4 | **子节点重启**（不是 VersionMode） |
| 子节点 `OperChanReport` | 1 | 5 | 5 | 子节点重启 |
| `BeaconMetricsQuery` | 30 | 0 | 0 | 随子节点下的终端而变（第 14 轮两个终端各恰好 15 条，= 域的 `BsMeasureLmt` 15；后面各轮 0 到 7 条）；**原厂的抓包里同样的终端在线，一条也没发，所以这不是"纯环境"，原因未查明，见第 15.2 节** |
| AutoconfResponse 的 `0xdd(1)` | 无 | 有（与原厂一致） | 无 | **`VersionMode` bit7（0x80）** |
| 控制器 `TopoResponse` 的 `0xe0(1)` | 无 | 有（与原厂一致） | 无 | **`VersionMode` bit7** |
| M2 里的 `0xe0` | 无 | `0xe0(1)`（原厂 `0xe0(31)`） | 无 | bit7 出 TLV；长度 31 要 `WLANSetting`（见 7.3） |

结论：`0xdd`、`0xe0` 是 Wi-Fi 7 MLD 相关 TLV，由 `MultiAPCfg.VersionMode` 的 bit7 控制。**shim 的服务配置应改用 `version_mode=128`**（与原厂基线一致；bit1 仍为 0，私有 TLV 行为不变）。

### 7.2 `CombinedInfraMetrics` 的触发机制（`multiapd` 反汇编，地址为该二进制内）
- 定时器：`master_set_combined_timer(原因位)`（0x420e4）在定时器 id 为 0 时启动一个 100 单位的定时器，并把"原因位"或进全局偏移 0x724。到点后 `master_combined_timer_process`（0x39a18）调 `master_combined_infrastructure_merics_send`（0x39994）。
- 两处触发：`OperatingChannelReportReceive`（0x443a4）收到子节点的 `OperChanReport`、回 Ack 之后**无条件**调用（原因位 2，之前先调 `master_update_dev_vendor_flag`）；`ClientCapabilityReportReceive`（0x423cc）在 `sta[0xc8]&7==1` 时调用（原因位 1）。
- 发送时遍历设备列表，每台设备要求：`dev[0x50]==1`，且 **`dev[0x5a]`（厂商标志）非零**；原因位恰为 1 时还要求 `dev[0x9e]==0`。
- `dev[0x5a]` 由 `master_update_dev_vendor_flag`（0x15578）填：为 0 时，若 `dev[0x4c]&1` 直接置 1，否则按 MAC 查视图 `P1905DeviceInfo`，把行内偏移 656 的一个字节存入（`ZTEDevFlag`，值 0 时每次 `OperChanReport` 都重新查）。
- 解释第 14 轮：子节点已带着配置，只发了 1 条 `OperChanReport`，而且在 M1 之前（此时 `ZTEDevFlag` 还是 0），之后没有新的 `OperChanReport`，标志位变 1 也没有机会触发定时器。子节点刚重启时在 M1 之后还会再发（第 15、16 轮收到 5 条），于是触发。
- `VersionMode` bit1 清零时发 ZTE 私有邻居表 TLV（原厂抓包里的 `020017/02002f/020047`），置位发标准链路度量（FINDINGS 第 6 节）。
- 内容差异（原厂 3 条的条目数 26/50/74 字节，shim 为 15 字节）取决于槽位/BSS 数量，槽位 1 到 3 配齐后应变化，待验证。
- 因此这**不是缺功能**，只是触发时机：控制器只在子节点入网流程里发。

### 7.3 第一条 `PolicyConfigReq`（`4d`、`4f[16]`、`60`）为什么 shim 是空体
- 这条由 `master_dev_online`（设备上线）发：用一个只置 Roam Caton 标志（参数结构偏移 0x13c）的参数调 `PolicyConfigRequestSend`，厂商体 = `4d`（兼容版本，恒有）+ `4f`（Roam Caton 16 字节）+ `60`（P1905ComCfg 同步）。
- `4f` 的内容来自 `MapGetRoamCatonCfgDB` 读表 **`MultiAPRoamCatonCfg`**。基线值：`OfflineRetrieveEn=0, TxDataAfterBaEn=0, TxBaDelayMs=10, ZeroDropCfgEn=0, BtmCacheEn=0, TxConDelayMs=100, TxDropCnt=0`，正好是原厂的 16 字节 `00000000 000a 00000000 0064 00000000`。
- **shim 没有注册这张表**：服务日志里 `db_get view=MultiAPRoamCatonCfg status=-2`、`get RoamCatonCfg error`。`WLANSetting` 也一样（`status=-2`）。这两个读失败正是第一条消息体残缺和 M2 的 `0xe0` 只有 1 字节的原因。
- `cspd` 里两张表都有符号：`dbDefMultiAPRoamCatonCfg`（0x1f4198）+`dbCreateMultiAPRoamCatonCfgTbl`（0x1f4260）；`WLANSetting` 是 `.data` 里的静态域数组 `tWlanSettingTblDm`（0x2ba298，0x3fc 字节＝15 个 68 字节条目＝14 字段加结束项）+`dbCreateWlanSettingTbl`（0x1edc2c）。`startup-schema.py` 目前只覆盖 6 张表，加这两张是 GAP_ANALYSIS P1 第 4 项的实质内容。
- 基线里 `WLANSetting`：`DualBandSync=1, SSIDSync=1, SpecialMode=0, ESSIDSuffix=-5G, MLOEnable=1, MLOSSID=1`（共 14 字段，字段名见 `db-baseline-s000.txt`）。

### 7.4 本节后的状态
- 子节点 Wi-Fi 配置与第 6.3b 节相同：槽位 3 仍关闭。最后一次运行是 `version_mode=0`，与此前一致，没有额外改动。
- mac 上带密码的配置、各轮运行目录、脚本已删；路由器无残留；子节点在线。

### 7.5 下一步
1. 在 `startup-schema.py` 加 `MultiAPRoamCatonCfg`、`WLANSetting`，`controller-run.c` 注册并按基线灌值；重跑冷入网，预期：第一条 `PolicyConfigReq` 出现 `4f`，M2 的 `0xe0` 变 31 字节，`0200fe` 值正确。
2. 服务配置默认改 `version_mode=128`；同时审视 `controller-run.c` 里 `CustomTag=69` 等依赖 VersionMode 位的注释是否仍成立（bit7 不涉及 bit0/bit2/bit6）。
3. 补槽位 1 到 3 的服务配置，之后再看 `CombinedInfraMetrics` 条目数与 M2 分片是否与原厂一致，并顺便恢复子节点的槽位 3。

## 8. 第 17、18 轮：GAP P1 第 4 项（2026-10-17）——`WLANSetting` / `MultiAPRoamCatonCfg` / `MultiAPCacheMac` 注册

目标：让 shim 的第一条 `PolicyConfigReq`、M2 的 `0xe0` 与原厂逐字节一致。两轮都是 `version_mode=128`、启动控制器后用 telnet 重启子节点；证据 `session/replay-run17-p14a/`、`session/replay-run18-p14/`（只留 `zmesh.pcap` 与 `state.json`）。

### 8.1 改动（mac `port/mesh-shim/`，未入 git；改前备份 `/tmp/shim-bak-20261017-pre-p1-4/`）
- `startup-schema.py`：`SPECS` 增加 `MultiAPRoamCatonCfg`（构造函数 `0x1f4260..0x1f4368`，7 字段，232 字节，1 行；注意 `0x1f4198` 是默认值设置函数而不是构造函数，本工具从不读它）、`WLANSetting`（`0x1edc2c..0x1edc74`，域数组在 `.data` 的 `0x2ba298`，14 字段，520 字节，主键 `ViewName`）、`MultiAPCacheMac`（`0x1f406c..0x1f4118`，2 字段 `ViewName`+`MacAddr` 均为主键，108 字节，200 行）。同时补回丢失的 `AclCfg`（`0x1cd334..0x1cd41c`，5 字段，268 字节，256 行）：`startup-views.inc` 早已含该块，但 `SPECS` 里没有，`--verify` 一直是失败的。现在 `make controller` 里的 `--verify` 和 `tests/test_startup.py`（7 个用例，新增 `test_getter_layouts`）都通过。
- `startup-views.inc`：由 `SPECS` 重新生成，原有 7 张表逐字节不变，只在末尾增加 3 张。
- `controller-run.c`：新增 `startup_exact_view()`，按 `cspd` 各 `_dbInitTbl` 调用点的真实参数注册这三个视图（`MultiAPRoamCatonCfg`：父 `"IGD"`、arg5=1、标志 12；`WLANSetting`：`dbMgrViewAddItem` 包装函数，父 `""`、arg5=1、标志 63；`MultiAPCacheMac`：第二名 `"DEV"`、父 `"IGD"`、arg5=200、标志 63）。启动时灌入原厂基线值（`TxBaDelayMs=10`、`TxConDelayMs=100`；`WLANSetting`：`ViewName=DEV.WIFI.SETTING`、`SSIDSync=1`、`ESSIDSuffix=-5G`、`MLOSSID=1`、扫描三个参数…），`DualBandSync` 取自 `wifi.dual_band_sync`，`MLOEnable` 取自新配置键 `mlo.enable`（默认 1，与基线一致）；之后仍调用 `db-profile` 加载（基线文件可覆盖其余字段）。`MultiAPCacheMac` 保持空表（基线 RowCount=0）。`CustomTag=69` 注释改成"VersionMode bit1 清零路径（0 与 128 均适用）"。
- `controller-service.c/.h`：新键 `mlo.enable=0|1`（默认 1，改动需重启），新增 `controller_set_wlan_setting()`。
- `tools/mkprofile.py`（本地文档目录）：`SKIP` 增加两张表，可由基线快照生成对应行。
- 未改：OpenWrt `zte-mesh.init` 的 `version_mode()` 映射（`zte`→0、`zte_compat`→1、`standard`→2）。**待办**：`zte` 应改 128、`zte_compat` 改 129（bit7 与 bit1 独立，已在第 15、16 轮验证只影响 `0xdd/0xe0`）；`standard` 模式（bit1 置位）是否同样加 bit7 还没试，先不动。

### 8.2 第 17 轮：只注册 `MultiAPRoamCatonCfg` 与 `WLANSetting`——不够
- 两个视图读取都变成 `status=0`，`get RoamCatonCfg error` 消失，M2 的 `0xe0` 变 31 字节。
- **但第一条 `PolicyConfigReq` 仍是 35 字节，体是单个字节 `01`**（第 7.3 节"缺表导致空体"的结论只对了一半）。
- 反汇编找到真因：`I5TlvVendorRoamCatonTypeInsert`（`multiapd` 0x3bbf4）在 `MapGetRoamCatonCfgDB` 之后还会 `dbAPILstView("MultiAPCacheMac","DEV")` 枚举缓存的 MAC（接着对每个条目 `dbAPIGetView` 取 6 字节 MAC，拼进 `0x4f`），列表失败时返回 `-8`（`mvn r0,#7`）。调用方 `I5TlvVendorPolicyTypeInsert` 是 `add r4, r4, r0`，没有检查负值：`4d`（4 字节）之后 `r4 = 4 - 8 = -4`，接着 `60`（5 字节）写在 `buf-4` 处、`r4 = 1`，最终只提交 `buf[0] = 0x01` 这 1 字节。日志里 "list view failed VID(%s) RVN(%s)" 正好紧跟在 `MultiAPRoamCatonCfg` 读取之后。
- 原厂 `0x4f` 的 16 字节里那个 `00` 是缓存 MAC 的个数，后面 `6×N` 字节是 MAC 列表。

### 8.3 第 18 轮：再注册空表 `MultiAPCacheMac`——对上了
- 第一条 `PolicyConfigReq` 为 62 字节，**与原厂逐字节相同**（除 MID）：`4d0001 01 | 4f0010 00000000 000a 00000000 0064 00000000 | 60000201 01`。
- M2 里 `0xe0(31)` = `0100…00`，AutoconfResponse 里 `0xdd(1)=40`，控制器 `TopoResponse` 里 `0xe0(1)=00`：三者字节值都与原厂相同。
- 这一轮子节点分两段上线（第一段 12:39:33 子节点带旧配置直接发 `TopoNotification`，第二段 12:40:19 起才 Search→M2）；`CombinedInfraMetrics` 共 4 条（原厂 3 条，条目数仍取决于槽位数，见下）。
- 多重集差异里剩下的与 P1 有关的项：
  1. `0200fe`（`cspd.wlan_manage.wlan_mgr`，值 `00000001`）原厂 3 条，shim 0 条（P1 第 5 项）；
  2. 槽位 1–3 的 `010001`/`010101` 消息 shim 是空体，原厂带 `0b/0c/46/11/0f/1e/0d`（要配槽位 1–3）；
  3. M2 分片：原厂 3 片（1324/1383/209），shim 单片 895（同样要配槽位 1–3）；
  4. `CombinedInfraMetrics` 条目数（26/50/74 对 15）；
  5. 控制器设备信息长度 `V[020009]` 175 对 204（设备标识字符串长度，属于身份而非功能）。
- 其余（`0x8011`、`OperChanReport` 个数、`TopoNotification` 个数）随子节点状态变化，不是控制器缺口。

### 8.4 下一步
1. P1 第 5 项：`0200fe`。`topoSendVendorMsgTodev`（0xa7088）没有直接调用者，是经函数指针被调用；它不是 `multiapd` 自发的，更像 `cspd.wlan_manage.wlan_mgr` 收到"设备上线"发布事件后经 OSS 消息让 `multiapd` 转发。shim 日志里有 `SubScribPublish multiapd topo dev Service Fail!!!`，说明发布订阅这一步在 shim 里没建立。
2. 槽位 1–3 的服务配置（之后恢复子节点的槽位 3 要你同意并给出密码）。
3. `zte-mesh.init` 的 `version_mode()` 映射改 128/129（见 8.1）。

## 9. 第 19–21 轮：GAP P1 第 5 项——跨设备模块消息 `0200fe`（2026-10-17）

### 9.1 机制（反汇编，均为原厂二进制内地址）
- 帧体 `0200fe` 里是 `{u16 BE 0x0024（名字缓冲区长度）, 名字[36] = "cspd.wlan_manage.wlan_mgr", u32 BE 消息号 1, u32 BE 值长度 4, u32 BE 值}`，值 = `WLANSetting.MLOEnable`（原厂抓包里出现过 `00000001` 5 次、`00000000` 2 次，后者是 FEATURE_MAP 里我切换 MLO 的实验）。
- 发送者是 `cspd` 的 `wlan_mgr` 模块：它调用 `libieee1905cfapi` 的 `CfIeee1905TunnelToDevice(mac, 消息, 长度)` → `_prepareAndSendTunnelMessage`：把 `{u16 0, AL MAC[6]}`、`SELF()` 取到的本模块名（写成 `00 24` + 36 字节）、消息体拼成一个事件，**`ASEND(0xa97e, …, "multiapd.topo.topo")`**（异步）。AL MAC 全零表示所有设备。
- `multiapd` 侧：`topoMain` 任务按消息种类分发——种类 1 走 `topo_handle_event`（事件表在 0xe2ab0，`0xa97e → _topoSendVendorMsgTodev → topoSendVendorMsgTodev(0xa7088)`）；种类 2（同步 `SSEND`）走 `topoSynMsg` 的请求表（0xe2a08/0xe2a88），里面没有 `0xa97e`。`topoSendVendorMsgTodev` 按 MAC 找设备（状态 4 = 掉线则跳过），调用 `_topoSendVendorTunnelMsg`，它用 CMDU 类型 4（VendorSpecific）+ 厂商头 `0200fe` 发出，MID 与别的主控消息共用同一个计数器。
- 触发：原厂入网时 3 条（`PolicyConfigReq` 第一条之前 7 到 13 毫秒各一次，对应设备上线事件），之后每次再上线（`master_dev_online`）一次；说明它是"设备上线"发布事件驱动的，`cspd` 订阅了 `multiapd.topo.dev`（shim 日志里 `SubScribPublish multiapd topo dev Service Fail!!!` 就是这个订阅没建立）。

### 9.2 实现与验证
- `zte-meshctl wlan-mgr <AL MAC> <0|1>`（`liboss_pub` 客户端，`ASEND`）；服务循环里 `mlo_reconcile()`：第一个 ZTE 子节点在线满 15 秒（更早的消息子节点会丢弃，与信道/功率下发同一规则）后发送启动时的 `mlo.enable` 值，子节点再上线重发，最多 4 次。
- 第 19 轮（用同步 `SSEND`）：helper 返回 0，线上没有 `0200fe`；第 20 轮：已改 `ASEND`，`0200fe` 出现（那次 `zmesh` 抓包没起来，证据是 `br-lan` 上的 `session/replay-run21-p15/run20-br.pcap`，线上 1 条 84 字节）。我当时误以为抓包被我清理掉了：拷贝命令因 `zmesh.pcap` 缺失而中断，后面的清理没有执行，事后才取回。**教训：拷证据与清理分开做，并且先核对文件再删。**
- 第 21 轮：`0200fe` 出现，84 字节，目标 = 子节点 AL MAC，与原厂帧**逐字节相同**（只有 MID 与源 MAC 不同）。证据 `session/replay-run21-p15/`。
- 与原厂的区别：原厂入网时 3 条且在 `PolicyConfigReq` 之前；shim 每个会话 1 条且在约 15 秒之后。Z506 的 `WLANSetting` 表没有 `MLOEnable`（只读自查：ViewName/DualBandSync=0/SSIDSync=1/AirMonitorDuration/SpecialMode），所以这条消息对它无实际作用；有 Wi-Fi 7 的子节点才会生效。要做到入网时序也完全相同，需要实现 `multiapd.topo.dev` 的发布订阅并常驻一个模拟 `wlan_mgr` 的订阅者——不值得，除非有 Wi-Fi 7 子节点要验证。

### 9.3 其它顺带修改
- `openwrt/zte-mesh/files/zte-mesh.init`：`version_mode()` 映射改为 `zte→128`、`zte_compat→129`（`standard` 仍为 2）。**这个映射没有在设备上跑过**，只有直接写 `version_mode=128` 的服务配置跑过（第 15、17、18、19–21 轮）。
- `SERVICE.md` 增加"MLO 开关与跨设备模块消息"一节。

### 9.4 P1 剩余
- 第 6 项 mesh 级 Renew 依赖 P0 的 CM 通知层，还没做（2026-10-17 已做，机制和原先理解不同：是守护进程重启，见第 12 节）；第 7、8 项优先级最低。
- 槽位 1–3 已在第 10 节配好并恢复了子节点的隐藏槽位 3（`010001`/`010101`、M2 分片与原厂一致，`CombinedInfraMetrics` 的私有 TLV 内容仍随邻居状态不同）。

### 9.5 收尾核对（第 22 轮，最终构建）
- 改动：`mlo_reconcile()` 改为按子节点逐个跟踪（最多 8 个，各自 15 秒、各自重发，每次循环最多跑一次 helper）；LuCI 状态页 `zte-mesh.js` 的"网络模式"查表补上 128、129。
- 最终构建、`version_mode=128`、重启子节点，完整入网（证据 `session/replay-run22-final/`）。与原厂冷入网逐项比较：第一条 `PolicyConfigReq` 逐字节一致（体 `4d0001 01 | 4f0010 … | 60000201 01`）；M2 里 `0xe0(31)`、`0xdd`、`TopoResponse` 的 `0xe0(1)` 与原厂相同；`0200fe` 条数 原厂 3 / shim 1，其余 `VendorSpecific` 差异只有槽位 1–3 的 `010001`/`010101` 空体。
- 仍与原厂不同、且都与槽位或身份有关：M2 分片（原厂 3 片 1324/1383/209，shim 单片 895）；`CombinedInfraMetrics` 条目数；控制器自身 `020009` 长度 175 对 204（设备标识字符串）。
- 新发现的非确定项：`PolicyConfigReq` 里 `MetricRptPolicy` 两个无线电的先后顺序每轮不同（第 14、18 轮 5G 在前，第 15–17、21、22 轮 2.4G 在前；原厂恒为 5G 在前）。两个无线电的内容都齐，只是顺序，属于竞争（谁先报），不是缺口，但多重集比较里它会显示成差异。
- 主机测试（mac，针对 `out/mesh-shim`）：`test_startup` 7、`test_schema` 16、`test_policies` 20、`test_profiles` 28、`test_device_workspace` 14 全过；`test_evidence` 17 个里有 1 个失败（`test_resolve_alias_and_reject_escape`，macOS 的 `/var` 与 `/private/var` 符号链接路径比较，和本次改动无关，本次没碰 `prepare.py`）；Makefile 里的两个 `--verify`（`cm-profile-evidence`、`startup-schema`）通过。C 层的 codec 测试（`make test-host` 需要容器内构建的测试二进制）这次没跑。
- 源码状态：mac 上的 `port/mesh-shim/` 仍未进 git；备份只有 mac 的 `/tmp/shim-bak-20261016/`、`/tmp/shim-bak-20261017-pre-p1-4/`（后者是本节改动之前的原文件），以及本机 `/tmp` 下的工作副本。

## 10. 第 23–25 轮：槽位 1–3 与子节点隐藏槽位 3 的恢复（2026-10-17，用户同意并指定统一密码）

用户的原话："配置槽位 1-3，恢复槽位 3，密码你就用<用户的 Wi-Fi 密码>好了。如果不是这个密码，就用这个覆盖掉"。所有槽位的密钥都用这个密码（会覆盖子节点原来的槽位密钥；槽位 1、2 的原密钥本来就无法恢复）。

### 10.1 配置依据
- 原厂基线（`db-baseline-s000.txt`，SSID 被遮蔽）每个无线电 4 个槽位：槽位 0 开、`BeaconType 12`；槽位 1 访客（`GuestSsid=1`、`VapIsolationEnable=1`）关、12；槽位 2 关、6；槽位 3 开、隐藏、`ACLPolicy=0`、`MapAccessAttr=64`、6。其余字段逐槽位比较只有这些不同。
- SSID 名称取自子节点自己保存的值（原厂控制器当时下发的，用只读的 `DB pf` 读出，只取 SSID/使能/隐藏/类型，不碰密钥表，读完删掉临时文件）：槽位 1 `ZTE-tsEG65_GUEST`（两个无线电相同）、槽位 2 `cc2-wifi5`（2.4G）/`cc2-5Gc-wifi5`（5G）、槽位 3 `ZMESH-c4FsH5_5G`（两个无线电相同）。第 23 轮之前子节点的槽位 3 两个无线电都是"关闭、隐藏、BeaconType 6"。

### 10.2 代码改动（mac `port/mesh-shim/`，改前备份 `/tmp/shim-bak-20261017-pre-slots/`，含原来的 `libzte_controller_profile.so`）
- 新服务配置键 `radioN.bssK.hidden=0|1`（默认只有槽位 3 为 1）与 `radioN.bssK.beacon_type=3|6|12`（默认 3）；`wlan_profiles_set_extra_bss_style()`；这两项改动需重启。
- **`controller-profile.c`（CM 配置适配器）原来只放行 `BeaconType 3`**，其它值回 `-95`，`CmWLANGetPsk` 随之失败，守护进程把这个槽位当"未实例化"，M2 里就没有它。第 23 轮因此仍是单分片 M2（895 字节）、槽位消息空体。现在放行 3、6、12（仍要求 PSK + AES）。
- `wlan_profiles_add_extra_bss()`：额外槽位的 `MapAccessAttr` 槽位 3 设 64、其余 32；`VapIsolationEnable` 只有槽位 1 设 1（原厂基线值；它们就是槽位消息里的 `0f` 字段与 M2 的 Multi-AP 扩展属性）。
- 主机测试（mac）：`test_startup` 7、`test_schema` 16、`test_policies` 20、`test_profiles` 28、`test_device_workspace` 14 全过。

### 10.3 结果
- 第 23 轮（`BeaconType` 6/12 被适配器拒绝）：M2 单分片，槽位消息 `010001`/`010101` 空体（证据 `session/replay-run23-slots-rejected/`）。
- 第 24 轮（放行后）：M2 变成 3 分片 `1324/1383/209`，与原厂相同；槽位 2、3 的槽位消息与原厂逐字节一致，槽位 1 只差 `0f`（`VapIsolationEnable`）。
- 第 25 轮（补上 `MapAccessAttr`/`VapIsolationEnable`）：和原厂冷入网的多重集比较里，`APAutoconfWSC`、`010001`/`010101` 全部消失，只剩：`0200fe` 3 对 1；`MetricRptPolicy` 两个无线电的先后顺序（非确定，见 9.5）；`CombinedInfraMetrics` 的私有 TLV 内容（原厂 `020017:26`/`02002f:50`/`020047:74` 与 `030001:4`，shim 为 `02000c`/`020024`、`030001:4` 或 `030007:10`，每轮不同——取决于当时的邻居/终端状态，不是配置缺口）；控制器自身 `020009` 长度 175 对 204。证据 `session/replay-run24-slots-ok/`、`session/replay-run25-slots-final/`。
- **子节点现状（第 25 轮后只读核对）**：

  | 槽位 | 2.4G (wlan*) | 5G (wlan5g*) |
  |---|---|---|
  | 0 | 开，`cc`，类型 3 | 开，`cc`，类型 3 |
  | 1 | 关，`ZTE-tsEG65_GUEST`（访客），类型 3 | 同左 |
  | 2 | 关，`cc2-wifi5`，类型 6 | 关，`cc2-5Gc-wifi5`，类型 6 |
  | 3 | **开，隐藏，`ZMESH-c4FsH5_5G`，类型 6，`MapAccessAttr=64`**；接口 `wlan3` 已起来 | 同左，接口 `wlan5g3` 已起来 |

  `WLANPSK` 的 8 行里 8 行的 `KeyPassphrase` 都是这个密码（只数了个数，没有打印任何密钥）。槽位 3 的 `MapAccessAttr` 从 32 变成 64，说明守护进程下发的 Multi-AP 扩展属性确实按槽位生效。槽位 1 的访客类型 12 在子节点上显示为 3，槽位 2 的类型 6 保持 6，与"下发类型 = 配置类型与子节点 M1 所报类型的交集"一致。
- 子节点槽位 1 的 `VapIsolationEnable` 仍显示 0（子节点没有按槽位消息改它）；这一项没有影响线上消息，不再追。

## 11. 第 26–31 轮：GAP P0（CM 通知层、按频段 Renew、BeaconType）（2026-10-17）

用户指示："继续做 P0：CM 通知层"。证据目录 `session/replay-run26-p0-cm-notify/`、`replay-run27-p0-slots-band/`、`replay-run28-p0-beacon-mlo/`、`replay-run29-p0-notify-pair/`、`replay-run30-p0-slot3-enable/`、`replay-run31-p0-enable-restart/`
（每个含 `zmesh.pcap`、`br.pcap`、`service.log`、`state.json`、`report.json`；第 28 轮另有 3 份子节点只读读数 `child-read28-N.txt`）。
测试方法：`mreplay.py` 新增 `--swap SECONDS:FILE`（子节点入网 30 到 40 秒后把运行中服务的配置原子替换，写到 `/run/zte-mesh` 里的临时文件再 `mv`）。
改动的 mac 源码（`port/mesh-shim/`，未入 git；改前备份 `/tmp/shim-bak-20261017-pre-p0-cm/`）：`zte-meshctl.c`、`mesh-policies.c/.h`、
`controller-service.c/.h`、`controller-run.c`、`wlan-profiles.c/.h`、`controller-profile.c`、`controller-trace.c`、`tests/cm-profile-codec.c`。

### 11.1 机制（反汇编，`libmultiapcmapi.so` / `multiapd`，均为各自二进制内地址）
原厂的"CM 通知"就是：CM 设置函数写完库后调用 `CmSendMsg2MM("multiapd.map.master", 0x2401, 消息号, name[32], 无负载)`，
即同步 `SSEND(0x2401, {u32 消息号, char name[32], u32 0}, "multiapd.map.master")`（`CmSendMsg2MMWithTimerOut` 里 `SSEND(事件, 缓冲, 长度+0x28, 6000 ms, …)`）。
主控任务 `multi_ap_master_main` 对同步消息走 `master_handle_synmsg`：事件 `0x2401` 用 `MeshMasterHandleSynSet` 表（`{消息号, 函数}`，`mesh_handle_obj` 线性查找）：

| 消息号 | 函数 | 做什么 |
|---|---|---|
| `0x10206` | `_master_set_doamin_cfg` | `master_update_gtDomain` → `master_update_roam_threshold`（对每个子节点 `PolicyConfigRequestSend`，带 `MetricRptPolicy` + 厂商 `4a`）→ `master_sta_update_bsloadstrategy` |
| `0x10205` | `_master_set_master_cfg` | `master_set_mgr_cfg`（重读 MultiAPMaster）→ `master_vendor_policy_notify`（只带厂商 `4c 5a 4d 60` 的 `PolicyConfigReq`）→ `master_set_meshsr_onoff`（版本位 `0x20` 才动，否则直接返回） |
| `0x10208` | `_master_set_btm_req_mode` | 更新 BTM 判断参数，无帧 |
| `0x10207`、`0x10216`、`0x10203`、`0x10226`、`0x10227` | 保存拓扑、无线电信息、信道配置、STA BTM/断开 | 本轮未用 |

调用方（`CmMultiAPSet*` 的反汇编）：`SetRoamCfg` 写 8 个 `MultiAPDomain` 行后发 `0x10206`；`SetDomainCfg`、`SetBSCfg`、`SetBSEnable` 也发 `0x10206`；
`SetMasterCfg` 发 `0x10205`（`Mode` 为 Slave 时发给 `multiapd.map.slave`）；`SetBtmReqModeCfg` 发 `0x10208`；`SetMultiAPCfg` 发 `0x10205`，另有两条 `ASEND`：
`0xa93c`（带整个 2952 字节的 `MultiAPCfg` 视图）和 `0xa93d` 发给 **`cspd.cspd.multiapd_mgr`**——这是 mesh 级变化（P1 第 6 项）的 cspd 一侧入口，我们不跑 `cspd`，没做。
`SetRoamCatonCfg` 是 `ASEND(0xa96a, 32 字节, "multiapd.map.master")`，没做（没有已知的界面对应）。
此前 FINDINGS.md 记的"域事件 `0x10206` 无效"当时的发送方式没有记录，推测是把 `0x10206` 当成了事件号；它其实是 `0x2401` 里的消息号。直接写库（`sendcmd 1 DB set`）不发这条消息，所以守护进程不会重读。

### 11.2 实现
- `zte-meshctl notify-master <id>...`：只允许 `0x10205`/`0x10206`/`0x10208`，一个进程里按给定顺序连发（原厂网页路径的两条通知相隔 12 ms）；自检看助手的 `SSEND` 拦截器打印 `event=0x2401 length=40`。
- `mesh_policies_live_*()`：共享池锁下把字段写进全部 8 个 `MULTIAPDOMAIN` 行（第 2 到 9 行，与 `CmMultiAPSetRoamCfg` 一样）或 Master 行。
- 服务：`roam.rssi24/5`、`band_steer.rssi24/5`、`band_steer.enabled`、新键 **`roam.force`**（`MultiAPDomain.EnLegacyStaRoam` + `MultiAPMaster.EnBtmKickoff/EnRoamKick`）改值热更新：Domain 字段变发 `0x10206`，Master 字段变发 `0x10205`，两者都变先 `0x10206` 再 `0x10205`；启动时不发；键的出现/消失仍需重启。
- 同一轮：WLAN 类变化只 Renew 变化了的频段（`changed` 位掩码，日志 `profile_update=ok bands=<掩码>`）；已有槽位的 ssid/key/hidden/beacon_type 热更新（写 `WLANCfg`、`WLANPSK`，并用 `dbAPIGetView` 读回核对，日志 `slot_update … 读回/期望`；**槽位的 `enabled` 不能热更新，见 11.7**）；主 SSID 新键 `radioN.hidden`、`radioN.beacon_type`（**主 SSID 的 `hidden` 没有试过**，`beacon_type` 的热更新在第 27 轮试过 12→6→12，且只对 5G）；`mlo.enable` 热更新（写 `WLANSetting.MLOEnable`、两个频段 Renew、`0200fe` 由 `mlo_reconcile` 补发）；适配器与配置解析放行 `BeaconType 0 / 10`（`11iAuthMode`/`11iEncryptType` 仍须是 PSK/AES，原厂界面也不改它们）。

### 11.3 第 26 轮：策略热更新，与原厂逐字节比较
入网约 40 秒后依次换 6 份配置（每份只改一处），线上的 `PolicyConfigReq`（厂商 TLV 子 TLV）：

| 动作 | shim（第 26 轮） | 原厂（FEATURE_MAP） |
|---|---|---|
| 2.4G 漫游阈值 -60→-65 | 1 帧 83 B：`4a=5a50505050500a000000012c 4d=01 60=0101` | 第 1 节同（12:40:18） |
| 5G 漫游阈值 -70→-75 | 1 帧 83 B：`4a=5a46505050500a000000012c …` | 第 2 节同（12:45:15） |
| 弱信号强制切换 关→开 | 83 B（同上）+ 52 B：`4c=0001 5a=01 4d=01 60=0101` | 第 3 节同（83 B + 52 B，间隔 12 ms） |
| 弱信号强制切换 开→关 | 83 B + 52 B（`5a=00`） | 原厂未单独抓 |
| `band_steer.enabled` 1→0 | 1 帧 52 B（`0x10205`） | 原厂无独立参照（原厂只在双频合一联动时改它） |
| `band_steer.rssi24` -60→-65 | 1 帧 83 B（`0x10206`，阈值未变） | 无参照 |

子节点对每条热更新帧在 2 ms 内回 CMDU ACK；`state.json` 全程 `restart_required=false`。入网首批 `PolicyConfigReq`（62 B `4d 4f 60`，92 B `4a 4c 5a 4d 60`）与第 25 轮相同，启动时没有多发通知。
差异：第 26 轮两条通知之间相隔 2 秒（服务每个循环周期只发一条）。之后改成同一助手进程里连发，并在 `live_write()` 里加了读回核对（写完后用 `dbAPIGetView`
读 8 个 `MULTIAPDOMAIN` 行或 Master 行，字段没写进去就报错，热更新日志里的 `status=0` 即读回一致）。第 29 轮（`session/replay-run29-p0-notify-pair/`，
改 `roam.force`、`band_steer.enabled`、`roam.rssi24` 的组合）：Domain 与 Master 两个变化同时出现时，`PolicyConfigReq` 83 B 与 52 B 相隔约 3 ms（原厂 12 ms），
`roam.rssi24=-62` 的 `4a` 首字节 `0x60`（= 2×(-62+110)），改回 -60 变 `0x64`；`band_steer.enabled` 变化只出一个 52 B 帧。`band_steer.enabled` 没有原厂参照帧，
这一项的验证依据是读回一致加线上帧形状。

### 11.4 第 27 轮：按频段 Renew 与槽位热更新
| 改动 | 线上（Renew 数 / M2 / 厂商查询对） | 原厂（FEATURE_MAP 第 8 节） |
|---|---|---|
| 5G 槽位 1 的 SSID | 1 个 Renew（SupportedBand=5G）、1 个 M1、1 个三分片 M2（1324/1383/209）、6 对查询 | 单频段：1/1/1/6 对 |
| 2.4G 槽位 2 隐藏 | 同，SupportedBand=2.4G；`010001` 槽位 2 回复 `0d=01` | 同 |
| 两个频段的槽位 1 安全类型 12→6 | 2 个 Renew（2.4G、5G）、各 1 个 M2、12 对查询 | 两频段：2/2/2/12 对 |
| 5G 主 SSID 安全类型 12→6、→12 | 各只 Renew 5G | 同 |

子节点读回（只读）：5G 槽位 1 的 SSID 变成新名字；槽位 1 类型配成 6 后读回 6（配成 12 时读回 3）。**隐藏标志只到了线上，没到子节点**：2.4G 槽位 2 配成隐藏后，控制器的 `010001` 回复里 `0d=01`（第 27 轮 16:23:22），但这一轮结束时子节点自己的 `ESSIDHideEnable` 读回仍是 0（配置一直保持隐藏）。也就是说停用的槽位上子节点没有存这个标志；Z506 会不会对启用的槽位应用 `0d`，没有试，原厂的行为同样没有验证。注意槽位消息的头：2.4G 是 `010001`，5G 是 `010101`（之前在这一节用 `010101` 过滤会漏掉 2.4G 的回复）。

### 11.5 第 28 轮：BeaconType 0 / 10 与 MLO 开关热更新
- 槽位 2（停用槽位，不碰主 SSID）2.4G 类型设 0（开放）、5G 设 10（纯 WPA3）：M2 第 2 分片 2.4G **1367**（少 16 字节，开放网络没有密钥属性，与 FEATURE_MAP 第 12 节"不加密第一分片短 16 字节"一致，那次动的是主 SSID 所以是第 1 分片）、5G 仍 1383；`slot_update` 读回 `beacon=0/0`、`10/10`。
- 子节点只读读数：槽位 2 2.4G 的 `BeaconType` 变成 **0**（开放）；槽位 2 5G（配置 10）**保持原来的 6**——纯 WPA3 与 Z506 M1 报的类型 `0x23`（无 SAE）取交集为空，M2 给 0，Z506 对这个槽位**忽略**，不报错、不掉线、后续 Renew 照常。恢复为 6 后两个槽位都回 6。类型 12 显示为 3、6 显示为 6、3 显示为 3，与原厂日志的交集（`0x60&0x23=0x20`、`0x22`、`0x20`）一致。**五个值：0→开放、3→WPA2、6→WPA+WPA2、10→被忽略、12→WPA2**，这就补上了 GAP P0 第 3 项，也回答了 GAP 第 4 节第 3 条里"Z506 对 M2 认证类型为 0（纯 WPA3）的处理"（仅限停用槽位）。局限：只在停用的槽位 2 上试过，主 SSID 没有试（0 会把用户真实 Wi-Fi 变成开放网络，10 会不会让 Z506 的主 SSID 配置被忽略没有验证）；守护进程自己的 `Change Auth Type to [..]` 日志在默认日志级别下不输出，新加的 `ZTE_CONTROLLER_TRACE auth` 没有出现，需要把守护进程日志级别调到 8（原厂 `sendcmd … -l`）才可能看到，这条路没走。
- `mlo.enable` 1→0（热更新）：两个频段各一个 Renew（`0200fe` 紧跟在 Renew 之后、新 M2 之前发出，原厂是在重新入网序列之后；这一点与原厂不同），M2 第 2 分片 **1383→1353**（两个频段都是 1324/1353/209），`0200fe` 值 0（`wlan_mgr_push value=0`）；0→1：恢复 1383，`0200fe` 值 1。与 FEATURE_MAP 第 7 节"第 2 分片 1383→1353"一致。原厂那次两个频段是 1324/1353 与 1340/1353（5G 名称当时已独立，推测是名称长度跨过 16 字节加密分组边界）；这一轮两个频段名称相同，都是 1324/1353，没有单独验证那个推测。
- 子节点最终状态与第 25 轮的基线一致（槽位 1 的 5G SSID 从第 27 轮的 `ZTE-tsEG65_GUEST2` 恢复）；第 28 轮入网时的基线配置就是恢复步骤。

### 11.6 同时核实的几点
- 主机测试在构建容器里全部跑过：`controller-wsc-codec`（331 断言）、`controller-packet-codec`（162）、`cm-profile-codec`（19 场景；故障 7 改用不存在的 `BeaconType 9` 作为"不支持"用例）、`bridge-codec`（49）、`test_evidence` 17、`test_device_workspace` 14、`test_schema` 16、`test_policies` 20、`test_profiles` 28、`test_startup` 7、`test_preload`、`test_sandbox` 15，全部通过（之前"C 层测试没跑"、macOS 上 `test_evidence` 一例失败，在 Linux 容器里都不存在）。
- 第 26 轮和第 18 轮各出现一次空 `ChanSelReq`（25 字节，无 TLV）；原厂抓包里没有。`ChannelPrerenceReportReceive` 和 `_master_recv_channel_change_notify` 会发它，应与子节点重启后信道变化有关，不是本轮改动引起，没有追。
- 仍未做：`CmMultiAPSetMultiAPCfg` 的 `ASEND 0xa93c`（`Mode`/`VersionMode` 变化的重启路径；`mesh.v2` 对应的 `0xa93d` 已在第 12 节做完）；`SetRoamCatonCfg` 的 `ASEND 0xa96a`；`0x10208`；UCI → 服务配置的映射（`zte-mesh.init` 只写版本模式与两个频段的基本项，漫游/槽位/MLO 等键还没有 UCI 选项，LuCI 页面也没有）。

### 11.7 第 30、31 轮：槽位的 `enabled` 不能热更新（2026-10-17）
- 第 30 轮（`session/replay-run30-p0-slot3-enable/`）：把 5G 槽位 3（隐藏回程 BSS）的 `enabled` 热更新 1→0→1。`WLANCfg.Enable` 读回 0（写进去了），5G 只 Renew 了一次，但控制器给子节点的 `010101` 槽位 3 回复里 `0c` 始终是 `01`，子节点读回槽位 3 的 `Enable` 也始终是 1。原因（反汇编加推断）：`0c` 来自 `acdsGetBssEnable`，它读 ACD 模块内存里 `g_AcdConfig` 指向的字节（反汇编确认）；这份缓存推断是 `acdInitBssCfgList` 在守护进程启动时从 `P1905AcdBssCfg` 加载的（没有逐行核对）；原厂应是 cspd 的 `wlan_mgr` 改了 `P1905AcdBssCfg` 之后用某种通知让 ACD 刷新（推测，没有找到具体消息），而我们的数据库引擎没有变更回调（启动日志 `callbacks_available=0`）。所以**增减槽位、槽位 `enabled` 的改动仍然需要重启服务**，其余槽位字段（SSID、密钥、隐藏、安全类型）仍可热更新（第 27、28 轮已验证 SSID、安全类型到了子节点，隐藏到了线上）。
- 主 SSID 的 `radioN.enabled` 热更新走同一份 ACD 缓存的可能性很大，**没有试**（会关掉用户真实的 Wi-Fi），文档里"主 SSID 开关热更新"的说法是旧文档沿用下来的，未验证。
- 第 31 轮（`session/replay-run31-p0-enable-restart/`）是同一最终构建上的回归运行：槽位 3 的 `enabled` 改动现在只打印 `bss_enabled_changed radio=1 slot=3 restart_required=1`，`state.json` 里 `restart_required=true`，不写库、不 Renew；其后槽位 2 隐藏的热更新与恢复照常（只 Renew 2.4G，`slot_update … hidden=1/1`、`0/0`）；`restart_required` 一经置位在本次运行内不会清除（沿用原有行为）。
- 最终构建（第 29 轮）与第 25 轮的入网窗口逐项比较：M2 分片 1324/1383/209 各两组、8 个槽位回复（`010001`/`010101` 的 `0b 0c 46 11 0f 1e 0d`）、最先两条 `PolicyConfigReq`（62 B `4d 4f 60`、92 B `4a 4c 5a 4d 60`）完全相同，本轮改动没有改变入网时的线上内容。
- **没有重跑的**：实验室（合成对端）路径的 `device-controller-smoke.py`。`startup_identity` 现在在桥写入之后也会写主 SSID 的样式（`hidden=0`、`BeaconType=3`，与桥写入的值相同），预计没有差别，但没有跑。


## 12. 第 32–38 轮：GAP P1 第 6 项——mesh 级变化 = 守护进程重启（2026-10-17，真实子节点 Z506）

结论先说：原厂"mesh 级 Renew"不是 Renew，而是 **cspd 的 `multiapd_mgr` 重启 `multiapd` 进程**；两个频段的 Renew 和 6 到 40 秒的拓扑抖动都是新进程起来的副作用。
shim 现在对 `mesh.v2` 的改动做同样的事：写 `P1905ComCfg.Enable`（读回）、在同一个数据库上换一个新的真守护进程，服务不退出。

### 12.1 原厂机制（反汇编 + 日志，细节见 FEATURE_MAP 第 9、10 节的 2026-10-17 更正）

| 环节 | 位置 | 做什么 |
|---|---|---|
| `CmMultiApTopoSetP1905ComCfg` | `libieee1905cmapi.so` 0x595c | 写 `P1905ComCfg` 视图后**无条件** `ASEND(0xa93d, 空负载, "cspd.cspd.multiapd_mgr")`（0x5b1c，没有新旧值比较） |
| `CmMultiAPSetMultiAPCfg` | `libmultiapcmapi.so` | 逐字段比较新旧值生成变化位；`Mode`/`Enable`/`VersionMode`（及 +0x46a 处一个未确认字段）变化时 `ASEND(0xa93c, 整个 2952 字节视图)`；`SetRoleEnable` 变化位只触发通知 |
| cspd `MultiapMgrModuleInit` 事件表 | cspd 0x18e134，表在 0x2b6e34 | `0xa93c`→`MultiapMgrEnableModeChange`（0x18eb9c），`0xa93d`→`MultiapMgrRestart`（0x18eb40），`0x9c8a`→`MultiapMgrWlanRenew` |
| `MultiapRestart` | cspd 0x18ea08 | `PcStopProgram` 停进程（厂商日志 `exit_code = 9`，即 SIGKILL），进程退出后 `MultiapStart` 再起；计数 `start count` 加 1 |
| ACD 第一次 Renew | `multiapd`：`_handlePhyLinkUp`（0x6e7a8，ACD 事件 2）→ 定时器 3 s → `acdsTimerMsg` 的 case 3（"timer4_renew_pkg"） | 第一次"物理链路 up"事件（守护进程自己的 base 模块在 `baseDoPhyLinkUp` 里发给 topo 和 ACD，见下一段）把 `g_AcdsRenewStat` 置 1 并启 3 秒定时器（日志 `RENEW_MSG_FIRST delay 3s`）；到期后 `acdsCheckMultiapdRestart`（0x6e53c）向 `cspd.cspd.multiapd_mgr` 同步发 `SSEND(0xa96e, …, 6 秒)` 取"守护进程启动次数"，**次数大于 1 才**对每个启用的频段发 Renew（一个循环里连发，原厂线上两帧相隔 3 ms） |

事件 2 的发送者是守护进程自己：`baseNotifyAcdAndTopoIFStatus`（0x65e48）把 `{事件, 接口信息 0x40 字节}` 异步发给 `multiapd.topo.topo`，再发给 `multiapd.acd.acd`；它由 `baseDoPhyLinkUp`（0x69418）在
**新接口登记**（`baseAddSock`）时调用（对事件 2，只有接口信息偏移 0x28 的标志非零才发给 ACD），`baseDoPhyLinkUp` 的调用方是启动时扫描活动接口（`_baseGetActiveIfByAdevList`、`_baseGetActiveWlanIf`，ADEV 列表的来源没有核实）和
**收到某个接口上第一个 1905 报文**（`_baseRecv1905PacketProc`）。推断：原厂启动时扫描就把接口登记好（所以重启后 1 到 2 秒就开口、3.3 s 后发 Renew）；shim 里这条路径取不到接口，守护进程要等收到子节点的第一帧才登记，
登记时发事件 2，3 秒后发 Renew（第 39 轮三次重启都是第一帧后 3.0 s，与此一致）。`acdsCheckMultiapdRestart` 只有 `acdsTimerMsg` 一个调用方；守护进程里向 `cspd.cspd.multiapd_mgr` 发请求的还有 `I5TlvVendorP1905ComCfgSyncExtract`、`acdcConfigM2EncryptedSettings`、`_autoSetRole`，事件号都不是 `0xa96e`，不受下面的应答影响。

所以首次开机（次数 1）没有这对 Renew，重启之后每次都有；原厂日志里 6 次 `MultiapRestart`（`vendor-log-filtered-final.txt`，start count 2 到 7）都紧跟 `RENEW_MSG_FIRST`。
原厂线上序列（FEATURE_MAP 第 9 节 13:12:57 起，对应日志 02:10:09 的重启）：新守护进程发 2 个 TopoDiscovery → +1 s TopoNotification → +3.3 s **两个 Renew（相隔 3 ms）**
→ 子节点 M1 ×2、M2 ×2（1324/1353/209 与 1340/1353/209）→ 12 对厂商查询 → 子节点每 3 秒 `TopoQuery`、控制器的 `TopoResponse` 不带 `1905Nbr`，直到 13:13:38（重启后 41 s）子节点自己
`TopoDiscovery`、控制器重走 `0200fe`（len 84）→ `PolicyConfigReq`（62 字节，`60[2]` = `0001`）→ APCap/ChanPref → `PolicyConfigReq`（92）→ TopoNotification。

### 12.2 shim 的实现

- `controller-run.c`：`spawn_daemon()` 从 `controller_boot` 里拆出来（fork + LD_PRELOAD + exec）；`controller_respawn_daemon()`：SIGKILL 旧进程、`waitpid`、探测数据库池锁和订阅池锁（被杀的进程可能握着锁；探测失败则不起新进程、返回错误，服务退出由 init 重启）、再 `spawn_daemon`。
  `controller_set_v2_live()`/`controller_read_v2()`：在池锁下写 `P1905ComCfg.Enable` 并读回（标志位是一个原始字节）；`controller_read_dev_num()` 读 `MultiAPCfg.P1905DevNum`。
- `controller-service.c`：`mesh.v2` 改动 → `v2_update`（写库，日志带 `db_before/db_after`）→ 主循环 `daemon_respawn`（新旧 pid、`epoch`）；`state.json` 新增 `daemon_restarts`；重启后清掉每个守护进程的推送记录（`mlo_push`、`radio_push`、待发通知）。
  失败（写库或读回不符）退回 `restart_required`；重启失败（锁探测/fork）服务退出。
- **推送要等拓扑里有子节点**：`topology_ready()`（`P1905DevNum >= 2`）。新守护进程起来时数据库里子节点的 `Active` 还是旧进程留下的 1（run 33 的 0200fe 在抖动期被推出去、被守护进程丢弃、又被记为已发送），
  所以 `0200fe` 和信道/功率推送在 `P1905DevNum` 为 1 时不算在线，等回到 2 后再计 15 秒。日志 `topology dev_num=… epoch=…` 记录每次变化。
- **原厂的"重启后第一对 Renew"**（`controller-route.c` 里的 `SSEND` 拦截）：定时器到期时 ACD 向 cspd 要启动次数，这个请求在沙箱里没人应答（`SSEND` 失败，检查结果 0，Renew 不发）。`controller-route.c`（守护进程里已有的预加载库）
  拦截这一条 `SSEND(0xa96e, …, "cspd.cspd.multiapd_mgr")`，用监督进程通过环境变量 `ZTE_MESH_START_COUNT` 给的启动次数回答 `{i32 0, u32 ?, u32 次数}`（调用方读偏移 8 的次数、要求首字非负），其余消息原样转发；
  首次开机次数为 1，所以冷启动行为不变（仍靠原来的 `startup_renew`）。事件 2 不用我们发：守护进程在登记接口时自己发。（第 36–38 轮试过自己发事件 2，见 12.3：多余，而且第 38 轮第二次重启里它抢先用掉了一次性的 `g_AcdsRenewStat`。）
- `dev-service-smoke.py`（私有实验室，`--no-attach --port 22`）新增一步：开 `mesh.v2` → `v2_toggle_ok`（服务在跑、`daemon_restarts` 为 1、pid 换新、无 `restart_required`）。

### 12.3 各轮结果（每轮 3 个设备均在 `--expect-macs` 里，子节点 d4:b7:09:0d:77:30；调度：+35 s `mesh.v2` 1→0，+95/+110 s `roam.rssi24` -60→-65→-60，+125/+140 s 槽位 2 隐藏开/关，+160 s `mesh.v2` 0→1）

| 轮 | 证据目录 | 结果 |
|---|---|---|
| 32 | `replay-run32-p1-6-v2-readback-bug` | 读回把标志位当 ASCII 解析（原始字节），`v2_update status=-1`，退回 `restart_required`，没有重启；每次配置变动都重试一遍。同轮的 roam、槽位隐藏热更新照常 |
| 33 | `replay-run33-p1-6-respawn-partial` | 修正后重启成功（`db_before=1 db_after=0`、`daemon_respawn old=1478 new=1634`）。抓包不完整：zmesh0 抓包在接口未 up 时起不来、我又手动补抓，并误杀了同轮的 br/port 抓包（见 12.6）。第一次重启的头 30 秒缺失 |
| 34 | `replay-run34-p1-6-respawn-no-renew` | 完整抓包：重启后子节点掉出拓扑（控制器 `TopoResponse` 无 `1905Nbr`，18 次，+2.8 到 +54 s），+54.9 s 重新入网；`PolicyConfigReq` 的 `60[2]` 先 `0001` 后 `0101`；**重启后没有 Renew**；重启后的 roam 通知（83 字节 `PolicyConfigReq`，RCPI 0x5a/0x64）和 `renew-acd`（只 Renew 槽位 2 所在的 2.4G 一个频段）都到了线上，说明 OSS 路由到了新进程 |
| 35 | `replay-run35-p1-6-startup-renew-no-effect` | 重启后 5 秒再发一次 `startup_renew`（`renew-acd` 两个频段）：线上没有 Renew。重新入网 +21 s / +16 s（子节点自己的周期帧恰好早到）。`P1905DevNum` 重启后立刻变 1，入网后回 2，门控按预期 |
| 36 | `replay-run36-p1-6-linkup-sync-fail` | 当时的版本在重启后 2 秒自己用同步 `SSEND` 发事件 2：返回 -1（`phy_link_up=3`），无效；入网 +35 s / +31 s；`0200fe` 在入网后 +15 s 补发（门控生效） |
| 37 | `replay-run37-p1-6-linkup-async` | 改成异步 `ASEND` 发事件 2，返回 0，但线上仍没有 Renew：定时器到期时 `acdsCheckMultiapdRestart` 的 `SSEND(0xa96e)` 没人应答（反汇编找到的真实原因）。同轮冷启动：子节点第一帧在启动后 2 s 到，`startup_renew` 的 Renew +4.9 s 发出 |
| 38 | `replay-run38-p1-6-respawn-final` | 加上 `controller-route.c` 的应答（仍自己发事件 2）：第一次重启 **+4.95 s 两个 Renew（相隔 12 ms）→ M1 ×2（+5.05/+5.16 s）→ M2 ×2 → 厂商查询**，与原厂 12.1 的序列一致（子节点第一帧恰好在 +4.76 s 到）；+34.8 s 重新入网，`PolicyConfigReq` 62/92 字节，`60[2]=0001`；`0200fe` +50 s。第二次重启（`mesh.v2` 0→1）：`60[2]=0101`，+29.8 s 重新入网，`0200fe` +44.9 s，但**没有 Renew**——我们的事件 2 在守护进程登记接口之前就用掉了一次性的 `g_AcdsRenewStat`，3 秒定时器到期时守护进程不认识任何接口 |
| 39 | `replay-run39-p1-6-stand-in-only` | **最终版**：去掉自己发的事件 2，只留 `SSEND(0xa96e)` 的应答。三次重启（`mesh.v2` 1→0→1→0，+35/+105/+175 s）**每次都有 Renew 对**：守护进程收到子节点第一帧后 3.0 s 发出（+21.15、+11.14、+32.25 s，两帧相隔 12 到 20 ms）→ M1 ×2 → M2 ×2；重新入网 +18.1、+8.1、+58.1 s，`60[2]` = `0001`/`0101`/`0001`，`0200fe` 在入网后 15 到 19 s；`daemon_restarts:3`。等于实测了"事件 2 由守护进程自己在登记接口时发" |

第 39 轮结束时：服务 `running`、`daemon_restarts:3`，`restart_required:false`；子节点读回 `P1905ComCfg.Enable=0`（控制器 `60[2]` 的 Enable 位子节点没有采用）、`MarketConfEn=1`、漫游阈值 -60/-70，与基线一致；路由器无残留进程/目录。

### 12.4 与原厂的差异和限制

- 新守护进程在**收到子节点的下一帧之前不主动发任何帧**（run 38 第二次重启：重启后 30 秒内控制器一帧没发，直到子节点 20:51:00.78 的 TopoDiscovery）；原厂启动后约 1 到 2 秒内自己发 2 个 TopoDiscovery、再过 1 s 发 TopoNotification。
  原因见 12.1（推断：接口要到收到第一个 1905 报文才登记，启动扫描在 shim 里取不到接口）。子节点的周期帧在 :00.8/:30.8 上，所以重新入网耗时取决于重启落在这个周期的哪里：15 次重启（第 33 到 39 轮）里 8 到 58 s（原厂一次样本 41 s）。
  第一对 Renew 因此也晚到：在守护进程登记接口（收到第一帧）后 3 秒才发，不是重启后 3.3 秒。冷启动同理（run 35–38 的冷启动里，`P1905DevNum` 变 2 在启动后 9 到 24 秒）。没有去模拟 cspd 的接口事件或 ADEV 列表。
- Renew 对相隔 12 ms（原厂 3 ms）、距重启 4.95 s（原厂 3.3 s，shim 的守护进程起来更慢）、`0200fe` 在入网后 15 秒（原厂入网当时）——`wlan_mgr` 没有常驻替身，靠轮询 `Active` 加拓扑门控。
- 冷启动的 `startup_renew`（启动后 5 秒的 `renew-acd`）是否出 Renew 取决于子节点的第一帧什么时候到：run 37（lan1 抓包）子节点第一帧在启动后 2 s 到，`startup_renew` 的 Renew 在 +4.9 s 发出（两帧相隔 100 ms）；
run 35、36、38 的子节点第一帧在启动后 9、23、22 s 才到（run 34 同样没有 Renew），`startup_renew` 在 +5 s 时没有任何 Renew 帧。与 12.4 第一条一致：守护进程要收到子节点的帧才会往外发。没有改冷启动。
- 只做了 `mesh.v2`。`version_mode` 仍是 `restart_required`（启动时还会写其它依赖它的行）；`SetRoleEnable`/角色选举不适用（shim 固定 `Mode=2`，见 GAP 第 7 项）。`MaxHop` 变化位走 `CmMultiAPWlanRenew` + `0xa932`（`cspd.wlan_manage.wlan_mgr`），没做。
- 槽位 `enabled`/增减槽位：重启守护进程会让 ACD 重新加载开关位缓存（推断，没试），可能比整个服务重启更轻；本轮没改。

### 12.5 主机侧测试

构建容器里的全部主机测试（C 编解码、Python）通过（构建 13）；私有实验室 `device-controller-smoke.py`（含 CMDU 10）通过（`probe_exit=0`、`host_state_unchanged=True`），
`dev-service-smoke.py --no-attach --port 22` 通过（含新增的 `mesh.v2` 重启步骤，`v2_toggle_ok=True`）。这两项同时验证了 P0 阶段之后 `startup_identity` 里主 SSID 样式改动没有破坏实验室路径。

### 12.6 测试工具的一个事故（已修）

`mreplay.py` 收尾用 `pkill tcpdump`，而路由器的 busybox 没有 `pkill`：每一轮的 br/port 两个 tcpdump 都没有被杀掉，30 多轮累计 66 个进程，各自往已删除的 tmpfs 文件里写，占了约 130 MB 内存
（`free` 里 used 247 MB → 清掉后 108 MB，shared 94 MB → 0.7 MB）。已全部清掉；`mreplay.py` 改为按命令行（含 `/tmp/zmc/`）逐个 `kill`，并且 zmesh 抓包等 `operstate` 为 up 才开始（原来只等接口存在，run 33 因此失败）。
证据：`tools/mreplay.py`、`tools/respawn_report.py`（本节表格的数据来自它：`respawn_report.py PCAP EPOCH...`）。

## 13. 第 40 轮与实验室：GAP P1 第 7、8 项——角色选举与 CurHop（2026-10-17）

### 13.1 先查：服务实际用的 `MultiAPCfg` 是什么

- `db-engine-probe` 的服务路径在注册表时调用 `map_cfg_create_test_table()`（名字里的 test 是历史命名），写入 `multiap-cfg.c` 的固定值：`Mode 2`、`Enable 1`、`SyncEnable 1`、`SetRoleEnable 1`、`EnMasterOnly 1`、`CurHop 2`、`MaxHop 2`（注册默认 `Mode 0`、`EnMasterOnly 0`、`CurHop -2`）。
  重放用的 `db-profile2` 只覆盖 34 个字段，**不含** `Mode`/`Enable`/`SetRoleEnable`/`EnMasterOnly`/`CurHop`/`MaxHop`/`RoleElect`，所以所有真机轮次（第 14–39 轮）都是这组值。原厂基线是 `SetRoleEnable 0`、`EnMasterOnly 0`。
- 新增服务日志 `ZTE_MESH_SERVICE cfg mode=… set_role_enable=… en_master_only=… cur_hop=… max_hop=… epoch=…`（只在变化时打印）。实验室（无 profile、`mesh.role=master`）实测：`mode=2 set_role_enable=0 en_master_only=1 cur_hop=2 max_hop=2`。

### 13.2 第 8 项 `CurHop`

- `slave_init` 会把原厂主控的 `CurHop` 重置成 -2，shim 的 `EnMasterOnly=1` 把这个函数整个跳过（`multiap-cfg.c` 注释，早先反汇编），所以 `CurHop` 恒为 2：
  实验室里 `mesh.v2` 重启、`mesh.role` 的两次重启前后，以及第 40 轮的两次重启前后，`cfg` 日志都没有出现过 `cur_hop` 的变化。
- 线上：`HigherLayerResponse` 的厂商子 TLV `0x43` 就是发送者的 `CurHop`（`I5TlvHigherLayerVendorTypeInsert` 反汇编）。原厂 4 帧全是 `00000002`，其中 13:15:32.08 那帧在 -2 快照（13:15:31）之后 1 秒；shim 第 39 轮 6 帧也全是 `00000002`。
  结论：-2 是原厂库里几秒的瞬态，有没有到过线上这几帧没有证明；shim 的 2 与观测到的每一帧原厂报文一致，第 8 项关闭。

### 13.3 第 7 项：角色选举

原厂机制见 FEATURE_MAP 第 10 节的 2026-10-17 补充：`Mode 0 = 自动`；"自适应"提交 = `Mode 0` + `SetRoleEnable 1` → 第一次重启；新守护进程里 `acdGetRole` 在 `SetRoleEnable && Mode 0` 时直接取 Master；
cspd 的 `cooperator_mgr` 判定路由模式 → ACD 的 `_autoSetRole(2)` 写 `Mode 2`、`SetRoleEnable 1`，并 `ASEND(0xa93d)` 要求第二次重启（约 3 秒后）。

**shim 的做法**（`mesh.role=master|auto`，可选；缺省不动数据库；出现/消失需重启服务；`slave` 和其它取值被拒绝）：
- 启动：写 `Mode 2`、`SetRoleEnable`（`master` 为 0，`auto` 为 1，选举后的稳定态），在 profile 之后应用。
- 热更新 `master→auto`：写 `SetRoleEnable 1`、`Mode 0`，守护进程重启；约 3 秒后写 `Mode 2`，再重启一次。**第二步是监督进程替 cspd 的 `cooperator_mgr` 做的模拟，不是原厂机制**：
  实验室里 `Mode 0` 停了 12 秒以上没有自己变回 2（守护进程自己不完成选举；`_autoSetRole` 要等 cooperator 的判定），所以才加了 `role_settle_at`。
  `auto→master`：只清 `SetRoleEnable`，不重启（FEATURE_MAP 10.1）；若在 3 秒选举窗口内切回，立即写 `Mode 2` 并重启。
- 实验室（`dev-service-smoke.py --no-attach --port 22 --extra mesh.role=master --then mesh.role=auto --then mesh.role=master`，证据 `session/lab-role-smoke-p1-7.json`）：`role_update role=auto` 与第一次 `daemon_respawn` 同一秒，
  `cfg mode=0 set_role_enable=1`，2 秒后 `mode=2`，第二次 `daemon_respawn` 在 3 秒后；`role_update role=master`：`set_role_enable=0`，无重启；`cur_hop` 始终 2。
- 第 40 轮（真子节点，`replay-run40-p1-7-role-auto`；`mesh.v2=1`，+35 s `auto`，+110 s `master`）：两次 `daemon_respawn` 相隔 3 s（第一次 `Mode 0`，第二次 `Mode 2`），终态 `mode=2 set_role_enable=1`；
  第二次重启后 +5.83 s 出现 Renew 对（相隔 11 ms）、M1 ×2；+29.4 s 重新入网，`PolicyConfigReq` 62/92 字节，`60[2]=0101`（线上没有任何角色相关内容，与原厂一致），`0200fe` +45 s；`auto→master` 后 `cfg mode=2 set_role_enable=0`，没有重启。
  与原厂第 10 节同一段（`vendor-eth4-1905-final.pcap` 13:15:26 起）的对照：原厂也只有最后一个守护进程发 Renew 对（第一个活不过 3 秒定时器）、之后 M1/M2、重新入网；以第二次重启请求为零点（`respawn_report.py` 同一口径）：原厂 Renew +5.4 s、重新入网 +9.7 s，shim Renew +5.8 s、重新入网 +29.4 s（重新入网的快慢取决于子节点的周期帧，见第 12.4 节）。
  差异：这轮 Renew 后子节点的两个 M1 没有得到 M2（守护进程那时还没把子节点登记进拓扑），子节点靠 +20 s、+35 s 的 `APAutoconfSearch` 重新入网（M2 在 +21.2 s、+36.1 s），原厂是 Renew 后立刻有 M2。
- 实验室另测了两条文档里的行为（证据 `session/lab-role-smoke-p1-7-slave-reject.json`、`…-key-appears.json`）：`mesh.role=slave` → `config=rejected`，没有 `role_update`、`cfg` 不变，之后合法的 `auto` 仍照常应用；起始配置不带 `mesh.role`、之后加 `mesh.role=auto` → `role_key_changed restart_required=1`，没有新的 `daemon_respawn`。
- 子节点前后读回：`Mode 1`、`SetRoleEnable 1`、`RoleElect 0`、`MaxHop 2`、`CurHop 2`、`P1905ComCfg.Enable 0`，与基线一致；路由器无残留进程/目录（`ps | grep tcpdump` 为 0）。

### 13.4 限制和没做的

- **Slave 不支持**（需要守护进程以 agent 身份在 OpenWrt 上应用 WLAN 配置，是另一个产品）；因此"桥接模式 → Slave"的判断没做，`auto` 一律落在 Master（路由模式）。
- ACD 自己的 1905 选举协议（`_autoRoleElect`/`_autoSendSearch`，设备之间 Search/Response 找控制器）没有触发，也没有验证：实验室里没有对端，守护进程要收到第一帧才登记接口（第 12 节），Mode 0 的 3 秒窗口里它基本是聋的。
- 守护进程发的 `ASEND(0xa93d)`（`_autoSetRole`）没有拦截成"监督进程重启"：这条路径在 shim 里不会被触发（选举不完成），只有上面的模拟。若以后有会触发它的功能，要在 `controller-route.c` 里拦截 `ASEND` 并通知监督进程。
- 缺省（不写 `mesh.role`）时数据库是固定值：`Mode 2`、`SetRoleEnable 1`（"自适应已选出 Master"的终态），不是原厂基线的 `SetRoleEnable 0`；线上无差别。
- 主机测试（构建 13）通过；`mesh.role` 的 UCI/LuCI 映射没做。

## 14. 第 41–46 轮与实验室：槽位 `enabled`、增减槽位热更新（2026-10-17）

### 14.1 为什么原来要重启（第 30 轮的推断是错的，真实原因）

反汇编 `multiapd` 的 ACD 模块（`acdGetWLANCFG`、`acdInitBssCfgList`、`_acdGetEnConfigBss`、`acdInitDefBssCfgList`、`acdInitConfigBssCfg`、`acdsGetBssEnable`）：
- ACD 只在启动时（`AcdMain` → `acdGetACDBaseInfo`，唯一调用方）建一次 `g_AcdBssCfgList`：先为 2 个频段 × 4 个槽位各建一个默认节点（标志 0），再按 `P1905AcdBssCfg` 的每一行（`WlanInstId2Index` 解出频段和槽位）把节点的标志设成该行 `CustomTag`、把节点的"自定义启用"设成该行 `Enable`。
- 之后每次 Renew（`_handleWlanRenew`）和每次作为 M2 / 厂商查询的来源，都会调 `acdGetWLANCFG(频段)` 重新构建该频段的 `g_AcdConfig`：对 4 个槽位逐个取 CM 配置；`_acdGetEnConfigBss` 在 `CustomTag` 没有 bit2 时返回 -1，**该槽位整个被跳过**（不进 M2、不进槽位回复）；
  槽位的"启用字节"取 CM 输出里的 `Enable`（偏移 412），频段射频关了则为 0；**只有 `CustomTag` 的 bit0 置位时才改用节点里的"自定义启用"**（启动时从 `P1905AcdBssCfg.Enable` 读到的那个值，之后不再变）。
  槽位回复里的 `0c` 就是这个字节（`acdsGetBssEnable`）。
- 原厂基线的 `P1905AcdBssCfg` 是 0 行：全部节点标志为 0，`acdInitConfigBssCfg` 给每个频段的 4 个槽位都置 bit2（无 bit0）→ 启用位每次 Renew 都从 `WLANCfg.Enable` 现读，所以原厂开关访客 Wi-Fi 不重启守护进程。
- shim 的 CM 适配层（`controller-profile.c`，替换了 `CmWLANGetAPConfig`）从来没填 `Enable` 字节（`cm-profile-layout.inc` 里没有它），所以 shim 当年必须置 bit0（`CustomTag=69`）才不被整体"拆除"；代价就是启用位被冻结在启动时的值。
  第 30 轮看到的"`0c` 不变"是这个原因，不是"没有变更回调"；原先文档里"原厂靠 cspd 的变更通知刷新"的猜测也没有根据，已改。
- 实验室先验证（`device-controller-smoke.py` 的 `map_teardown`，不碰子节点）：`CustomTag=68`（bit2+bit6）而适配层不补 `Enable` → `map_teardown=1`；适配层补上 `Enable`（`WLANCfg.Enable`，偏移 412，置位标志在偏移 386）→ `map_teardown=0`。

### 14.2 实现

- `controller-profile.c`：`CmWLANGetAPConfig` 补 `Enable` 字节；不存在的槽位（`missing`）为 0。
- `CustomTag` 改 68（0x44）：启动时的主槽位行和额外槽位行都是（`create_extra_slot_rows`，启动和热更新共用同一段代码）。
- **槽位 `enabled`**：和 ssid/密钥一样热更新：写 `WLANCfg.Enable`（以及 `P1905AcdBssCfg.Enable`，保持一致），只 Renew 那个频段。
- **增减槽位**：加 = 行不存在则建（`WLANCfg`、`WLANPSK`、`P1905AcdBssCfg`、4 行 WEP；行号取当时的行数），行已存在（启动时建过或之前加过）则更新字段并重新标为存在（`InstExist 1`、`CustomTag 68`）；减 = `InstExist 0`、`Enable 0`、`CustomTag 0`（行保留，读起来和从没配置过的槽位一样：`CmWLANGetAPConfig` 的 `missing`）。
  ACD 只在启动时读 `P1905AcdBssCfg`，所以两者都在写表后重启守护进程（第 12 节机制）；失败退回 `restart_required`。
- **新的配置校验**：同一无线电上配置的槽位必须是 1..k，否则整份配置被拒绝（`fail=config-missing-or-invalid`，实验室验证）。原因见 14.4。
- `dev-service-smoke.py` 的 `--then` 步骤日志加了 `slot_add`/`slot_remove`/`slot_update`。

### 14.3 各轮结果

| 轮 | 证据目录 | 结果 |
|---|---|---|
| 41 | `replay-run41-slots-enable-live-a` | **事故**：只把 `CustomTag` 改 68、适配层还没补 `Enable`，第一次 Renew（频段 1）发出的 M2 是 895 字节的拆除 M2（正常 1324+1383+209），子节点的 5G 主 SSID（AP5）和槽位 3（AP8）被关，约 4 到 5 分钟 |
| 42 | `replay-run42-restore-after-teardown` | 回退到旧版本、全部槽位 + 一次频段 1 的 Renew，M2 恢复正常大小，子节点 AP5、AP8 回到 `Enable 1` |
| 43 | `replay-run43-slot-enable-live` | 修好适配层后：radio1 槽位 3 `enabled` 1→0→1，**没有守护进程重启**，每次只有频段 1 一个 Renew，M2 1324+1383(+209)，槽位 3 回复 `0c=00` / `0c=01`、`0d=01`，子节点 AP8 读回 0 / 1，AP1/AP4/AP5 始终为 1；与原厂第 11、13 节同形 |
| 44 | `replay-run44-slot-add-remove` | **事故 2**：radio1 去掉槽位 2（槽位 3 还在）→ 频段 1 的 M2 变成 1324+911（3 个 BSS 条目），子节点把槽位 3 的设置套到了 AP7（开启、隐藏）并把 AP8 关了。结论：M2 的 BSS 条目是**按位置**对应子节点槽位 |
| 45 | `replay-run45-restore-after-gap` | 全部槽位 + 频段 1 的 Renew，AP7 回 0、AP8 回 1 |
| 46 | `replay-run46-slot-add-remove-prefix` | 前缀合法的序列：+40 s radio0 加槽位 3（行是热更新时新建的）、+130 s radio1 去掉槽位 3、+220 s radio1 把槽位 3 加回来（复用行）。三次都是写表 + `daemon_respawn`；每次重启后 Renew 对（+12.3 s，守护进程收到子节点第一帧后 3 s）、M1/M2；去掉后频段 1 的 M2 是 1324+911（3 个 BSS），子节点 AP8 读回 0，其余不变；加回后 1324+1383+209，AP8 回 1；最终子节点回到基线 |

实验室：`dev-service-smoke.py --no-attach` 的加槽位 → 去槽位 → 再加（复用行）→ `enabled` 切换序列全部成功（后者只有 `slot_update`，没有重启）；带空槽位的配置启动即被拒绝。

### 14.4 事故、规则和限制

- **两次事故**：第 41 轮关掉了用户真实的 5G 主 Wi-Fi 约 4 到 5 分钟；第 44 轮没动主 Wi-Fi，只是 AP7（Wi-Fi 5 兼容槽，原本关闭）被开成隐藏、AP8（隐藏回程 BSS）被关，约 2 到 3 分钟。都用"全部槽位 + 一次 Renew"恢复。恢复后读回了子节点 `WLANCfg` 全部 8 行的 `InstExist`、`IfName`、`Enable`、`ESSID`（名称，不含密码）、`BeaconType`、`ESSIDHideEnable`、`VapIsolationEnable`、`GuestSsid`、`MapAccessAttr`、`ACLPolicy`，与第 28 轮结束时的基线逐行相同（AP7：关、`cc2-5Gc-wifi5`、BeaconType 6、不隐藏、MapAccessAttr 32；AP8：开、`ZMESH-c4FsH5_5G`、BeaconType 6、隐藏、MapAccessAttr 64、ACLPolicy 0）。`WLANPSK` 没读：槽位的密码在所有配置里都是用户指定的密码，与基线相同，但没有核对子节点上的值。读完删除了子节点上的 `DB pf` 导出文件。教训已记入记忆：改动 M2 内容的东西先在实验室验证（`map_teardown` 必须为 0），真机运行中轮询子节点的 `WLANCfg`，一出异常立刻恢复。
- **槽位按位置对应**：M2 里没有的槽位，子节点会把它**关掉**（去掉期间只读回了 `Enable`，名称等其它字段是否保留没有读）。所以"去掉槽位"在子节点上等于关闭那个槽位；去掉槽位 3 会关掉回程 BSS（AP8）。配置只允许前缀 1..k。
- 增减槽位要重启守护进程：子节点掉出拓扑 8 到 58 秒（第 12 节）。`enabled` 没有这个代价。
- 主 SSID 的 `radioN.enabled` 现在走同一条现读路径（`WLANCfg.Enable`，由配置桥写入），预期热更新有效，但没有在真子节点上试（会关真实的 Wi-Fi）。
- `version_mode` 仍需重启服务（启动时写的行依赖它，且没有在真子节点上试过别的值）。

### 14.5 行为变化和回归核对

- **`radioN.enabled=0` 现在真的会关掉子节点的主 SSID**（原厂行为）。以前主槽位的 `P1905AcdBssCfg.Enable` 被写死成 1 且 bit0 置位，ACD 总是按"开"下发，`radioN.enabled` 被悄悄忽略（`WLANCfg.Enable` 写了但 ACD 不看）。现在 ACD 每次 Renew 都读 `WLANCfg.Enable`（配置桥按 `profile.enabled` 写）。
  静态核对：`radioN.enabled` 是必填键（缺了整份配置被拒绝，没有默认 0；`wifi.dual_band_sync` 只复制 radio0 的 ssid/密钥给 radio1，`enabled` 各写各的）；OpenWrt 的 `zte-mesh.init` 从 UCI 的 `wifi_24g_enabled`/`wifi_5g_enabled` 取值，默认都是 1。所以只有用户明确写 0 才会是 0。
- **配置校验变严**：同一无线电上有空槽位（有槽位 3 没槽位 2）的配置以前能启动，现在启动即被拒绝（`fail=config-missing-or-invalid`）。
- **回归核对**（CustomTag 69→68 和新的 `Enable` 字节会影响所有 `CmWLANGetAPConfig` 的使用者）：把第 39 轮（旧代码）和第 46 轮（新代码）里一次 Renew 之后 45 秒内控制器发出的 `APAutoconfWSC`/`VendorSpecific`/`PolicyConfigReq` 逐帧签名比较：
  16 个共有签名完全相同（两个频段的 M2 分片 1324/1383/209、12 个厂商查询回复里全部的槽位回复 `0c 0d 0f 1e 46 11`、频段回复），差别只有第 46 轮窗口里多出一对重新入网后的 `PolicyConfigReq`（62/92 字节，`60[2]=0101`，窗口位置不同造成）。
  `TopoResponse`（259/275）、`HigherLayerResponse`（501）的长度分布两轮相同；`CombinedInfraMetrics` 的长度随启用的 BSS 数变（第 39 轮 179；第 43 轮槽位 3 关闭时 200/166；第 46 轮 206/179/166/139），符合它按 BSS 条目数增减的设计。

## 15. 第 47–49 轮及实验室：5G 名称派生、`0200fe` 入网触发源、`BeaconMetricsQuery`、访客槽位、UCI 映射（2026-10-17）

### 15.1 `0200fe` 在入网当时发：查了触发源，没有改（第 47、48 轮，真子节点，只读观察）

目标：原厂入网时在第一条 `PolicyConfigReq` 前 7 到 13 毫秒发 3 条 `0200fe`（第 9 节），shim 只在上线满 15 秒后发 1 条。这条消息只对 Wi-Fi 7 子节点有意义（第 9.2 节：Z506 的 `WLANSetting` 没有 `MLOEnable`），所以只能比线上时序，没法验证行为。
- 第 9.1 节认定的触发是守护进程对 `"multiapd.topo.dev"` 的发布 `ASEND(0xa937, {MAC[6], u16})`（`i5DmNotifySlaveDevON`，0x92120）。**我们的守护进程里它没有发**（第 48 轮的证据见下）：函数里有一个 `topology+0x8a1 == 1` 就整段跳过的条件，`0x8a1` 是什么我没有查。第 47 轮在 trace 库里加了对这条发布的应答（读 `WLANSetting.MLOEnable` 后 `ASEND(0xa97e)`），线上一条 `0200fe` 也没有；第 48 轮给 trace 库的 `ASEND` 包装加了 `device_event` 日志（0xa937/0xa938 的目的地），三次入网（一次启动、两次守护进程重启）每次只看到一条 `0xa937 → "multiapd.map.slave"`，没有 `topo.dev`。
- 这条 `map.slave` 的消息来自 `master_send_dev_on_msg`（0x409a4），守护进程拿到子节点第一份拓扑回复后约 1 秒发出；而第一条 `PolicyConfigReq` 在那之后 6 到 38 秒才出现（子节点抖动后重新入网，与第 12 节一致）。所以拿它当触发，`0200fe` 会比原厂早几十秒，不是"前 7 到 13 毫秒"，我没有实现。
- `slave_dev_online`（0x611c0）确实会 `ASEND(0xa937, 空, "cspd.wlan_manage.wlan_mgr")`，但那是 slave 模块的代码，`EnMasterOnly=1` 时 `slave_init` 被跳过。原厂主控的 3 条消息由 cspd 里 wlan_mgr 收到什么事件触发，需要读 cspd，没读。
- 结论：shim 仍是"上线满 15 秒后 1 条，MLO 开关变化时再发"。未做，没有可验证的收益。trace 库保留了 `device_event` 日志（独立的 48 条预算）。证据 `session/replay-run47-0200fe-hook-silent/`、`session/replay-run48-0200fe-device-events/`（子节点未改动，用的是基线配置 + `mesh.v2` 切换触发重新入网）。
- 顺带踩的坑（已记入记忆）：给 `multiapd` 多传第二个环境变量会让它在启动时被 ld.so 以 `dl-minimal.c: realloc` 断言杀掉（退出码 127）；一开始我把它当成 ASEND 拦截的问题查了三个版本。

### 15.2 `AssocSTALinkQuery` 和 `BeaconMetricsQuery`（对原厂抓包和各轮 `zmesh.pcap` 的统计）

- **`AssocSTALinkQuery` 和原厂一样**：原厂 12:32 到 12:54 每分钟 24 条（2 个终端，每个每 5 秒，紧跟 `APMetricsQuery` 1 毫秒），shim 每个终端同样每 5 秒一条。之前"频率更高"的说法是错的；我对比用的 13:30 到 13:32 窗口里原厂当时没有终端接入。
- **`BeaconMetricsQuery`：不是每 5 秒，是一小串**。第 14 轮两个终端各恰好 15 条（= 域的 `BsMeasureLmt` 15），前 68 秒发完就停；第 35 到 46 轮每轮 1 到 7 条；第 15、16、17、21、22 轮（只有终端 …9730 在线）为 0。
- **原厂 1.5 小时的抓包一条也没有**，包括：12:32 到 12:54 两个终端在线；14:03:09 终端重新接入后 30 次 `AssocSTALinkQuery`（约 2.4 分钟）；以及四次 `multiapd` 重启之后。所以"原厂的预算早就用完了"不成立，也不是"纯环境"。
- 机制（`multiapd` 反汇编）：`master_beacon_metrics_query`（0x35428）的调用者有 `master_sta_get_dualband_by_rrm`（0x35564，来自 `master_sta_status_transition_strategy`）、`master_update_sta_timer`、`master_do_11k_measure_roaming`、`master_check_valid_candidate_ap` 等；周期性的一小串来自 `master_sta_get_dualband_by_rrm`，它在这些条件**全部**满足时才发：`sta+0x3c == 0`、`g_tMaster+0x47c != 0`（从主控配置装入，默认 1）、`sta+0x304 == 3`、`sta+0xc8` 的 bit1 为 0（应是"已知双频"）、`sta+0x40`（u16）不为 0、`所属域+0x144 != 0`（可能是 `EnStaBandSteer`）、已发次数（`sta+0x5d0`）小于域的 `BsMeasureLmt`（`+0xec`），再对另一频段的每个 BSS 发一条。
- **原厂为什么条件不满足仍没查明**：库里 `EnBandSteer`、`EnStaBandSteer`、`BsMeasureLmt` 和 shim 一致；`sta+0xc8` 和 `sta+0x40` 的来源（写入点太多）没有逐个追。对子节点无害（Z506 回 `not support beacon report`），线上多一小串帧。

### 15.3 5G 名称派生和 `DualBandSync` 热更新（实验室 + 真子节点第 49 轮）

- 实现：`mb_derive_name`（`mesh-bridge.c`，主机测试 56 条里含 7 条派生用例：`cc` + `-5G`、31 字节边界、32 字节拒绝、空/过长/不可打印后缀、基名为空）；服务配置新增 `wifi.essid_suffix`，`radio1.ssid` / `radio1.key` 在双频合一关闭时可省略（省略 = 2.4G 名称 + 后缀 / 2.4G 密钥）；`controller_set_wlan_setting_live` 写 `WLANSetting.DualBandSync` 和 `ESSIDSuffix` 并读回，`DualBandSync` 变了就 Renew 两个频段。**以前这两个字段只在启动时写**。
- 实验室（私有，`dev-service-smoke.py --then`，2.4G 名称 8 字符）：`~radio1.ssid` 派生 → `ssid_len=8,11`（只 Renew 5G，`bands=2`）；再省略密钥 → 仍 `8,11`（密钥变成 2.4G 的，`bands=2`）；`wifi.dual_band_sync=1` → `wlan_setting_update status=0`、`bands=3`、`8,8`；改后缀 `-5GHz` 且 sync 关 → `8,13`；空后缀 → `config=rejected`；`wifi.dual_band_sync=1` 同时给 `radio1.ssid/key` → `config=rejected`。
- 没有强制"关双频合一时关频段引导"（原厂在同一步 `EnBandSteer 1→0`）：这是 UCI/init 层的事，目前 `band_steer` 是独立选项。
- **真子节点**：只做了名称不变的版本（第 49 轮，见 15.4）。派生的改名会改用户真实的 5G 主 SSID，没有试；它走的是已验证的"主 SSID 改名 → 只 Renew 5G"路径。

### 15.4 第 49 轮（真子节点 Z506）：`DualBandSync` 热更新（名称不变）和访客槽位 1 的开关

配置：run46 的基线（`dual_band_sync=1`，槽位：2.4G 1–2，5G 1–3，槽位 3 隐藏回程）；+60 s `wifi.dual_band_sync=0` 并显式给 `radio1.ssid=cc`、`radio1.key`（名称和密钥不变）；+150 s 换回 1；+240 s 两个频段的访客槽位 1 `enabled=1`；+330 s 关回。LAN 终端仍按 `--expect-macs` 放行。
- 日志：`wlan_setting_update dual_band_sync=0/1 status=0` + `profile_update=ok bands=3`；`slot_update radio=0/1 slot=1 enable=1/1` + `bands=3`；每次切换线上一个 Renew 对（相隔 0.1 秒）→ M2 ×2（2.4G 1324+911，5G 1324+1383+209，四次大小完全相同）。没有守护进程重启。
- 子节点读回（`DB pf WLANCfg` 的 `InstExist`、`Enable`、`ESSID` 名称、`BeaconType`、`ESSIDHideEnable`、`VapIsolationEnable`、`GuestSsid`、`MapAccessAttr`、`ACLPolicy`，不含 `WLANPSK`）：访客开着期间（切换后约 35 秒）AP2、AP6 的 `Enable=1`，其余行与基线相同；结束后 8 行与开始前逐行相同（`session/` 里没有存读回文件，输出只在会话中比较）。
- **访客隔离没有验证**：访客开着时 AP2、AP6 的 `VapIsolationEnable` 读回仍是 0（控制器的槽位 1 行里按原厂基线是 1）；子节点是否应用这一项、访客网络的行为是否和原厂一样，没有查。访客 SSID 在两个频段上开了约 90 秒，密钥是用户的 Wi-Fi 密码。
- 证据 `session/replay-run49-p1-dualband-guest-live/`。路由器和 mac 上的配置文件、脚本已删，子节点上的 `/var/tmp/dbtable` 已删。

### 15.5 UCI 与 LuCI 映射，以及顺带发现的服务配置 bug

见 SERVICE.md "UCI 与 LuCI 映射"。测试：`tests/test_init.sh` 45 条（busybox ash 和 macOS sh 都过）、`node tests/test_luci.js`、`dev-config-check.py`（init 生成的 8 份配置交给私有实验室里的真服务，含一份故意重复键的负例）。
这条链找到一个老 bug：`parse_config` 里"ACL 规则的 `bss`、`radioN.bssK.acl_policy` 必须指向已配置的槽位"的检查在槽位表填好**之前**执行（2026-10-17 之前的备份里就是这个顺序），所以带槽位的 ACL 规则和槽位策略一直被整份拒绝；已把槽位块挪到前面。实验室：`acl_update rules=1/2 status=0`。**槽位上的 ACL 没有在真子节点上试过**（第 4.6 节说过 Z506 的 ACL 规则不分接口，效果要单独验证）。
**init 和 LuCI 没有在真实 OpenWrt（procd、rpcd、LuCI）上运行过**：init 用 UCI 读取桩测，LuCI 用 form 桩测。

### 15.6 本轮之后仍开着的事

- `0200fe` 入网时发 3 条的触发源（要读 cspd 的 wlan_mgr）。
- `BeaconMetricsQuery` 原厂不发的原因（15.2 的哪个条件不满足）。
- 派生的 5G 名称、槽位上的 ACL、主 SSID `radioN.enabled` / `hidden` 热更新在真子节点上没试。
- UCI/LuCI 未在真实 OpenWrt 上运行；mac 上的源码仍未进 git。

## 16. 第 50–54 轮及实验室：GAP P2（子节点管理、拓扑页、IPTV、访客、定时）（2026-10-18）

机制的静态分析和 Web 请求见 FEATURE_MAP 第 17 节。这一节记录实现和验证。

### 16.1 P2 第 9 项：重启子节点、开关指示灯（第 50、51 轮，真子节点）

- 实现：`zte-meshctl reboot-node <AL MAC>`、`light-node <AL MAC> 0|1`（`ASEND(0x90a0 | 0x909f, 16 字节 {MAC[6], …}, "multiapd.topo.dev")`，重启的值在第 8 字节，指示灯的值在第 6 字节，全零 MAC 被拒绝）；服务里的"节点动作"通道：宿主往 `/run/zte-mesh/actions/<id>.act` 写一行 `reboot <mac>` 或 `light <mac> 0|1`（0600，服务每秒读一次，读完即删；服务启动时先清掉遗留的请求，重启不会被重放），服务只对**在线且已识别为中兴设备**的节点执行，结果（0 已发出、-2 格式错、-3 无此在线中兴节点、-4 守护进程没就绪、其它 = helper 状态）写进 `state.json` 的 `actions`；rpcd 方法 `reboot_node`、`light_node`（写请求、最多等 8 秒取结果）；LuCI 节点表里的"重启""开灯""关灯"按钮（重启要确认）。
- **真机**（第 51 轮，AL 前缀 41，子节点在线、已被识别为中兴设备）：`light 0` → 线上 `0019c6 02 00 07 | 21 0007 d4b7090d7730 00`，子节点 `LEDCfg.EnableLEDCfg` 1→0；`light 1` → `… 02 01 07 | 21 0007 d4b7090d7730 01`，`EnableLEDCfg` 0→1；`reboot` → `… 02 02 07 | 20 0007 d4b7090d7730 01`（都是 44 字节的单播 `VendorSpecific`，目的 = 子节点），子节点的帧中断 48 秒（19:08:54→19:09:42），重启后读到 uptime 385 秒；结束时 `EnableLEDCfg` 回到 1，`WLANCfg` 8 行与基线相同。证据 `session/replay-run51-p2-9-light-reboot/`。
- **第 50 轮是一次失败**：服务启动时子节点早已入过网，数据库里它没有型号/软件版本、`ZTEDevFlag` 为 0，三个请求都被拒绝（-3）。原因是原厂只在**重启之后**才发第一对 Renew（`SSEND(0xa96e)` 的启动次数 > 1），首次启动不发，子节点就没有机会重新入网。**修法**：监督进程把每次守护进程启动都按"重启"报（`ZTE_MESH_START_COUNT` 从 +1 改成 +2）。第 52、54 轮验证：守护进程收到子节点第一帧后 3.0 秒发出 Renew 对，服务启动后 38 秒（第 54 轮）到 59 秒（第 52 轮）子节点被识别（`mlo_node online=1`，型号、名称出现），不需要任何配置切换。这也让 MLO 推送、信道/功率推送在"服务单独重启"的场景下重新生效。
- 副产物（踩坑，已记入记忆）：mac 上构建/抓包文件很多时，`mreplay` 的允许名单要包含新出现的客户机；第 50 轮第一次启动就因为一台没列出的 DHCP 客户机（用户自己的电脑）被守卫拒绝，核对过是普通客户机后才加入名单。

### 16.2 P2 第 10 项：拓扑页信息（第 54 轮）

原厂每个 AP 一张卡：名称、IP、MAC、回程 RSSI、指示灯、接入方式、终端数、角色，加改名/重启/指示灯。我们的节点表现在有：名称（可被 `zte_mesh_node` 别名覆盖，只存在主控这边，和原厂的 `i5DmAddOrModifyAliasName` 一样不下发）、型号、地址（LuCI 的主机提示）、MAC、软件、类型、**指示灯**、**终端数**、状态、操作按钮。
- 数据源：数据库里没有这两项，但它们都在守护进程本来就收的帧里。trace 库的接收钩子（`note_peer`）解析：子节点 HigherLayerResponse 的厂商子 TLV 0x21 `{AL MAC, 状态}`（指示灯），和每个 APMetricsResponse 里 AP Metrics TLV（0x94）的各 BSS 终端数之和；写到 `/run/zte-mesh/peers/<MAC>.light`、`.stations`（原子替换），服务读它们写进 `state.json` 的 `light`（-1 未知）、`light_age`、`stations`（-1 未知，30 秒没更新也算未知）。服务启动时清掉旧文件。只看单片、完整的帧，不保存别的内容；LAN 上任何设备都能发带任意 AL MAC 的 1905 帧，所以每个守护进程生命周期最多记 32 个不同的 MAC，超过的忽略（防止伪造帧填满 tmpfs）。最终版本在真子节点上又跑了一轮（第 55 轮，只启动 120 秒）：`zte:true`、`light:1`、`stations:2` 正常，`WLANCfg` 读回仍与基线相同。
- **真机**（第 54 轮）：`state.json` 里 `light` 从 1 变 0（关灯请求之后同一秒，子节点马上上报），再回到 1；`stations` = 2（当时子节点下有两个终端，我没有独立核对数字）。`model` 为 `ZXHN Z506`，名称为子节点自己的名称，软件版本仍是空的（数据库的 `SoftwareVer` 没被填，HLR 的厂商 TLV 里有 `V1.0.0.2B2.8000`，没有去接）。
- 没做：回程 RSSI（有线子节点没有）、父子树状图、每个 AP 下的终端列表。

### 16.3 P2 第 11 项：`acap_iptv`——不适用于 Z506，没有实现

AC→AP 的 IPTV 配置同步不走 1905：cspd 的 `AcApIptvEvent` 在 AP 上线时（"AP up, AC need to call IDM syn iptvcfg to AP"）通过 `IdmSsendApi` 把 `AcIptv`/`ApIptv` 表的内容发给对方的 `idm_service`（libidmapi）。证据：Z506 的 `/lib` 里没有 libidm*、`/bin` 里没有 `idm_service`，`cspd` 里没有 `IdmSsendApi`/`AcApIptv*`，`multiapd` 里没有 IPTV/IDM 字符串，数据库表里只有它自己的 `IPTVCfg`（它作为路由器自己的 IPTV 设置）。所以主控没有任何办法把 IPTV 配置推给这台子节点；主控本机（SR1010 的 OpenWrt）的 IPTV 桥接是它自己的网络配置，不属于 mesh。如果以后有带 `idm_service` 的原厂子节点，需要另外实现 IDM 通道（没有对端可验证）。

### 16.4 P2 第 12 项：访客限速/限时、Wi-Fi 5 兼容

- **限速（`LimitEnable/DownRate/UpRate`）：没有 mesh 层的表示，没有实现。** 厂商 SSID 查询回复只有 0c 使能、0d 隐藏、0f 访客、11 ACL 策略、1e VAP 隔离、46 `ForceWiFi5`（`acdsGetSSIDConf` 反汇编），子节点的限速是它自己 cspd 里的功能（`dynQosSetGuestSSIDSpeed`），主控不能下发。想限速只能在 OpenWrt 一侧对访客 VLAN（原厂 `GuestIsolateInfo` 里 4091/4090）做 SQM/tc。
- **限时：实现为槽位的 `access_minutes`**（原厂是 cspd 的 wlan 模块到点把访客 `Enable` 写 0 并发 Renew 通知）。槽位配置为开之后开始计时，到点有效使能位变 0（配置里仍是开），只重新 Renew 那个频段；把它关掉再打开、或改这个值会重新计时；暂时被定时关闭压着的时候不计时；服务重启后重新计时。**真机**（第 52 轮）：槽位 2（备用/Wi-Fi 5 兼容网络）配置 `access_minutes=1`，切换为开之后子节点 AP3、AP7 的 `Enable` 0→1，约 60 秒后 1→0。实验室另测了"关掉再打开重新计时、再到期"。
- **Wi-Fi 5 兼容（`force_wifi5`）：发现并修了一个适配器 bug。** 原厂 `CmWLANGetAPConfig` 把数据库 `WLANCfg.ForceWiFi5` 复制到 CM 输出结构偏移 2268（厂商查询回复的子 TLV 0x46 的来源）；证据生成器 `cm-profile-evidence.py` 只认相邻的"读 r3、存 r3"，漏了中间夹着一条别的读取的这一对，所以 `cm-profile-layout.inc` 缺 `CM_COPY(2180, 2268, 4, 13)`，**我们的控制器的 46 一直是 0**（所有以前的重放里 ForceWiFi5 恰好都是 0，没被发现）。已让生成器认这种"中间夹一条读取"的模式（38 个标量复制 + 6 个字符串 = 44，只多出这一条），重新生成并通过校验；`tests/cm-profile-codec.c` 加了一条用例。新增服务配置键 `radioN.bssK.force_wifi5=0|1`（写 `WLANCfg.ForceWiFi5`，读回；改动热更新并 Renew 该频段）。**真机**（第 54 轮）：槽位 2 配置为 1 时，线上槽位 2 的回复 `46[4]=00000001`，其它槽位 0，与原厂一致；Z506 较旧的 `multiapd` 没有这段代码（字符串里没有 FORCEWIFI5），所以这台子节点上没有可见效果（槽位 2 本来就是关的）。

### 16.5 P2 第 13 项：Wi-Fi 定时关闭

- 原厂机制（`wlancfg_hdl_time_action` 反汇编）：到点写全局 WLAN 状态，再对每个无线卡、每个实例（带标志 0x40 的跳过）发 `CmMultiAPWlanRenewNotify`，没有新的 1905 报文；子节点从那一次 Renew 的 M2 里看到被关掉的 BSS。我们复刻成服务里的调度器：服务配置键 `timer.utc_offset=<分钟>`（沙箱里没有时区，宿主传入）和最多 8 条 `timer.N.start/end=HH:MM`、`timer.N.days=<7 位 0/1，周一到周日，窗口开始那一天>`、`timer.N.scope=all|guest`。`all` = 所有主 SSID 加槽位 1、2 的有效使能位置 0（隐藏回程槽位 3 永远不动，mesh 要用）；`guest` = 只关槽位 1。结束时间不晚于开始时间表示跨过午夜。窗口开关只改"有效使能位"（配置里的开关不变），由同一条热更新路径 Renew 变化了的频段；`state.json` 的 `sched` 里有规则数、当前是否在窗口内、各访客倒计时。
- **实验室**（`dev-sched-check.py`，真实时钟，约 5 分钟）：窗口 19:26–19:28 在 19:26:00 和 19:28:00 整点开关，槽位 1–2 在窗口内关、之后开，槽位 3 不动，`profile_update bands=3`；最终版本重跑 20:11–20:13 同样通过。
- **真机**（第 52 轮，范围 `guest`）：窗口开始后约 8 秒子节点 AP2、AP6 的 `Enable` 1→0，窗口结束后恢复。**范围 `all`（主 SSID 也关）在真子节点上没有试**：会断掉用户真实的 Wi-Fi，而且用户的开发电脑很可能就挂在这台子节点上，没有得到明确同意不测。主 SSID 关闭的路径（`radioN.enabled=0` → 桥接写入 → Renew）在实验室里验证过，与槽位使能位走的是同一条 M2 使能位。
- 注意：测试期间路由器的时钟比 mac 慢约 13 小时（没有网络授时），调度用的是路由器自己的时钟，所以窗口要按路由器的 `date -u` 算；正常装机有 NTP 就没有这个问题。夏令时切换后需要重新保存设置（`timer.utc_offset` 是写配置时的偏移）。

### 16.6 其它改动和行为变化

- `MAX_CONFIG` 从 2048 提到 8192 字节：16 条 ACL、6 个槽位、8 条定时加起来会超过 2 KB（实验室里 3.9 KB 的最大配置被接受）。
- 每次守护进程启动都按"重启"计（16.1）：已入网的子节点在服务重启后会收到一对 Renew，重新应用同样的配置；正在入网的子节点会多收到一对 Renew。
- trace 库多了接收钩子 `note_peer` 和 16.2 的 `/run/zte-mesh/peers/` 文件。
- UCI/LuCI：新增 `zte_mesh_timer`（`start`、`end`、`days`、`scope`）、`zte_mesh_node`（`mac`、`name`）区段，槽位区段新增 `access_minutes`、`force_wifi5`；init 把 `date +%z` 换算成 `timer.utc_offset`；rpcd 新增 `reboot_node`、`light_node`；`tests/test_init.sh` 64 条、`tests/test_rpcd.sh` 15 条（用桩 `jsonfilter` 和假服务；已加进 `make test-host`）、`tests/test_luci.js`、`dev-config-check.py`（15 份配置，含 3.9 KB 的最大配置和一份负例）、`dev-sched-check.py`。
- 暴露：第 52 轮子节点的访客 SSID（密钥为用户的 Wi-Fi 密码）约 7 分钟是开着的（其中 2 分钟被定时关了），备用 SSID（AP3/AP7，`cc2-wifi5`、`cc2-5Gc-wifi5`）开了约 60 秒；第 53 轮恢复，8 行 `WLANCfg` 读回与基线逐行相同；第 54 轮结束后再次读回相同，`LEDCfg.EnableLEDCfg` 为 1。LAN 允许名单这次加了一台新出现的 DHCP 客户机（主机名 `cnjn-Code01-Ver2-1`，用户自己的电脑）。证据：`session/replay-run50-p2-9-actions-refused-not-zte/`、`replay-run51-p2-9-light-reboot/`、`replay-run52-p2-sched-access-restart/`（含 `child-wlan-enable.txt` 子节点使能位时间线）、`replay-run54-p2-peers-wifi5-light/`（含 `state-watch.txt`）。

### 16.7 本轮之后仍开着的事

- 定时的 `all` 范围（主 SSID）和 `radioN.enabled` 在真子节点上没试（要用户同意；可能断开开发电脑）。
- `0200fe` 入网时发 3 条的触发源、`BeaconMetricsQuery` 原厂不发的原因（15.6）。
- 子节点软件版本（HLR 里有，数据库没填）、父子拓扑树、终端列表、回程 RSSI。
- 访客限速只能在 OpenWrt 一侧做；IPTV 同步需要有 IDM 的子节点。
- UCI/LuCI 和 rpcd 仍没有在真实 OpenWrt 上运行过。
