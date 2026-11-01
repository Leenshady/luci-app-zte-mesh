# ZXHN E2633「转正」工具链分析：开 telnet、改型号、刷 boot

对象：`F:/中兴E2633转正/Telnet/`（7 个文件，91 MB，其中 91 MB 是 Node 运行时）。
同目录上一级还有 `6B5改_109适配/`（26.8 MB 官方固件改包，已单独记录在
[E2633_FIRMWARE](E2633_FIRMWARE.md)）和 `7B5改.zip`、`8B8改.zip`。

本文只做**静态**分析（读文件 + 逐字节校验），没有设备参与，没有执行任何烧写。

## 0. 清单与哈希

| 文件 | 大小 | MD5 | 性质 |
|---|---|---|---|
| `README.txt` | 2 856 | — | 原作者 zlion（zlion.top）的说明，**与实际文件已不同步**（见 §7.1） |
| `点击开启telnet.bat` | 1 255 | `8382dae049e8d7d20004066d13962e89` | 入口 1：`config.bin` → `config-telnet-enable.bin` |
| `telnet_enable.js` | 11 092 | `960875b033fd3e9a18badb480a39d079` | 实际干活的解密/改 XML/加密；**已被改过**（相对原版加 E2637、修 defval bug） |
| `zte_config_telnet_enable.js` | 9 530 | `45d3f8b4d68b0d9efabbc0423f9bc1ac` | 原版（2026-08-06），保留未删 |
| `一键换型号并刷boot.bat` | 3 923 | `e52cd02a3b569a0256f303233a1435f8` | 入口 2：提权加防火墙规则 → 调 Node → 删规则 |
| `change-model-boot.js` | 26 414 | `0e6ca905f2ffd97fcd13d5c5f95b08ea` | telnet 客户端 + 改型号 + 传文件 + 烧 mtd1 |
| `boot.bin` | 559 340 | `22acbfdf2e93e5f823483bd1ee6829cb` | **mtd1 bootloader**（带 0xAC 容器头），脚本里硬编码了这个 md5 |
| `node.exe` | 91 380 224 | `350e0cd211bd0266e361a4a18cb1ff05` | Node v24.14.0 便携版。**黑盒二进制**，无签名可查，别从别处换 |

README 说"推荐用工具包自带的 `node.exe`"——这条是对的：整套东西的信任边界就在这两个二进制
（`node.exe` 和 `boot.bin`）上，其余全是可读 JS。

## 1. 一句话结论

**这是一条"官方固件 → 改配置开 telnet → telnet 里改型号 + 刷 bootloader"的清洗链，
唯一的不可逆动作是把 `boot.bin` 写进 mtd1。** 前面两步都只是改配置，能重来；
第三步一旦断电或写错，设备只能靠编程器救（同目录的 `F35SQA001G(ECC_EN)@WSON8_*.BIN` 就是这么来的）。

链条设计得比一般"一键脚本"谨慎：`boot.bin` 大小和 md5 双闸门、设备侧 md5 双闸门、
`curUpgradeKey1` 闸门、烧写中断后的 5 次重连状态机、`setmac` 前后分区 md5 取样、
跑完把备份写到 `tag_<时间戳>.txt`。**这些闸门的存在本身说明作者被砖过。**

## 2. 四步流程（`说明和教程.txt`）与每步的真实门槛

| 步 | 动作 | 通道 | 真正的门槛 |
|---|---|---|---|
| 1 | 刷 `6B5改_109适配.bin`（26.8 MB 官方改包） | Web 版本升级 或 `http://192.168.5.1/supgrade.html` | 只改机型/区域标识，内核 rootfs 仍是官方加密件 |
| 2 | 备份配置 → `config.bin` 丢进本目录 → 双击 `点击开启telnet.bat` → 恢复该配置 | Web 备份/恢复 | 需要先知道该机型的配置密钥种子（见 §3） |
| 3 | 双击 `一键换型号并刷boot.bat` | telnet 23（admin/admin） | `curUpgradeKey1` 必须 = 0，否则脚本自己拒绝 |
| 4 | 重启 | — | 型号变 `ZXHN E2631`，bootloader 变新门 (1351,5211) |

第 1 步和其余步骤是**耦合**的：`boot.bin` 里的引导会校验升级密钥对，在役固件若不是
"key0 包"，刷完这个 boot 就再也起不来。脚本把这条写成硬拒绝：

```
在役包 key1 ≠ 0: 烧入新门 (1351,5211) 后会拒收在役系统, 重启即无法开机。
    先把在役系统换成 key0 包后再来烧。现已安全中止, 设备零改动。
```

所以"先刷 6B5 改包再刷 boot"不是顺序习惯，是前置条件。

## 3. `config.bin` 容器格式（这套工具唯一真正"技术"的部分）

结构已完整解出，可以脱离这两个脚本用任何语言实现。全部大端。

```
外层（文件本身）
0x00  u32 magic = 0x01020304
0x04  u32 type  = 4
0x08  ⋯0x3B   全 0（原厂可能写过东西，重打包时被清零，见 §7.4）
0x3C  chunk[0] { u32 inner_len; u32 enc_len; u32 next=0; AES-256-CBC(inner) }

内层（AES 解密后的 0x3C 头 + N 个 deflate 块）
0x00  u32 magic = 0x01020304
0x04  u32 type  = 0
0x08  u32 xml_size            明文 XML 总长
0x0C  u32 first_chunk_off = 0x3C
0x10  u32 chunk_unpacked = 0x10000（64 KiB 一片）
0x14  u32 crc32(所有块的压缩后 payload 顺序拼接)
0x18  u32 crc32(header[0x00..0x18))
0x1C  ⋯0x3B   全 0
0x3C  chunk[i] { u32 解压后长度; u32 压缩后长度; u32 下一块偏移(最后一块=0); deflate 数据 }
```

密钥派生（**没有 KDF，就是拿 SHA-256 当哈希**）：

| 项 | 取值 |
|---|---|
| `keySeed` | `"<机型>Key<8 位十六进制>"`，恰好 **16 字符** → `SHA256(ascii)` 32 字节 → AES-**256** |
| `ivSeed` | `"<机型>Iv<8 位十六进制>"`，恰好 **15 字符** → `SHA256(ascii)[0:16]` → IV |

