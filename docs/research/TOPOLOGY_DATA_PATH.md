# 组网拓扑的数据来源与接收逻辑（2026-11-27）

对象：`openwrt/luci-app-zte-mesh` 的拓扑页（`zte-mesh.js` 的 Nodes 表）与它背后的接收路径。

**一句话结论**：页面上的每个字段都能追到「原厂库表一行 + 帧里的一位/一个 TLV」；子节点的
`0x0006`（Link Metric Response）确实收到并用到了（深度/存活/站点数），`0x0003`（Topology
Response）也能收到但只取 relayed 一位，两者都不携带「谁挂在谁下面」，所以页面是表而不是连线图。

## 1. 数据链

| # | 环节 | 位置 |
|---|---|---|
| 1 | 原厂 multiapd（1905 控制器）收帧，把设备写进自己的库表 `P1905DeviceInfo` | 厂商库，见 `metrics` 与 `dbGetTblRowCount` |
| 2 | 设备身份逐行读出（只读七列：`ViewName`/`MacAddr`/`ModelName`/`Friendlyname`/`SoftwareVer`/`ZTEDevFlag`/`Active`） | `src/controller-run.c:150-174` |
| 3 | daemon 的 `recvfrom()` 被 LD_PRELOAD 拦截，EtherType `0x893a` 的完整帧进 `controller_peer_note()` | `src/controller-trace.c:246-267` |
| 4 | 解帧：relayed 位 → `hops`；`0x94` → 站点数；`0x83` → BSS 数；`0xd4`（仅 0x8002 内）→ 软件版本；`0x0e` 的厂商子 TLV `0x21` → light | `src/controller-1905.c:188-222`、`src/controller-peer.c:227-300` |
| 5 | 每秒合成 `/run/zte-mesh/state.json`（`self` + `nodes[]`，含 `hops`/`stations`/`bss`/`bss_detail[]`/`online`/`metrics_age`） | `src/controller-service.c:897-995` |
| 6 | `ubus call luci.zte-mesh status` 读出该文件；LuCI 用 `renderNodes()` 画表 | `openwrt/luci-app-zte-mesh/files/luci.zte-mesh`、`.../zte-mesh.js:236-308` |

别名列来自 uci `zte_mesh.zte_mesh_node`（`zte-mesh.js:257-262`）。

## 2. 每个页面字段的来源

- `Model`/`Name`：库表 `ModelName`/`Friendlyname`；读不到就显示 `-`（`controller-service.c:923-936` 的注释：
  空单元格是诚实答案，不编模型名）。
- `Software`：库表 `SoftwareVer`；子节点这一列在库里常为空，于是回退到 `peers/<mac>.software`，
  即 AP Capability Report（`0x8002`）里的厂商 Device Inventory TLV `0xd4`（`controller-service.c:943-952`）。
- `Clients`：`peers/<mac>.metrics` 的 `stations=`，若有按 BSS 的记录则用它们的和（`controller-service.c:965-980`）。
- `BSS detail`：`peers/<mac>.<bssid>.sta` 文件逐条读回（`controller_peer_bss_load`）。
- `Direct`/`Relayed`：`peers/<mac>.metrics` 的 `hops=`，来自 1905 头的 relayed 位（1 直连，2 中继）。
- `Online`：库表 `Active` **且** 本机在 `NODE_SILENT_SECONDS` 内真的收到过该 MAC 的帧；
  本机自己例外（`controller-service.c:960-964`）。
- 新鲜度 `metrics_age`：读回时 `hops` 允许 300 s、`stations`/`bss` 允许 30 s，超时即 `-1`，
  页面显示 `-`（`controller-service.c:965-968`）。

## 3. 0x0006（Link Metric Response）的接收

实测见 `AGENT_PROBE.md:597-599`：控制器发 `0x0002` Topology Query，子节点回 `0x0006`（373 B）。

- 入口闸：`cmdu_decode` 成功 + 完整且未分片（`last_fragment && !fragment`），否则丢弃
  （`src/controller-peer.c:196-198`）；没有分片重组。
- 存活：通过 `admit()` 即写 `peers/<mac>.seen`，每 MAC 5 s 一次（`controller-peer.c:149-180`）。
- 解码：只取 1905 头 relayed 位、`0x94` 的 u16 站点数、`0x83` 的 BSS 数
  （`controller-1905.c:188-222`）。**不解析** Link Metric TLV（速率/可用性），也不记录
  发送方↔接收方接口对。
