# 二进制 + 垫片：原厂 mesh 栈的复用可行性（2026-11-01）

用户提问：现在手上有原厂 mesh 用户态二进制，能不能参考 controller 的做法，用**二进制 + 垫片**
的方式做出中兴 mesh。以下是基于实测的可行性分析，不是设想。

## 0. 结论先说

1. **这条路成立，而且比通常的 blob 移植干净得多。** 这套栈是一个**闭包**：2 个守护进程 +
   `lib/` 下 114 个条目（含 uClibc 本体），`multiapd` 的 225 个外部符号里 **220 个由随包的
   `.so` 导出，剩下 5 个是 GCC 的 ITM 弱符号**（不提供也能加载）。**没有一个需要"内核专用 API"。**
   目标机只要能跑 ARM32 EABI 的 uClibc 程序，这套栈自己就能起。
2. **真正绑死硬件的只有一个文件：`mt7916.ko`。** 它是 MTK 私有 `CSP_WLAN` 栈、绑 4.1.25、
   无源码。所以「换内核不换无线」是**死路**，其余都能垫。
3. **垫片位置在 `cspd` 的"执行外部程序"那一层，但覆盖面比原先设想窄得多。** 实测 `cspd`
   通过自封装 `MonitorPcStartProgram()` 执行外部程序共 **30 处**，其中 **7 处是 `iwpriv`**
   （`iwpriv %s set BASetup=%s-%d` + 6 条 `iwpriv wlanX mac 0x83xxxxxx=...`）——
   **全是 Block Ack / 寄存器级配置，没有 SSID/PSK/ACL**。真正的主配置面是 `cspd` 里的
   **68 处 MTK 私有 `ioctl`**，文本层翻译不了。所以最终推荐是「**DB 层垫片**」（选项 A）：
   `multiapd`/`libcmapi` 只写 DB 不碰驱动，而 `libdb.so` 自带 `DBShmSrvInit`，
   服务端不用我们写。§3.1 记录了这个结论两次反转的完整过程，包括我第一版的误判。

## 1. 样本与它和前一份分析的关系

[MULTIAPD](MULTIAPD.md) 剖析的那份 `multiapd` 来自 SR1010/ZX279133 的 runtime 包（988 140 B，
**没被 strip**，6 835 个符号）。本文用的是**原厂 E2631 的那一份**，两者不同构建：

| 项 | SR1010 runtime 版 | **E2631 原厂版（本文）** |
|---|---|---|
| 路径 | `root/bin/multiapd` | `/kmodule/bin/multiapd` |
| 大小 | 988 140 B | **983 141 B** |
| 符号表 | `.symtab` 6 835 符号 / 2 184 具名函数 | **已 strip**：`9` 个定义、`225` 个导入 |
| `NEEDED` | 25 | **21**（全部命中 `/kmodule/lib`，0 缺失） |
| 调试命令 | 有 `pktdump`/`setrole`/`querydev` | 只剩 `sendcmd multiapd..master ...` 外壳 |

**代价说清楚**：原厂这份被 strip，函数名全没了，所以「按函数读设计」只能继续用 SR1010 那份；
但**导入符号表是完好的**——而判断"垫片要盖多宽"恰恰只需要导入表。本文所有结论只依赖导入表。

## 2. 闭包性：为什么这套栈能整体搬

`multiapd` 的 225 个导入，逐个回溯到"/kmodule/lib 里哪个 `.so` 导出了它"（`deps.py --graph`
的求供统计，不是按前缀猜）：

| 提供者 | 数量 | 是什么 |
|---|---:|---|
| `libuClibc-0.9.33.2.so` | 50 | libc（`socket`/`recvfrom`/`memcpy`/`fopen`…） |
| `libcmapi.so` | 32 | 配置/无线 API（`Cm*`） |
| `libcfapi.so` | 28 | 1905 门面 + 无线表读写（`Cf*`/`Wlan*`） |
| `libieee1905cfapi.so` | 24 | `CfIeee1905*` + `I5Tlv*` |
| `liboss.so` | 22 | 内部总线与定时器（`ASEND`/`RegDoMsg`/`SET_TIMER`） |
| `libcrypto.so.1.0.0` | 14 | OpenSSL（`AES_*`/`BN_*`/`RAND_bytes`） |
| `libcommfun.so` | 13 | 通用工具（`common_ioctl`/`Safe*`） |
| `libdb.so` | 10 | 配置库客户端（`dbAPI*`/`DBShmCliInit`） |
| `libmultiapcfapi.so` | 10 | `CfMultiap*` |
| `libzte_slibc.so` | 10 | 中兴 libc 扩展 |
| `libpthread` / `libctype` / `libmultiapcmapi` | 4 / 2 / 1 | — |
| **无（GCC 弱符号）** | **5** | `_ITM_*`、`__register_frame_info`，不提供也能加载 |
| **内核** | **0** | — |