已确认的种子（`telnet_enable.js` 的 `PROFILES`，按顺序试，靠 CRC 判对错）：

| 机型 | 8 位后缀 |
|---|---|
| E2637 / E2633 / E2638 / E2631 / E1630 | `0238002E` |
| E2631、E1630（旧） | `13515211` |

后缀可以互相印证：脚本报错里写的是"新门 **(1351,5211)**"，而旧参数族后缀正是 `13515211`
——**这个 8 位十六进制是"两个 16 位密钥号"拼起来的**，`0238002E` 就是 `(0x0238, 0x002E)`。
同一套编号在 bootloader 里也有痕迹（§5.4）。

判对错靠**五重校验**，不靠文件名或机型标签：外层长度 → 内层头 → 内层头 CRC32 →
数据 CRC32 → zlib 解压长度 → XML 形状 → 加密后再解密的往返比对。全过才算命中，
所以拿错种子必然失败而不是"解出垃圾"。

上面这张表已被一个独立的英文实现 `zcu` 逐字段确认过（含两个 CRC32 的覆盖范围，见 §10.2）。

改的是什么：`TelnetCfg` 表里 `TS_Enable=1 / Wan_Enable=0 / Lan_Enable=1 / TS_Port=23 /
TS_UName=admin / TS_UPwd=admin`，外加把 `ServName="TELNET"` 那条端口规则 `PortEnable=1`。
**没有 WAN 侧暴露**，只在内网可用——这点是克制的。

## 4. 两个 telnet 脚本的差异（4 处，其中 1 处是真 bug 修复）

| # | 改动 | 意义 |
|---|---|---|
| 1 | 新增 `E2637-0238002E` 参数族（且排在最前） | README 没提 E2637，实际支持 |
| 2 | `setDm()` 正则容忍 `defval="…"` 属性 | **真 bug**：全量备份导出的 `<DM>` 带 `defval`，原版正则失配会走 fallback **插入一条同名 DM**，配置里出现重复键 |
| 3 | `ServName="TELNET"` 判定从 `includes('…/>')` 改为正则 | 同源问题：带 `defval` 的行整条漏匹配，端口规则不会被打开，telnet 白开 |
| 4 | 新增 `--keep-creds` 和 `TELNET_USER`/`TELNET_PASS` 环境变量 | 默认仍是 admin/admin |

第 2、3 条是同一类错误的两个面：**XML 不是按固定形状序列化的**，凡是用"精确字符串相等"
判断属性存在，遇到多一个 `defval` 就静默失效。改完的版本对两种形状都成立。

## 5. `boot.bin` 拆解

### 5.1 外层：ZXIC 烧写容器（0xAC 字节）

| 偏移 | 值 | 含义 |
|---|---|---|
| 0x00 | `99999999 44444444 55555555 AAAAAAAA` | 容器魔数（四段各 4 字节） |
| 0x18 | `1` | 类型/标志 |
| 0x20 | `0x00005211` | **未解**（见 5.4） |
| 0x24 | `"V1.0.0"` | 容器格式版本 |
| 0x44 | `7B 01` + `"Me20230828191152"` | 打包时间戳，2023-08-28 19:11:52 |
| 0x60 | `0x00100000` | 未解（对齐/载入地址？） |
| 0x64 | `0x000000AC` | 头长 = 172 |
| 0x68 | `0x00088840` | 镜像长 = 558 656 |
| 0x6C | `0x97E1DA67` | **CRC32(镜像)** —— 实测一致 |

`0xAC + 0x88840 = 0x888EC` = 文件长度，一个字节不多。**只有 CRC32，没有签名**：
末尾没有 RSA-2048 签名块，也没有证书。所以"能不能烧"的唯一门槛在设备侧，
不在这份文件里（这也是 `boot_flashing` 敢接收任意同结构文件的原因）。

### 5.2 镜像本体 = mtd1 原样内容（两段式 + 两个包尾）

外层头之后的 558 656 字节，就是 mtd1 从 0 开始的原始内容。**用同目录的 E1630/6B5 整机 dump
逐偏移比对过，布局完全同构**（`e1630.raw` 偏移 0 起就是同一形状）：

| 镜像内偏移 | 内容 |
|---|---|
| `0x00000` | 槽 0：一级引导（CRPM / DDR / 串口 / NAND 初始化 + 一个 DDR、NAND 调试命令行） |
| `0x00080` | 描述符 1：`0001ZXIC` / `BOOTROM` / `RSA2048`，长度字段 `0x6100` |
| `0x20000` | 槽 1：U-Boot，前面同样有 `0x80` 处描述符 2（长度 `0x66080`） |
| `0x20800` | ARM 入口：`14 00 00 EA`（`b +0x14`）+ 异常向量表，接着 `MCR p15` 设 VBAR |
| `0x86804` | 包尾 1（0x3C）：`55555555 44444444 99999999` + `V1.0.0` + `19:11:33` + **长度 `0x66040`** |
| `0x88804` | 包尾 2（0x3C）：同上，`19:11:52` + **长度 `0x88840`** |

两个包尾是整包和各段的长度+编译时间自证，魔数相对文件头是**倒序**的
（头 `9999 4444 5555 AAAA`，尾 `5555 4444 9999`），跟 U-Boot 的尾部标记习惯一致。

一级引导的字符串把自己说得很清楚：`crpm init` / `ddr init enter, rate is … 1066/1333/1600/2133 Mbps` /
`serial init` / `SPI NOR` / `SPI NAND` / `no nand !` / `skip bad block !` / `Debug mode!` / `Trigger debug mode !` /
`bootloader err!!!` / `----test ddr rw` / `x [addr]`、`r addr [size]`、`w addr data`（调试命令行）。
**串口在的话，一级引导是能停下来敲命令的**——这是砖机救援的最后一道软件后路。

### 5.3 U-Boot 身份

```
U-Boot 2013.04 (Aug 28 2023 - 19:11:33)
CPU  : ZX279128S@A9,
Board: ZTE zx279128sevb
```

