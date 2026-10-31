# 中兴 mesh 组网：外部资料、固件证据与互操作边界（2026-10-31）

> 这一篇回答三个问题：中兴的"自组网"到底是什么协议、网上和 GitHub 有什么可用的、我们做功能时
> 哪些事不能做。结论直接进 [FEATURE_STATUS](../FEATURE_STATUS.md) 的口径，实现细节仍在
> [FINDINGS](../FINDINGS.md) 与 [REAL_CHILD](../REAL_CHILD.md)。

## 1. 结论

1. **中兴的 mesh 就是 IEEE 1905.1 + Wi-Fi Alliance Multi-AP**，不是自研二层协议。线上帧是
   `0x893a`，消息号落在 1905 的 0 到 7 与 13/14，配置走 Multi-AP 的 `0x8001`/`0x8003` 系列。
   中兴自己的公告也这么讲：ZXHN F680 在 2019 年是业界第一台过 EasyMesh 认证的 PON ONT，
   F689 在 2020 年过了 R2。
2. 所以"兼容中兴 mesh"= **在 0x893a 上按 Multi-AP 说话**，不是复刻中兴固件。本项目把原厂
   `multiapd` 当控制器跑，方向是对的；换成自己写一套控制面反而是倒退。
3. **固件解包对"抄代码"没用**：E2633 的 rootfs 是 AES-128-ECB 的 JFFS2，密钥运行时推导、镜像里没有，
   而 `multiapd`/`libcmapi`/`cspd` 全在那里面（[E2633_FIRMWARE](E2633_FIRMWARE.md)）。
   已经拿到的内核 `.config` 有价值，但它证明的是"原厂无线不在标准栈里"，不是"可以照抄"。
4. 因此两条路不能走：**给非中兴机器刷中兴固件**（无线是 CSP_WLAN 私有栈，没有 cfg80211/mac80211，
   我们的 `wlan-sync` 落不上去）；**在插件内部自造一套组网协议**（那就是独立 mesh 插件，且与中兴
   设备互不相通，正好违背项目目标）。
5. 第三方项目（prplMesh、BBF 1905.1a 栈）可用来**对照编码**，不能整套搬进来：它们各自都是完整的
   mesh 栈，搬进来等于把项目变成另一个 prplMesh，而且都要求 mac80211 + hostapd。
6. **原厂内核里没有 mesh 协议**：它只做三件事——认 `0x893a`（连 802.1Q 内层一起认）、把 1905 组播帧
   从桥接路径里丢掉、用一个 u32 位表示"当前是不是 multi-ap 模式"。协议栈全在用户态 `cspd`（§5）。
   这条把第 2 条硬约束从推论变成了实证。

## 2. 协议层：我们到底在跟谁对齐

| 层 | 内容 | 本项目状态 | 依据 |
|---|---|---|---|
| IEEE 1905.1 | CMDU 0/1/2/3（拓扑发现/通知/查询/响应）、5/6（链路度量） | 已实现 | FINDINGS 第 4 节，真机通过 |
| 1905.1a | 13/14（邻居/非 1905 邻居相关） | 已观测到，未主动用 | REAL_CHILD 第 1 节 |
| WFA Multi-AP R1 | `0x0007`/`0x0008`/`0x000a`（AP 自动配置）、`0x8006`/`0x8007`（信道选择请求/响应）、`0x0009` 里的 WSC M1/M2、`0x8003` Multi-AP Policy Config Request、`0x8001`/`0x8002` 能力查询/报告 | 已实现，M2 在 Z506 上真机生效（编号已用 dissector 的表核过，见 [WIRESHARK_1905](WIRESHARK_1905.md)） | REAL_CHILD 第 1 节 |
| ZTE 私有 TLV | 厂商 TLV（`0xdd`/`0xe0`），由 `MultiAPCfg.VersionMode` 决定发不发 | 已实现，`version_mode=128` 时发 | REAL_CHILD 第 2 节 |
| ZTE 私有策略 | `SSEND(0x2401, 0x10205/0x10206)` 热更新域/漫游阈值 | 已实现 | FINDINGS 第 4 节 |
| ZTE 内部总线（不是协议） | `ASEND`/`SSEND` 消息号：`0x9095` 角色、`0x90d3` DevSearch、`0x90aa` 拓扑查询、`0xa932`/`0xa93c` Multi-AP 配置、`0xa94e` VLAN、`0xa980` 信道扫描 | 不适用——我们直接把 CMDU 组出来发 | [VENDOR_MESH_STACK](VENDOR_MESH_STACK.md) 第 3 节 |
| ZTE 内核（不是协议层） | `br_ieee1905_check_is1905_msg` 认 `0x893a` 与 802.1Q 内层；桥接钩子丢弃 1905 组播；`wlan_get_wlan_multiap_mode()` 只读一个 u32 位 | 不适用——内核里没有可复用的实现 | 本文 §5 |

对表的直接用途：**任何新功能先问"它对应哪个 CMDU/TLV"**。答不出来，说明这个功能要么是本机行为
（UCI 侧），要么就是自造协议。

## 3. 外部资料与 GitHub 上有什么

