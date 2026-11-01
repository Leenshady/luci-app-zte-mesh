# multiapd：原厂 mesh 的 1905 协议实体（2026-11-01）

`cspd` 是 stripped，3 MB 全是匿名函数，我们只能靠字符串和总线号猜。**`multiapd` 没被 strip**：
6 835 个符号、2 184 个具名函数，1905 的 TLV、拓扑、M1/M2、角色选举全在里面，函数名直白到能当
设计文档读。这份文档就是读它的结果。

**先说清样本归属**：本文剖析的 `multiapd` 来自 `openwrt/zte-mesh/zte-mesh-runtime.tar.gz`
（`root/bin/multiapd`，988 140 B，`sha256 a0ba6bee…6280a`），上游是 **SR1010 / ZX279133**；
**不是** E2631 原厂 `/kmodule/bin/multiapd`（那份我们取漏了，大小未知）。两者同为 ARM32 EABI、
同族代码，但版本与裁剪未知——凡涉及「E2631 到底怎么做」的结论都带这个前提。

## 1. 身份与为什么值得单独写

| 项 | 值 |
|---|---|
| 文件 | `root/bin/multiapd`，988 140 B |
| sha256 | `a0ba6beeb1eb4e0c900b24d06e9d37e7174f9f640320d12803d7d80c4276280a` |
| 格式 | ELF32 ARM EABI，`uClibc` 目标，动态链接 |
| 段 | `.text` 0x94948、`.rodata` 0x22102、`.data` 0x79e8、`.bss` 0x2d094 |
| **符号表** | **`.symtab` 有，0x1ab30 → 6 835 个符号** |
| 具名函数 | **2 184**（其中 2 174 带 size，可直接按边界反汇编） |
| 依赖 | 25 个 `NEEDED`，含 `libdb.so`、`libcmapi.so`、`libcfapi.so`、`libieee1905cfapi.so`、`libevent.so`、`libssl/crypto` |
| 原厂进程实测 | PID 1293，`VSZ 45344 / RSS 8260` KiB（E2631，见 [VENDOR_MESH_STACK](VENDOR_MESH_STACK.md) §2.1） |

一句实用结论：**以后调试带 `multiapd` 的东西，能拿到函数级调用栈**——我们线上跑的 runtime 包里
就是这个字节（`sha256` 与 Zlion-Y 仓库内那份完全一致），不需要重新编译。

## 2. 模块划分（按符号前缀数）

| 前缀 | 函数数 | 是什么 |
|---|---:|---|
| `master_*` / `MeshMaster*` | 412 | Controller（MASTER）侧行为 |
| `I5Tlv*` | 360 | **TLV 编解码**，Multi-AP 与 1905 的字段级实现 |
| `topo*` | 188 | 拓扑发现、邻居、设备表 |
| `slave_*` / `MeshSlave*` | 150 | Agent（SLAVE）侧行为 |
| `base*` | 73 | 主循环、socket、消息分派、定时器 |
| `multiap_*` | 37 | 平台适配：信道/带宽/功率/时间 |
| `acdc*` | 35 | ACD，**Controller 侧**（发 M1、收 M2） |
| `acds*` | 23 | ACD，**Agent 侧**（收 M1、发 M2） |

残留源文件名（`.rodata` 里）：`source/base/ieee1905_base.c`；上一份文档记到的
`source/ieee1905_cfapi.c`、`source/multiap_sub.c` 属于库侧。

**关键分层判断**：`libieee1905cfapi.so` 只有 56 个导出，全是门面 API——
`CfIeee1905RegI5Msg`（注册 I5 回调）、`CfIeee1905UnRegI5Msg`、`CfIeee1905RoleElect`（选举）、
`CfIeee1905OpenWanPortListen`、`CfIeee1905SendMsg2TopoDev`、`CfIeee1905CommDevSearch`、
`CfIeee1905ActiveSendTopologyQuery`、`CfIeee1905WanForward(Set)`、`CfIeee1905RegTunnelMsg`、
`CfIeee1905SendRebootMsgForBaffalo`。**TLV 解析不在库里，在 `multiapd` 自己的 360 个 `I5Tlv*` 里。**
所以「1905 协议栈在哪个 .so」这个问题的答案是：**没有那个 .so**，实现在主程序。