命令集是完整 ZTE 改版：`nand read/write/erase/scrub/markbad/biterr`、
`nand write.uboot …(4-bit-ecc, compatible with bootrom)`、`nand read.raw`（绕 ECC 裸读）、
`i2c probe/read/write`、`mm/mw/nm`、`crc32`，还有 `setenv bootcmd` 里的
`fullfile=upgrade.bin`、`vmlinux.bin` 之类默认环境，以及 `Hit 1 to upgrade software version`。

升级/校验链路的关键字符串：

```
zteboot_upgrade / zteboot_verify_signature / zteboot_verify_firmware
zteboot_flash_boot / zteboot_flash_burn / zteboot_upgrade_burn
public_key inited but CSPBOOT_VERIFY_SIGN_XXX not enable
verify boot[%d] crc need %x but (%x)
No need to check upgrade key!!
Upgrade key ERROR!need(%x,%x) but (%x,%x)
```

**"签名能力在、但没启用"**——`public_key inited but …NOT enable` 和一级引导里那句
`non secure uboot` 是同一个结论。所以本机烧写走的是 **CRC + 升级密钥对**这条路，
不是 RSA 验签。这解释了：为什么一个 559 KB 的第三方 `boot.bin` 能烧进去，
也解释了为什么作者必须盯着 `curUpgradeKey1`——**唯一的门是密钥对，不是签名。**

一级引导里还有 `CBC decrypt OK!` / `HASH timeout` / `RSA timeout` / `AES timeout` / `verify OK!` /
`chip id err!` / `backup header!!` 和 `uE1 … uE15` 这一串错误码。`uE<n>` 是中兴 BootROM 的
错误编号，串口看到它就知道卡在哪一级（**建议：真刷之前先接串口**）。

串口这条路的口令在 bootloader 里是当作**环境变量名**存的（`USERNAME` / `USERPASSWD` 紧跟在
`zte_serial` 后面），不是明文常量；已知的 U-Boot 控制台口令
`5cE080@fyBD` 见 [E2633_FIRMWARE](E2633_FIRMWARE.md) §5.3。本文件里搜不到这串，
说明它是从设备侧环境/后续版本带出来的。

### 5.4 与在役固件的版本对照

| 镜像 | 一级引导描述符长度 | U-Boot 编译时间 | 包尾长度 |
|---|---|---|---|
| 本目录 `boot.bin` | `0x6100` | 2023-08-28 19:11:33 | `0x66040` / `0x88840` |
| `e1630.raw`（实为 E2633 6B5 整机 dump） | `0x6180` | 2024-09-25 15:05:03 | `0x68040` / `0x8A840` |

`e1630.raw` 包尾 2 的时间戳 `20240925150510` 与官方包 `6B5改_109适配.bin` 的构建日期
**逐位相同**，可以确认那份 dump 就是 6B5 版本的整机镜像（此前只按文件名判断过）。

由此两条结论：

1. **`boot.bin` 不是 6B5 包里的那一版**，它比在役固件的 boot 还老（2023-08）。我在
   `6B5改_109适配.bin` 全文件里搜 `Me20230828191152`、`UUUUDDDD`、`BOOTROM` 都是 0 命中，
   也验证了这一点。它来自别处，只能靠 md5 认人——**这是整条链上最不可核验的一环。**
2. 两版 dump 都不足 `0x90000`，所以 mtd1 至少 0x8A840；具体分区大小以设备 `/proc/mtd` 为准，
   烧写工具按分区整擦。

外层那个未解字段 `0x5211`，与脚本里"新门 (1351,**5211**)"的第二个数同值。可能是升级密钥号，
也可能是芯片/板型编号——**未证，别当结论用。**

## 6. `change-model-boot.js` 的工程细节（值得抄的部分）

### 6.1 telnet 层

自己实现了 IAC 协商（`DO/WILL/DONT/WONT/SB…SE` 全走一遍，`DO 1/3` 回 `WILL`，其余 `WONT`，
`WILL` 回 `DONT`），不依赖 telnet 客户端。登录是**状态机而不是脚本固定顺序**：

```
等到 password 提示 → 发密码
等到 login/username 提示 → 发用户名
3 秒没动静 → 盲发一次用户名（防 busybox telnetd 先要密码的怪顺序）
凭据被拒（incorrect/failed/denied/again/wrong）→ 清状态重来
拿到 PROMPT（行尾 # 或 $）→ 认为进了 shell
```

最后用 `echo LOGIN_OK` 校准一次，再开始下命令。`expect()` 只匹配**尾部 800 字节**，
避免历史输出里的旧提示符造成误判——这是手写 telnet 最容易踩的坑，作者处理对了。

### 6.2 改型号

```
setmac 2 33024            # 读：返回 16 进制串，解成 "ZXHN E2633"
setmac 1 33024 <hex>      # 写：返回 ret = 0 才算成功
setmac 2 33024            # 回读比对
md5sum /dev/mtd2          # 前后取样，判断是否真落盘
```

tag id `33024` = `0x8100`，分区 `/dev/mtd2`（脚本里 `OPT.part`）。写之前先判断读到的值
是不是可打印 ASCII：不是就**跳过改型号只刷 boot**；是但不等于 `ZXHN E2633` 就要求人工确认。
写完回读不一致也**不阻断刷 boot**——把"可选步骤"和"必做步骤"分开，这个取舍是对的。

跑完写一份 `tag_<ISO 时间戳>.txt`，里面有 `setmac show2` 全表、改前改后十六进制+md5，
并附一行可直接粘贴的回滚命令：

```
# rollback: setmac 1 33024 <改前十六进制>
```

**这是整个工具包里我最认可的一段：它给回滚留了路，而且回滚命令是现成的。**

### 6.3 把 559 KB 送上设备：三条路

| 顺序 | 方式 | 判成功 | 备注 |
|---|---|---|---|
| 1 | 设备侧 `curl -s http://<PC>:8000/boot.bin -o …` | `ls -l` 里有精确字节数 | E2633 的 busybox **多半没有 curl** |
| 2 | 设备侧 `wget -O …` | 同上 | 实机上真正会走这条 |
| 3 | telnet 内 `cat > file << 'B64EOF'` + `base64 -d` | `wc -c` 命中三个合法值之一 | **实机大概率不可用**：E2631 的 busybox `--list` 里没有 `base64` |

