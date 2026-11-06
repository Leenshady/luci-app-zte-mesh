# 开发、构建、测试与上机联调

架构见 [ARCHITECTURE.md](ARCHITECTURE.md)，服务配置语法见 [REAL_CHILD.md](REAL_CHILD.md) 第 3 节，脚本见 [tools/README.md](../tools/README.md)。

## 1. 机器与连接

**三台测试机的清单（机型、地址、角色、能力差异）只在 [LAB](LAB.md) 第 2.1 节**：`ax3600` 192.168.1.1、`owrt` 192.168.1.20、
`ubuntu` 192.168.1.191，登机器一律 `python tools/lab/rssh.py`，账号与口令只在本机 `.local/lab-env.md`（脚本读那张表，也认
`ZTE_LAB_USER_<别名>` / `ZTE_LAB_PASS_<别名>`）。别名 `owrt` 是登录用的名字，
那台机器跑的同样是 ImmortalWrt——`owrt` 只是登录用的别名，不是另一套系统。以前那台测试主机上的旧清单（2026-10
之前：mac `192.168.167.93` 上的 `/Volumes/code/sr1010-mesh`、开发机 `git push` + `receive.denyCurrentBranch updateInstead`、
ZXSLC SR1010、ZXHN Z506、测试环境里的 ImmortalWrt 虚拟机）已不再保留，复盘历史证据按 [journal](journal/) 查，本文不再维护第二份机器名单。
这里只留三件别处没有的事：

- **脚本默认值仍指向旧环境**，不清掉就会被当成现役：测试环境里的脚本默认 `--host root@192.168.5.1 --port 22`；子路由 telnet 是
  `192.168.5.216:23`（`tools/child/ztetn.py` 默认值，`--host`/`CHILD=` 可改），口令只从 `ZTE_TN_PASS` 读
  （用户名 `ZTE_TN_USER`，默认 root），**不要写进任何文件**。那台旧 SR1010 没有 python3、没有 `pkill`，只有 BusyBox 和 `tcpdump`。
- **OpenWrt 构建机**：`ssh -p 2022 192.168.223.19`，树在 `~/workspace/immortalwrt`，只用来编包（第 8 节）。
- **换机的产物可信**：交叉工具链与当初的容器同为 **GCC 11.4.0 Ubuntu 11.4.0-1ubuntu1~22.04.3**。2026-11-02 在 `ubuntu` 上
  重编 `libzte_mesh_shim/route/profile/agent_mm.so`、`db-engine-probe`、`zte-meshctl`，与随包提交的
  `zte-mesh-runtime.tar.gz` 里对应成员**逐字节相同**。

## 2. 构建

**现役构建机是三台测试机里的 `ubuntu`（192.168.1.191），不用 Docker**：同步 → 编译 → 测试的日常流程在 [LAB](LAB.md) 第 3 节，
`tools/lab/build.sh` 做的就是把下面这些目标在没有容器的机器上跑一遍。下面这张表是**容器路径**（mac 上的离线镜像
`sr1010-mesh-shim-builder`，`orbctl start` 先把 Docker 拉起来）——`build.sh` 与 `container-make.sh` 这两个入口还在仓库里，
复盘历史产物或需要原厂镜像时才走它。

| 命令 | 做什么 |
|---|---|
| `sh build.sh` | 完整流程：从固件提取并审计运行环境（`tools/dev/prepare.py`，需要 `unsquashfs`）、核对表结构证据、建镜像、`make all database controller test-host`、离线加载测试 |
| `sh tools/build/container-make.sh <目标>...` | 在容器里编指定产物（`db-engine-probe`、`zte-meshctl`、`ns-run`、`libzte_controller_trace.so` 等）或跑 `test-host`、`controller`。会在容器里先删旧产物，保留 make 的退出码 |
| `sh tools/build/runtime.sh svctest` | 测试环境运行包 `out/zte-mesh-svctest.tar.gz`（启动器根是 `/tmp/zte-mesh-svc/root`，路由器上只写 tmpfs） |
| `sh tools/build/runtime.sh install` | 包用的运行包 `openwrt/zte-mesh/zte-mesh-runtime.tar.gz`（随仓库提交，根是 `/usr/lib/zte-mesh/root`） |

- 原厂镜像默认读 `../zx279133/vendor-reference/2b5/rootfs.jffs2`，`SR1010_VENDOR_ROOTFS` 可改。重复准备会核对已有文件哈希。
- **在 mac 上不要原地覆盖源码文件再编译**：容器经 virtiofs 读源码，变短的文件报 `is shorter than expected`，变长的可能编出
  新旧混合的东西。用 git 更新，或先写临时名再 `mv`。产物同理，所以 `container-make.sh` 在容器里删。
- 运行包内有 `SHA256SUMS` 自校验，`make-runtime.py` 还会审计 ARM32 ABI（强符号、GNU 符号版本）。
- 改了 C 产物但手上没有原厂镜像时，用**只换成员**的 `python3 openwrt/repack-runtime.py --replace
  root/lib/libzte_controller_trace.so=out/runtime/lib/libzte_controller_trace.so`（步骤与两个坑见 LAB.md 第 3.0 节）：
  其余成员逐字节不动，只重算 `SHA256SUMS`，`python3 tests/test_runtime.py` 的守卫会守住"成员与仓库文件一致"。
  换完要逐条 diff 解出来的两个包，确认只有该成员和 `SHA256SUMS` 变了。少了的那一样是 `make-runtime.py` 的
  GNU 符号版本审计——重打完对着成品补跑一遍，方法与那个假缺符号的坑在 LAB.md 第 3.0 节。