抽查最"像要内核配合"的五个符号，逐个查了提供者：

| 符号 | 谁导出 | 说明 |
|---|---|---|
| `WlanCfgDbRead` / `WlanBaseDbRead` | `libcfapi.so` | 读的是 DB 行，不是驱动 |
| `bridge_ioctl` | `libcfapi.so` | 网桥私有 ioctl，随包 |
| `common_ioctl` | `libcommfun.so` | 同上 |
| `DBShmCliInit` | `libdb.so` | **共享内存**客户端，不是 socket 协议 |
| `ASEND` / `RegDoMsg` | `liboss.so` | 进程内/间消息总线 |

**判断**：这套东西对内核的全部要求就是「一个能跑的 4.1.25 或兼容 ABI 的 ARM32 内核 + 网桥」。
它不 `dlopen` 驱动、不读 `/dev/wlan*`、不依赖任何厂商 netlink family。实测 `libcmapi.so`
的 11 922 条字符串里 `nl80211`/`NETLINK`/`AF_NETLINK` **零命中**。

## 3. 它对外只有五个口子

垫片不需要盖住"整个操作系统"，只需要盖住这五个：

| # | 口子 | 证据 | 谁在用 | 移植性 |
|---|---|---|---|---|
| 1 | **DB 共享内存** | `DBShmCliInit`（libdb）；`dbAPI*` 9 个 | cspd / multiapd / Web | 自带，只缺"DB 端"进程 |
| 2 | ~~**`iwpriv <if> set K=V`**~~ → **MTK 私有 ioctl** | 见 §3.1：`iwpriv` 调用 **0 处**；`cspd` 里 `ioctl` 68 次、`common_ioctl` 68、`setsockopt` 88、`CfIoctlCfg` 2 | **只有 cspd** | ❌ MTK 私有，最难 |
| 3 | **netfilter/ebtables** | `ebtables -P mf_fwd_policy ...`、`0x893a` 过滤链 | cspd | ✅ 命令级，可平移到 nft |
| 4 | **AF_PACKET raw socket，EtherType 0x893a** | `1905sockfd`、`sendto`/`recvfrom`/`if_nametoindex` | multiapd | ✅ **标准，零垫片** |
| 5 | `/proc`、`/sys` 统计 | `brport`、温度、流量 | 两边 | ✅ 小 |

第 4 条要单独强调：**1905 这一面是完全标准的**。`multiapd` 自己在 `br0` 上开 raw socket
收发 0x893a，帧格式是 WFA Multi-AP（`MULTI_AP_GET_DPPURI`、`DPP_BSS_CONFIG_REQUEST` 这些
消息类型在字符串里都在）。这正是本仓 [MESH_INTEROP](MESH_INTEROP.md) 一路在做的互通工作——
**它不需要任何垫片**。

第 2 条还有一条旁证：`multiapd` 里有 `ctrl_interface=/var/run/hostapd`，但**整个 rootfs
没有 hostapd**（`unpack_jffs2.py --list` 的 531 个条目里 `hostapd`/`wpa_` 零命中）。
说明这串是多平台 OEM 构建的遗留，ZTE 这个 SKU 的 AP 完全由 `mt7916.ko` + cspd 拉起——
**没有 hostapd 可以借，也就没有"换个 hostapd 就完事"的便宜可占**。

### 3.1 实测：两次反转，最后落在「自封装 exec」上（2026-11-01 深夜）

这一节的方法论比结论更值得记：**同一条链路，我先后得出过两个相互矛盾的答案。**