第 3 条作为"ping 不通时唯一后路"其实很脆——脚本先 `which base64` 才动手，至少不会瞎传。
但真实失败模式是：防火墙挡了 ICMP 也挡了 8000，然后 `base64` 又不存在，于是卡在"三种传法都失败"。
所以 .bat 里那段提权 + `netsh advfirewall` 加 `TCP 8000` / `ICMPv4` 入站规则**不是可选项**。

### 6.4 烧写与中断处理

```
/bin/boot_flashing -f /var/tmp/boot.bin
判据：输出出现 "write boot ok"（也监听 boot key check error / bad magic / crc check fail）
```

断线后：等 60 秒 → 最多 5 次重连（每次间隔 20 秒）→ 重连成功后看 `/var/tmp/boot.bin`
**还在不在**：没了就当"已消费=烧完"，还在就重烧。最多 5 轮。

这条判据有个坑：如果 `boot_flashing` 是"先删源文件再写"，中途失败也会被判成成功。
脚本唯一的补正是最后 `md5sum /dev/mtd1` 记录指纹——**但只记录，不比对**。
建议刷之前手工记一次 `md5sum /dev/mtd1`，刷完自己比。

### 6.5 三道闸门

1. `boot.bin` 本地 md5 + 大小，不符直接退（防止把别的版本拖进来）。
2. 设备侧 `md5sum /var/tmp/boot.bin` 必须等于同一 md5，才允许动 mtd1。
3. `cat /proc/zte/verinfo/versionstates` 取 `curUpgradeKey1`，**必须 = 0**，否则拒绝（`--yes` 可绕过）。

第 3 条是"防砖"的核心，也是 `--yes` 最危险的地方。

## 7. 风险与坑（按严重度）

1. **`boot.bin` 不可溯源**（最严重）。它不在任何官方包里，脚本只认 md5，而 md5 只能证明
   "和作者用的一致"。写的是 mtd1，写坏只能上编程器。
2. **`--yes` 会同时跳过 key 闸门和所有确认**，包括"在役包 key1≠0"那条。别用。
3. **升级密钥对不匹配 = 变砖**，且是**刷完重启才发作**——烧写过程本身会打印 `write boot ok`，
   一切看着正常。所以"刷完先别重启、先接串口"是有意义的。
4. **外层容器头 `0x08..0x3C` 被清零**（**风险已降级**，见 §10.6）。原厂文件那段是 0，重打包也写 0，
   而独立实现 `zcu` 对 type 3/4 同样写 0——这几个字段在 CBC 类型里不参与校验。唯一残留的可能是
   `[2]`（未加密长度），若某个版本的原厂文件填过、我们不保留。另一半仍是真风险：工具只对自己的输出
   做往返校验，**没有对"设备是否接受"做验证**——README 里"等路由器重启完成后 telnet 开启"是作者的
   实测结论，不是代码能保证的。
5. **`base64` 兜底路径在 E2633/E2631 上多半不可用**（busybox 无此 applet）。
6. **临时防火墙规则会残留**：脚本被 Ctrl-C/强杀时 `netsh delete` 不执行，
   `ZTE_Flash_8000_TCP` 和 `ZTE_Flash_ICMP` 会留在系统里。
7. **本机会对同网段开一个 8000 端口 HTTP 服务**，且不校验路径（任何请求都回 `boot.bin`）。
   同一网段谁都能下这份 bootloader。
8. `README.txt` 与现实不同步（见下），照 README 找文件会找不到。

### 7.1 README 与实际文件的三处不符

- README 让双击 `config-telnet-enable.bat`，实际叫 `点击开启telnet.bat`；
- README 说 E2638 / E1630 的种子"未经验证"，代码里它们和已验证机型**同等对待**（同一套五重校验）；
- README 没提 `E2637`，改过的脚本里它排第一位。

判断依据以 **JS 里的 `PROFILES`** 为准，README 只是历史说明。

## 8. 对我们项目（`luci-app-zte-mesh`）有什么用

### 8.1 立刻可用：解自己的 `config.bin`，把 mesh 配置库读出来

这条路给了我们一把**不用 root 就能拿原厂配置库**的钥匙：Web 界面备份 → `config.bin` →
用 `PROFILES` 里的种子解出 XML。里面是 `DM`/`Tbl`/`Row` 结构，也就是我们在
[VENDOR_MESH_STACK](VENDOR_MESH_STACK.md) 里靠 `db-engine-probe` 一行行摸的那张库。
对 1905 相关项的用处：**直接把原厂 mesh 配置（SSID/回传/VLAN/ACL/漫游过滤表）的字段名和取值看清楚**，
不用再猜 `0xa932/0xa94e/0xa971` 这些总线号背后写的是哪一行。

注意：E2633 用的是 `0238002E` 族，E2631 也是同一族但机型不同——**先按后缀试，CRC 会告诉我们对不对**。

### 8.2 顺手做的交叉实验（负结果，别再重复）

配置密钥用的是 `SHA256("<机型>Key<8hex>")` 派生，这套"种子当密钥"的风格让我怀疑
rootfs 是不是也这么来的。用 `e1630.raw` 的 rootfs 密文区（`0xA40000`，22.25 MiB）
试了 **6 机型 × 2 后缀 × 7 种派生 = 84 组**（`SHA256`/`MD5`/`MD5×2`/ASCII 直用 ×
ECB/CBC × 128/192/256 位），输入 1 MiB：

- 全部输出熵仍为 **7.996**、zlib 压缩比 **0.996**（与密文本身一致），无任何 `ELF/JFFS2/UBI/zlib` 头命中。

**结论：配置密钥族与 rootfs 密钥无关。** rootfs 仍是 AES-128-ECB + 运行时派生，
[E2633_FIRMWARE](E2633_FIRMWARE.md) §5 的三条路（BOOTROM/cspboot、NAND 控制器透明解密、eFuse）
一条没少。这次只是排除了一个便宜的猜测。