- x86_64 主机可用 `Dockerfile.cross`（`make ROUTERCC=aarch64-linux-gnu-gcc`）交叉编译。

## 3. 跑控制器

三种方式（现役那台 `ubuntu` 上跑服务用 `sudo sh tools/lab/run-service.sh`，见 [LAB](LAB.md) 第 3 节；下面这几条是容器/离线那条路径的入口）：

1. **测试环境（不碰子节点）**：`python3 tools/dev/dev-service-smoke.py --no-attach --seconds 24`——路由器私有命名空间里跑服务，
   验证配置、热更新、守护进程重启，不把线接进 `br-lan`。`tools/dev/dev-config-check.py DIR` 批量喂配置看接受/拒绝，
   `tools/dev/dev-sched-check.py` 用真实时钟测定时窗口和访客限时（约 5 分钟）。
2. **中兴原厂路由器当子节点**：`python3 tools/capture/mreplay.py --config <0600 配置> --expect-macs <允许的 MAC> --out <证据目录> --seconds N`。
   接进 `br-lan`，触发子节点重新入网（默认把子节点端口 down/up），抓两侧的包、服务日志和 `state.json`；`--swap 秒:文件` 中途换配置，
   `--action '秒:reboot <mac>'` 或 `'秒:light <mac> 0|1'` 发节点动作，`--al-last` 每次换一个控制器 AL MAC。
3. **已安装的包**：`uci set zte_mesh.main.enabled=1` 等，`/etc/init.d/zte-mesh start`；状态 `ubus call luci.zte-mesh status`。
   只验证 procd/rpcd 而不碰子节点时，建一个空网桥（`ip link add br-zmtest type bridge`），把 `zte_mesh.main.bridge` 指过去。

- 配置文件里有 Wi-Fi 密钥：放仓库外、0600、跑完删除（证据目录里也不要留）。
- 服务运行目录：配置 `/run/zte-mesh/config`、状态 `state.json`、节点动作 `actions/`、子节点摘要 `peers/`。
- 调试日志级别（原厂守护进程）：`sendcmd multiapd.acd.acd -l 8`（还有 `base`、`topo`、`slave`），重启后复位，结束用 `-l 7` 还原。

## 4. 测试

- 主机测试：`sh tools/build/container-make.sh test-host`（C 编解码含 10 万次变异、证据处理、表结构、策略、profile、
  启动视图、init 158 条、终端数采集 32 条、启动器入口 20 条、启动器架构构建 26 条、rpcd 24 条、装机验收 278 条、preload、沙箱）。看到 `make: *** ... Error` 就是失败，退出码非 0。
  **本机（Windows + Git Bash）跑启动器入口那组会 skip 2 条**：`chmod 644` 清不掉执行位，"文件在但不可 exec"这一支在这套文件系统上表达不出，
  计数里那两条不是回归（owrt 的 busybox ash 上能跑全）。架构构建那组的编译器是 stub——它判的是 `tools/build/launcher-arches.sh` 的选址顺序、
  `-dumpmachine` 映射与 `ZTE_MESH_REQUIRE_ARCHES`，不是真编译；真编译在 CI 那一步，产物成员清单是它的判据（第 8 节）。
  `tests/test_verify_install.sh` 判的是 busybox ash 的语义，本机 `sh` 过了以后最好还在 owrt 上跑一遍。
  脚本读子节点上报时会**连读几份快照**（`METRICS_READS` 默认 3 次、每次隔 `METRICS_READ_GAP` 默认 5 秒，只在一次都没读到数时才继续等）；
  测试必须把这两值导出成 `1` 和 `0`，否则每个"这台没有数"的用例都真睡满 10 秒——同一套测试实测没导出要 4 分 57 秒、导出后 3 分 7 秒，
  睡出来的绿不算绿。
  装机验收脚本在设备上会**等主控的 SSID 在本机生效**（`SSID_WAIT` 默认 60 秒、每 `SSID_POLL`=3 秒看一次），等不到才判 FAIL 并列出
  `ssid_pending=`；测试用 `SSID_WAIT=0` 把等待关掉，另有一条用例喂"第三次才出现"的 uci 桩来守这段。uci 桩处理 `uci -q get` 是
  **按最后一个键名去查一个文件**——以前不分键，任何键都回角色那一行，新加的读回会"看起来总是对的"；`iw`/`ip` 同样是桩
  （各自 cat 一份 fixture），因为脚本对射频侧只 grep 不作判定。同一类"问得太早"
  在跳数那一列也犯过一次（见 [INSTALL](INSTALL.md) 第 3 节的 `cur_hop_age`），所以脚本读时钟的 `date` 也是桩——
  年龄窗口是秒级的，跟着主机墙钟跑就变成"什么时候跑这套就算什么时候"。
- 没有原厂镜像的机器（构建机 `ubuntu`、测试环境的 ImmortalWrt 虚拟机）也能跑绝大多数：`make test-host` 直接在 `$PWD/out`
  上跑。要 `out/analysis/cspd` 的只有 `cspd` 证据那几组——`test_schema.py` 4 条、`test_policies.py` 5 条、
  `test_profiles.py` 6 条、`test_startup.py` 整组卡在 `setUpClass`（`cspd` 只从哈希钉住的原厂镜像提取，`evidence/`
  不入库），所以 `make test-host` 会停在 `test_schema` 就退出；要看这四组之后的结果就单独跑，别拿"make 停在第几行"当回归。
  这几组仍只在容器里跑。虚拟机的 `/tmp` 是可执行的，`container-make.sh` 的 noexec 注意项在这里不适用。