**起点**：用户想按原方案验证"`cspd` 是否真的调 `iwpriv`"，`mv /kmodule/bin/iwpriv` 直接失败：
`Read-only file system`——`/kmodule` 是 squashfs ro，`/` 是 jffs2 ro，两边都改不动。
于是改用静态定位（`tools/lab/armcalls.py`）。

**第一次结论（错）**：扫全程序集的 `system`/`popen`/`execv`/`execve`/`execl`/`execlp`/`execvp`/
`posix_spawn`，共 134 处调用点，参数字面量含 `iwpriv` 的 **0 处**；`cspd` 的 `system()` 只有 5 处，
全是 `cat /proc/meminfo`、`ps`、`killall udpechos` 这种。据此我写下"iwpriv 那条路不存在"。

**发现矛盾**：接着查"那 8 条模板串到底谁在用"，结果每一条都有 **2–8 处代码引用**
（如 `iwpriv %s set BASetup=%s-%d` 的 VA `0x1cbe10` 被 `0x1e9b0`/`0x1ea00`/`0x23a40`/`0x23ad4` 等处加载）。
若真是死代码，不会有这么多引用。

**真因**：回到引用点看指令，`0x23a40` 是 `ldr r0, = "iwpriv"` + `bl MonitorPcStartProgram`，
后面还跟着日志 `PcStartProgram(%s, %d, %d) Runtime=%ums iRet`。**`cspd` 不用 libc 的
`system`/`popen` 执行外部程序，而是用自己封装的 `MonitorPcStartProgram()`。**
我的扫描器只覆盖 libc 那几个入口，漏了这种"项目自有 exec 封装"，于是把"没找到"错读成"不存在"。

**第二次结论（对）**：`MonitorPcStartProgram` 在 `cspd` 里被调用 **30 次**，目标程序统计如下：

| 目标程序 | 次数 | 用途 |
|---|---:|---|
| `echo` | 11 | 往 `/proc`、`/sys` 写值 |
| **`iwpriv`** | **7** | **`BASetup` + 6 条 `mac 0x83xxxxxx=...`，全是 Block Ack / 寄存器级** |
| `devmem2` | 3 | 直接读写物理内存 |
| `sendcmd` | 2 | 内部 CLI |
| `fpga` | 2 | FPGA 寄存器 |
| `brctl` / `switchtst` / `cat` / `ebtables` | 各 1 | 桥、交换、读取、过滤 |

`iwpriv` 的 7 条模板全文：

```
iwpriv %s set BASetup=%s-%d
iwpriv wlan0  mac 83082038=300      iwpriv wlan0  mac 8308203c=0
iwpriv wlan5g0 mac 83092038=300     iwpriv wlan5g0 mac 8309203c=0
iwpriv wlan0  mac 820e2098=4001000  iwpriv wlan5g0 mac 820f2098=4001000
```

**全是 BA（Block Ack）与寄存器写，没有一条是 SSID / PSK / ACL / 信道。** 那部分配置走的是
`cspd` 里的 **68 处 `ioctl`**（`common_ioctl` 68、`setsockopt` 88、`bridge_ioctl` 10、`CfIoctlCfg` 2）。

### 3.2 数据流：`Cm*` 是纯 DB 客户端（这部分结论有效且重要）

与上面并行查的另一条线，结论独立成立：

| 测什么 | 结果 |
|---|---|
| `libcmapi` 有多少函数碰硬件 | 1 551 个导出逐个扫，只认 `ioctl`/`CfIoctlCfg`/`common_ioctl`/`bridge_ioctl`/`socket`/`setsockopt`/`sendto` 的**精确**匹配（注意 `nl` 会误中 `oss_strnlen_s`）→ **12 个**，全是 LED/桥老化/端口 FDB/镜像/路由查询，**没有一个 `CmWLAN*`** |
| 是否存在函数指针表绕过它 | 查 `ioctl`/`CfIoctlCfg` 的 PLT stub 地址在非 `.plt`/`.rel.plt` 区域的 32 位引用 → **0 处** |
| `CmWLAN*` 到底干什么 | `CmWLANGetAPConfig`→`WlanCfgDbRead`+`WlanSettingDbRead`；`CmWLANGetPsk`→`WlanPSKDbRead`；`CmWLANSetRoamingCfg`→`WlanRoamingCfgDbRead/Write`；`CmWLANGetStaStatus`→`CmSendMsg2MM` |
| 谁建 DB 表 | `DBShmSrvInit`/`dbCreateTbl`/`dbCreateDomainNew`/`dbSetValCommByRowNo` **只有 `cspd` 导入**，但全部由 `libdb.so`（90 133 B）**导出** |
| `cspd` 的 DB 活动量 | `dbCreateTbl` 278、`dbCreateDomainNew` 276、`dbSetValCommByRowNo` 659、`dbAPIGetView` 399 |

