# 用 Wireshark 的 ieee1905 dissector 核对我们发出的帧（2026-11-01）

> [MESH_INTEROP](MESH_INTEROP.md) 第 6 节第一条：不用真机先查编码错误。这篇是那次核对的记录：
> 抓了什么、每个字段是什么、跟规范和对端比哪里不一样、改了哪一处、哪一处故意不改。

## 1. 结论

1. **lab 里的线只要有人说话就能抓**：`mesh-search`（`tools/lab/enroller.c`）发一个 `0x0007`，
   沙箱里的原厂 `multiapd` **每次都回一个 `0x0008`**，同时它自己每隔 10 秒左右发 `0x0000`
   （发现）和 `0x000a`（renew）。90 秒抓到 34 个 `0x893a` 帧，五类消息，全部可解析。
2. **我们的 `0x0007` 少了 End of Message TLV**，Wireshark 3.6 因此把整帧标成
   `[Malformed Packet: ieee1905]`（异常，不是警告）。同一帧补上 `00 00 00` 三个字节即可正常解析
   —— 已修（`tools/lab/enroller.c` 的 `search_frame()`）。原厂在同一根线上发的四类帧，**无一例外**
   都以 EOM 结尾，所以这不是 dissector 挑剔。
3. **我们的 `0x8006`（信道选择请求）逐字段与 dissector 的解析一致**：`0x8b` Channel preference
   （radio ID + 操作类列表计数 + 操作类 + 信道列表计数 + 信道 + 14/0 的偏好/原因半字节）、
   `0x8d` Transmit power limit（radio ID + dBm）、EOM，全部落在规范位置上。这一帧是**静态构造**核对
   的（lab 里没有节点，走不到 `radio_select`），不是线上抓的。
4. **`0x0008`（AP 自动配置响应）我们比原厂少两个 TLV**：原厂的响应带 `0xb3` Multi-AP Profile 和
   EOM，我们的只有 `0x0f`/`0x10`/`0x80`。同样会被 dissector 标 malformed。**这一处故意不改**：
   现有形状是 Z506 上验证过能触发 M1 的那一份（`controller-packet.c` 的注释与
   REAL_CHILD 第 1 节），没有子路由可复验之前不动它，只加注释说明。
5. **`0x8006`/`0x8007` 的编号是对的**：dissector 的表里 `0x8006` = Channel Selection Request、
   `0x8007` = Channel Selection Response，与 `controller-packet.c` 的请求/应答配对一致。
   顺带发现 [MESH_INTEROP](MESH_INTEROP.md) 第 2 节那张表的几个名字错位（`0x8001` 不是
   AP Policy Config Request、`0x8003` 才是），已按 dissector 的表改正。

## 2. 怎么抓到的

工具：lab 虚拟机（Ubuntu 22.04）+ `tshark 3.6.2`（`apt install tshark`），dissector 是
`ieee1905`（Wireshark 内置，会用 `-G values` 打出 CMDU 类型名表）。

三个坑，都不是协议问题，是 lab 环境问题：

| 坑 | 现象 | 原因 |
|---|---|---|
| `nsenter -t <pid> -n` 进错命名空间 | 看到 `ens33`/`docker0`，没有 `veth-lab` | `timeout`/`ns-run` 外层进程没 unshare，要挑**真正 exec 进客户机**的那个 pid（`/proc/<pid>/ns/net` 与自己的不同） |
| `lab-peer` 是 `DOWN`，一个包都抓不到 | `tcpdump -i lab-peer` 0 包 | 服务模式把 `lab-peer` 挪进**宿主**命名空间（真机上由 init 挂进 `br-lan`），lab 里没人给它 up，veth 对端 down 就丢包 |
| 守护进程自己不定期发包 | 干等 75 秒 0 帧 | 它的 search 定时器要 cspd 置位；sandbox 没有 cspd。发一个 `0x0007` 进去它就活了 |

可用的做法（`sudo`，70 秒左右）：

```sh
# 1. 服务起在沙箱里
ZTE_MESH_EMULATED_LAB=1 out/ns-run-svctest /tmp/zte-mesh-svc/root \
    /lib/ld-linux.so.3 --library-path /lib /bin/db-engine-probe --controller-service &
# 2. 等 lab-peer 出现（它在宿主命名空间），一定要 up
ip link set lab-peer up
# 3. 在 lab-peer 上抓，同时每 5 秒往里送一个 search
tcpdump -i lab-peer -s0 -U -w wire.pcap &
cc -O2 -std=c11 -static tools/lab/enroller.c -o out/enroller
for i in $(seq 12); do out/enroller lab-peer 5 --search 02:00:00:00:19:07; done
```

`config` 必须是完整八行（`version_mode` + 两个 radio），少一行 `controller_service()` 直接
`ZTE_MESH_SERVICE fail=config-missing-or-invalid`，沙箱起不来，什么都抓不到。