> 2026-11-01 再补一个同款便宜猜测的排除：SR1010 社区仓库的「CSPBOOT 明文常量当密钥」打法
> （`SR1010V102020030` 那类）对 E2633 的 boot/kernel/JS 全量扫描也不中——
> 不止配置密钥族无关，**rootfs 密钥也不是任何盘上明文常量**。
> 详见 [E2633_FIRMWARE](E2633_FIRMWARE.md) §5.4。

### 8.3 一个副产品结论

`boot.bin` 里 `public_key inited but CSPBOOT_VERIFY_SIGN_XXX not enable` +
一级引导 `non secure uboot`，**实证了这些机器的安全启动没启用**，校验只有 CRC 和
升级密钥对。这与我们在 [MESH_INTEROP](MESH_INTEROP.md) §4.1 的结论
（内核读路径没有解密代码、rootfs 密钥来自更底层）是一致的：**厂商的信任根在 BootROM，
而 BootROM 在这批机器上没开验签。**

## 9. 复现（只读，不碰设备）

```bash
# 校验 boot.bin 容器头与 CRC32（应输出 97e1da67 / 0x88840 / 0xac）
python3 - <<'PY'
import zlib,struct
d=open('boot.bin','rb').read()
h=struct.unpack_from('<I',d,0x64)[0]; n=struct.unpack_from('<I',d,0x68)[0]
print(hex(zlib.crc32(d[h:h+n])), hex(n), hex(h), len(d)==h+n)
PY

# 试解 config.bin（改 XML 并重新加密，输出 config-telnet-enable.bin）
node telnet_enable.js config.bin config-telnet-enable.bin
node telnet_enable.js config.bin out.bin --keep-creds   # 不动账号密码
```

`boot.bin` 的哈希必须一直是 `22acbfdf2e93e5f823483bd1ee6829cb`；`change-model-boot.js` 里
`CFG.expectMd5` 就是它，改了任何一个字节脚本会拒绝继续。

## 10. 拿密钥去搜外文资料（2026-11-01）

### 10.1 字面密钥：公开渠道零命中

| 关键字 | 渠道 | 结果 |
|---|---|---|
| `"E2633Key0238002E"` | GitHub 代码搜索（全站） | **0 命中** |
| `"Iv0238002E"` | GitHub 代码搜索 | **0 命中** |
| `0238002E` | GitHub 代码搜索 | 930 命中，全是浮点常量（`4.0238002e-01`）之类噪声，无 ZTE 相关 |
| `13515211` | GitHub 代码搜索 | 3 784 命中，同类噪声 |
| `0238002E` / `13515211` + ZTE | 网页搜索 | 只有中兴官网/百科，无技术内容 |
| `ZTE ZXHN config.bin decrypt AES key` | 网页搜索 | **命中 ZCU**（见 10.2） |
| `zcu type 4 signature keygen` | 网页搜索 | 命中 ZCU 的 deepwiki 文档，含各型号密钥生成表 |

结论：这五个种子的字面值**没有任何人公开过**。但**这套构造法早被独立实现过**，而且细节逐条对得上。

### 10.2 ZCU：同一格式的英文参考实现

`mkst/zte-config-utility`（Python，README 说核心来自 Felis-Sapien 的 pastebin 泄漏，社区有大量 fork）。
它自报验证过的机型是 H298A/H298N/H267A/H268Q/H298Q/H168N/H108N/F600W/F680/SR7410 那一批，
**没有 E 系**——所以 E 系的种子没进它的表。

三个常量（`zcu/constants.py`）：

```python
PAYLOAD_MAGIC   = 0x01020304
SIGNATURE_MAGIC = 0x04030201
ZTE_MAGIC       = (0x99999999, 0x44444444, 0x55555555, 0xAAAAAAAA)
```

第三行就是 §5.1 里 `boot.bin` 的那四个魔数——**所谓"ZXIC 烧写容器"其实是 ZTE 的通用信封**：
boot 的 `[2]`（0x18）= 1，config 的 `[2]` = 4，同族不同布局。config(type 4) 的长度在 `[13]/[14]`
（0x44/0x48），boot(type 1) 的在 `[21]/[22]`（0x64/0x68）——这解释了 §5.1 里那两个"未解"槽位。

密钥派生（`zcu/xcryptors.py` 的 `CBCXcryptor.set_key`）：

```python
key = sha256(self.aes_key_str.encode()).digest()
iv  = sha256(self.aes_iv_str.encode()).digest()
self.aes_cipher = AES.new(key, AES.MODE_CBC, iv[:16])
```

与我们 JS 的 `aesDecrypt()` **逐字相同**（AES-256-CBC、SHA-256 派生、关自动填充）。
所以"拿 SHA-256 当 KDF"不是 E 系的怪招，是 type-3/4 的通用做法（type 2 才是 AES-128-ECB + 短口令）。

内层 60 字节头，`zcu/compression.py` 的注释给了字段名：

```
[0] Magic 0x01020304      [1] type 0 = 仅 zlib
[2] 解压后总长            [3] 累计压缩长度（不含最后一块）
[4] 分块大小              [5] 压缩数据 CRC32
[6] 头前 24 字节的 CRC32  [7..14] 32 字节保留
```

**这就是 §3 那张表**，一个字段不差，包括那两个 CRC32。zcu 对 `[3]` 的描述很绕
（"累计长度含 60 字节头、不含最后一块"），换算过来正好是"最后一块的起始偏移"，
也就是我们写的 `offsets[last]`。

一处容易踩的差异：**type 2 的分块头字段序是反的**（`chunk_size, dec_size, more`），
type 3/4 才是 `dec_size, chunk_size, more`。我们这份是 type 4，用后者——对。

### 10.3 我们的 8 位后缀 = ZCU 的 `key_suffix` 家族

`zcu/known_keys.py`：

```python
def signature_keygen(params, key_suffix="Key02721401", iv_suffix="Iv02721401"):
    nospaces = params.signature.replace(" ", "")
    return (nospaces + key_suffix, nospaces + iv_suffix, ...)
```

`<签名去空格><Key|Iv><8 位>`——**和我们这五个种子的拼法完全同构**。它已收录的后缀：