于是真实数据流是：

```
multiapd（1905 + steering 决策）
   │ CmWLAN*/Cf*          ← 61 个，全是 DB 客户端语义
   ▼
libcmapi.so / libcfapi.so
   │ dbAPIGetView / WlanCfgDbWrite / ...
   ▼
DB 共享内存（libdb.so，DBShmSrvInit 由 cspd 调起来）
   │
   ▼
cspd  ← 唯一同时拥有 ioctl(68) 与 MonitorPcStartProgram(30) 的进程
   │ MTK 私有 ioctl（主配置面）+ iwpriv（BA/寄存器）+ echo 写 /proc
   ▼
mt7916.ko
```

**这个结构对搬移是利好**：`multiapd` 与硬件之间被 `cspd` 单点隔开，而 `multiapd` 那一侧
完全不需要垫片。需要重写的是 `cspd` 里"把 DB 变更应用给驱动"的那一块，
它对应**一张 DB 表 → 一组 nl80211 调用的映射**，不是 1 551 个 `Cm*` 函数，也不是 223 个 key。

> 方法论教训，写在这里免得重犯：判"某进程会不会执行外部命令"时，**不能只扫 libc 的
> `system`/`popen`/`exec*`**。厂商固件普遍有自己的 exec 封装（本例 `MonitorPcStartProgram`，
> 由 `libcfapi.so` 与 `cspd` 各导出一份），正确做法是**先找"程序名字符串被谁引用"**，
> 再顺着引用点看调用的是什么函数——字符串引用数比调用点扫描更能兜住未知封装。
## 4. 四个切点，选哪个（§3.1/§3.2 之后重排）

| 切点 | 位置 | 要写多少 | 风险 | 评价 |
|---|---|---|---|---|
| **A** | `libcmapi` 之下（DB 层） | **一个 DB 初始化 + 一个 DB→nl80211 应用器**（服务端由 `libdb.so` 自带） | 中：要还原"哪些表/列真的驱动无线" | ⭐ **推荐**（原判"不划算"是基于"得逆 shm 布局"，实测 `DBShmSrvInit` 随包导出，这条风险不成立） |
| **B** | `CmWLAN*`/`Cf*` 层 | 实测 multiapd 只用 **61 个**（33 `Cm*` + 18 `Cf*` + 10 `Wlan*`），不是 1 551 个 | 中 | 次选：只有当 A 的表语义实在还原不出来时才走 |
| **C** | `iwpriv` 文本层 | 7 条命令的 wrapper（不是 223 key） | 低 | 补充手段：**只覆盖 BA/寄存器**，挡住它等于挡住"BA 协商"这一小块，主配置面挡不住（见 §3.1） |
| **D** | 1905 报文层 | 0 | 无 | 直接互通，已有基础 |

切点 A 为什么成立，证据链（全部实测，不是推测）：

1. **`libdb.so` 同时导出服务端与客户端**：`DBShmSrvInit`@0x6a54、`dbCreateTbl`@0x7890、
   `dbCreateDomainNew`@0x7acc、`dbSetValCommByRowNo`@0xa798 全在里面（共 208 个导出），
   而 `cspd` 只是这些函数的**调用者**——所以在自己的进程里起一个 DB 服务端是可能的。
2. **`multiapd` 到硬件的最短路径经过 DB**，不经过驱动：见 §3.2 的调用链；
   `CmWLANSetAPConfig` 本体只有 60 余条指令（校验参数 + 打日志 + 尾调内部实现），
   全函数里没有一次 `ioctl`。