## 3. 1905 收发链路（实测级证据）

数据符号直接给出 socket 句柄与状态：

```
0010e9bc  4  g_ieee1905_sockfd          ← 1905 的 fd
0010e9c4  4  g_ieee1905_msgseq          ← CMDU messageId 计数器
0010e9c8  1  g_bPacketDumpEnabled       ← 抓包开关
0010f58c 20  g_roleElectInfo            ← 角色选举
0010f38c  1  g_ACDNotifyRoleChange      ← 角色变更通知
000e2b38 936 g_WiFiMode / 000ab100 16 strDevMode
000e2f58 2760 g_tMaster / 000e4d20 171112 g_tSlave   ← 两侧上下文（slave 那份 167 KB）
```

导入符号里 `socket / bind / recvfrom / sendto / if_nametoindex / if_indextoname / ioctl / recv`
齐全，配合 `.rodata` 的失败路径：

```
bind socket to br0 failed, close 1905Sockfd
BaseRecv1905Packet recvfrom fail
1905 Packet fd is %d
_baseSetSocketEvent FD_READ_EVENT g_ieee1905_socket fail
```

**结论：1905 帧由 `multiapd` 在 `br0` 上用用户态 raw socket 收发，不是驱动收。**
[VENDOR_MESH_STACK](VENDOR_MESH_STACK.md) §2 原来的「驱动收发、不走 socket」已据此修订。

收包路径的三条日志值得记住，它们是**现成的观测点**：

```
AGENT RECV: ENTER srcdev %02x:...:%02x, msgtype[%x], len=%d, id=0x%x
CONTROLLER RECV: ENTER srcdev %02x:...:%02x, msgtype[%x], len=%d, id=0x%x
Rcv ApAutoConfig Msg on %s
```

`msgtype` 就是 CMDU 的 messageType（`0x0000`–`0x0008`），`id` 是 messageId。设备上 `logcat`
（PID 596）本来就在跑，所以**不用抓包也能看到每条 1905 消息的类型、长度、来源 MAC**。

还有三条过滤规则，直接解释我们 lab 里踩过的坑：

```
Loopback Topology discovery message %04x ignore            ← 自己发的会回环，被丢弃
Topology query message 0x%04X is not sent to us,ignore      ← 目的 MAC 不是我们 → 忽略
This 1905 msg type: %#X msg ID:0x%04x has been receieved before   ← 按 (type,id) 去重
Msg: %d not last fragment.                                  ← CMDU 分片重组
```

第二条对我们尤其重要：**帧到了不等于被处理**，目的地址（单播 MAC 或 1905 组播）必须命中。
第一条则是「网桥上抓到自己刚发的 0x893a」这个现象在进程侧的对应实现。

## 3.1 一条 M1 从函数到网线的完整路径

`_acdcSendApAutoConfigWSCM1`（`0x71188`，756 B，源文件 `source/acd/acd_client.c`）走到 `sendto` 只有五步，
每一层都能按地址核：

```
_acdcSendApAutoConfigWSCM1  0x71188
  acdGetACDConfigByFreq(band)      0x77b74   取本频段 ACD 配置，为 0 直接 return -1
  acdcRestBssCfg()                 0x77844   清 BSS 暂存
  I5MessageInit(buf, 0x9a, band)   0x66220   分配消息体（0x9a = 单播目的槽位）
  I5PacketHeaderInit(msg, 9, seq)  0x68284   messageType = 9 = 0x0009（AP-Autoconfiguration WSC）
  acdcBuildM1Msg(两个栈缓冲, band)  0x75aa8   非 0 即失败：打 "acdcBuildM1Msg fail" 并 free
  I5MessageSend(ctx, msg)          0x66bd8   逐 fragment 发送
    I5MessagePacketSend(ctx, buf, len)  0x66b0c
      sendto(ctx->fd, buf, len, 0, (sockaddr_ll*)(ctx+44), 20)
```