- LuCI 页面的 JS 测试：`node tests/test_luci.js`（模拟 LuCI 的 `form`/`uci`/`rpc`；PATH 里没有 `node`，测试环境那两台机器也没装）。
  实测可用的解释器是这台机器上另一套工具带的独立二进制，直接给绝对路径就能跑：
  `"<本机那个 node.exe 的绝对路径>" tests/test_luci.js`（v24.21.0）。
  这个测试只读仓库里的 `zte-mesh.js` 与 `tests/test_init.sh`，不需要 LuCI 运行时，所以不必上开发机。
- 单个 Python 测试按脚本方式跑，第一个参数是产物目录：`python3 tests/test_schema.py out`（`unittest discover` 不适用）。
- 不依赖 `out/` 的三组可以直接在本机跑：`sh tests/test_init.sh`（它真的**执行** init 里的函数：`write_config()` 与预检 `launcher_ready()`。后者是 2026-11-06 从 `start_service()` 里拆出来的——`start_service` 先要 `/sys/class/net/<桥>/bridge` 再要 procd，主机上跑不动，于是那三段判断以前只能被 grep 字符串，"入口不在"那一行搬到 `--launcher-check` 后面也不会红。拆出来的这段只用 `ZTE_MESH_ROOT`/`ZTE_MESH_NSRUN` 那两个已有的环境变量就能喂各种形状，**判据读的是日志落到哪一句**，反面跑"删掉 `[ -x "$NSRUN" ]`"那份 init 能读到装机时那句假诊断）、`python tests/test_runtime.py`（31 条，守的是"tarball 成员与仓库文件逐字节一致"，另加两条"包里的库认不认得这条新帧路"）、`python tests/test_i18n.py`。
  `sh tests/test_rpcd.sh` 也能本机跑，但它的 `jsonfilter` 桩写死调 `python3`——这台机器上只有 `python`，没有那个名字时**七条用例全回 `{"result":-2}`**
  （看着像校验把合法参数也拒了，其实是桩没跑起来）。本机这样给它一个名字：`mkdir -p /tmp/pyshim && printf '#!/bin/sh\nexec "<本机 python.exe 的绝对路径>" "$@"\n' > /tmp/pyshim/python3 && chmod +x /tmp/pyshim/python3 && PATH=/tmp/pyshim:$PATH sh tests/test_rpcd.sh`——24 条全绿；要么上测试主机跑这一组。
- 容器把 `/tmp` 挂成 noexec：测试不能执行自己写到 `/tmp` 的脚本（`tests/test_rpcd.sh` 用 shell 函数和 `sh -c`），
  dash 的 `.` 不接受额外参数（先 `set --`）。
- **判"某次改动不该影响产物"要现编现比**：`out/` 不入库，`make` 又只比 mtime，陈旧产物会静悄悄混进哈希比对。
  2026-11-04 那次"三个产物与基线不符"就是这个——基线抄的是 11-01 用 `ROUTERCC=cc` 留下的 x86 版，而比对走的是
  `aarch64` 那条命令行。同一命令行、同一台机器上现编两份来比才算比过；拿 `out/` 里的历史文件当基线不算。

## 5. 上机测试守则（中兴原厂路由器）

向中兴原厂路由器推配置会真实修改并持久化它的设置。在 OpenWrt 路由器上推配置同样会持久改动它的配置，只是能用 `uci` 直接改回来。

- **每轮前后读回子节点基线**：`ZTE_TN_PASS=… sh tools/child/childread.sh`，8 行 `WLANCfg` 与 [REAL_CHILD.md](REAL_CHILD.md)
  末尾的基线逐行比较。改网络序号先在测试环境验证（`tools/dev/device-controller-smoke.py` 的 `map_teardown` 必须为 0），再上中兴原厂路由器轮询
  子节点 `WLANCfg`，一出异常立刻用"全部序号 + 一次 Renew"恢复。
- **永远不要向中兴原厂路由器推占位值。** 额外 BSS 的 `enabled/ssid/key` 必须三项给齐；同一块射频上的网络序号必须是 1..k。
- **不要改子路由的模式**（`MultiAPCfg` 的 `Mode`/`VersionMode`/`Enable`（MESH 开关）与 `P1905ComCfg.Enable`（MESH 兼容）），这些由用户在子路由上设置。
- **接入保护**：网桥转发表里出现不在 `--expect-macs` 里的 MAC 就拒绝接入。长期放行：控制器 `d0:11:e5:c5:cb:4a`、
  子路由 `d4:b7:09:0d:77:30`、一加 `32:83:90:d9:0f:62`、iPhone `aa:8b:bc:9f:1f:c7`、Mac `aa:c9:63:3e:7e:70`、
  用户电脑 `e8:bf:b8:9b:40:a1`。新 MAC 先用 `/tmp/dhcp.leases` 确认是普通客户机，不确定就问用户（一加还会用私有 MAC
  `42:be:ab:fd:c3:88`，不在长期名单里）。
- 会让用户设备掉线的测试（关主 SSID、ACL 封禁真实终端、信道/功率、改 SSID、定时 `scope=all`）必须先得到用户同意，
  测完立即还原。用户的电脑可能就接在子节点后面。