| 后缀 | 用在 |
|---|---|
| `Key02721401` / `Iv02721401` | 默认（ZXHN H168N V3.5 等） |
| `Key02710010` | ZXHN H298Q / H268Q |
| `Key02710001` | H188A / H288A |
| `Key02660004` | H196Q |
| `Key02670001` | ZTE H1600 / ZXHN H1600 |
| 长后缀 `8cc72b05…` / `667b02a8…` | ZXHN F450(EPON) |

我们这五个：`0238002E`（E2633/E2631/E1630/E2637/E2638 **五个机型共用**）、
`13515211`（E2631/E1630 旧族）。

由此三条：

1. **构造同源，不是巧合**：`<前缀><Key|Iv><8 位>` 是 ZTE 通用拼法。
2. **8 位后缀是"密钥组号"，不是机型号**——五个机型共用一个；zcu 里 H188A/H288A 也共用一个
   （`02710001`）。"换机型但后缀不变"是正常的。
3. **前缀是裸机型码 `E2633`，不是 `ZXHN E2633` 去空格**（那样会得到 `ZXHNE2633`）。
   E 系把"签名"这一环简化成了机型码——这也解释了为什么它的 config.bin 里没有签名块（见 10.4）。

### 10.4 E 系把信封剥掉了两层

zcu 读文件是三步：`read_header()`（找 `ZTE_MAGIC`，128 字节）→ `read_signature()`
（找 `SIGNATURE_MAGIC`）→ `read_payload_type()`。前两步都靠魔数判定，**匹配不上就 seek 回 0**。

E2633 的 config.bin 以 `0x01020304` 直接开头（§3 的表就是从 0 开始），所以：**没有 128 字节信封、
也没有签名块**，60 字节 payload 头就是文件头。这与"密钥不再由设备签名派生、而写死成机型+组号"
是同一件事的两面：密钥来源从"每台机器的签名"退化成"每类机型的常量"——**这也是那个一键脚本
能对所有同型机通用的原因。**

### 10.5 一条可以照抄的 ZCU 命令（推断，未实测）

```bash
git clone https://github.com/mkst/zte-config-utility && cd zte-config-utility
python3 -m pip install pycryptodomex
# type 4 只走 keygen，不读 --key；--try-all-known-keys 让通用 signature_keygen 用我们给的后缀跑一遍
python3 examples/decode.py config.bin config.xml \
    --signature E2633 --key-suffix Key0238002E --iv-suffix Iv0238002E --try-all-known-keys
# 期望看到：Trying key: 'E2633Key0238002E' iv: 'E2633Iv0238002E' … Successfully decoded using key: …
```

为什么这么写（依据 `examples/decode.py` 分支）：`decode_payload_type_4()` 只调 `run_keygens()`；
`--signature E2633` 匹配不到任何已登记签名，所以必须加 `--try-all-known-keys` 走 `run_all_keygens()`；
通用 `signature_keygen` 会读 `params.key_suffix/iv_suffix`，于是拼出我们要的串。

**未实测**：手上没有真实 `config.bin` 样本。要试先按 §9 确认文件头是 `01020304 00000004`。
命中就说明可以拿它替代那两个 JS（对我们更有用：Python、直接吐 XML、还能把种子回馈上游）。

### 10.6 顺手核对了 §7 第 4 条

用 JS 自己的 `encryptConfig()` 合成一份样本（232 325 B 假 XML → 4 块 → 14 616 B 文件），
再逐字段读回：

```
outer head : 0x01020304 0x4 0x0 …（其余全 0）
outer chunk: unpacked=14535 packed=14544 next=0
inner head : 0x01020304 0x0 0x38b85 0x3068 0x10000 0x94a47222 0x89858e88 0x0…
chunk0 @3c   unpack=65536 packed=4332 next=0x1134
chunk1 @1134 unpack=65536 packed=3993 next=0x20d9
chunk2 @20d9 unpack=65536 packed=3971 next=0x3068
chunk3 @3068 unpack=35717 packed=2131 next=0
crc32(inner[0:24]) = 0x89858e88 = field[6]
crc32(全部压缩块)  = 0x94a47222 = field[5]
```

`0x38b85` = XML 总长、`0x3068` = 最后一块起始偏移、`0x10000` = 分块大小，与 §3 表一致。

而且 zcu 对 type 3/4 写头时同样把 `[2]/[3]/[4]` 填 0（只有 `include_unencrypted_length`
才填 `[2]`），所以 **§7 第 4 条"清零外层头 0x08–0x3C 会丢信息"降级**：CBC 类型这些字段
本来不参与校验，唯一残留可能是 `[2]`（未加密长度）。风险从"中"改判为"低"。

### 10.7 两条附带线索（与密钥无关）

- 那篇恩山 TTL 教程把 U-Boot 口令的来源指向越南 voz.vn 的 "unlock mesh ZTE … ban-quoc-te" 帖，
  它给的口令与我们 [E2633_FIRMWARE](E2633_FIRMWARE.md) 里记的不同——说明口令随 bootloader 版本变，
  真要用两个都试。
- 顺带发现 `Zlion-Y/zte-e2631-openwrt`（GitHub，E2631 主线 OpenWrt 移植，带 `docs/`、`re/`、
  `board/`），正是这套配置工具作者的另一个仓库。它里面**没有** `0238002E`（代码搜索 0 命中），
  所以种子确实没公开；但那个仓库对我们 E2631 那条线可能有料（驱动对齐、
  `wanlan-adaptive` 逆向记录、有线吞吐测量）。

## 11. 深挖 §10.7 的两条线索（2026-11-01）

§10.7 留了两条口头线索，这次各自挖到底。结论先摆：**两条都挖出了比原目标更有用的东西**，
而且其中一条**推翻了我们自己前一天的文档结论**。

### 11.1 越南那条：U-Boot 口令没找到，找到的是换区开关

先说没找到的。按 10.7 的指路去找 voz.vn 那篇 "unlock mesh ZTE … ban-quoc-te"，用站点检索
（`site:voz.vn` + E2631/mesh/unlock）**命中不了那篇**，也没有任何一页给出可复用的 U-Boot 口令。
`5cE080@fyBD` 这种串本来就是"某版 bootloader 的某行字符串"，论坛转述时极易串版本——
所以 **10.7 第一条的判断要收紧**：不是"口令随版本变，两个都试"，而是
**"口令没有可靠公开来源，只能从手上的 bootloader 里读"**。这反而回到了我们自己的强项：
`boot.bin` 就在本地，串口一接就能看到 U-Boot 提示符，不需要外部口令。