| 来源 | 是什么 | 对本项目的用法 |
|---|---|---|
| [prplMesh](https://github.com/prplfoundation/prplmesh)（prpl 基金会，Intel 贡献） | 开源的 Multi-AP/EasyMesh 控制器 + agent，OpenWrt/RDK-B 双平台，带 EasyMesh 认证测试脚本 | **对照 CMDU/TLV 编码**；agent 侧（我们 `slave` 角色）的参考实现 |
| prplMesh-openwrt | prplMesh 的 OpenWrt 集成层 | 看它怎么把 hostapd 的 multi_ap 用起来（我们 `backhaul=multi_ap` 走的是同一条） |
| [BBF 1905.1a 参考栈](https://github.com/BroadbandForum/1905.1a) | 1905.1/1a 的参考实现，prplMesh 的前身 | 查 1905 基础消息的字段定义 |
| Technicolor `multiap_controller` | 另一套开源 Multi-AP 控制器（libuv 事件循环 + TLV 引擎） | 结构对照：它的拓扑树、策略下发与我们 `controller-run.c` 的差异 |
| Wireshark `ieee1905` dissector | 现成的 1905/Multi-AP 解析器 | 抓我们的帧逐字段核对，不需要中兴设备 |
| Zlion-Y 的 E2631 移植记录 / `e2631-openwrt` | 同一平台的第三方 OpenWrt 移植，给了固件槽位表与 rootfs 加密算法 | 固件线的唯一可复用外部事实来源 |

一条现实约束：OpenWrt 官方**没有**中兴这几代 SoC（ZX2791xx 系列）的支持，论坛的答复是"驱动与
主控都没有，等于从零做"。这正是"互操作"而不是"替换"的理由 —— 我们的目标只能是"让 OpenWrt
设备当中兴 mesh 的控制器/子节点"，不是"让中兴设备跑 OpenWrt"。

## 4. 固件解密：现状与为什么不作为路线

已经做过的事（[E2633_FIRMWARE](E2633_FIRMWARE.md) 有完整记录）：

- E2633 官方固件 26.8 MB，容器头明文，uImage 解开后是 zImage，再解开得到 4.1.25 内核；
- 内核带 IKCONFIG，**完整 `.config` 已提取**，这是本次唯一"能直接用"的产物；
- rootfs 23.4 MB 全程熵 7.99，判定 AES-128-ECB 加密的 JFFS2，**密钥不在镜像里**；社区工具
  （binwalk / unblob）能处理容器和 squashfs/UBI，对这一层没有公开密钥，也就没有下一步。

所以固件线只有两个产出值得记，都不是代码：

1. `CONFIG_CSP_WLAN_*` + 没有 `CONFIG_CFG80211`/`CONFIG_MAC80211` → 原厂无线是私有栈，
   **第二个机型独立确认**（此前只有 e2631-openwrt 一个来源）。
2. `EXTENDER_LINK_BY_DRIVER=y`、`br_ieee1905_filter_init` → 回程由驱动建、内核网桥对 1905 有专门钩子。
   后者解释了 lab 与真机可能的行为差异（沙箱里没有这个钩子）。

**真正有效且合法的取代码方式是运行态**：本项目用的 `libcmapi.so`、`libzte_controller_route.so`
就是从自己拥有的设备上取的（root 后读分区），不是从加密镜像里撬出来的。这条路的成本比破解低两个
数量级，而且拿到的是**和目标机同版本**的库。固件线在拿到可 root 的新机型之前不再推进。

### 4.1 内核读路径里没有解密代码（E2631 2024-09 vmlinux 实证，2026-11-01）

编程器固件的 rootfs 是 AES-ECB 密文，于是最自然的假设是"内核在 NAND 读路径上解密，密钥就编在里面"。
把这颗内核（6,627,680 B，`zx_denali_nand.c` 有 BUG 点可定位）拆开逐条核过，**假设不成立**：

| 查什么 | 结果 |
|---|---|
| `zx_denali_nand.c` 的读页路径 | 3 处 BUG 点落在 `0xC023FE2C`(L1261) / `0xC0240080`(L1454) / `0xC0240B6C`(L1370)；整段是 Denali 控制器的寄存器序列（命令码 `0x2200`/`0x2300`/`0x2400`、bank/地址/`dsb st`、DMA 启动），**没有一次对 crypto 的调用** |
| 内核编了哪些 crypto | 只有 `crypto/algapi.c`、`crypto/api.c`、`crypto/scatterwalk.c` 三个文件留下 BUG 点 |
| 有没有 ECB/CBC 模板实例 | `"ecb(aes)"`、`"cbc(aes)"`、`"ecb"`、`"cbc"` 在镜像里**一个都没有** |
| AES 原语在不在 | 在：`crypto_aes_set_key` @ `0xC015A194`、`crypto_aes_expand_key` @ `0xC0159D1C`、T 表 @ `0xC045D690` / `0xC0464668`。但 `crypto_aes_set_key` 是 `cra_cipher.cia_setkey` 回调，靠函数指针调用，**没有直接调用者**；`"aes"` 字符串（`0xC052AF40`、`0xC063A278`）也**零引用** |
| `CBC decrypt OK!` 在哪 | `0x00619E`，和 `HASH timeout`/`RSA timeout`/`AES timeout`/`verify OK!` 连在一起 —— 属于 **Bootloader 分区（`0x0`–`0x100000`）的一级加载器**，不是内核 |

所以密钥派生只有三个可能的挂点，**都不在这颗 Linux 里**：

1. **BOOTROM / cspboot**（`0x0`–`0x100000`，LZ 之后是 ARM32，这次没反汇编）；
2. **NAND 控制器硬件透明解密** —— 编程器关 ECC 裸读拿到的是物理密文，设备通过控制器读同一页拿到明文。
   这一条能同时解释三件事：OOB 全 `0xFF`、rootfs 高熵、以及同型号活机器的 rootfs 是明文 JFFS2
   （最后这一条当时是从 `mount`/`df` 推的，实证判据与真机侧做法见 §4.2）；
3. eFuse/OTP 里的设备唯一密钥，运行时喂给上面两者之一。

**一条能证伪的判据**：在任意一台能 root 的 ZTE 上读 `hexdump -C -n 64 /dev/mtdblock8`
（原文写的是 `dd … | hexdump`，但 E2631 的 busybox 没编 `dd`，见 §4.2.2/§4.2.3）
——若看到 `hsqs`/`UBI#`/JFFS2 节点而编程器 dump 同页是密文，就是第 2 条（硬件透明解密），
密钥根本不在可读的镜像空间里；若也是高熵，才回落到第 1/3 条。

### 4.2 手上有 telnet root：能取，但"解密"这个动作不存在（2026-11-01）

按目的拆开看，成本差三个数量级：

| 你要的是 | 能不能 | 怎么做 | 成本 |
|---|---|---|---|
| rootfs 的**文件内容** | 能，而且不用解 | `cat` 单个文件，或 `tar` 挂载点 | 十分钟 |
| rootfs 的**整块镜像** | 能 | `cat /dev/mtdblock8 > …`（没有 `dd`，见 §4.2.3） | 十分钟 + 一次 tftp |
| 解别人编程器 dump 用的**密钥** | 大概率不能 | §4.2.4 | 半天，期望值低 |

原因是一件事：**设备上这块 Flash 的可读视图就是明文**。JFFS2 在 mtdblock8 上挂载成功、
`df` 报 17 MiB / 100% 占满，说明内核从 MTD 读出来的是合法节点（`85 19` + nodetype），不是密文。
所以对 mtdblock8 做 `dd`，拿到的和 `cat` 出来的是同一份字节——**中间没有一层需要你去解**。

#### 4.2.1 一处要纠正：这条判据至今没有真跑过

[VENDOR_MESH_STACK](VENDOR_MESH_STACK.md) §1 把根文件系统写成"JFFS2 明文"，那是从
`/proc/mounts` + `df`（类型 jffs2、17 MiB、100% 占满）推的，**没有做过 hexdump 比对**。
它和 §4.1 那条判据是同一件事的两端，而判据本身仍是待执行项。区别在于：

- "挂载成功" 只证明**内核看到的字节**是合法 JFFS2；
- "同一物理页在编程器 dump 里是密文、在 `/dev/mtd8` 里是明文" 才证明**变换发生在读路径**。

前者成立，后者未验。下面第一条命令就是补这一步。

#### 4.2.2 判据：读 64 字节，把三种机制分开

```sh
# 只读。不写 flash、不动配置库
cat /proc/mtd                    # 确认 mtd8 的名字与大小
cat /proc/mounts | grep mtdblock # 确认根确实是 mtdblock8

# 判据本体。不要用 dd，也不要 head —— 这台机器的 busybox 1.17.2 两个都没编（见 4.2.3）
hexdump -C -n 64 /dev/mtdblock8
```

`hexdump` 的 `-n`/`-s` 在 busybox 里是有的，所以这一条不依赖任何被裁掉的东西。
若 `hexdump` 也没编，用 `busybox hexdump` 或 `cat /dev/mtdblock8 | od -c` 兜底。

| 输出 | 机制 | 结论 |
|---|---|---|
| `85 19 03 20 …`（JFFS2 magic `0x1985` + nodetype） | 读路径给出明文；内核里 AES 零调用者（§4.1）⇒ 变换在**控制器硬件**或 **BootROM 一次性解密** | 没有"密钥文件"可偷，但也**不需要**——`dd` 就是解密器 |
| 高熵、无 `85 19` | mtd8 不是挂载的根（换分区号重试），或读路径确实返回密文 | 回 §4.1 的第 1/3 条，走 §4.2.4 |
| 全 `0xFF` / `Operation not permitted` | 不是 mtd 的问题，是节点权限或分区未映射 | 先 `ls -l /dev/mtd*` |

`mtd8`（字符设备）与 `mtdblock8`（块设备）走同一个 `mtd_read`，字节应当一致；
**若不一致，差异点本身就把解密层定位出来了**，那是意外收获，值得单独记。

#### 4.2.3 取镜像与取文件：两条都不需要"解密"

**先确认工具在不在**。这台机器的 busybox 是 `1.17.2 (2026-08-26)` 裁剪版：**没有 `dd`**
（`dd: not found`，rc=127），也没有 `head`；`df` 不支持 `-h`（只有 `-P/-k/-a/-i/-B`）。
照本节最早的版本抄命令时，`dd` 直接失败、文件从未存在，`tftp` 才报
`can't open '<file>': No such file or directory`——**那条报错是后果，不是原因**。

```sh
busybox --list | grep -E '^(dd|head|tail|cat|hexdump|md5sum|tar)$'
busybox dd 2>&1        # applet 编进去了、只是没做软链时，这条路仍然可用
```

同一次实测拿到的 tmpfs 布局（`size` 是上限，还要刨掉 root 自己占的那点）：

| 挂载点 | size | 评价 |
|---|---|---|
| `/DPITMP` | 61440k（60 MiB） | 最宽松，但属于 DPI 模块，别长期占 |
| `/var` | 40960k（40 MiB） | **推荐**：放 17 MiB 镜像后还剩 20 MiB |
| `/tmp` | 20480k（20 MiB） | 勉强够，只剩 3 MiB 余量 |
| `/tracelog` `/owjdxbplugin/tmp` `/owdptsplugin/tmp` `/uadd` `/mnt` | 16k–3 MiB | 太小，别用 |

**取镜像：`cat` 顶替 `dd`**。mtdblock 是块设备，`cat` 读到设备尾就是 EOF，效果与
`dd if=… of=…` 相同，只是不能 `skip`/`count`。目标选 `/var`，不是 `/tmp`：

```sh
cat /dev/mtdblock8 > /var/rootfs.jffs2 ; echo "rc=$?"
ls -l /var/rootfs.jffs2        # 大小必须等于 /proc/mtd 里 mtd8 的 size，这步别省
md5sum /var/rootfs.jffs2
tftp -p -l /var/rootfs.jffs2 -r rootfs.jffs2 192.168.1.191
rm /var/rootfs.jffs2

# B. 只要文件（更省内存，且能立刻 grep）
tar -czf /var/etc.tgz /etc /usr/sbin /usr/lib 2>/dev/null
tftp -p -l /var/etc.tgz -r etc.tgz 192.168.1.191
```

**没写出文件时的成因**（前两条是 2026-11-01 实测，按出现频率排）：

| 现象 | 成因 | 处置 |
|---|---|---|
| `dd: not found`（rc=127） | busybox 裁剪版根本没编 `dd` | 用 `cat 设备 > 文件`；先试 `busybox dd` |
| 无文件，报 `No space left on device` | 目标在满的 rootfs 上，或 tmpfs 太小 | 换 `/var`；或 `mount -t tmpfs -o size=32m none /mnt` |
| 进程被 `Killed`（rc=137） | tmpfs 挤内存，OOM 杀掉它（`cspd` 已占 174 MB VSZ） | 改流式（见下），别落盘 |
| 目标目录不存在 | 部分固件只有 `/var/tmp` | `df -k /var` 后换路径 |
| 文件在但 `tftp` 仍打不开 | busybox 没编 put（`tftp --help` 里没有 `-p`） | 用 `/kmodule/bin` 的 `wput` 或 `curl` |

还有两种是文件**消失**而不是没生成，现象一样、处置完全不同：① 整段粘贴时末尾那条 `rm`
已经执行过；② tmpfs **设备一重启就清空**。这两种先在对端 `ls -l` 一眼分开，别在设备上白折腾。

最快的定性办法完全不落盘：`hexdump -C -n 64 /dev/mtdblock8`
——既回答读不读得出来，也顺手把 §4.2.2 那条判据做了。

**分块法**（需要 `dd`，这台机器没有；脚本留给 applet 齐全的固件）：**分块法**（只要 `/tmp` 有几百 KiB 空间就能走完）：

```sh
# 每块 2 MiB；放不下就把 CH 降到 262144（256 KiB），块数变多而已
CH=2097152 ; i=0
while [ $i -lt 9 ]; do
  dd if=/dev/mtdblock8 of=/tmp/c.bin bs=$CH skip=$((i*2)) count=2 2>/dev/null
  [ -s /tmp/c.bin ] || { echo "chunk $i 空，停"; break; }
  tftp -p -l /tmp/c.bin -r part$i.bin 192.168.1.191 && rm /tmp/c.bin
  i=$((i+1))
done
```

实验机侧按序拼回来，**首 2 字节应当是 `85 19`**（JFFS2 magic），不等就说明拼接或偏移错了：

```sh
cat part0.bin part1.bin part2.bin part3.bin part4.bin \
    part5.bin part6.bin part7.bin part8.bin > rootfs.jffs2
hexdump -C rootfs.jffs2 | head -2
```

**完全不落盘的做法**（空间与内存都不占，**这台机器首选**——`cat` 已经够用，`curl` 又不需要
`dd`）：`/kmodule/bin` 里有 `curl`，`-T -` 能直接从 stdin 读：

```sh
cat /dev/mtdblock8 | /kmodule/bin/curl -T - http://192.168.1.191:8000/rootfs.jffs2
```

前提是对端有一个接受 `PUT` 的服务（`python3 -m http.server` **不接受** PUT）。仓库里带了一个：

```sh
# 实验机上
python3 tools/lab/httpput.py --dir /srv/tftp --port 8000
# 设备上（没有 dd，用 cat；curl 在 /kmodule/bin 里，可能不在 PATH）
cat /dev/mtdblock8 | /kmodule/bin/curl -T - http://192.168.1.191:8000/rootfs.jffs2
# 校验：curl -s http://192.168.1.191:8000/rootfs.jffs2 | sha256sum（与设备侧对比）
```

`httpput.py` 支持 `Content-Length` 与 `chunked`（`curl -T -` 走的是后者）、直接落盘不占内存、
写临时名完成后才 `rename`，所以传一半中断不会留下一个看起来正常的小文件。已用 3 000 001 字节
随机数据对两种传输各测一遍，逐字节一致。

`tftp -p`（put）是 busybox 自带，这条通道在实验机上已经验过（见 [LAB](../LAB.md)）。
离线侧用 `jefferson`（pip）或 `mtd-utils` 解 JFFS2；`unsquashfs` 用不上，它不是 squashfs。

#### 4.2.4 如果非要那把密钥：只有 RAM 一条路，期望值不高

先说为什么"拿真机当解密机"没用：把 dump 的密文写回同地址 NAND 再读出来，确实得到明文，
但那是**解密服务**不是密钥；AES 下已知明文-密文对推不出 key。而写 NAND 是这台机器上
唯一可能变砖的操作。**不做。**

剩下的只有"密钥是否在 CPU 可见的 RAM 里出现过"。两种可能：

1. **控制器从 OTP/eFuse 直接取 key**（最可能）：CPU 一次都看不到，搜 RAM 必然空手；
2. **驱动把 key 写进控制器寄存器**：它至少在内核栈/堆里存在过一瞬，那一刻的内存还在就有机会。

判据很好构造，因为 E2633 的 dump 里有一个**已知明文的密文块**：JFFS2 未用区是 `0xFF` 填充，
ECB 下 `AES_ECB(0xFF×16)` 恒定，实测重复 5906 次、值
`9b58f878bbb341366b2b543daffb0185`（[E2633_FIRMWARE](E2633_FIRMWARE.md) §5.1）。于是任意候选
key 只需一次块加密就能判：

```
AES_encrypt(key, FF×16) == 9b58f878 bbb34136 6b2b543d affb0185
```

范围也明确：`/proc/iomem` 里 `System RAM` 的 256 MiB，重点在 lowmem 映射的低 64–128 MiB。

```sh
cat /proc/iomem                            # 看 System RAM 与 carveout（secure/tee/reserved）
/kmodule/bin/devmem2 0x80000000            # 一次读，判断 /dev/mem 是否被 STRICT_DEVMEM 挡住
# 能读就分段 dump（每段 16 MiB，传完即删）——这行需要 dd
dd if=/dev/mem of=/var/m0.bin bs=1048576 count=16 skip=2048
```

**这段在 E2631 上跑不了**：既没有 `dd`、也没有 `head`，而 `devmem2` 一次只读一个字，
256 MiB 走不通。真要查这条路，得先用 VM 上的 `arm-linux-gnueabi-gcc` 编一个静态小工具
（`open("/dev/mem")` + `read()` 16 MiB 到 stdout，管道接 `curl -T -`），
这也顺带解决了"落盘空间"问题。工具没写，写之前先确认 `/dev/mem` 能不能读、值不值。

离线扫描的 C 骨架（**未实测**，跑通再考虑收进 `tools/`）：对 4 字节对齐的每个偏移取 32 字节
当 AES-256 key（也试 16 字节当 AES-128），做一次 `AES_encrypt(key, FF×16)` 比对。256 MiB ÷ 4 × 2
≈ 1.3 亿次单块加密，C 实现分钟级。

两种结果都有信息量，别只看"命中"那一种：

| 结果 | 含义 | 下一步 |
|---|---|---|
| 命中 | key 在 CPU 侧出现过，**且跨机型共用**（我们是用 E2633 的密文块验的） | 直接解 `e1630.raw` 的 rootfs，回头补 [E2633_FIRMWARE](E2633_FIRMWARE.md) §5 |
| 全空 | key 不经 CPU（OTP 直连控制器），或**每台设备唯一** | 编程器 dump 这条线对 rootfs 永久关闭；只剩串口进 BootROM，收益不抵成本 |

> 一个容易混的点：用 E2633 的密文块去验 E2631 的 RAM，前提是**同一密钥组**。这个前提有旁证
> （config 的密钥组号 `0238002E` 被 E2631/E2633/E1630/E2637/E2638 五个机型共用），但没有实证。
> 所以"全空"**不能**单独证明 OTP——它同样兼容"每台设备一把 key"。要分开这两者，得在**同一台
> 设备**上同时拿到 RAM 和它的编程器 dump。

#### 4.2.5 真正值钱的不是密钥，是脚本

对"复现 mesh"这个目标，rootfs 的边际价值不在二进制（`multiapd`/`libcmapi` 已有，见
[MULTIAPD](MULTIAPD.md)），在**胶水**：

| 路径 | 为什么值钱 | 对应待办 |
|---|---|---|
| `/etc/init.d/*` | `cspd`/`multiapd` 的启动顺序与命令行参数（决定谁是 master） | D7 前置 |
| `/etc/scripts/*`、`/usr/sbin/*.sh` | 区域/模式判断、`/defcfg/current_region` 的读写者 | [E2633_TELNET_BOOT](E2633_TELNET_BOOT.md) §11.1 换区 |
| Web 的 CGI（`/www`、`/usr/www`） | **写 DB 的字段名**——VLAN/ACL/漫游过滤/BTM 的入口命名 | D3–D6 |
| 谁 `dlopen` 了 `libzte_agent_mm.so` | agent 侧落配置的真实调用点 | D6 |

这四类全在明文里，`cat` 就能拿——不需要密钥，一次 telnet 就够。

#### 4.2.6 风险

- `cat /dev/mtdblock8` 或 `hexdump -n 64` **只读**，不写 flash、不磨损、不改配置库，安全；
- `/tmp` 是 tmpfs（吃 RAM），17 MiB 镜像没问题，但别同时开大 tar，先看 `free`；
- **不要** `sendcmd 1 DB set/save`、**不要** `rreset`（按区域重建整库 = 恢复出厂）；
- 不要往任何 mtd 写字节。本节命令没有一条是写操作。

## 5. 内核里的 1905：识别命中、丢转发、送本机，但不做协议

> 标题原为"只识别、只丢弃"。2026-11-01 用第三方的全符号表补出 `br_ieee1905_local_in_handle`
> 后修正：**丢的是转发，不是本地投递**。详见 §5.5，那一节也交代了这处修正的依据与边界。

这一节回答"原厂内核为 mesh 做了什么"。素材是 E2631（2024-09 版）的 `vmlinux.bin`，方法见
§5.4，可复现。结论先说：**内核里没有 Multi-AP/1905 协议逻辑**，只有三件事——识别帧、把 1905
组播帧从桥接路径里摘掉、暴露一个"当前是不是 multi-ap 模式"的位。协议栈全在用户态 `cspd`。

### 5.1 全部相关字符串（原始命中 76 条，去噪后逐条列出）

按性质分四组，地址为文件内虚拟地址（基址 `0xC0008000`，与文件偏移差 `0x8000`）。

**(a) 导出符号 / 名（11 条）**

| 地址 | 名字 | 性质 |
|---|---|---|
| `0xC02AE2D4` | `wlan_get_sta_bandcap` | 函数，"这个 STA 支持哪些频段" |
| `0xC02AE460` | `wlan_get_wlan_multiap_mode` | 跳转桩，实体在 `0xC02AF210` |
| `0xC02AE45C` | `wlan_get_limit_num` | 跳转桩，实体在 `0xC02AF170` |
| `0xC02ADD08` | `get_wlan_cspkernel_param` | cspd 取内核侧参数的入口 |
| `0xC02AFB3C` | `br_ieee1905_check_is1905_msg` | **判定 1905 帧**（§5.2） |
| `0xC05B0A23` | `wlan_cspkernel_ops` | 数据对象（`ops` 表），不是函数 |
| `0xC04097D0` | `CSPKernel_skb_SelectQueue` | QoS 选队列 |
| — | `CSPKernel_QC_AppSet` / `CSPKernel_SKBQC_AppSet` / `CSPKernel_InMcast_SKBQC_AppSet` / `CSPKernel_QoS_QueueRun` | QoS/队列，与 mesh 无直接关系，列此备查 |

**(b) cspd 的模块名，13 条**（写进内核日志的 view 名，说明 cspd 的日志通道直连内核）

```
cspd.dbd_task.DB                cspd.cspd.dev_mgr             cspd.wlan_manage.wlan_mgr
cspd.cspd.led_ctrl              cspd.cspd.led_priority_mgr    cspd.adev_task.adev_mgr
cspd.cspd.fw_core_mgr           cspd.cspd.simulation_mgr      cspd.cspd.addr6_mgr
cspd.cspd.slaac_mgr             cspd.cspd.ppp_mgr             cspd.cspd.trafficsts_by_dev
cspd.cspconfig.wlan_config
```

外加一条打印格式 `[wlan_cspkernel] [%s(%d)] : %s(%s)!` @ `0xC0572198`——`cspd` 与内核共用一个
带模块名/行号的日志门面，这解释了为什么 `/kmodule` 里的用户态进程能出现在内核日志里。

**(c) ZTE 私有内核补丁的源文件路径，22 条**（全部在 `/zte/csp/opensource/cspkernel4.1/` 下）

```
common/common_ioctl.c                 HAL/ver_info_nand_v2.c        HAL/ledkey_mod_v2.c
protocol/netif_flag.c                 protocol/bridge/br_component.c
protocol/wan_bridge.c                 protocol/wan_bridge_if.c      protocol/nd_agent.c
protocol/dev_mirror.c                 protocol/netfilter/{xt_limitperip,nf_conntrack_pt,
                                      nf_conntrack_rtsp,nf_nat_rtsp,xt_layer7}.c
protocol/ffe/{ffe_main,ffe_pkt_misc,ffe_sharedata,ffe_sk_buff,ffe_fk_buff,ffe_proc,
              ffe_flush,npu_main}.c   protocol/include/ffe/ffe_main.h
```

`protocol/bridge/br_component.c` 与 `protocol/netif_flag.c` 是网桥改造的落点，
`protocol/ffe/*`（Fast Forwarding Engine，NPU 加速）是转发面——**没有一个是 mesh/1905 的**。

**(d) 其余相关串**

| 地址 | 字符串 |
|---|---|
| `0xC0572614` | `br_ieee1905_filter_init register nf hooks failed` |
| `0xC05B0974` / `0xC05B0A0F` / `0xC05B0C26` | `wlan_get_wlan_multiap_mode` / `get_wlan_cspkernel_param` / `br_ieee1905_check_is1905_msg`（`__ksymtab_strings`） |
| `0xC064EC00` | `wlan_multiap_mode`（变量名/调试名） |
| 散落在 `0xC04E1A8E`–`0xC0530570` | 另有 8 处 `ieee1905_msg` 碎片（`0xC04E1A8E`、`0xC04FDCB9`、`0xC05194B6`、`0xC051FFAC`、`0xC0521359`、`0xC0523C48`、`0xC05256CD`、`0xC0530570`），**查不到任何引用**，推测是 trace/调试字符串池的残留，未能确证，不据此下结论 |

### 5.2 三个函数的行为（反汇编实证）

**`br_ieee1905_check_is1905_msg(skb)` @ `0xC02AFB3C`**

```asm
ldrh r3, [r0, #168]      ; skb->protocol
movw r2, #0x3a89         ; htons(0x893a) —— 注意是按网络序存的值
cmp  r3, r2   ; beq -> return 1
cmp  r3, #0x81           ; ETH_P_8021Q=0x8100，按网络序存成 0x0081
              ; beq -> 到 VLAN 分支
                                  ; VLAN 分支：
ldrh r1, [r0, #174]      ; 以太帧头在 skb->head 里的偏移
ldr  r3, [r0, #212]      ; skb->head
add  r3, r3, r1
ldrh r3, [r3, #16]       ; 跳过 14 B 以太头 + 2 B TPID + 2 B TCI
rsb  r0, r2, r3 ; clz ; lsr #5    ; return (内层 ethertype == 0x893a)
```

**它认 802.1Q 封装的 1905 帧**——这是我们此前没记录的一条：ZTE 允许 1905 走 VLAN tag，
判定时剥掉一层 VLAN 再比 ethertype。对我们意味着"带 VLAN 的回程链路上，1905 仍然要被认出来"。

**桥接钩子 @ `0xC02AFB84`**（`0xC0617954` 处的 ops 结构第 3 个字段指向它）

```asm
if (*(u32*)0xC064EED8 == 0) return NF_ACCEPT   ; 全局开关，关掉就不过滤
p = skb->head + *(u16*)(skb + 174)
if ((*p & 1) == 0) return NF_ACCEPT            ; 目的 MAC 首字节的组播位为 0 -> 放行
return !check_is1905_msg(skb)                  ; 是 1905 -> 返回 0 = NF_DROP，否则放行
```

也就是：**目的 MAC 是组播、且是 1905 帧 → 丢弃**。这是一条"把 1905 从 Linux 桥接转发路径里摘出去"
的规则，和 `EXTENDER_LINK_BY_DRIVER=y` 是同一套设计：1905 帧由驱动直送 cspd，不该被网桥泛洪、
不该被送到协议栈。`br_ieee1905_filter_init()` @ `0xC05F723C` 负责注册它，失败就打印 §5.1(d) 那条串。

**`wlan_get_wlan_multiap_mode()` @ `0xC02AF210`——全函数三条指令**

```asm
movw r3, #0xBFA8 ; movt r3, #0xC066
ldr  r0, [r3, #0x94C]      ; return *(u32 *)(0xC066C8F4)
bx   lr
```

**内核眼里的 Multi-AP 就是一个 u32 位。** 没有任何 TLV 解析、没有 CMDU、没有拓扑。

### 5.3 这些证据改变了什么

1. **§6 的第 2 条硬约束（不新增控制通道）从"推论"变成"可复现的事实"**：内核里连解析器都没有，
   任何 mesh 语义只可能存在于 1905 帧里。想做新功能又没有对应 CMDU/TLV，就必然无处落地。
2. **解释了 lab 与真机的行为差**：沙箱里没有这对钩子（丢转发 + 送本机，见 §5.5），`0x893a` 帧会在 `br-lan` 上被泛洪回
   `zmesh0`——上一轮自检功能的假阳性正是这个原因（[WIRESHARK_1905](WIRESHARK_1905.md) 记的
   `/run/zte-mesh/search-sent` 绕法，本质是在用户态模拟这个钩子该做的事）。
3. **对我们自己的实现没有要求变更**：我们发的帧本来就是单播/组播的裸 `0x893a`，不带 VLAN。
   要留意的只有一种情况——将来若在回程上启用 VLAN（D5），1905 必须被对端认出来，而 ZTE 的
   判定支持"VLAN 内层 1905"，说明这条路上它是有准备的。

### 5.4 方法（可复现）与局限

镜像没有 ELF 头，也没有可用的 kallsyms token 表，但有两张可解析的表：

- **`__bug_table`**：ARM 4.1 是 `{u32 bug_addr; u32 file; u16 line; u16 flags}` 绝对指针。
  用"指针必须落在镜像内且指向 `.c/.h/.S` 路径"筛出 **1908 条** `(地址, 文件, 行号)`，
  覆盖 **375 个源文件**——等价于一张函数级地图，`zx_denali_nand.c` 的 3 个 BUG 点就是这么定位的。
- **`__ksymtab`**：`{u32 value; u32 name}`，要求 `value` 处的 4 字节像 ARM 指令（排除把 name
  指针误当 value），恢复 **6393 个导出符号**。
- 基址 `0xC0008000` 由 `w - 文件偏移` 投票得到，脚本 `_analysis/kmap2.py` + `_analysis/xref.py`
  （后者实现 `movw/movt` 配对与 `ldr pc` 字面量两种绝对地址加载方式，用来从字符串反查引用代码）。

局限：非导出符号拿不到名字（`br_ieee1905_check_is1905_msg` 的调用者就是这样一段匿名代码）；
`0xC0617954` 那个 ops 结构的 `pf`/`hooknum` 字段布局未能确证，所以本节只按"钩子 + 丢弃 1905 组播"
描述行为，不引用具体 hook 点编号。

### 5.5 用第三方全符号表交叉验证，并修正本节两处（2026-11-01）

上一节最后那条局限——「非导出符号拿不到名字」——可以直接消掉：第三方 E2631 移植仓库
[Zlion-Y/zte-e2631-openwrt](https://github.com/Zlion-Y/zte-e2631-openwrt) 里带了一份
`re/ffe/ksym-all.txt`，**21 946 行全符号表**（含 static，不是只有 `__ksymtab` 那 6 393 个）。
拿它对照，先验证、再补漏：

**验证**：三个同名符号全部对上，地址因固件版本不同而平移，**且平移量互不相同**
（`+0x11b0` / `+0x28b8` / `+0x412c`），符合代码增删的正常形态，不是系统性偏移：

| 符号 | 我们（e1630 2024-09） | 第三方（E2631 较新版） |
|---|---|---|
| `wlan_get_wlan_multiap_mode` | `0xC02AF210` | `c02b03c0` |
| `br_ieee1905_check_is1905_msg` | `0xC02AFB3C` | `c02b23f4` |
| `br_ieee1905_filter_init` | `0xC05F723C` | `c05fb368` |

这条交叉验证的意义不小：**两张表由两条互不相干的路径产生**（我们靠 `__bug_table`+`__ksymtab`
从无 ELF 头的裸镜像里恢复，他们那份是现成的），结果逐名对上——`__ksymtab` 那套恢复方法是可信的。

**补漏一：`br_ieee1905_*` 是 6 个函数，不是 3 个**。多出来的三个：

```
c02b243c  br_ieee1905_filter_handle
c02b2484  br_ieee1905_local_in_handle     ← 关键
c06087f0  br_ieee1905_filter_exit
c0611f20  __initcall_br_ieee1905_filter_init6
```

**补漏二：§5 的"只识别、只丢弃"要改。** `br_ieee1905_local_in_handle` 这个名字指向
**local delivery**——桥的 LOCAL_IN 钩子，即把命中 1905 的帧**送一份给本机协议栈**，而不是
只有 `NF_DROP`。这和 [MULTIAPD](MULTIAPD.md) §3 发现的「`multiapd` 在 `br0` 上用用户态
raw socket 收 1905」互为对端：**丢弃的是转发，不是本地投递**。

正确的模型是三分支：

```
1905 帧进桥
  ├─ br_ieee1905_check_is1905_msg   命中（含 802.1Q 内层）
  ├─ br_ieee1905_filter_handle      FORWARD 钩子：不放行 → NF_DROP（不再跨口泛洪）
  └─ br_ieee1905_local_in_handle    LOCAL_IN 钩子：送本机 → multiapd 的 raw socket 收到
```

§5.3 第 1 条的结论**不受影响**（内核仍无解析器，mesh 语义只在 CMDU/TLV 里）；但第 2 条
「`0x893a` 被泛洪回 `zmesh0`」的解释要更精确：我们 lab 里缺的不是"丢弃"，而是**这对
"丢转发 + 送本机"的配套**——沙箱里两者都没有，帧才回到了自己身上。

**补漏三：内核里的 multiap 侧符号是 22 个，不是 3 个**，而且是一整套 **procfs 读/写配对**：

```
wlan_get_wlan_multiap_mode / _wlan_get_wlan_multiap_mode
wlan_multiap_mode_read / wlan_multiap_mode_write / _wlan_multiap_mode_read
wlan_multiap_btm_kick_read / _write
wlan_multiap_btm_cache_en_read / _write
wlan_multiap_TxDataAfterBaEn_read / _write
wlan_multiap_trans_mac_read / trans_mac_last_read / is_wlan_multiap_trans_mac
```

`read`/`write` 成对 + `_read` 下划线变体，是 ZTE 那套 procfs 属性的典型形态。**这解释了一件事**：
`wlan_get_wlan_multiap_mode` 只读一个 u32 位（§5.2 反汇编结论），而这个位**有对应的写入口**——
也就是说 mesh 模式在驱动侧是可读可写的，`/proc/wlan_cfg/map/btm_kick` 那条待验证项
（[VENDOR_MESH_STACK](VENDOR_MESH_STACK.md) §8）由此得到符号级支持：
`wlan_multiap_btm_kick_write` 确实存在。节点名与属性名仍需在设备上 `ls` 确认。

**局限的更新**：上述三个修正都建立在「两份符号表指的是同一份源码的不同版本」这个前提上。
固件差一年多（我们 2024-09、他们较新），`local_in_handle` 在我们那版是否**同址同存**未验证——
它在我们恢复的导出表里不存在，因为它是 static（`__ksymtab` 只收导出符号）。
要落实，只需在 e1630 镜像里按 `br_ieee1905_filter_init` 附近的函数边界找相邻未命名块。



## 6. 兼容性原则（做功能时的硬约束）

1. **必须落到线上**：新功能要能指出它用哪个 CMDU/TLV；指不出来的，要么做成本机 UCI 行为，要么不做。
2. **不在自己节点之间新增控制通道**：角色协商、状态同步走 1905 或本机 UCI，不发明"插件内部协议"。
   一旦发明，装了这个插件的两台机器能组网、装一台就不行 —— 那就是独立 mesh 插件。
3. **私有扩展必须可选、可降级**：`mesh_v2=0` 时只发标准 TLV，中兴设备照样能入网（现在就是这个行为）。
4. **验收门槛是真中兴子路由**：只有 Z506 上实测生效的功能才写"支持"；只在自己沙箱里成立的，写"沙箱"。
5. **不碰对端身份**：发给子路由的身份（DevInfo 的 ZTE / `ZXSLC SR1010`）是为了过准入检查，
   **不要**换成宿主的真实型号 —— 换掉的表现是子路由直接不收（REAL_CHILD 第 2 节）。
   宿主型号只用于页面显示（`state.json` 的 `self`）。

## 7. 下一步（按性价比排序）

| 事项 | 为什么值得做 | 需要什么 |
|---|---|---|
| 跑 §4.2.2 那条 `hexdump -C -n 64 /dev/mtdblock8`（一秒，只读） | 把"rootfs 明文"从**推断**变成**实证**，顺手把 §4.1 的判据关掉；同时决定要不要走 §4.2.4 | 一次 telnet，无需停机 |
| `tar` 一份 `/etc` + `/etc/init.d` + Web CGI | D3–D6 的入口命名全在这几处明文里（§4.2.5），比继续逆向二进制便宜 | 一次 telnet + tftp |
| ~~用 Wireshark `ieee1905` 对照我们发出的帧~~ **已做**（2026-11-01） | 不用真机就能查编码错误，比"装上去看现象"快 | 结果与改法见 [WIRESHARK_1905](WIRESHARK_1905.md)：`0x0007` 缺 EOM 已修，`0x0008` 的 EOM/`0xb3` 留待真机复验 |
| 拿 **AX3000（E2631）** 当子路由，验 Steering / BTM | D7 的接收路径（`handle_wlan_multiap_steering_req`、`wnm_send_btm_req_raw`）在原厂 `cspd` 里是齐的，但线上没有证据 | 停机窗口 + 限时抓包，步骤见 [VENDOR_MESH_STACK](VENDOR_MESH_STACK.md) 第 8 节 |
| 找一台**非中兴**的标准 agent 做对照 | 证明我们不是"只会跟 ZTE 说话"；这是"兼容任意 OpenWrt"这句话的唯一证据 | 一台支持 Multi-AP 的 OpenWrt 设备（hostapd 带 multi_ap） |
| `slave` 角色补 Topology Response 的 BSS/终端 TLV | 现在中兴主控看不到我们下面挂了哪些终端 | Z506 或任意主控做对照 |
| 纯 OpenWrt 组网（无中兴设备）另开一档 | 想做就复用 prplMesh 当 agent，而不是自己再写一套控制面 | 明确的范围与独立验收 |

## 8. 参考

- IEEE 1905.1 技术报告（CMDU 帧格式、`0x893a`、组地址 `01:80:C2:00:00:13`）：
  <https://www.ieee802.org/1/files/public/docs2012/802-1-phkl-P1095-Tech-Presentation-1207-v01.pdf>
- Wi-Fi Alliance Multi-AP 规范（R1/R2，EasyMesh 的底座）；prplMesh 的架构讲稿把 1905 与 Multi-AP
  的关系讲得最清楚：<https://events19.linuxfoundation.org/wp-content/uploads/2017/12/prplMesh-An-Open-source-Implementation-of-the-Wi-Fi-Alliance®-Multi-AP-Specification-Arnout-Vandecappelle-EssensiumMind.pdf>
- 中兴 EasyMesh 认证公告：[F680（2019，R1）](https://zte.com.cn/global/about/news/20190620e1.html)、
  [F689（2020，R2）](https://www.zte.com.cn/global/about/news/20201229e1)
- prplMesh：<https://github.com/prplfoundation/prplmesh>；BBF 1905.1a：<https://github.com/BroadbandForum/1905.1a>
- E2631 移植记录（固件槽位表、rootfs 加密算法）：<https://blog.zlion.top/posts/zte-e2631-mainline-linux-port/>