- 还原办法：信道先发原信道再发 `0`（自动）；SSID 被重配后掉到隐藏默认名时改成别的名字再改回；ACL 删光所有 `acl.N.*`；
  功率设回最大档。子路由原始值备份在 `evidence/build-mesh/config_z506.xml`（含明文密钥，不外传）。
- 不要在局域网里有别的真实 mesh 节点时把沙箱线接到 `br-lan` 用测试配置。

## 6. 踩过的坑

- **守护进程环境里只能有一个变量**（`ZTE_MESH_START_COUNT`）：多一个 `setenv` 原厂 ld.so 就断言失败
  （`dl-minimal.c: 126: realloc`，exit 127）。给 preload 库传状态走数据库。
- **路由器没有 `pkill`**：脚本按命令行找进程再杀（`mreplay.py` 曾每轮漏两个 `tcpdump`，攒到 66 个占 130 MB）。每轮后看 `ps`。
- **不要用 `kill multiapd` 逼子路由重新入网**：反复这样做后子路由日志出现 `restart unexpected N times`、
  `SubScribPublish ... already exist`，`cspd` 收不到上线通知（FINDINGS 3.4）。改用重启控制器、`zte-meshctl renew-acd <band>`、
  `mreplay.py` 的端口 down/up，或重启子路由。
- **不要自己发 ACD 事件 2**：守护进程收到第一个 1905 帧时自己发，提前发会用掉一次性的 `g_AcdsRenewStat`。
- **`pkill -f "<模式>"` 会杀掉自己**（命令行里含同样字符串的 shell 也匹配）：用 `[.]` 之类的写法。
- **zsh**：变量不自动分词；以 `=====` 开头的词会被当成命令展开。
- **子路由 telnet 会话不能叠加**；两个 `sendcmd` 同时跑会串话。重启会清掉 `/var/tmp/busybox`，自带工具只有
  `grep tr cut sed awk pgrep wget tftp`；行缓冲超过约 1 KB 会截断，长内容按 500 字符分块 base64 发（`tools/capture/vt.sh`）。
- `iwconfig` 在刚重启或已关闭的接口上读回的 SSID 不可靠；短时间连续重配 2.4G 会掉到隐藏默认 SSID。
- 测试路由器没有授时，时钟可能差十几个小时：定时窗口按路由器 `date -u` 算。
- **装包后服务起不来，先看是不是缺模拟器**：日志里 `execv errno=8` 几乎总是这个（`ENOEXEC`，内核拒绝执行 ARM32 ELF），
  不是代码退化。确认顺序：`uname -m` → 私有根里有没有 `qemu-arm-static` → [LAB.md](LAB.md) 第 4.2 节。

## 7. 改了控制器代码之后的检查

1. 本机提交 → 构建机编改动的产物（零警告，`ubuntu` 上 `sh tools/lab/build.sh all database`）→ `test-host` 通过 → `runtime.sh svctest`。
2. 测试环境：`tools/dev/dev-service-smoke.py --no-attach`；涉及配置解析的跑 `tools/dev/dev-config-check.py`；涉及调度的跑 `tools/dev/dev-sched-check.py`。
3. 中兴原厂路由器（需要时）：`childread.sh` 基线 → `mreplay.py` → 看 `state.json` 节点 `model:"ZXHN Z506"`、`zte:true`、
   `online:true` → `childread.sh` 读回。
4. 结束：路由器无残留（`/run/zte-mesh`、`/tmp/zte-mesh-svc`、`zmesh0`、`ns-run`、`db-engine-probe`、`tcpdump`）；
   mac 上含密钥的配置已删。

## 8. OpenWrt 包、版本控制与 CI

**feed**：本仓库是 ImmortalWrt 的一个 feed，包在 `openwrt/` 下。构建机的树（25.12，目标 `zte/zx279133`，apk 包管理，
分支 `sr1010-zx279133`）里登记两处：`feeds.conf.default`（已提交，`src-git zte_mesh https://github.com/cnjn/zte-mesh.git`，
不钉版本，每次 `feeds update` 取最新 `main`）和 `feeds.conf`（不受 git 跟踪、存在时优先，`src-link zte_mesh <本地克隆>/openwrt`）。

**打包**：

```sh
# mac：重新生成运行包（含原厂二进制，随 feed 提交）
sh tools/build/runtime.sh install                       # -> openwrt/zte-mesh/zte-mesh-runtime.tar.gz，提交它
# 构建机
git -C ~/workspace/zte-mesh pull && cd ~/workspace/immortalwrt
./scripts/feeds update zte_mesh && ./scripts/feeds install -a -p zte_mesh
# .config 里加 CONFIG_PACKAGE_{zte-mesh,luci-app-zte-mesh,luci-i18n-zte-mesh-zh-cn}=m，再 make defconfig
make package/feeds/zte_mesh/zte-mesh/compile \
     package/feeds/zte_mesh/luci-app-zte-mesh/compile \
     package/feeds/zte_mesh/luci-i18n-zte-mesh-zh-cn/compile   # 翻译是独立的包，不编译它就没有中文
```