但同一趟检索命中了别的东西——voz.vn 的 H196A 固件长帖（`t/cap-nhat-firmware-home-wifi-mesh-h196a.662355`
第 162 页），里面有一条**可直接执行的 mesh 模式开关**：

```sh
# 适用于 H3601P / E2633 / HV3601P V9.3，刷完巴西固件后转 full mode
echo 'current : 1017001002' > /defcfg/current_region
sendcmd 1 DB rreset
reboot
```

四条信息：

1. **mesh/full 模式是"区域"的一个函数**，不是独立开关。越南社区的做法是改区域号再重建配置库。
2. 落点是**一个文件**：`/defcfg/current_region`，内容格式是 `current : <10 位数字>`（注意冒号和空格）。
3. `sendcmd 1 DB rreset` 是"按当前区域重建配置库"，不是简单恢复出厂。
4. `1017001002` 这个 10 位数没有公开释义。**不要猜**——要动就先 `dd` 备份 mtd5。

`/defcfg` 是 mtd5（[E2633_FIRMWARE](E2633_FIRMWARE.md) 与实机挂载都证实），而且实机上
`/userconfig` 是指向 `/defcfg` 的软链：

```
lrwxrwxrwx  userconfig -> /defcfg        (board/caldata.out.txt)
```

所以网上那些 `/userconfig/flag_type`、`/defcfg/flag_type` 是同一个文件的两个名字。

### 11.2 `sendcmd 1 DB`：第二条读配置库的路，不用破解 `config.bin`

§8.1 讲的是"解 `config.bin` 把 mesh 配置库读出来"。那需要种子、需要 Web 备份、需要五重校验。
**`sendcmd` 是另一条路**：设备自己有 DB 命令行，telnet 进去就能读。

语义（多来源一致，且与 [MULTIAPD](MULTIAPD.md) §7 的符号对得上）：

| 命令 | 作用 |
|---|---|
| `sendcmd 1 DB p <表名>` | 打印整张表（`p` = print） |
| `sendcmd 1 DB set <表> <Row> <Key> <Val>` | 改一个字段 |
| `sendcmd 1 DB save` | 落盘 |
| `sendcmd 1 DB rreset` | 按当前区域重建配置库 |
| `upgradetest sdefconf <N>` | 切换区域码（三位数），之后配 `rreset` |
| `cat /etc/init.d/regioncode` | 列区域码 |
| `cat /defcfg/flag_type` | 看当前区域 |

**为什么这条能通**：`multiapd` 的导入符号里有 `DBShmCliInit`、`dbAPI{Add,Get,Qry,Set,Lst,Save}View`、
`dbFindTbl`、`dbClearTbl`（见 [MULTIAPD](MULTIAPD.md) §7）。`sendcmd` 和 `multiapd` 操作的是
同一块共享内存 DB（对应 `/SYSV0000ffff`）。DB 视图名我们在 [VENDOR_MESH_STACK](VENDOR_MESH_STACK.md) §3
已经逐个反汇编出来了：

```
MultiAPCfg / MultiAPRoamCatonCfg / MultiAPCacheMac / MultiAPStaCfg
```

所以**待验证清单里"`EnVbss`/`ToVbssMode` 在哪个视图"这一条，现在有零成本的做法**：
`sendcmd 1 DB p` 挨个表打出来 grep 就行，不用上编程器、不用解 rootfs。

风险也要写清：`set` + `save` 会改真机配置，`rreset` 会重建整库（相当于恢复出厂）。
**只读优先**：先 `p`，先 `dd if=/dev/mtdblock5 of=/tmp/defcfg.bak` 备份，再考虑写。

### 11.3 `/kmodule/bin` 里的运维工具清单（实机 `ls` 实证）

这是这次顺带挖到的、对日常调试最有用的东西。设备的 `/kmodule/bin` 不是精简 busybox，
是**一整套厂商工具**（`board/pass16.out.txt` 的原始 `ls`）：

| 工具 | 用途 | 对我们 |
|---|---|---|
| `sendcmd` | DB 读写（§11.2） | 读 mesh 配置库 |
| `setmac` / `setmac show2` | 读写设备串码（MAC/SN/区域） | 改型号实验，**§6.2 那套改的就是它** |
| `upgradetest` | `getver` 查版本、`sdefconf <N>` 换区 | 区域/mesh 模式 |
| `wlandiagcli` | 无线诊断 CLI | **可能直接看 mesh 关联状态**（未试） |
| `wificali` | 射频校准 | 别动 |
| `multicast_test` | 组播收发测试 | 1905 组播通路验证 |
| `switchtst` | 交换芯片测试 | VLAN/端口实验（D5） |
| `devmem2` | **直接读写物理地址** | 看 PPE/寄存器，不用写内核模块 |
| `ebtables` / `brctl` / `brconfig` / `tc` / `mirror` | 桥与流控 | 抓包重定向、镜像口 |
| `boot_flashing` / `fw_flashing` | 分层刷写 | 与 `boot.bin` 那条链同源 |
| `wgets` / `wput` / `wput_ftp` / `axel` / `curl` | 传输 | 比 busybox 的 wget 强；**注意是 `wgets` 带 s** |
| `stun` / `cpeserver` / `dnsapi` / `dnspods` / `phddns` | 打洞/云 | 与 mesh 无关 |
| `syn_version` / `logctrl` / `set_process_threshold` | 版本同步/日志 | 调日志级别可能用得上 |

两条实用推论：

- **`devmem2` 在，就不用为"读一个寄存器"去编内核模块**。[VENDOR_MESH_STACK](VENDOR_MESH_STACK.md) §8
  里那些"只读查 DB/寄存器"的待验证项，成本再降一档。
- 之前记的「busybox 没有 `nc`/`base64`」仍然成立，但**不代表没法传文件**：`wgets`/`wput` 才是正路，
  §6.3 那条"三条路降级"里最该先试的是这个，不是 `base64`。