3. **表结构我们已经有**：`docs/milestones/SCHEMA.md` 里 `MultiAPCfg`(50 字段)、
   `P1905ComCfg`(8 字段)、`AclCfg`、`WLANCfg` 这些正是 `multiapd` 要读写的表，
   不需要"逆 shm 布局"，只需要"按已知列名建表"。
4. **应用器这一半我们本来就在写**：controller 侧已经有 nl80211 建 BSS / 踢人 / 漫游参数的
   代码路径，A 方案要的正是这些能力，只是触发源从"我们的策略"换成"DB 变更"。

切点 C 的准确定位（别夸大它）：它确实能拦到东西，但拦到的是
`BASetup` + 6 条 `mac 0x83xxxxxx=...`——**BA 会话与 MTK 寄存器直写**。
在新平台上这些要换成 nl80211 的 BA 会话控制（`NL80211_CMD_...`）或 mt76 的 debugfs，
属于"能跳过就跳过"的一类能力，不是 mesh 的必要条件。

代价与残余风险（诚实标注）：

- `cspd` 不只做 WLAN，它还是 DHCP/PPPoE/防火墙/nat 的执行体（3 MB、`dbCreateTbl` 278 次）。
  A 方案**不搬 cspd**，意味着那部分能力也没有了——但这与 mesh 无关，本来就由我们自己的网络栈承担。
- `CmSendMsg2MM`（`CmWLANGetStaStatus` 调它）说明 `multiapd` 还有一个"MM"对端要吃消息。
  MM 的身份未确认，可能是 `bin/pc`，也可能是 `liboss` 的总线对端。**这是 A 方案唯一的硬未决项**。

## 5. 三条可行路径 + 一条死路

### P1 参考 Agent（零垫片，今天就能做）

原厂机不动，我们的 controller 去 steer 它，观察它的 M1/M2/BTM 反应。
这正是 [MESH_INTEROP](MESH_INTEROP.md) §4 与 D7 在做的事。**不需要垫片，也不需要密钥。**

### P2 旁路控制（反向垫片，性价比最高）

不是"让 `multiapd` 跑在新平台"，而是**让我们的控制逻辑用原厂的 API 去驱动原厂 AP**：
在原厂机上放一个小进程，链接 `libcmapi.so`，直接调它导出的控制函数。

有现成的 API 证据（`libcmapi.so` 1 556 个导出里挑出的关键三个）：

| API | 对应我们要做的事 |
|---|---|
| `CmWLANKickOutSta` | 踢人（比我们现在 `EnActiveDeauth` 那条路更直接） |
| `CmWLANSetBandSteer` | 频段引导开关（band steering） |
| `CmWLANSetRoamingCfg` | 漫游参数（11k/11v 门限） |

可行性高的原因：ABI 已知（ARM32 uClibc）、符号未 strip（1 556 个导出）、参数是结构体指针
（需要补头文件，但可以从 `E2631_DB_SNAPSHOT` 的列定义反推）。
**代价**：结构体布局要靠调试确认，不是纯读字符串能定的。

### P3 整包搬迁 + DB 层垫片（真正的"跨平台"）

把 `/kmodule` 整个搬走（本地解包 946 个文件，其中 115 个是 0 字节——解包时丢掉的 busybox
符号链接，真机上是软链；真二进制都完整），目标是 mac80211/mt76 平台。
**关键变化：`cspd` 不进新栈**（它就是那个 68 次 ioctl + 30 次 exec 的硬件执行体，
且边界在"DB 之后"，搬它等于搬半个运营商固件）。

```
multiapd ──► libcmapi / libcfapi / libmultiapcfapi / liboss（原样搬，闭包自足）
                  │ dbAPIGetView / Wlan*DbWrite
                  ▼
            ┌───────────────────┐
            │ DB 共享内存        │  ← libdb.so 自带 DBShmSrvInit（随包 90 KB）
            │ （表结构见 SCHEMA）│
            └───────────────────┘
                  │ 变更通知 / 轮询
                  ▼
            ┌───────────────────┐
            │ 自写 applier      │  ← 替代 cspd 的 WLAN 那一小块（唯一要写的新代码）
            │ DB 表 → nl80211   │     含 BA/寄存器（原 cspd 用 iwpriv 干的那 7 条）
            └───────────────────┘
                  ▼
            mac80211 + mt76（新内核）
```

需要解决的三件事，按难度排：