- 包直接解包同目录下提交的 `zte-mesh-runtime.tar.gz`，不需要往 `dl/` 放东西；`ZTE_MESH_RUNTIME=` 可指向别的运行包。
- **一个包目录**（2026-11-05 合并 `zte-mesh-launcher`）：`zte-mesh` 既装脚本、LuCI、rpcd、配置和运行时 tarball，也编启动器，
  `PKGARCH:=all`。拆成两个包为的是 `package-pack.mk` 的 `Architecture` 读 Makefile 级全局 `PKGARCH`——数据包要 `all`，编译机器码的包
  就得在自己的 Makefile 里给别的值。两边都成了 `all` 之后这句话不再逼着什么：启动器**不是"给不了两个架构"，而是把三个架构都装进一个包**——
  `arch/<cpu>/{ns-run,mesh-search}` 各一份 `-static` 构建，外层那两个名字是同一份 shell 入口（按 `$0` 选程序、按 `uname -m` 选目录）。
  拆开唯一多出来的是两次事务：升级时先换 `zte-mesh` 还是先换 launcher，中间那几秒服务已按新脚本重启过一次。打包细节与那张映射表在
  [LAB](LAB.md) 第 4.3 节，脚本是仓库根的 `tools/build/launcher-arches.sh`（`zte-mesh` 的 `Build/Compile` 调用它，`ZTE_MESH_LAUNCHER_BUILD=` 可指路）。
  `Build/Compile` 这一行不能省：数据包不写它，OpenWrt 会在只有 tarball 的构建目录里跑 `$(MAKE)`，报 `No targets specified and no makefile found`。
- **`src-link` 与 `src-git` 对启动器构建脚本的可见性不一样**：`src-link zte_mesh <克隆>/openwrt` 时包目录是指向克隆的符号链接，
  `$(CURDIR)/../../tools/build/launcher-arches.sh` 就落在克隆根上（CI 用的正是这一种，`.github/workflows/build-apk.yml` 的 Install feeds 一步）；
  `src-git` 会把包**复制**进 `package/feeds/zte_mesh/`，那里没有仓库根的 `tools/`，于是 `Build/Compile` 直接停在一条写明原因的报错上——
  这种构建树要 `make … ZTE_MESH_LAUNCHER_BUILD=~/zte-mesh/tools/build/launcher-arches.sh`。两种情况下构建树里都只有目标机那一档工具链，
  所以 feed 构建出的 apk 通常只带一档；另外两档要宿主的 `gcc-aarch64-linux-gnu` / `gcc-arm-linux-gnueabihf` 在场。
- 大小：合并前那两份 CI 产物是 `zte-mesh` 3 882 850 字节 + `zte-mesh-launcher` 1 221 700 字节（2026-11-05，r21 / launcher r2）；
  **合并后的那一份 `zte-mesh-0.1.0-r22.apk` 是 5 101 814 字节**（`ci/batch21`），比合并前那一份大 1.2 MB，但比两份相加（5 104 550）还小 2 736 字节——**合包在磁盘上不花钱**。
  LuCI 与中文包同一批合计 18 KB。要这个数只为判断 flash，**比对以设备上 `apk info -s zte-mesh` 的读数为准**：装 r22 后两台都读 **10 MiB**
  （合并前 `zte-mesh` r21 单独是 8 034 KiB、launcher r2 是 2 413 KiB；更早文档里那个 8 007 KiB 只覆盖运行环境那一半，别再拿它当合并后的数）。
- 包没签名：`apk add --allow-untrusted`；卸载 `apk del <三个包名>`。运行包含原厂二进制，随这个公开仓库一起发布
  （2026-10-19 决定）；`zte-mesh` 仍是 `PKG_FLAGS:=nonshared`，不进共享软件源。
- **读 apk 的成员要用 SDK 自己编出来的那份 `apk`**（`staging_dir/host/bin/apk --allow-untrusted extract`，解到临时目录再 `find`）：
  `.apk` 是 ADBD/libadb 容器，`tar -t` 换四种压缩标志都回 2，一个成员也列不出来。
- **Actions 的 `run:` 是 `bash -e`**，`var=$(会失败的命令)` 就是这一步的退出码——探针写成 `if cmd; then ...` 才不会带走整步；
  `2>/dev/null` 还会让它一句都不留。r21 第一次跑就栽在这两条上：读成员的 `members=$(tar ...)` 排在构建那一步的尾巴上，
  四个包都编好了（那时 launcher 还是单独一个包），步骤红在第 2 秒且日志是空的。**读包这一步现在单独成步，排在 `Upload packages` 之后**——判定的工具坏了只红一次运行，不吞掉一批能装的包。
- **包里没有 ARM32 模拟器**（2026-10-31 定）：x86_64 设备和没开 `CONFIG_COMPAT` 的 arm64 设备必须自己准备 `qemu-arm-static`——
  放进私有根，或把 `zte_mesh.main.emulator` 指到**这台机器上的任意绝对路径**（init 比 sha256 后拷进私有根的 `usr/bin/`，2026-11-05 起），
  否则 controller 实例每 5 秒以 `execv errno=8` 退出。理由与三条出路见 [LAB.md](LAB.md) 第 4.2 节。

**CI 全部手动触发**（`workflow_dispatch`），push 和 PR 都不会自动跑构建：