### 三层都把返回值丢了

这是读这段代码最要紧的一点：

| 层 | 指令 | 后果 |
|---|---|---|
| `sendto` 的结果 | `0x66b74 subs r6, r0, #0` → 失败才打日志 | 失败时打 `sendto [%s] 1905 packet fail,iRet %d errno = %d`，但**返回值只在本函数内用到** |
| `I5MessagePacketSend` | 循环末尾 `0x66c24 mov r0, r4`，此时 `r4` 恒为 0（循环退出条件） | **永远返回 0** |
| `I5MessageSend` | 同上，调用方的错误码无处可传 | **永远返回 0** |
| `_acdcSendApAutoConfigWSCM1` | `0x71438 bl I5MessageSend` 之后 `0x71444 mov r0, r5`，`r5` 在 `0x713fc` 被置 0 | **无条件 `return 0`** |

所以上层打印的 `_acdcSendApAutoConfigWSCM1 success` **只证明函数跑到了最后一行**，不证明任何字节上了网线。
反过来说，它也**不证明没发出去**——2026-11-01 我们就用这句话配一次错误的抓包统计，得出过
「M1 根本没上以太网」的错结论（见下面的「怎么量」）。

### 实测：M1 真的在线上，771 / 801 字节

同一台 AX3600（`slave`）+ 沙箱控制器（`master`），一次服务重启的完整交换：

```
240  e3 -> 广播     0x0007 Search
230  03 -> e3       0x0008 Response
771  e3 -> 03       0x0009 M1    tlv 0x11 WSC(539) + 0x85 RadioId(17) + 0x0b Vendor(181) + EOM
911  03 -> e3       0x0009 M2    tlv 0x11 WSC(656) + 0x82(6) + 0xe0(31) + 0x0b Vendor(181) + EOM
801  e3 -> 03       0x0009 M1    tlv 0x11 WSC(539) + 0x85 RadioId(47) + 0x0b Vendor(181) + EOM
911  03 -> e3       0x0009 M2
```

M1 的 WSC 属性（`frames.py --cmdu 9 --tlv` 原样输出）：`MessageType=0x4`、`PublicKey` **192 字节**
（1536-bit DH，是真算的，不是占位）、`AuthTypeFlags=0x63`、`EncryptTypeFlags=0x0f`、`ConfigMethods=0x1aa`、
`WPSState=0x2`(Configured)、`Manufacturer='ZTE'`、`ModelName`/`DeviceName` 为 32 字节定长串、
`SerialNumber='PUBLIC-NOT-DEVICE-SERIAL'`（原厂写死的占位）、`RFBands` **0x1(2.4G) / 0x2(5G)**。
M2 侧 `MessageType=0x5`，`EnrolleeNonce` 与对应 M1 逐字节相同——配对是严格的。

两个 M1 的 `0x85` Radio Identifier TLV 长度不同（17 / 47），频段差异就在这里，**不是重复发包**。

### 型号名的来源

`ModelName` 不是从芯片或 `/tmp/sysinfo` 读的，而是环境变量 `ZTE_MESH_REAL_MODEL`
（`openwrt/zte-mesh/files/zte-mesh.init:638` 固定写 `ZXSLC SR1010`）。所以拓扑页上那个
「假身份」其实是我们自己给沙箱 rootfs 定的牌子，改这个变量就能改 M1 里申报的型号。

### 它处在哪一步：ACD Client 的五个状态

`_acdcSetRenewStateMachine`（`0x70ff4`）是唯一的转移入口，末尾三句就够读：

```
71144: ldr  r3, [r7, #64]        ; r3 = ctx->state（每频段一份，ctx+band*4+0x40）
71154: add  r5, r5, r3, lsl #2   ; r5 = 0xbe98c；+32 之后正好是 g_Acdc_Sm
7115c: ldr  r5, [r5, #32]        ; handler = g_Acdc_Sm[state]
71160: blx  r5                   ; handler(band, event, msg, 0)
```

`g_Acdc_Sm`（`0xbe9ac`）是一张纯函数指针表，按状态号排：