1. **MM 是谁**（`CmSendMsg2MM` 的对端）——如果不是 `cspd` 而是 `pc`/`liboss` 内部总线，
   那它随包可搬；如果 MM = cspd 的某个模块，就要在 applier 里补一个最小实现。
2. **哪些 DB 表真的驱动无线**——`WLANCfg`/`WLANBase`/`WLANPSK`/`WLANRoamingCfg`/`AclCfg`
   是明确在链路上的（`CmWLAN*` 直接读写它们）；`MultiAPCfg`(50 字段) 走的是 1905 决策面，
   不一定要落驱动。**从 `CmWLANGetAPConfig` 的读路径反推，比"223 个 key 里挑活跃的"清楚得多。**
3. **`multiapd` 的 `multiap_*` 平台层**（[MULTIAPD](MULTIAPD.md) §2 记的 37 个函数）——
   它虽是内部函数，但出口全在 `CmWLAN*`/`Wlan*`，所以垫在 A 切点即可，不必动它。

> 这条路上不再需要的东西，免得白花时间：`iwpriv` 包装器（只覆盖 BA/寄存器 7 条）、
> `echo > /proc` 那 11 处（新内核的 `/proc/sys/net` 路径不同，按需重写即可）、
> `devmem2`/`fpga`/`switchtst`（交换芯片与 FPGA 直写，mt76 平台没有这些东西）。

### 死路 P4：把 `mt7916.ko` 搬到新内核

`/sbin/initwifi` 里是 `insmod /lib/modules/mt7916.ko`（这个串来自二进制内部，rootfs 里
`mt7916` 字样出现 5 次，模块大概率在 `/lib/modules/`）。它绑死 4.1.25、CSP 私有栈、
无源码，**没有任何垫片能做内核 ABI 平移**。这条路直接放弃——也正是
[E2633_FIRMWARE](E2633_FIRMWARE.md) §6 已经给出的结论：要换栈就得换整个无线栈。

## 6. 未决项（按验证成本排序）

| 未决 | 验证办法 | 成本 |
|---|---|---|
| **MM 是谁**（`CmSendMsg2MM` 的对端） | 真机 `ps` 看常驻进程；或扫 `bin/pc`、`liboss.so` 是否导出 `MM` 相关符号 | 一条命令 |
| 哪些 DB 表真的驱动无线 | 从 `CmWLANGetAPConfig`/`CmWLANSetBaseConfig` 的读写行反推表名，与 `SCHEMA.md` 对齐 | 半天（可静态做） |
| `CmWLANKickOutSta` 的参数结构 | 写个 5 行的 C 测试程序，链接 `libcmapi.so` 试调用 | 1 天（需交叉编译） |

> **已答（本轮）**：
> - ~~DB 段由谁创建~~ → `libdb.so` 自带 `DBShmSrvInit`，`cspd` 只是调用者。
> - ~~`cspd` 是否真 fork `iwpriv`~~ → **确实 fork，7 处**，但走的是自封装
>   `MonitorPcStartProgram()` 而非 `system()`，所以第一版扫 `system`/`popen` 得 0 处是**误判**
>   （完整过程见 §3.1）。原计划的 `mv /kmodule/bin/iwpriv` 在真机上直接失败：
>   `Read-only file system`（`/kmodule` squashfs ro、`/` jffs2 ro，两边都不可写）。
>   要在只读系统上验证/拦截，走 `mount --bind /tmp/fake_iwpriv /sbin/iwpriv`
>   （前提：`/tmp` 可写且有 root），或者给 `cspd` 换 `PATH` 后重启——**但见 §3.1，
>   这条链路只覆盖 BA/寄存器，不是主配置面，投入产出不高**。
> - 已不需要再查：`rootfs` 里 `/bin`、`/lib`、`/usr`、`/home` 不是"缺失"，
>   而是软链——`unpack_jffs2.py --list` 实测 `/bin -> ./kmodule/bin`、`/lib -> ./kmodule/lib`、
>   `/usr -> ./kmodule/usr`、`/linuxrc -> /bin/busybox`。整个 rootfs 只有 **107 个普通文件**
>   （全是配置与脚本），二进制本来就在 `kmodule`（squashfs）里，而那份是完整的。
>   本地看到的 0 字节是 Windows 上建不了符号链接，不是解包损失。