- 落盘：`peers/<mac>.metrics`（`stations=`/`bss=`/`hops=`），按 BSS 明细只来自 `0x800c`
  （`controller-peer.c:253-287`）；`0x800c` 里的 `0x70` 子 TLV 才写 `<mac>.assoc`（288-300）。

## 4. 0x0003（Topology Response）的接收

- 与 0x0006 走同一个钩子与同一道「未分片」闸；多节点的拓扑应答经常超 MTU，一旦分片就在入口被丢。
- 通过后同样只 `note_seen()` + `node_metrics_decode()`：TLV 常量表只有
  `0x01 AL_MAC`、`0x0b VENDOR`、`0x83`、`0x93`、`0x94`、`0xd4`（`src/controller-1905.h:22-27`），
  Device Information（`0x03`）、Bridge Configuration（`0x04`）、1905 Neighbor（`0x06`）、
  Supported Service（`0x07`）都没有解析器。所以 0x0003 最终只写一个 `hops=` 的 `metrics` 文件。
- 唯一「读懂」0x0003 的地方是实验室路径：`controller_topology_response_matches()`
  （`src/controller-packet.c:46-82`）逐字节校验方向、CMDU type 3、mid `0x1202`、单个
  Device Information 的 AL MAC、TLV 边界、EOM 之后只允许零填充、总长 ≤1600；它由
  fixture 时序触发（`src/controller-run.c:943-953`），只打印元数据，`body_logged=0`，
  不写任何状态文件。

## 5. 为什么页面是表格而不是连线图

- 没有边：库表七列里没有 parent/port，帧里只有一个 relayed 位，能确定的只有「直连」与「中继」两圈，
  中继节点各自挂在哪台下面不知道。画出来就是编的。
- 成对链路数据拿不到：`CONTROLLER_CAPABILITIES.md` E 节记录，原厂子路由在入网初期丢弃
  AP 能力/信道/客户端能力查询且之后不重发；能确认有应答的只有拓扑、链路指标、AP 指标这一类。
- 工程理由：LuCI 视图零依赖（`E('table')`），表格还能同时承载 Software/Clients/BSS/Online/age，
  这些字段在图上一个都放不下。

## 6. 要变成连线图，需要什么

1. 解析 0x0003 载荷：Device Information 的 AL MAC + Neighbor TLV `0x06` 的邻居 MAC/接口；
2. 落一个新文件（如 `peers/<mac>.neighbors`）给 `write_state()` 读，`nodes[]` 加 parent 字段；
3. 入口加分片重组（现在的 `last_fragment && !fragment` 闸会直接丢掉大应答）；
4. 前提是它真的会来：控制器要主动重发 Topology Query，否则按 E 节的记录，入网后不会再有 0x0003。

## 7. 0x0003 的 TLV 解析逻辑（2026-11-27 复核）

分两层：**框架层已经通用，语义层（拓扑 TLV）一个都没有。**

**框架层（可直接复用）**

- 帧头：`cmdu_decode()`（`controller-1905.c:17-29`）要求 EtherType `0x893a`、`frame[14]`/`frame[15]`
  都为 0、长度 ≤ `CONTROLLER_1905_MAX_BYTES`=2000（`controller-1905.h:15-17`）；`type`/`mid` 在 16/18 字节，
  `fragment` 在 20，`last_fragment`/`relayed` 是 `frame[21]` 的 bit7/bit6。
- TLV 游标：`tlv_next()`（`controller-1905.c:39-64`）固定 `type(1B) + length(2B BE) + value`，从第 22 字节开始；
  `type==0`（EOM）即停，以太网填充不算 TLV；`at+3+len > bytes` 时把游标标为 done 并**静默截断**。
- 厂商 TLV `0x0b`：`zte_vendor_open()`（`controller-1905.c:66-76`）比对 OUI `00:19:c6`，`at=6`（OUI+3 字节保留），
  子 TLV 同一套 1+2 框架。

**0x0003 载荷布局（1905.1）**

| TLV | 值布局 | 用途 |
|---|---|---|
| `0x03` Device Information | 6B AL MAC、1B 接口数、每接口 `6B MAC + 1B media + 1B 介质数据长度 + N` | 确认拓扑归属；接口 MAC 可用于识别下行口 |
| `0x04` Bridge Configuration | 1B 桥 MAC 数 + 6B×N | 是否桥接、桥 MAC 是否等于 AL MAC |
| `0x05` Non-1905 Neighbor | 6B MAC + 1B 标志 | 非 1905 邻居，一般不建边 |
| `0x06` 1905 Neighbor | 6B 邻居 AL MAC + 1B 桥标志 | **唯一能给出边的 TLV** |
| `0x07` Supported Service | 1B 服务数 + 每个 8B（6B MAC + 2B 类型） | 判断对端是 Controller/Agent |

