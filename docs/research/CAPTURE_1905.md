# 实验台上的 1905 实测：线上到底有哪些帧、哪些字段（2026-11-01）

> 起因：批 2 的 A18a 要给拓扑页补"每个节点的回程指标与终端数"。以前的 `controller-trace.c` 只统计帧长，
> 值从哪来一直没落到字节上。这次不猜，直接在实验台上抓一段真流量，再用 Wireshark 的 dissector 核对
> 构造帧的字段布局。

## 1. 结论

1. **有线实验台上，子节点真正发的只有四类帧**：`0x0000`（发现）1 个、`0x0004`（ZTE 专用）98 个、
   `0x0005`（链路度量查询，我们发）4 个、`0x0006`（链路度量应答）4 个。40 秒 107 帧。
2. **没有拓扑应答（`0x0003`）、没有 AP Metrics 应答（`0x800c`）**——只有子节点被查拓扑、或被要求度量时
   才发。所以"终端数"在一条空闲的有线链路上本来就是"没上报"，`state.json` 里就该是 `-1` 而不是 `0`。
3. **这段流量里没有任何 RSSI**。链路度量应答里装的是 ZTE 自己的定长结构（子 TLV `0x49`，110 字节），
   两端的条目除 MAC 外逐字节相同，不含信号强度。**回程 RSSI 的来源仍然未知，只能等无线回程的真机**，
   见 [PLAN](../PLAN.md) H-05。
4. **ZTE 把所有私有数据包在 `Vendor specific`（TLV `0x0b`）里**：`OUI 00:19:c6` + 3 字节 + 一串
   `[类型(1) 长度(2) 值]` 子 TLV。这个结构在 107 帧里的 61 个厂商 TLV 上**全部按字节走完、无剩余**，
   不是猜的。
5. 构造帧验证了两个标准 TLV 的布局（`tshark 3.6.2` 的 `ieee1905` dissector 逐字段认可）：

| TLV | 布局（已核对） |
|---|---|
| `0x83` AP operational BSS | 射频数(1)，每个射频 `{射频标识(6), 接口数(1)}`，每个接口 `{BSSID(6), SSID 长度(1), SSID}` |
| `0x94` AP metrics | `BSSID(6), 信道利用率(1), u16 终端数`，其后可跟可选的估算业务参数 |

6. 第一次构造 `0x83` 时**漏了射频标识 6 字节**，dissector 立刻把紧随其后的 SSID 长度读成"接口数 71"并
   判 malformed —— 这类错误只有对着 dissector 看才发现，靠读规范容易漏。

## 2. 怎么抓的

```sh
# 1. 在实验台主控（owrt，192.168.1.20）上抓 40 秒，只留 1905 以太类型
python tools/lab/rssh.py owrt "tcpdump -i br-lan -s0 -U -w /tmp/1905.pcap 'ether proto 0x893a' -G 40 -W 1"
# 2. 取回（Git Bash 会把 /tmp 改写成 Windows 路径，必须关掉路径转换）
MSYS_NO_PATHCONV=1 python tools/lab/rssh.py owrt get /tmp/1905.pcap ./out/lab-1905.pcap
# 3. 分类（本机没有 tshark 时，用 Python 读 pcap 记录头即可，见 tools/capture/frames.py）
python tools/capture/frames.py out/lab-1905.pcap --cmdu
```

三个环境坑，都不是协议问题：

| 坑 | 现象 | 原因 |
|---|---|---|
| `timeout` 不存在 | `ash: timeout: not found` | 这台 immortalwrt 的 BusyBox 没编 `timeout`，用 `tcpdump -G 40 -W 1` 限时 |
| `ip -br link` 不认 | BusyBox `ip` 用法报错 | 只有精简版 `ip`，直接看 `/sys/class/net` |
| `get /tmp/x` 变成 `cat C:/Users/.../Temp/x` | `cat: can't open` | Git Bash 把 `/tmp` 改写成 Windows 临时目录，`MSYS_NO_PATHCONV=1` |

## 3. 帧清单（40 秒，无终端在线）

| CMDU | 条数 | 方向 | TLV |
|---|---|---|---|
| `0x0000` Topology discovery | 1 | 子 → 组播 | `0x01` AL MAC、`0x02` MAC、`0x0b` 厂商（子 TLV `0x40` 设备序列号、`0x41`） |
| `0x0004` Vendor specific | 98 | 子 → 主控 | 只有 `0x0b` 厂商：子 TLV `0x07`、`0x0b`、`0x0c`、`0x0d`、`0x0f`、`0x11`、`0x19`、`0x1b`、`0x1c`、`0x1d`、`0x1e`、`0x46`、`0x55` |
| `0x0005` Link metric query | 4 | 主控 → 子 | `0x08` 查询（`00 02`） |
| `0x0006` Link metric response | 4 | 子 → 主控 | `0x0b` 厂商：两个子 TLV `0x49`（各 110 字节，第一个是本机 AL MAC，第二个是主控 AL MAC） |

`0x0004` 每 0.4 秒一个，是子节点的状态心跳。**它是 ZTE 的私有消息，子 TLV 的语义没有证据**，因此没有
解析它的任何字段——只有"这个节点在说话"这件事被用来刷新 `metrics_age`。

`0x49` 那 110 字节里，除开头 6 字节 MAC 外是可复现的定长块（大量 0 与几处小整数），两端的条目完全相同：
**同一份模板 + 不同 MAC**，不像运行时测量值。

## 4. 对插件的结论（落到代码）

`controller-1905.c` 只解码**字段含义有依据**的东西：

| 字段 | 来源 | 依据 |
|---|---|---|
| `stations` | `0x94` AP metrics 的 u16，按帧内多个 TLV 求和 | dissector 字段 `ap_metrics.sta_count`；子节点实测报过 2 |
| `bss` | `0x83` AP operational BSS 的接口数 | dissector 字段 `ap_bss_intf_count`，构造帧核对 |
| `hops` | 1905 头 flags 的 relay 位：0 → 1 跳（直连），1 → 2 跳（经中继） | dissector 把 bit6 叫 "Relay indicator" |
| `light` | 厂商子 TLV `0x21`（仅 `0x000e` 帧） | 原有的实现，主控 `_topoLightStatusHandle` 读的是它 |
| ~~`rssi`~~ | **没有来源** | 这段流量里不存在；无线回程真机才能验（H-05） |

`state.json` 的每个节点因此多了 `bss`、`hops`、`metrics_age`，取值 `-1` 表示"没上报"，与 `0` 区分开：
一个报 0 个终端的节点和一个从来没说过话的节点，页面上不该长一样。

## 5. 局限

- **有线拓扑**：主控是 x86 虚拟机（没有无线网卡），子节点走网线，所以没有 4 地址回程、没有 RSSI、
  没有无线侧的 1905 帧。无线那半边等 H-01。
- **`0x0003`/`0x800c` 没出现**：这两类只有在被要求时才发。要看它们的真实形状，得在真机上让子节点
  带着终端跑一轮，见 H-05。
- **pcap 不入库**（`*.pcap` 在 `.gitignore` 里）。能复现的部分放进了 `tests/controller-1905-codec.c`：
  一帧是上面这段抓包的原文（260 字节，`0x0006`），其余是构造帧。