抓完 `tshark -r wire.pcap -Y ieee1905 -T fields -e ieee1905.message_type` 分类，
`-V` 看单一帧的逐字段，`-x` 看字节。

## 3. 我们自己发出的帧

### 3.1 `0x0007` AP autoconfiguration search（12 帧，47 字节）

dst 组地址、src 是本机 AL MAC。字节与字段一一对应（offset 从帧首算）：

| 偏移 | 我们发的 | dissector 的说法 | 规范/对端 | 判定 |
|---|---|---|---|---|
| 0–5 | `01:80:c2:00:00:13` | IEEE-1905.1-Control | 1905.1 规定的组地址 | ✅ |
| 6–11 | `02:00:00:00:19:0a` | Source | 本机 AL MAC（lab 里是 `lab-peer`） | ✅ |
| 12–13 | `0x893a` | Type: 1905.1a CDHN | — | ✅ |
| 14 | `0x00` | Message version: 0 | 1905.1 是 0 | ✅ |
| 15 | `0x00` | Message reserved: 0 | 保留，发 0 | ✅ |
| 16–17 | `0x0007` | AP autoconfiguration search | — | ✅ |
| 18–19 | 递增 | Message id | 响应要原样带回 | ✅ 对端每次都带回同一个 mid |
| 20 | `0x00` | Fragment id: 0 | 不分片 | ✅ |
| 21 | `0x80` | Flags: Last fragment | 同时说明"不是中继转发的" | ✅（原厂的 `0x0001`/`0x000a` 是 `0xc0`，它在中继） |
| 22–25 | `0d 00 01 00` | SearchedRole: Registrar | 找注册者 | ✅ |
| 26–29 | `0e 00 01 00` | AutoconfigFreqBand: 2.4 GHz | `--band 1` 就是 5 GHz | ✅ |
| 30–34 | `80 00 02 01 01` | Supported service: count 1, `0x01` Multi-AP Agent | 声明自己是 agent | ⚠️ 见第 5 节 |
| 35–39 | `81 00 02 01 00` | Searched service: count 1, `0x00` Multi-AP Controller | 说明在找控制器 | ✅ |
| 40–43 | `b3 00 01 01` | Multi AP Profile: Profile 1 | EasyMesh R1 | ✅ |
| 44–46 | `00 00 00` | **End of message** | — | ✅（**修之前没有这三个字节**） |

### 3.2 `0x0008` AP autoconfiguration response（35 字节，未上线，静态核对）

`controller-packet.c:controller_autoconfig_response()` 的形状，lab 里只有 enroller 的
`response_frame()` 会发它（听 `0x0007` 才回，这次没触发）。

| 偏移 | 我们发的 | dissector | 判定 |
|---|---|---|---|
| 16–17 | `0x0008` | AP autoconfiguration response | ✅ |
| 18–19 | `0x1203 + band` | Message id 与 band 绑定 | ✅ |
| 22–25 | `0f 00 01 00` | SupportedRole: Registrar | ✅ |
| 26–29 | `10 00 01 <band>` | SupportedFreqBand | ✅ |
| 30–34 | `80 00 02 01 00` | Supported service: `0x00` Controller | ✅ |
| — | 无 | **缺 `0xb3` Multi-AP Profile 与 EOM** | ⚠️ 见第 5 节，故意保留 |

### 3.3 `0x8006` Channel Selection Request（49 字节，补到 60，静态核对）

| 偏移 | 我们发的 | dissector | 判定 |
|---|---|---|---|
| 16–17 | `0x8006` | Channel Selection Request | ✅（`0x8007` 才是 Response，代码等的是它） |
| 21 | `0x80` | Last fragment | ✅ |
| 22–35 | `8b 00 0b` `MAP24G` `01 51 01 06 e0` | Channel preference：radio ID、操作类计数 1、操作类 81、信道计数 1、信道 6、偏好 14/原因 0 | ✅ 与规范的分层列表结构一致 |
| 36–45 | `8d 00 07` `MAP24G` `14` | Transmit power limit：20 dBm | ✅ |
| 46–48 | `00 00 00` | End of message | ✅ |
| 49–59 | 11 个 0 | `Extraneous data after EOM TLV`（警告） | ⚠️ 见到太网最小帧长 60 的填充，不是错误 |

注意 `8b` 里那个"操作类计数"字节：直觉上会写成"操作类 + 信道列表"，dissector 的解析说明它是
**先计数再列表**（`Operating class count: 1` → 列表里一项 → `Channel list count: 1` → 信道）。
按直觉写出来的形状（去掉那个 `01`）反而被 dissector 判 malformed，已实测。

## 4. 对端在同一根线上发的帧（原厂 `multiapd`）