### 11.4 一条间接证据：跨机型混组是通的（关系 D7）

同一个帖子里有人直接问、有人直接答：

> "Con H196A này có thể làm controller, mesh mấy con E2631/2633 làm agent được không?"
> （这台 H196A 能当 controller、让几台 E2631/2633 当 agent 吗？）
> —— "Được bác nhé"（可以）

这是**不同型号之间当 controller/agent 混组的第三方口述实证**，正对着我们 D7
（E2631 当 agent 能否被我们控制器接住）。但它只有一句话、没有固件版本、没有报文、没有复现步骤。

**强度评级：弱。** 它的价值是"这条路有人走通过"，不是"我们能走通"。按要求，
D7 仍然只能由线上 `0x893a` 证据定性；不过它把"完全不可能"这个可能性排掉了。

### 11.5 Zlion-Y 仓库：三个实质收获 + 一处自我纠错

通读了 `Zlion-Y/zte-e2631-openwrt`（浅克隆 15 MB，247 个 `board/` 实测记录、25 个 `docs/`）。
它不是"可能有料"，是**真的补了我们三个洞**：

**(1) 那份 `ps` 暴露了我们取漏了 `multiapd`。** `board/mr4.out.txt` 是同型号机器的完整进程表：

```
  597  Z500_w9b 174m   10800  S  <0>  cspd
 1293  Z500_w9b 45344   8260  S  <0>  multiapd
```

我们那 22 个文件按「cspd + 6 个 mesh 库 + 15 个支撑库」挑，**把 `multiapd` 整个漏掉了**。
而它才是 1905 的协议实体。这一条直接推翻了 [VENDOR_MESH_STACK](VENDOR_MESH_STACK.md) §2
原标题「cspd 是唯一的 mesh 进程」和结论「1905 由驱动收发，不走 socket」——两处都已修订，
逐条剖析另开一份 [MULTIAPD](MULTIAPD.md)。

**(2) `multiapd` 没被 strip。** 6 835 个符号、2 184 个具名函数，1905 的 TLV/拓扑/M1/M2/角色选举
全在里面。这是我们第一次拿到**函数级**的 mesh 栈视图（cspd 是 3 MB 匿名代码）。
顺带确认：它和我们 runtime 包里那份**是同一字节**（`sha256 a0ba6bee…6280a`），
所以我们线上跑的东西**本来就带符号**，调试时可以直接拿函数名。

**(3) `re/ffe/ksym-all.txt` 是一份 21 946 行的内核全符号表。** 我们那份 6 393 个是从
`__ksymtab` 恢复的**导出**符号；这份含 static。拿它对照，三个同名符号逐一对上（地址因版本平移），
同时补出我们漏掉的 `br_ieee1905_local_in_handle`——进而修正了 [MESH_INTEROP](MESH_INTEROP.md) §5
「只识别、只丢弃」的判断：**丢的是转发，本地投递是放行的**。详见那份文档 §5.5。

**(4) 一处需要记下的自我纠错：他们的 runtime 包是我们的旧快照。** 两份 `tar.gz` 只差四个成员：

```
我们多：controller-net.c、enroller.c、ns-run.c        ← 我们的源码
我们多：root/lib/libzte_agent_mm.so                   ← 6,436 B，3 个导出
        CmWLANSetAPConfig / CmWLANSetPsk / zte_agent_mm_layout
```

`multiapd`、`libcmapi.so` 两边 `sha256` **完全相同**。也就是说上游那份就是我们的历史版本，
`libzte_agent_mm.so` 是我们后加的垫片——而它导出的两个函数恰好是 **agent 收到 M2 之后要落的
两件事：下 AP 配置、写 PSK**。这解释了为什么我们的包能当 agent 用而上游那份不能，
也是 D6（agent 侧落配置）现有实现的位置。

**附带的两条社区经验**（来自该仓库 README 对 `cnjn/zte-mesh` 的引用，都是别人踩过的坑）：
PAUSE 帧会压住上传、PPPoE 不走硬件转发、JFFS2 清除标记要放数据区。
这三条与 mesh 无关但会影响带 mesh 的整机表现，做整机镜像时要记得。

**还有一条方法论值得抄**：那个仓库明确列出了它**推翻掉的公开说法**
（H3600 那套的 `kernel1`/`kernel2` 分区名、"`mtd erase kernel1` 就能回原厂"、
某份 `TESTED.md` 里的 sha256 和"USB 3.0"），每条都附"是哪个实测把它关掉的"。
我们的文档也该保持这个习惯——§11.1 和 [VENDOR_MESH_STACK](VENDOR_MESH_STACK.md) §2
这次就是这么改的。

### 11.6 这两条线索各自改变了什么

| 线索 | 原来的期待 | 实际得到 | 影响 |
|---|---|---|---|
| 越南 / U-Boot 口令 | 拿到一个能进 U-Boot 的口令 | **口令没有可靠公开源**；换区开关 + `sendcmd` 语义 + 跨机型混组口述 | 收紧 §10.7 判断；新增 §11.2 一条零成本读 DB 的路 |
| Zlion-Y 仓库 | "可能有料，值得过一遍" | `multiapd` 有符号、我们取漏了它、21 946 行内核符号表、runtime 包差异 | **推翻两份文档各一条结论**，新增 [MULTIAPD](MULTIAPD.md) |

按性价比排，接下来该做的三件事（都不需要停机窗口）：

1. `dd` 备份 `/dev/mtdblock5`，然后 `sendcmd 1 DB p` 把 mesh 相关表全打出来（只读）。
2. `cat /proc/kallsyms | grep -E "1905|multiap"` —— 若开机能读到，就能把
   [MESH_INTEROP](MESH_INTEROP.md) §5.5 那三条从"对方符号表"变成"本机实证"。
3. 取 `/kmodule/bin/multiapd` 原件（一次 tftp，几 MB），确认它与 SR1010 版差多少。

**仍然要停机的只剩一件**：把 E2631 拉成我们控制器的 slave 看 M2 是否被接受（D6/D7）。
但它现在的前置条件变好了——`pktdump` 和 `logcat` 的 `AGENT RECV` 日志都能在同一窗口里取证，
一次停机能拿到的信息比原计划多。