这些 type 目前都没有定义（常量表只有 `0x01/0x0b/0x83/0x93/0x94/0xd4`，`controller-1905.h:22-27`）。

**已有的一份参考实现**：`controller_topology_response_matches()`（`controller-packet.c:46-82`）按字节读过 0x0003，
但只验**我方发出**的那一份——帧头方向、`type==3`、`mid==0x1202`、只允许一个 `0x03` 且 AL MAC 等于发送方、
接口记录按 `6+1+1+N` 推进（`p != n` 判错）、EOM 之后只允许零填充、总长 ≤1600。接收侧镜像这套即可，
边界检查无需重写。

**接入点**：`controller_peer_note()` 对每帧已调用 `node_metrics_decode()`（`controller-peer.c:248`），
在同一 TLV 循环里加 `topology_decode()`；输出写 `peers/<mac>.neighbors`（邻居 MAC + 桥标志 + 帧龄），
`write_state()` 再拼成 `edges[]`。

**风险（按严重度）**

1. **分片**：2000 上限 + 未分片闸（`controller-peer.c:196-198`），多邻居的 0x0003（>1500）必然被丢；先做重组才有意义。
2. **静默截断**：坏长度只让游标 done，调用方分不清「无此 TLV」与「格式错」，返回值要区分。
3. **重复 0x03**：宽松游标不拦；镜像校验器要保留「只有一个且等于发送方」。
4. **边的方向**：`0x06` 只表达「我看见了它」，是无向边；上行口要靠接口 MAC 与父节点邻居表交叉比对，否则成环，需要显式选根/剪枝。
5. **伪造**：LAN 上任何主机都能发 `0x893a`，`admit()` 只有 32 个 MAC 的预算（`controller-peer.h:34-35`）；
   建边前只应采用「已入网且库表有行」的 MAC。

**结论**：框架够用，语义缺一层；真要画图，先解决分片与「边的方向/可信来源」，再谈渲染。

## 8. 终端清单怎么到主控（2026-11-27 补：主动上报）

设备页的 SSID / 位置（via）/ 终端归属来自 `peers/<child>.assoc` 与 `.metrics`，这两份文件由
`controller_peer_note()` 在收到子节点的 0x800c 时写出。原来的路径是「主控问、子节点答」：

| 环节 | 代码 | 现场事实 |
|---|---|---|
| 主控发 AP Metric Query（0x800b，TLV 0x93） | `ap_metrics_query_build()`，只在 `controller-packet.c` 的 fixture 里被调用 | 运行时一次也不发（211 上抓不到） |
| 子节点应答（0x800c，每 BSS 一条 0x94 + 0x70 客户端清单） | `agent_ap_metrics_answer()`，由 `libzte_controller_trace.so` 的 `recvfrom` 钩子触发 | 表在刷新（`/run/zte-mesh/ap-sta`，`sta-collect` 每 5 秒重写），但没人问就没有答 |
| 主控归档 | `controller_peer_note()` 按以太源写 `.metrics`/`.sta`/`.assoc` | `peers/02:00:00:00:19:e3.metrics` 只有 `stations=0` |

**做法**：子节点自己发，主控被动收。

- 接口：`agent_ap_metrics_report(frame, cap, al, dest)`（`src/agent-ap-metrics.h`）。
- 帧：与应答同一个构造（`agent-ap-metrics.c` 的 `answer_build()`），内容=整个表的每个 AP 行；
  `dest` = 广播 `ff:ff:ff:ff:ff:ff`，源 = 本机 AL（主控按以太源归档，写成别的就是在页面上造节点）。
- 节拍：`sendto` 钩子里最多每 5 秒一次（`REPORT_SECONDS`），借守护进程自己的 1905 流量当时钟，
  不在别人的进程里开线程。
- 停报：表不存在 / epoch 超过 30 秒 / 0 行 ⇒ 返回 0，不发。采集暂停（`collect-pause`）后自然静默。
- 主控侧：无改动。`controller_peer_note()` 对任何完整 0x800c 都成立。

**判据**：`tests/agent-ap-metrics-codec.c::proactive_report()` 把上报帧交给主控自己的写侧，断言
`.metrics` 的 `stations=`、每 BSS 的 `.sta`、`.assoc` 的 `<mac>\t<bssid>\t<ssid>` 行；空 AP 表仍上报，
且 `.assoc` 只剩 epoch（把已离开的终端清掉）。