| 帧 | 类型 | 长度 | 目的地 | flags | TLV 顺序 |
|---|---|---|---|---|---|
| 4/5/12/18 | `0x0000` Topology discovery | 43 | 组地址 | `0x80` | `0x01` AL MAC、`0x02` MAC 地址类型、EOM |
| 7 | `0x0001` Topology notification | 34 | 组地址 | `0xc0` | `0x01` AL MAC、EOM |
| 3/9/…/39 | `0x0008` AP autoconfiguration response | 42 | **我们**（单播） | `0x80` | `0x0f` registrar、`0x10` band、`0x80` controller、`0xb3` profile 1、EOM |
| 10/11/20/28/33 | `0x000a` AP autoconfiguration renew | 42 | 组地址 | `0xc0` | `0x01` AL MAC、`0x0f` registrar、`0x10` band、EOM |

三条可以直接用的结论：

1. **它的 `0x0008` 是单播回给发 search 的那个 AL MAC**，mid 原样带回 —— 我们的 `controller_wait_cmdu()`
   用 `(cmdu, mid)` 配对是对的。
2. **它的响应里 `Supported role = Registrar`、`Supported service = 0x00 Controller`**：在这条线上
   它把自己当注册者/控制器，我们的 search 在它眼里是"一个 agent 在找控制器"。lab 里这是
   沙箱自嗨（没有第二个 1905 设备），真机上谁扮演哪个角色要看 Z506 的对端行为。
3. **它每 10 秒一轮 `0x000a` renew 且永不停止** —— renew 是"我还没被配置"。lab 里没有人给它 M2，
   所以它一直循环；这也说明 **M1/M2 在 lab 里走不通**（要 cspd 把 search 标志置位），
   M2 只能到真子路由上验。

## 5. 差异清单与处置

| # | 差异 | 证据 | 处置 |
|---|---|---|---|
| 1 | `0x0007` 缺 End of Message | 12 帧全被标 `[Malformed Packet]`；补三个 0 后同一帧干净；原厂四类帧全带 EOM | **已修**（`enroller.c` `search_frame()`），改完重抓 34 帧 0 malformed，且对端照样回 `0x0008` |
| 2 | `0x0008` 缺 `0xb3` 与 EOM | 静态构造 + 原厂 42 字节响应逐字段对照 | **不改**，只加注释：现有形状是 Z506 上验过触发 M1 的那一份，动它要重新做真机验收（MESH_INTEROP 硬约束 4） |
| 3 | `0x8006` 尾部 11 字节 0 被读成 extraneous | dissector 警告（warning，非 error） | **不改**：那是太网最小帧 60 字节的填充，真网卡也会补，只是我们补在用户态 |
| 4 | `0x0007` 声明 `Supported service = Agent`、`Searched service = Controller` | dissector 逐字段；原厂响应把发送方当 agent | **记录待判**：这是"以 agent 身份找控制器"的形状。若本意是控制器去发现 agent，这两个 TLV 的值要反过来 —— 需要一台真子路由观察谁回应，不在 lab 里猜 |
| 5 | `MESH_INTEROP` 第 2 节 CMDU 名字错位 | `tshark -G values` 的表：`0x8001` AP Capability Query、`0x8002` AP Capability Report、`0x8003` Multi-AP Policy Config Request、`0x8004/0x8005` Channel Preference Query/Report | 已按表改正 |

## 6. 局限

- **没有真中兴设备**：对端是沙箱里的原厂 `multiapd`，它和真子路由的差异（`br_ieee1905_filter`
  钩子、cspd、无线栈）在这里都看不到。这张表证明的是"我们的字节与规范一致"，不是"ZTE 认"。
- **`0x8006` 只在静态核对里验过**：`radio_reconcile()` 要节点表里有一个 ZTE agent 才会发，
  lab 的 `state.json` 永远 `nodes: []`，所以这一帧没有线上证据。要抓它，得先让沙箱的拓扑里
  出现一个节点（真子路由，或者给沙箱造一个假节点）。
- **M1/M2 没验**：这块要 cspd 把 search 标志置位，lab 里它是无限 renew。
- 抓到的 pcap 里有 SSID/PSK 相关的 lab 配置，按仓库规矩不入库（`*.pcap` 在 `.gitignore` 里）。

## 7. 环境

| 项 | 值 |
|---|---|
| 抓包机 | lab VM，Ubuntu 22.04，x86_64 |
| tshark | 3.6.2（Ubuntu 包），dissector `ieee1905` |
| 沙箱 | `out/ns-run-svctest`（`ROUTERCC=cc` 编的 x86_64 版）+ `qemu-arm-static` 跑 ARM32 probe |
| 接口 | `lab-peer`（宿主命名空间，手工 `ip link set up`） |
| 抓法 | `tcpdump -i lab-peer -s0 -U -w wire.pcap`，无过滤，事后用 `-Y ieee1905` 分类 |