| state | handler | 地址 |
|---:|---|---|
| 0 | `_acdcInitialHandler` | `0x72bf8` |
| 1 | `_acdcSearchHandler` | `0x72000` |
| 2 | `_acdcConfigHandler` | `0x729cc` |
| 3 | `_acdcVendorHandler` | `0x7266c` |
| 4 | `_acdcCompleteHandler` | `0x72408` |

**M1 在 state 2（Config）发出**——`_acdcConfigHandler` 收到 `0x0008` 后就是这条路。所以真机日志
`acdc current state[0]:3,event:10` 里的 **3 是有意义的**：两个频段都已经过了 Config、发过 M1、
现在停在 Vendor 处理厂商 TLV。**卡在 3 而不进 4，是「M2 之后」那条链上的问题，不是发送问题。**

### 怎么量（别再用 tcpdump 的文本长度）

| 手段 | 命令 | 给到什么 |
|---|---|---|
| trace 库已拦的 `sendto` | `logread \| grep tx1905` | `cmdu=9 bytes=771 sent=771 errno=0`，**长度和返回值都有**，不用抓包 |
| 进程自带 dump | `pktdump 1` → `/var/tmp/<pid>_multiapd` | 它自己认为收到/发出的帧 |
| 抓包后精确统计 | `python tools/capture/frames.py <pcap> [--cmdu 9] [--tlv]` | 直接读 pcap 记录头，绕开文本解析 |

**踩过的坑**：`tcpdump -e` 的文本输出在流式管道里会被 hex dump 打断，`awk '{print $NF}'`
取到的"长度"是 dump 碎片，能把 771 字节的 M1 读成 68 字节。上一节记的「没有大帧」就是这么来的。
量帧长必须读二进制记录头。

## 4. M1 / M2：两端都在，且对称

| 方向 | 函数 | 地址 | 大小 |
|---|---|---|---:|
| Controller → Agent：**发 M1** | `_acdcSendApAutoConfigWSCM1` | `0x00071188` | 756 |
| Agent 收 M1 | `_acdsRcvApAutoConfigWSCM1.constprop.10` | `0x0006edd0` | **2 388** |
| Agent → Controller：**发 M2** | `_acdsWSCM2TlvInsert`（只插 TLV） | `0x0006e4a4` | 152 |
| Controller 收 M2 | `_acdcRcvApAutoConfigWSCM2` | `0x00072f1c` | 1 892 |
| Agent 发 Renew | `_acdsSendApAutoConfigRenew` | `0x0006dc44` | 404 |
| Search（双向） | `acdsRcvApAutoConfigSearch` / `acdSendApAutoConfigSearch` | `0x0006f758` / `0x00089e64` | 1 424 / 304 |
| 厂商私有 TLV | `_acdcRcvApAutoConfigVendor` / `_acdcSendApAutoConfigVendor` / `_acdsRcvApAutoConfigVendor.isra.5` | `0x0007173c` / `0x00071dc0` / `0x0006ea70` | 1 432 / 576 / 864 |
| M1 频段 TLV | `I5TlvAutoconfigFreqBandTypeInsert` / `…Extract` | `0x000786f8` / `0x00078754` | 92 / 112 |

命名规律：`acdc` = **Controller 侧**（发 M1、收 M2），`acds` = **Agent 侧**（收 M1、发 M2）。
`_acdsWSCM2TlvInsert` 只有 152 字节，说明 M2 的主体组装在别的函数里，它只负责塞 WSC TLV——
**想改 M2 的形状，从 `0x6e4a4` 往上找调用者**。

角色是二元的，字符串写死了编码：

```
Role change to %d(1:SLAVE 2:MASTER)
Role Elect is Running!
supportedRole is %d
Acd Role is Client. / Acd Role is Server. / Acd Role is undefined.
```

`1=SLAVE`、`2=MASTER` 这组与 [VENDOR_MESH_STACK](VENDOR_MESH_STACK.md) §3 里 `0x9095`
（`_Ieee1905RoleSet`）的载荷语义应当一致，但**尚未在线上核对过取值**——列进待验证。
另有 `Acd Role is {Client,Server,undefined}` 三态串，语义未定，**不要**直接等同 MASTER/SLAVE。

