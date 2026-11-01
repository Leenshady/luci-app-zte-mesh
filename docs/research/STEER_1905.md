# 终端动作消息：`0x8014` 这一类到底长什么样（2026-11-01）

> 起因：批 3 的 A17a 要让控制器能"踢一台手机、封一个 MAC"。以前这一项在
> [FEATURE_STATUS](../FEATURE_STATUS.md) 里只有一句"`0x8014` 等，接口已在 libcmapi"——名字有了，
> 字节没有。这次不靠规范正文回忆，直接拿 Wireshark 的 `ieee1905` dissector 当裁判，把每条消息的
> 字段顺序和长度逐项问出来。

## 1. 结论

1. **消息号**（dissector 的 `Message type` 表，`0x8000` 段是 Multi-AP）：

   | 号 | 名字 | 用途 |
   |---|---|---|
   | `0x8014` | Client Steering Request | 让子节点把某个终端引导走（BTM） |
   | `0x8015` | Client Steering BTM Report | 子节点回报 BTM 结果 |
   | `0x8016` | Client Association Control Request | 拒绝/放行某终端在某 BSS 上关联 = **封禁** |
   | `0x8017` | Steering Completed | 终端确实离开了原来的 BSS |

2. **TLV 号**：`0x01` AL MAC、`0x90` Client info、`0x9b` Steering request、`0x9c` Steering BTM report、
   `0x9d` Client association control request、`0xb8` BSSID。

3. **四处字段顺序与"按规范正文写"的直觉不同，以 dissector 为准**：

   - Steering request 里 **flags 在 opportunity window 之前**（不是 BSSID、window、flags）；
   - 两个列表的**计数各自紧贴在自己的列表前面**，不是把 `STA 数`、`目标 BSS 数` 一起放在列表区之前；
   - Client info 是 `BSSID + 终端 MAC` 共 12 字节，`0x8014` 与 `0x8017` 都用它；
   - Association control 的 validity 是**秒**（大端 u16），`0` = 一直有效。

4. **三个编码器都有独立判据**：`tests/controller-steer-codec.c` 比字节，`tests/test_steer_tshark.sh`
   用 tshark 把字段读回来（20 项，`tshark 4.4.2` 上 20/20）。两条都过，才说明"字节对"且"含义对"。

5. **仍未验证的是对端行为**：ZTE 固件收到 `0x8014` 之后会不会真踢，取决于它自己的
   `MultiAPMaster.EnBtmKickoff`/`EnRoamKick`/`EnActiveDeauth` 与终端是否支持 11v。出厂
   `EnBtmKickoff=0`（[E2631_DB_SNAPSHOT](E2631_DB_SNAPSHOT.md)），所以**很可能发了也不动**——
   这一步只能真机看，见 [PLAN](../PLAN.md) H-03。

## 2. 怎么问出来的

不读规范、先问 dissector 的三步，全部可复现（`out/` 下的探针脚本是临时的，方法抄在这里）：

```sh
# 1. 256 个 TLV 号各来一帧，看 dissector 叫它什么（type 0 是 End of Message，跳过）
python out/tlv-probe.py out/tlv-probe.pcap
tshark -r out/tlv-probe.pcap -V | awk '/^Frame [0-9]+:/{f=$2} /TLV type:/{if(!seen[f]++){sub(/.*TLV type: /,""); print f"\t"$0}}'

# 2. 消息号同理（0x0000..0x000f + 0x8000..0x803f）
python out/msg-probe.py out/msg-probe.pcap
tshark -r out/msg-probe.pcap -T fields -e frame.number -e ieee1905.message_type -e _ws.col.Info

# 3. 候选布局：一次一帧，只看字段值对不对（out/one.py 就是"给十六进制 TLV，回 dissector 树"）
python out/one.py 8014 "010006…" "90000c…" "9b001b…"
```

第 3 步里第一次按"BSSID, window, flags, timer, ..."拼，dissector 把 **flags 读成窗口的高字节**、
窗口读成 51392，还把 `target MAC` 读成 `01:aa:bb:cc:dd:ee`——一眼就能看出错位。改两处后
`Extraneous TLV data` 与 `Malformed Packet` 全部消失。