| workflow | 产物 | 说明 |
|---|---|---|
| `build-apk.yml` | `zte-mesh-apk`（三个 apk） | `ubuntu-latest` 交叉编译 `zte/zx279133`，断言三个包都在。`zte-mesh` 的编译装 `gcc-aarch64-linux-gnu` + `gcc-arm-linux-gnueabihf` + `musl-tools musl-dev linux-libc-dev`（x86_64 那一份因此由 `musl-gcc` 编，宿主 `cc` 只是它编不出来时的退路——见 [launcher-arches.sh](../tools/build/launcher-arches.sh) 那段内核 UAPI 头的注释），`ZTE_MESH_REQUIRE_ARCHES` 设成三档全要（少编一档直接红），构建完**用 SDK 自己那份 `apk` 解出 `zte-mesh` apk 的成员**：确认 `arch/{aarch64,armv7,x86_64}/{ns-run,mesh-search}` 与两个外层入口脚本都在，用 `file -b` 问每个二进制"你是不是这一档的代码"（光看路径会放过"同一份构建复制三遍"那种装法），再按字节数问它带的是哪份 libc（要 musl 的那两档每份上限 204 800 字节——宿主 `cc` 编的 x86_64 那对是 735 932 与 854 716，通不过；`armv7` 仍是 glibc，只报数不判，`launcher-weights.txt` 六行进 run summary），再确认运行环境那半（`root/bin/db-engine-probe`、`root/lib/libzte_controller_trace.so`、`etc/init.d/zte-mesh`、`usr/libexec/zte-mesh/sta-collect`）没被安装规则漏掉。产物**未签名**，装的时候要 `--allow-untrusted` |
| `build-firmware.yml` | `CONFIG_COMPAT=y` 的固件 | 路由器摆脱模拟器的唯一干净解法 |
| `build-lab-tools.yml` | `prebuilt/*`（提交回当前分支） | 见第 10 节。跑完会自己 commit + push，这是唯一更新 `prebuilt/` 的途径 |

**版本控制**：仓库在 GitHub `Leenshady/luci-app-zte-mesh`，日常只有 `main` 一条长期分支（`feat/child-node` 是给上游提 PR 用的）。
批末的那次 CI 走**一次性分支** `ci/batchN`：`main` 直连 ImmortalWrt feed 不能随便推，所以把这一批推成临时分支再在
那个 ref 上触发，两台测试机装机读回之后就把临时分支删掉（细节见 [PLAN](PLAN.md) 第 6.1 节）。
`out/`、`evidence/`、pcap、`*.i64`、apk、tar.gz 在 `.gitignore` 里，提交前确认没有原厂二进制、证据或密钥混进来。

**取产物**：artifact 下载要认证（匿名 401），本机没有 `gh`，走 `git credential fill` 拿到的 token 拼 API——
token 只留在 shell 变量里，不回显、不落盘。zip 解开是三个 apk，`sha256sum -c SHA256SUMS.txt` 在机器上核对最省事。

**发布 release**：`Build APK` **只构建，不发布**（2026-11-01 起；以前那个 `release_tag` 输入把"开始构建"和"把这些字节承诺给外面"
做成同一个按钮，而一批的包要先在两台测试机上装机验过才谈得上发布）。它产出两个 artifact：`zte-mesh-apk`（三个 apk +
`SHA256SUMS.txt`）和 `build-log`，run summary 里每个文件一行，不下载也能认出这一批是什么。发布是另一次手工动作，用的就是这份产物：

```sh
# 1) 下载 zte-mesh-apk 这个 artifact，解开就是 dist/
# 2) 两台测试机装机验证，核对 sha256sum -c SHA256SUMS.txt
# 3) 成立才发布，tag 打在本次构建的那个提交上
gh release create v0.1.0 dist/*.apk dist/SHA256SUMS.txt
```

- **本机没有 `gh`**，2026-11-05 发 `v0.2.0` 走的是同一套 API 的三次调用：`POST /repos/<owner>/<repo>/releases`（`tag_name` + **`target_commitish` 填本次构建的那个 commit**，tag 由这一步建出来，不用先 `git push` tag）→ 对每个文件 `POST <upload_url>?name=<文件名>`，`Content-Type: application/octet-stream` + `--data-binary @文件` → **回到公开 URL 再下一个副本，在临时目录里 `sha256sum -c SHA256SUMS.txt`**（上传把字节弄坏了只有这一步会发现，而这一句正是 release 说明里让新用户执行的那一句）。`v0.2.0` 的 tag 指 `d9ce19d`（`ci/batch21` 的 tip，也就是那次构建吃的树），**不是**它上面那份只改文档的批末提交。
- workflow 的权限是 `contents: read`：它写不了仓库，也建不了 tag，跑绿了对外什么都不会发生。
- tag 要指对着提交——发布出来的字节和 tag 上的代码必须是同一件事，所以先装机验证、再在**那个**提交上打 tag。
- 包**没有签名**，装的时候要 `apk add --allow-untrusted`；release 说明里也要写清这一条，不然新的人只会看到"装不上"。
- **tag 名与包版本串是两件事**：`v0.2.0` 是 tag，apk 里仍写 `0.1.0-r22`（`PKG_VERSION` 从 2026-10 起没动过，每批只升 `PKG_RELEASE`）。说明第一行就要把这两个数都写出来，否则按 tag 去找文件名的人会以为发错了。
- **同一棵树重编，apk 的 sha 不一定相同，所以"sha 不同"不等于"内容变了"**。2026-11-05 发完 `v0.2.0` 当天在 `main`（`b2c5e4b`，与发布那棵 `d9ce19d` 只差文档）又跑一次 `Build APK`（run #60）：`zte-mesh-0.1.0-r22.apk` **逐字节相同**（`8778aac6…`、5 101 814 B），两个 LuCI 包的容器却变了（`10 206 → 10 207 B`；i18n 大小不变、sha 变了）。把四份推到 `owrt` 上解——**`.apk` 是 libadb 容器，本机与构建机的 `tar -t` 一个成员也列不出来**——`luci-app` 4 个、`i18n` 3 个成员的 sha256 与大小两边**逐一相同**，唯一的区别是成员 mtime 变成新的构建时刻（`17:16:44 +0800` 对 `19:20:07 +0800`）：**时间戳进了 tar 头，内容没变**。两边的差别在时间戳从哪来：`zte-mesh` 解出来的成员全是同一个时刻 `17:13:40`，等于 `git log -1 --format=%ct -- openwrt/zte-mesh/`（`d9ce19d` 的提交时刻，epoch `1793870020`）——**那一侧有时间戳钳制**，两次构建只要那个包目录没动，apk 就一模一样；LuCI 那两个包没有钳制，成员跟每次构建的墙上时钟走（`openwrt/luci-app-zte-mesh/` 的最后一次改动是 `0b25ca1`，可 mtime 不是它）。要比内容就这样读：

  ```sh
  # 测试机上有 apk-tools 3.0.5；--allow-untrusted 不给就是四行 UNTRUSTED signature 加一个空目录
  mkdir -p /tmp/apkcmp/a /tmp/apkcmp/b
  cd /tmp/apkcmp/a && apk --allow-untrusted extract /tmp/apkcmp/one.apk
  cd /tmp/apkcmp/b && apk --allow-untrusted extract /tmp/apkcmp/two.apk
  for d in a b; do (cd /tmp/apkcmp/$d && find . -type f | sort | xargs sha256sum); done
  ```

  **这条对既有判据的意思是**：好几批写的是"某个包与上一批**逐字节相同**"——那句在 `zte-mesh` 上是真有意义的（它的时间戳钳在包目录的最后一次改动上，改了 `openwrt/` 里别的地方不会让它变），但**判"这两份是不是同一批"不能拿 LuCI 那两个包的容器哈希当依据**，它们每次重编都换哈希。钳制发生在 `apk mkpkg` 还是 OpenWrt 按包目录算 `SOURCE_DATE_EPOCH`，没查（`mkpkg` 那一行的原文在 `build.log:4026`）。