进程内消息表两张：`g_map_ctler_msg_list` / `g_map_agent_msg_list`，配
`MapControllerMsgListInit` / `MapAgentMsgListInit` / `MapGetControllerMsgList`——
说明同一份二进制按角色注册不同 handler 集，**不是两套代码**。

## 5. 调试接口：一张现成的命令表

`.rodata` `0x0a9f8a`–`0x0aa3ac` 连续排着一整张 debug 命令表（每条都有 `[name]: 说明`）：

| 命令 | 说明 | 对我们的用处 |
|---|---|---|
| `showalmac` | show ALMAC | 看本机 AL MAC（1905 身份） |
| `showsock` | show sockets | 看 `g_ieee1905_sockfd` 绑没绑上 |
| `showmsgreg` | show registered message handlers | 看当前角色注册了哪些 handler |
| `showmsg` | show message queue | 内部消息队列 |
| `querydev` | query topo devices on condition | **直接看拓扑**，不用抓包 |
| `switch` | enable or disable 1905 function | 总开关 |
| `showlinkreg` | show registered phylink handlers | 链路事件回调 |
| `setrole` | set role | **强制角色** |
| `roleplay` | elect role | **触发角色选举** |
| `listen` | start listen on wan port | 对应 `CfIeee1905OpenWanPortListen` |
| `showerrormsg` | show error message queue | 收包错误 |
| `showMsgStatistics` / `HandleMsgStatistics` | 消息统计，`<begin[0]/end[1]/clear[2]> <Max num>` | **按类型计数**，判断 M1 到底来没来 |
| `showElectInfo` | show history role elect info | 角色选举历史 |
| `pktdump <0\|1>` | enable/disable packet dump to `/var/tmp/<pid>_<proc>` | **原地抓 1905**，见下 |

### `pktdump`：设备上没有 tcpdump，但它自带

```
[pktdump <0|1>]: enable/disable packet dump to /var/tmp/<pid>_<proc>
Current packet dump status: %s
Usage: pktdump <0|1>  (0:disable, 1:enable)
Packet dump to file %s
```

配套函数 `_basePktDump`、`I5MessageSetPacketDumpEnable`、`I5MessageGetPacketDumpEnable`、
`I5MessageDumpPacketToFile`。**两件事还没确认**：这张命令表的入口是 stdin、还是某条内部消息
（`I5Message*` 那组像是消息接口）；以及落盘格式（裸 CMDU 还是带长度头）。

价值在于：原计划是「static 编一个 armv7l musl 的抓包程序 + 限时开 promisc」，
现在只要让 `multiapd` 自己把帧写进 `/var/tmp` 再 tftp 取——**不用交叉编译，不用 promisc，
不用停机窗口**。这是把 D7「抓 0x893a」从高成本项降成低成本项的路径。

## 6. 隧道模式：现成的注入通道

```
tunnelSendVendorMsg / _tunnelDo1905Msg / _tunnelResolveMsg / _tunnelDoVendorM
_tunnelStart / _tunnelReadDataFromFile
g_TunnelInfo
```

`_tunnelReadDataFromFile` + `CfIeee1905RegTunnelMsg` 说明：**它可以从文件里读 1905 报文并当成
收到的帧处理**。如果这条路径在当前配置下可达，那就是一个不用改线缆的注入通道，可用于
「拿真实帧回放给 E2631」这类实验。**未验证**，且属于要小心的一类（注入错误拓扑可能让它
改 Wi-Fi 配置）。

## 7. 配置面：DB 就是 `sendcmd 1 DB` 背后的东西

导入符号把链路接得很清楚：