**没有 TLV 名靠猜**：第 1 步的映射表里，`0x90` 到 `0xa0` 这一段（Client info / Steering request /
Steering BTM report / Client association control request / Backhaul steering）本来就是我打算猜的那几个，
名字和编号一起对上了才开始拼字节。

## 3. 字段布局（dissector 读回来的结果）

统一前置：以太头 14 字节（目的、源、`0x893a`）+ CMDU 头 8 字节（版本 0、保留 0、类型 2、消息号 2、
分片号 1、flags 1，最后一帧置 `0x80`）+ TLV 序列 + `00 00 00`（End of Message）。

| 消息 | TLV | 布局 |
|---|---|---|
| `0x8014` | `0x01` AL MAC | `AL MAC(6)` |
| | `0x90` Client info | `BSSID(6), 终端 MAC(6)` |
| | `0x9b` Steering request | `源 BSSID(6), flags(1), 机会窗口(2), 解除关联定时器(2), STA 数(1), 每 STA 的 MAC(6), 目标 BSS 数(1), 每目标 {BSSID(6), 运行类别(1), 信道(1)}` |
| `0x8016` | `0x01` AL MAC | 同上 |
| | `0x9d` Association control | `BSSID(6), 控制(1: 0 封 / 1 放行), 有效期秒(2), STA 数(1), 每 STA 的 MAC(6)` |
| `0x8015` | `0x9c` BTM report | `源 BSSID(6), 终端 MAC(6), 状态(1), 目标 BSSID(6)`（目标 BSSID 可缺，前 13 字节不可缺） |
| `0x8017` | `0x90` + `0xb8` | `BSSID(6)+终端 MAC(6)`，可再跟一个 `BSSID(6)` 说明去了哪 |

`0x9b` 的 flags：bit7 = mandate（不是"给个机会"，是"必须引导"）、bit6 = 解除关联即将发生、
bit5 = abridged。dissector 的原文：

```
Steering request flags: 0xc0, Mode, BTM disassociation imminent
    1... .... = Mode: Request is a steering mandate to trigger steering for specific client STA(s)
    .1.. .... = BTM disassociation imminent: BTM disassociation imminent
```

## 4. 对插件的结论（落到代码）

`controller-steer.c` 只实现上面四类，编解码都是纯函数（无 socket、无日志）：

| 函数 | 做什么 | 谁会用 |
|---|---|---|
| `steer_build_request()` | 组 `0x8014`：一脚踢（mandate + 解除关联即将发生，无目标 BSS，让子节点自己挑频段） | A17b 的 `kick` |
| `steer_build_assoc_control()` | 组 `0x8016`：封/放行，一次最多 8 个 MAC | A17b 的 `ban` |
| `steer_decode_btm_report()` | 读 `0x8015`：这条踢到底成没成 | A17b 的去抖与回滚 |
| `steer_decode_completed()` | 读 `0x8017`：终端确实走了 | A17b 的"提前结束等待" |

`0x8014` 的 `bssid` 与 `client` 必须来自我们自己的关联表（子节点不认识"不在自己身上的终端"，
发了也是白发），所以 A17b 的输入不是配置，而是 `state.json` 里已有的节点/终端关系。

## 5. 局限

- **没有真机证据**：这一轮所有结论都是"dissector 认可"，不是"ZTE 照做"。ZTE 的接收代码
  （`handle_wlan_multiap_steering_req`）在，但触发它之后的行为、以及它是否还要求厂商 TLV 陪跑，
  只能上真机（H-03）。**特别注意**：出厂 `EnBtmKickoff=0`，`0x8014` 很可能被接受但什么也不做。
- **窗口/timer 的单位照发不译**：`window` 原样写进帧（`steer_request.window` 注释里说明了这一点），
  因为"100 ms 还是秒"没有 dissector 可问，只有真机日志能定。
- **一帧一个终端**：`0x9b` 本身支持 STA 列表，这里只发一个。多终端踢是 A17b 的批量语义，
  届时决定是"一条消息多个 STA"还是"多条消息"。
- **组播还是单播没定**：builder 只接受调用者给的 `dst`，单播到子节点 AL MAC 与组播到
  `01:80:c2:00:00:13` 都能编。哪个有效要真机看（组播会被所有子节点看到，单播不会）。