## 9. 界面文案与中文翻译

**用词（界面和文档同一套）**：不造词，也不把通用缩写硬翻成中文——`SSID`、`BSSID`、`MAC`、`UCI`、`TLV`、`apk` 这些
保持英文原形，写"SSID"而不是"无线名称/空中名称"。代码里的英文标识符（`live_ap_iface`、`init_seed_from_air`、
`air_matched_plan`、`applier` 这些函数名、键名、日志串）照原样引用并加反引号，不译。中文与英文/数字之间空一格。

| 想说的是 | 就写 | 不要写 |
|---|---|---|
| 代码与日志里的 `air` / `from the air` | 本机当前广播的 SSID | 空中、空中名称 |
| 代码里的 `seed`（服务启动时写进配置的那份） | SSID 初始配置 | 种子、无线种子（哈希的 seed 仍写"种子"） |
| 写 `/etc/config/wireless` 的那段 | `applier`（首次出现说明它干什么） | 落地器 |
| `per-BSS` | 按 BSSID；页面那一列的列名是「BSS 明细」 | 逐路 |
| 某种输入形态 | 情况（段名、字节格式那种写"格式"） | 形状 |
| 把值显示到拓扑页上 | 显示到页面 | 上页 |
| 机器清单、功能总账 | 清单、总账 | 台账 |
| `pin` 某个频段用哪块射频 | 指定 | 点名 |
| 一次改动把 SSID 改掉又改回来 | 改两次、改回去 | 翻两次、翻回去 |
| 主控下发的那份 Wi-Fi 设置 | Wi-Fi 配置、配置参数 | 档案 |
| 每频段那 4 行网络（0 主 Wi-Fi、1 访客、2 备用 SSID、3 隐藏回程）的行号 | 网络序号 | 槽位、slot（代码里的 `slot`/`extra_slots` 照旧） |
| 把配置写进 `/etc/config/wireless` 并让它生效 | 写进本机无线配置、应用、生效 | 落到、落地、落成 |
| 内容有改动才写文件 | 只在确实有改动时写入 | 只写真变化 |
| 一台真实的设备 | **OpenWrt 路由器** 或 **中兴原厂路由器**（按实际那台写）；泛指"得上真实路由器才能验"写 **上机** | 真机、真子路由、真中兴主控 |
| 机器与环境 | 测试主机、测试环境、测试机 | 实验台、实验室、实验机 |
| 那台 ImmortalWrt 虚拟机 | 「ImmortalWrt 虚拟机」（`owrt` 只是 `rssh.py` 的别名） | 把 `owrt` 当成系统名 |
| 机型 | `ZXHN Z506`（又叫中兴小方糖）、`ZXSLC SR1010`（又叫中兴星云 MAX）、小米 AX3600 | "中兴 Z506"、第一次出现只写 `Z506` / `SR1010` |

这套称呼**只读资料也跟着改**（`docs/research/`、`docs/milestones/`、`docs/journal/` 里旧的那些）：改的是称呼，不是结论，
所以不违背"冻结"。存档里当时没写明是哪一台的，泛指用「真实路由器」，不要替它猜型号。

界面上**所有**给用户看的文字都要走 `_()`，msgid 就是那句英文本身（LuCI 用 msgid 当键，没有别名字符串）。改文案时
`.po` 里那条旧 msgid 就失效了——它不会报错，只会安静地变回英文，所以有一条测试盯着这件事。