```
DBShmCliInit                      ← 共享内存 DB 客户端（对应 /SYSV0000ffff）
dbAPIAddView / GetView / SetView / QryView / LstView / Save / ChgNotifyReg
dbFindTbl / dbClearTbl
MapGetMultiAPCfgDB / MapGetRoamCatonCfgDB
MapGetHighCapacityMode / MapSetHighCapacityMode / MapAddCacheMac
MapMessageSend / MapMessageGet / MapMessageTlvExtract
CmMultiApRefreshStaDB / CmWLANListAdByCard
CmWLANSetACLIsFrom1905Slave / CmWLANSetAclList_1905_Slave   ← 1905 下发 ACL 到本地 WLAN
```

于是那套运维命令的落点就明确了：`sendcmd 1 DB p <表> / set … / save / rreset`
操作的就是 `libdb.so` + `DBShmCliInit` 这块共享内存，`multiapd` 通过 `MapGet*DB` 读它。
**这给了我们一条不碰二进制的读写通道**——详见 [E2633_TELNET_BOOT](E2633_TELNET_BOOT.md) §11。

## 8. 对我们项目的三个影响

1. **`MESH_INTEROP` §5 要改**：内核不是「只识别、只丢弃」，还有 `br_ieee1905_local_in_handle`
   把帧送给本机——这正好是 `multiapd` 那条 raw socket 的对端。已在 §5.5 补。
2. **「M1/M2 走不通」已经证伪**：`0x71188` 已逐函数反汇编（§3.1），M1 在线上是 771 / 801 字节，
   对端回 911 字节的 M2，`EnrolleeNonce` 严格配对。真正没有证据的是**收下 M2 之后有没有落地**——
   那条链在 `0x6edd0`（agent 收 M1，2 388 B）与 M2 应用侧，还没反汇编。
3. **验证成本整体下降**：`pktdump` 替代自带抓包程序，`showMsgStatistics` 替代逐帧人工数，
   `querydev` 直接吐拓扑。原先「需要停机窗口」的几项，至少前两项不再需要。

## 9. 局限（必须连着读）

- **只对 M1 发送链做了逐函数反汇编**（§3.1：`0x71188` → `0x75aa8` → `0x66bd8` → `0x66b0c` → `sendto`）。
  其余结论仍来自 `.symtab`、`.dynsym`、`.rodata` 字符串。**M2 的组装与角色选举取值仍需实证**。
- 样本是 **SR1010 版**，不是 E2631 原厂那份；两者差异未知。
- 命令表只证明「命令存在」，**不证明命令在当前构建/配置下可达**；`pktdump` 的入口尤其未定。
- `1:SLAVE 2:MASTER` 是日志串里的编码，**没在线上验证过 DB/消息里用的是不是同一套值**。
- 隧道注入路径未验证，且属于有副作用的一类，动之前先备份 `/defcfg`。

## 10. 复现

```sh
# 取样本（本文用的那份）
tar xzf openwrt/zte-mesh/zte-mesh-runtime.tar.gz root/bin/multiapd
sha256sum root/bin/multiapd        # 期望 a0ba6bee…6280a

# 符号表（ELF32/ARM，无 binutils 也能读：解析 .symtab 即可）
python3 - <<'PY'
import struct
d=open('root/bin/multiapd','rb').read()
sh=struct.unpack_from('<I',d,0x20)[0]
n=struct.unpack_from('<H',d,0x30)[0]
secs=[struct.unpack_from('<10I',d,sh+i*40) for i in range(n)]
sym,st=secs[28],secs[29]
for i in range(sym[5]//16):
    no,v,sz,info,_,shndx=struct.unpack_from('<IIIBBH',d,sym[4]+i*16)
    if info&0xf==2 and shndx:            # STT_FUNC
        s=st[4]+no
        print(f"{v:08x} {sz:6d} {d[s:d.index(b'\\0',s)].decode()}")
PY
```

设备侧（**只读，先别写**）：

```sh
multiapd 相关：无 tcpdump 时
  logcat | grep -E "AGENT RECV|CONTROLLER RECV|Rcv ApAutoConfig"   # 看得到 msgtype/len/id
  # 若找到 debug CLI 入口：
  #   showmsgreg / showElectInfo / querydev / showMsgStatistics <0> <64>
  #   pktdump 1    → /var/tmp/<pid>_multiapd，再 tftp 取回
```