## 7. 建议的第一步

按"投入产出"排，不建议一上来做 P3：

1. **先做 P2 的探针**：写 20 行 C，`dlopen("libcmapi.so")` 调 `CmWLANKickOutSta`，
   在一台能随便重启的机器上试。成功就证明"旁路控制"这条路通，后面所有 mesh 动作都能复用原厂执行器。
   > 看完 §3.2 后这条更值了：`CmWLAN*` 是**纯 DB 客户端**，意味着这个探针不需要碰驱动，
   > 只要 DB 服务端在跑就能成功——比原先估计的更好调试。
2. **做"DB 表 → 无线"的映射表**：从 `CmWLANGetAPConfig`/`CmWLANSetBaseConfig` 的读写行反推
   哪些表列真在链路上，与 `SCHEMA.md` 对齐。纯静态活，半天，直接决定 P3 的规模。
3. **需要看 cspd 到底怎么用 ioctl 时**，用 `tools/lab/armcalls.py <cspd> ioctl common_ioctl`
   把 68 处调用的 `r1`（request 号）与 `r0`（socket）挖出来——这是唯一还没量化的一块。
4. **P1 不停**：互通测试本来就是零成本的证据来源。

## 8. 工具

本文的依赖数据来自 `tools/lab/elfdeps.py`，调用点数据来自 `tools/lab/armcalls.py`
（本轮新写，纯 stdlib，不需要 binutils / capstone——后者在本机 pip 无源、装不上）。

```sh
# §2 闭包表
python3 tools/lab/elfdeps.py --closure <kmodule>/bin/multiapd <kmodule>/lib

# §3.1 调用点：谁在调 system/popen/exec*，参数是什么
python3 tools/lab/armcalls.py <kmodule>/bin/cspd system popen --all

# 换符号就能查别的：ioctl / CfIoctlCfg / setsockopt ...
python3 tools/lab/armcalls.py <kmodule>/lib/libcmapi.so ioctl socket
```

`armcalls.py` 的实现坑（都已踩过，写在这里免得下次重犯）：

1. **不能用"搜 GOT 槽字节"找 PLT**：`.rel.plt` 里每条 `r_offset` 本身就是 GOT 槽地址，
   一搜一大片重定位表。要靠 PLT stub 的形态识别。
2. **这份 PLT 是经典 lazy 12 字节版式**（`add ip,pc,hi; add ip,ip,lo; ldr pc,[ip,#imm]!`），
   没有字面量；`ldr` 的 imm12 是相对那个**非页对齐**基址的偏移，所以也不能用
   `imm12 == slot & 0xFFF` 校验。正确做法是把三条的立即数累加出真实 GOT 地址再回查。
3. **`nl` 这种两字母关键词会误中 `oss_strnlen_s`**，统计 ioctl 调用时要用精确集合。
4. 回溯 `r0` 只能处理 `ldr r0,[pc,#imm]` / `adr r0` / `mov r0,#imm`；遇到 `pop {r0,…}`
   或 `bl sprintf` 之后的缓冲就如实报"非字面量"，**不要硬猜**。
5. **最重要的那条：`--targets` 里必须把"自封装 exec 函数"也列进去。** 厂商固件不只用
   libc 的 `system`/`popen`，本例 `cspd` 用的是自己导出的 `MonitorPcStartProgram()`
   （由 `libcfapi.so` 和 `cspd` 各实现一份）。只扫 libc 三个入口会得到"0 处调用"的假阴性——
   我因此得出过错误结论（§3.1）。**兜底办法：先统计"程序名字符串被哪些指令加载"，再顺着
   引用点看调用者**，这比枚举函数名可靠。

```sh
# 正确的两条交叉验证
python3 tools/lab/armcalls.py <kmodule>/bin/cspd MonitorPcStartProgram --all   # 30 处，7 处 iwpriv
python3 tools/lab/armcalls.py <kmodule>/bin/cspd system popen --all            # 6 处，0 处 iwpriv
```

它与已有的 `tools/lab/syms.py`（单文件符号视角）互补：`elfdeps.py` 答"谁供货"，
`armcalls.py` 答"谁在调、拿什么参数调"。
