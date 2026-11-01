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
3. **最省力的垫片位置不在 API 层，在 `iwpriv` 那一行 shell 命令上。** `cspd` 导入 `system()`
   和 `popen()`，无线配置最终以 `iwpriv <if> set <Key>=<Val>` 的文本形式下发，私有 key 223 个。
   拦一个可执行文件，比拦 70 个 C 函数便宜两个数量级。

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
| 2 | **`iwpriv <if> set K=V`** | cspd 导入了 `system`/`popen`；8 条 `iwpriv` 模板串；223 个 `KEY=` | cspd | ❌ MTK 私有，最难 |
| 3 | **netfilter/ebtables** | `ebtables -P mf_fwd_policy ...`、`0x893a` 过滤链 | cspd | ✅ 命令级，可平移到 nft |
| 4 | **AF_PACKET raw socket，EtherType 0x893a** | `1905sockfd`、`sendto`/`recvfrom`/`if_nametoindex` | multiapd | ✅ **标准，零垫片** |
| 5 | `/proc`、`/sys` 统计 | `brport`、温度、流量 | 两边 | ✅ 小 |

第 4 条要单独强调：**1905 这一面是完全标准的**。`multiapd` 自己在 `br0` 上开 raw socket
收发 0x893a，帧格式是 WFA Multi-AP（`MULTI_AP_GET_DPPURI`、`DPP_BSS_CONFIG_REQUEST` 这些
消息类型在字符串里都在）。这正是本仓 [MESH_INTEROP](MESH_INTEROP.md) 一路在做的互通工作——
**它不需要任何垫片**。

第 2 条还有一条旁证：`multiapd` 里有 `ctrl_interface=/var/run/hostapd`，但**整个 rootfs
没有 hostapd**（`unpack_jffs2.py --list` 的 531 个条目里 `hostapd`/`wpa_` 零命中）。
说明这串是多平台 OEM 构建的遗留，ZTE 这个 SKU 的 AP 完全由 `mt7916.ko` + cspd 的
`iwpriv ... set K=V` 拉起来——**没有 hostapd 可以借，也就没有"换个 hostapd 就完事"的便宜可占**。

## 4. 四个切点，选哪个

| 切点 | 位置 | 要写多少 | 风险 | 评价 |
|---|---|---|---|---|
| **A** | `libcmapi` 之下（DB 层） | 一张 DB 服务端 + 25 个 `Wlan*Db*` 语义 | 高：得先逆 DB 的 shm 布局 | 不划算，libdb 已经能用 |
| **B** | `CmWLAN*`/`Cf*` 层 | **70 个函数**，等于重写 `libcmapi`（它自己有 1 556 个导出） | 高 | 等于不要原厂栈了 |
| **C** | `iwpriv` 文本层 | **一个 wrapper 程序** + 223 个 key 的映射表 | 低-中 | ⭐ **推荐** |
| **D** | 1905 报文层 | 0 | 无 | 直接互通，已有基础 |

切点 C 为什么可行，证据链是完整的：

1. `cspd` 导入 `system` 与 `popen`（`execv`/`fork` **没有**导入，说明它只走 shell）；
2. 字符串里有 `iwpriv %s set BASetup=%s-%d` 这种**带格式化的模板**，说明命令是拼出来的；
3. `rootfs/sbin/iwpriv` 是个独立 13 687 B 的可执行文件，**换掉它不影响任何静态链接的调用方**；
4. `KEY=` 形式的私有参数共 **223** 个，全是 MTK Ralink 家族的词表
   （`ApCliSsid`/`ApCliWPAPSK`/`ACSChannelScan`/`ACLAddEntry`/`APBE`…），这套词表在 MTK SDK
   与被淘汰的 `mt_wifi` 驱动里有公开对照，**不是黑盒**。

也就是说：把 `iwpriv` 换成一个"记录 + 翻译"的 wrapper，就能把原厂的无线控制面**完整截获**，
再决定是转发给原厂驱动、还是映射成 nl80211/hostapd。这是这条路真正的技术支点。

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

### P3 整包搬迁 + iwpriv 垫片（真正的"跨平台"）

把 `/kmodule` 整个搬走（本地解包 946 个文件，其中 115 个是 0 字节——解包时丢掉的 busybox
符号链接，真机上是软链；真二进制都完整），目标是 mac80211/mt76 平台：