```sh
python tests/test_i18n.py        # 12 项：覆盖率、格式符、菜单标题、编译成 lmo 后逐条能查到
```

测试做四件事：视图里每个 `_()` 都在 `.po` 里有译文；`.po` 里没有源码已不再用的条目；`%s`/`%d` 在译文里个数与顺序一致；
编成 lmo 后每条 msgid 都能按哈希查到译文。加新文案的流程：写 `_('...')` → 把同一句英文抄进
`openwrt/luci-app-zte-mesh/po/zh_Hans/zte-mesh.po` 并翻译 → 跑上面的测试。

**它怎么到设备上**：`luci.mk` 把 `po/<lang>/` 编成独立的包 `luci-i18n-<app>-<lang>`，文件写到
`/usr/lib/lua/luci/i18n/zte-mesh.zh-cn.lmo`；`zh_Hans` 经 `LUCI_LC_ALIAS` 映射成 `zh-cn`。装应用包**不会**带上它——
`build-apk.yml` 因此单独编译它，并断言三个 apk 都在。页面从 `/cgi-bin/luci/admin/translations/<lang>` 拿到所有
`<lang>.lmo` 拼成的 JS，`cbi.js` 的 `_()` 查的就是它——所以"源码里有译文"和"设备上有 lmo"是两件事，缺一件页面就是英文。

手工装到运行中的设备（测试环境里的路由器是拷文件而不是装包）：

```sh
python tools/lab/po2lmo.py openwrt/luci-app-zte-mesh/po/zh_Hans/zte-mesh.po /tmp/zte-mesh.zh-cn.lmo
cp /tmp/zte-mesh.zh-cn.lmo /usr/lib/lua/luci/i18n/     # 不需要重启服务，刷新页面即可（Ctrl+Shift+R 绕过缓存）
```

`tools/lab/po2lmo.py` 不是另写一套格式：按 `modules/luci-base/src/po2lmo.c` 与 `lib/lmo.c` 实现（字符串块 + 16 字节索引 +
结尾块长度，键用 SuperFastHash 且以长度作种子），只用来给没有构建机的设备补装；真正的包仍由构建树的 `po2lmo` 生成。

语言怎么选：LuCI「系统 → 语言和界面」里选简体中文（写进 `luci.main.lang`），或保持 `auto` 让 `Accept-Language` 决定。
中文目录只翻这一页。

## 10. 分支与 `prebuilt/`

仓库只有 `main` 一条分支，1.7 MB 的 `prebuilt/` 是 lab 主机用的宿主二进制：

| 文件 | 是什么 | 谁用 |
|---|---|---|
| `db-engine-probe` | ARM32、`-nostdlib` 链原厂 glibc 2.26 的沙箱宿主程序 | **产线**：`repack-runtime.py --replace` 把它塞进 `zte-mesh-runtime.tar.gz` 的 `root/bin/`，随 apk 分发 |
| `ns-run-x86_64` / `ns-run-aarch64` | 两份宿主架构的沙箱启动器 | 只剩 lab 主机 `wget` 取（包里按 CPU 各带一份，[LAB](LAB.md) 第 4.3 节） |
| `libzte_mesh_shim.so` / `libzte_agent_mm.so` | ARM32 的原厂符号 shim | lab 里挂给 probe 用 |
| `*.sha256`、`*-build.log` | 校验和与构建日志 | 追溯（构建失败也发布日志，这是无 token 时读失败日志的唯一通道） |

**它为什么存在**：`db-engine-probe` 是 ARM32、`-nostdlib`、要对准原厂 glibc 2.26，**在 mac/Windows/路由器上都编不了**，
只能在 CI 的 `debian:bookworm` 容器里用 `arm-linux-gnueabi-` 交叉编。所以流通路径是
`build-lab-tools.yml`（容器编）→ `prebuilt/`（提交回仓库）→ `repack-runtime.py --replace` → tarball → apk。
`build-apk.yml` 完全不碰 `prebuilt/`，它只通过 tarball 间接影响 ARM32 那一半。

**怎么更新**：手动跑 `Build lab tools`，它编完自己 `git add -f prebuilt/ && commit && push`；然后在 mac 上
`python3 openwrt/repack-runtime.py --replace root/bin/db-engine-probe=prebuilt/db-engine-probe`（参数见脚本头部注释）
重新生成 tarball 并提交。漏了这步就会被 `tests/test_runtime.py` 拦下——它断言 tarball 里那份必须带 `config-reject` 与
`wlan-plan` 两个标记，这两个正是 C6 那次"冻结的副本不认 `mesh.role=slave`"的特征串。

**历史（不要再重建双分支）**：`prebuilt/` 原先挂在 `lab-tools` 分支上，因为设备靠
`raw.githubusercontent.com/<owner>/<repo>/lab-tools/prebuilt/<file>` 直链 `wget`（Actions artifact 要登录且会过期），
而那个 workflow 每次触发都会把产物提交回分支，不能落在 `main` 上。代价是 `main` 落后 47 个提交、apk CI 一直编的是旧代码，
且 lab-tools 侧的文件逐渐比 `main` 旧。现在 CI 全手动、直链改指 `main`，两个前提都没了，所以分支已合并、只剩 `main`。
下次需要"只有某台机器用"的产物，先问它能不能用现编的方式解决，不要再开长期分支。批末 CI 那个 `ci/batchN`
不算在内——它不生成任何产物，装机读完就删。