```
multiapd ──► libcmapi/libcfapi/libdb/liboss（原样）
                    │
                    ▼
              cspd（或自写的 applier）
                    │  system("iwpriv wlan0 set SSID=xxx")
                    ▼
             ┌──────────────┐
             │ iwpriv 垫片  │  223 key → nl80211 / hostapd ctrl_iface
             └──────────────┘
```

需要解决的三件事，按难度排：

1. **DB 段由谁创建**（`DBShmCliInit` 的另一端）——`init.norm` 里 `pc&` 是嫌疑进程；
2. **223 个 key 里哪些真在链路上**——用 `iwpriv ... show` 侧反推，实际活跃的大概 40–60 个；
3. **`multiapd` 的 `multiap_*` 平台层**（[MULTIAPD](MULTIAPD.md) §2 记的 37 个函数）——
   它虽是内部函数，但出口全在 `CmWLAN*`/`Wlan*`，所以垫在 C 切点即可，不必动它。

### 死路 P4：把 `mt7916.ko` 搬到新内核

`/sbin/initwifi` 里是 `insmod /lib/modules/mt7916.ko`（这个串来自二进制内部，rootfs 里
`mt7916` 字样出现 5 次，模块大概率在 `/lib/modules/`）。它绑死 4.1.25、CSP 私有栈、
无源码，**没有任何垫片能做内核 ABI 平移**。这条路直接放弃——也正是
[E2633_FIRMWARE](E2633_FIRMWARE.md) §6 已经给出的结论：要换栈就得换整个无线栈。

## 6. 未决项（按验证成本排序）

| 未决 | 验证办法 | 成本 |
|---|---|---|
| DB 段由谁创建、`pc` 是不是 DB 端 | 真机 `ps \| grep pc`、看 `/dev/shm` | 一条命令 |
| `cspd` 是否真 fork `iwpriv`（有模板 ≠ 有调用） | 真机上 `mv /kmodule/bin/iwpriv /kmodule/bin/iwpriv.bak` 后改一次 SSID，看是否报错；或用 `sendcmd cspd` 的调试口 | 一次重启，可回滚 |
| 223 key 里活跃集 | 原厂 `iwpriv <if> show` 全量抓一遍，与 cspd 模板求交 | 半天 |
| `CmWLANKickOutSta` 的参数结构 | 写个 5 行的 C 测试程序，链接 `libcmapi.so` 试调用 | 1 天（需交叉编译） |

> 已经不需要再查的一条（此前误判）：`rootfs` 里 `/bin`、`/lib`、`/usr`、`/home` 不是"缺失"，
> 而是软链——`unpack_jffs2.py --list` 实测 `/bin -> ./kmodule/bin`、`/lib -> ./kmodule/lib`、
> `/usr -> ./kmodule/usr`、`/linuxrc -> /bin/busybox`。整个 rootfs 只有 **107 个普通文件**
> （全是配置与脚本），二进制本来就在 `kmodule`（squashfs）里，而那份是完整的。
> 本地看到的 0 字节是 Windows 上建不了符号链接，不是解包损失。

## 7. 建议的第一步

按"投入产出"排，不建议一上来做 P3：

1. **先做 P2 的探针**：写 20 行 C，`dlopen("libcmapi.so")` 调 `CmWLANKickOutSta`，
   在一台能随便重启的机器上试。成功就证明"旁路控制"这条路通，后面所有 mesh 动作都能复用原厂执行器。
2. **同时做 `iwpriv` 截获**：`mv` 一下改名，确认这条链路真的是它。花费 5 分钟，决定 P3 是否成立。
3. **P1 不停**：互通测试本来就是零成本的证据来源。

## 8. 工具

本文全部依赖数据来自 `tools/lab/elfdeps.py`（本次新写，不依赖 binutils 的 ELF32 依赖/符号
分析器）。复现 §2 那张表就一条命令：

```sh
python3 tools/lab/elfdeps.py --closure <kmodule>/bin/multiapd <kmodule>/lib
# multiapd: NEEDED 21，导入 225
#   libuClibc-0.9.33.2.so   50   libcmapi.so  32   libcfapi.so  28   ...
#   (无 .so 供货)            5   ← 只有 _ITM_*/__register_frame_info 这类 GCC 弱符号
```

它与已有的 `tools/lab/syms.py` 是互补关系：`syms.py` 回答"这个符号在哪导出"（单文件视角），
`elfdeps.py --closure` 回答"这个可执行的全部导入分别由谁供货"——判断闭包性要的是后者。
