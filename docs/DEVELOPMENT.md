# 开发、构建、测试与真机联调

架构见 [ARCHITECTURE.md](ARCHITECTURE.md)，服务配置语法见 [REAL_CHILD.md](REAL_CHILD.md) 第 3 节，脚本见 [tools/README.md](../tools/README.md)。

## 1. 机器与连接

| 角色 | 怎么连 | 说明 |
|---|---|---|
| mac（构建机） | `ssh 192.168.167.93` | 主要工作副本 `/Volumes/code/sr1010-mesh`。编译在离线 Docker 镜像 `sr1010-mesh-shim-builder` 里做；`out/`、`evidence/` 只在这里 |
| 开发机 | — | 克隆后 `git push`；mac 仓库设了 `receive.denyCurrentBranch updateInstead`，推送直接更新工作区 |
| SR1010 路由器 | 从 mac：`ssh root@192.168.5.1` | 实验室脚本默认 `--host root@192.168.5.1 --port 22`。没有 python3、没有 `pkill`，只有 BusyBox 和 `tcpdump` |
| 真子路由 ZXHN Z506 | 从 mac：telnet `192.168.5.216:23`（`tools/child/ztetn.py` 默认值，可用 `--host`/`CHILD=` 改） | 口令只从 `ZTE_TN_PASS` 读（用户名 `ZTE_TN_USER`，默认 root），**不要写进任何文件** |
| OpenWrt 构建机 | `ssh -p 2022 192.168.223.19`，树 `~/workspace/immortalwrt` | 只用来编包（第 8 节） |
| 实验室虚拟机 | `ssh zzb@10.23.90.131`（x86_64 Ubuntu 22.04） | 没 mac、没路由器、没原厂镜像时也能编译、跑主机测试、在沙箱里跑服务（qemu 模拟 ARM32）。见 [LAB.md](LAB.md) |

## 2. 构建

在 mac 上（`orbctl start` 先把 Docker 拉起来）：

| 命令 | 做什么 |
|---|---|
| `sh build.sh` | 完整流程：从固件提取并审计运行环境（`prepare.py`，需要 `unsquashfs`）、核对表结构证据、建镜像、`make all database controller test-host`、离线加载测试 |
| `sh tools/build/container-make.sh <目标>...` | 在容器里编指定产物（`db-engine-probe`、`zte-meshctl`、`ns-run`、`libzte_controller_trace.so` 等）或跑 `test-host`、`controller`。会在容器里先删旧产物，保留 make 的退出码 |
| `sh tools/build/runtime.sh svctest` | 实验室运行包 `out/zte-mesh-svctest.tar.gz`（启动器根是 `/tmp/zte-mesh-svc/root`，路由器上只写 tmpfs） |
| `sh tools/build/runtime.sh install` | 包用的运行包 `openwrt/zte-mesh/zte-mesh-runtime.tar.gz`（随仓库提交，根是 `/usr/lib/zte-mesh/root`） |

- 原厂镜像默认读 `../zx279133/vendor-reference/2b5/rootfs.jffs2`，`SR1010_VENDOR_ROOTFS` 可改。重复准备会核对已有文件哈希。
- **在 mac 上不要原地覆盖源码文件再编译**：容器经 virtiofs 读源码，变短的文件报 `is shorter than expected`，变长的可能编出
  新旧混合的东西。用 git 更新，或先写临时名再 `mv`。产物同理，所以 `container-make.sh` 在容器里删。
- 运行包内有 `SHA256SUMS` 自校验，`make-runtime.py` 还会审计 ARM32 ABI（强符号、GNU 符号版本）。
- x86_64 主机可用 `Dockerfile.cross`（`make ROUTERCC=aarch64-linux-gnu-gcc`）交叉编译。

## 3. 跑控制器

三种方式，都在 mac 上执行：

1. **实验室（不碰子节点）**：`python3 dev-service-smoke.py --no-attach --seconds 24`——路由器私有命名空间里跑服务，
   验证配置、热更新、守护进程重启，不把线接进 `br-lan`。`dev-config-check.py DIR` 批量喂配置看接受/拒绝，
   `dev-sched-check.py` 用真实时钟测定时窗口和访客限时（约 5 分钟）。
2. **真子节点**：`python3 tools/capture/mreplay.py --config <0600 配置> --expect-macs <允许的 MAC> --out <证据目录> --seconds N`。
   接进 `br-lan`，触发子节点重新入网（默认把子节点端口 down/up），抓两侧的包、服务日志和 `state.json`；`--swap 秒:文件` 中途换配置，
   `--action '秒:reboot <mac>'` 或 `'秒:light <mac> 0|1'` 发节点动作，`--al-last` 每次换一个控制器 AL MAC。
3. **已安装的包**：`uci set zte_mesh.main.enabled=1` 等，`/etc/init.d/zte-mesh start`；状态 `ubus call luci.zte-mesh status`。
   只验证 procd/rpcd 而不碰子节点时，建一个空网桥（`ip link add br-zmtest type bridge`），把 `zte_mesh.main.bridge` 指过去。

- 配置文件里有 Wi-Fi 密钥：放仓库外、0600、跑完删除（证据目录里也不要留）。
- 服务运行目录：配置 `/run/zte-mesh/config`、状态 `state.json`、节点动作 `actions/`、子节点摘要 `peers/`。
- 调试日志级别（原厂守护进程）：`sendcmd multiapd.acd.acd -l 8`（还有 `base`、`topo`、`slave`），重启后复位，结束用 `-l 7` 还原。

## 4. 测试

- 主机测试全在容器里：`sh tools/build/container-make.sh test-host`（C 编解码含 10 万次变异、证据处理、表结构、策略、profile、
  启动视图、init 87 条、rpcd 15 条、preload、沙箱）。看到 `make: *** ... Error` 就是失败，退出码非 0。
- LuCI 页面的 JS 测试：`node tests/test_luci.js`（模拟 LuCI 的 `form`/`uci`/`rpc`；mac 上没有 node，在开发机上跑）。
- 单个 Python 测试按脚本方式跑，第一个参数是产物目录：`python3 tests/test_schema.py out`（`unittest discover` 不适用）。
- 不依赖 `out/` 的三组可以直接在本机跑：`sh tests/test_init.sh`、`python tests/test_runtime.py`、`python tests/test_i18n.py`。
- 容器把 `/tmp` 挂成 noexec：测试不能执行自己写到 `/tmp` 的脚本（`tests/test_rpcd.sh` 用 shell 函数和 `sh -c`），
  dash 的 `.` 不接受额外参数（先 `set --`）。

## 5. 真机测试守则

向真机推配置会真实修改并持久化它的设置。

- **每轮前后读回子节点基线**：`ZTE_TN_PASS=… sh tools/child/childread.sh`，8 行 `WLANCfg` 与 [REAL_CHILD.md](REAL_CHILD.md)
  末尾的基线逐行比较。改槽位的实验先在实验室验证（`device-controller-smoke.py` 的 `map_teardown` 必须为 0），真机轮询
  子节点 `WLANCfg`，一出异常立刻用"全部槽位 + 一次 Renew"恢复。
- **永远不要向真机推占位值。** 额外 BSS 的 `enabled/ssid/key` 必须三项给齐；同一无线电的槽位必须是 1..k。
- **不要改子路由的模式**（`Mode`、自组网 V2、`VersionMode`、MESH 开关），这些由用户在子路由上设置。
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

1. 开发机提交并推送 → mac 上 `container-make.sh` 编改动的产物（零警告）→ `test-host` 通过 → `runtime.sh svctest`。
2. 实验室：`dev-service-smoke.py --no-attach`；涉及配置解析的跑 `dev-config-check.py`；涉及调度的跑 `dev-sched-check.py`。
3. 真子节点（需要时）：`childread.sh` 基线 → `mreplay.py` → 看 `state.json` 节点 `model:"ZXHN Z506"`、`zte:true`、
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
# .config 里加 CONFIG_PACKAGE_{zte-mesh,zte-mesh-launcher,luci-app-zte-mesh,luci-i18n-zte-mesh-zh-cn}=m，再 make defconfig
make package/feeds/zte_mesh/zte-mesh/compile package/feeds/zte_mesh/zte-mesh-launcher/compile \
     package/feeds/zte_mesh/luci-app-zte-mesh/compile \
     package/feeds/zte_mesh/luci-i18n-zte-mesh-zh-cn/compile   # 翻译是独立的包，不编译它就没有中文
```

- 包直接解包同目录下提交的 `zte-mesh-runtime.tar.gz`，不需要往 `dl/` 放东西；`ZTE_MESH_RUNTIME=` 可指向别的运行包。
- **两个包目录是必需的**：`package-pack.mk` 的 `Architecture` 读的是 Makefile 级全局 `PKGARCH`，一个 Makefile 给不了两个架构。
  `zte-mesh` = `arch: all`（脚本、LuCI、rpcd、配置 + 运行时 tarball），`zte-mesh-launcher` = `aarch64_cortex-a53`
  （用 `$(TARGET_CC)` 现编 `ns-run` 与 `mesh-search`）。数据包必须显式写空 `Build/Compile`，否则 OpenWrt 会在只有 tarball 的
  构建目录里跑 `$(MAKE)` 报 `No targets specified and no makefile found`。
- 大小：`zte-mesh` 4.1 MB（装上占 jffs2 约 4.9 MB，解压约 8 MB，大头是原厂库），LuCI 与中文包合计约 11 KB。
- 包没签名：`apk add --allow-untrusted`；卸载 `apk del <四个包名>`。运行包含原厂二进制，随这个公开仓库一起发布
  （2026-10-19 决定）；`zte-mesh` 仍是 `PKG_FLAGS:=nonshared`，不进共享软件源。
- **包里没有 ARM32 模拟器**（2026-10-31 定）：x86_64 设备和没开 `CONFIG_COMPAT` 的 arm64 设备必须自己把 `qemu-arm-static`
  放进私有根，否则 controller 实例每 5 秒以 `execv errno=8` 退出。理由与三条出路见 [LAB.md](LAB.md) 第 4.2 节。

**CI 全部手动触发**（`workflow_dispatch`），push 和 PR 都不会自动跑构建：

| workflow | 产物 | 说明 |
|---|---|---|
| `build-apk.yml` | `zte-mesh-apk`（四个 apk） | `ubuntu-latest` 交叉编译 `zte/zx279133`，断言四个包都在。产物**未签名**，装的时候要 `--allow-untrusted` |
| `build-firmware.yml` | `CONFIG_COMPAT=y` 的固件 | 真机摆脱模拟器的唯一干净解法 |
| `build-lab-tools.yml` | `prebuilt/*`（提交回当前分支） | 见第 10 节。跑完会自己 commit + push，这是唯一更新 `prebuilt/` 的途径 |

**版本控制**：仓库在 GitHub `Leenshady/luci-app-zte-mesh`，只有 `main` 一条分支。`out/`、`evidence/`、pcap、`*.i64`、apk、tar.gz
在 `.gitignore` 里，提交前确认没有原厂二进制、证据或密钥混进来。

**发布 release**：`Build APK` 是唯一出包的地方，但它的 artifact 不是分发渠道——下载要登录 GitHub、会过期、还是一包 zip。
要发布就在 dispatch 表单里填 `release_tag`（`v` 开头，如 `v0.1.0`），workflow 会在**本次构建的那个提交**上建 tag，上传四个
apk 加 `SHA256SUMS.txt`，并按上一个 tag 生成"本版提交"清单：

```
Actions → Build APK → Run workflow → release_tag: v0.1.0
# 发布后：https://github.com/Leenshady/luci-app-zte-mesh/releases/tag/v0.1.0
```

- 留空 = 只构建，产物仍只在 artifact 里（默认行为，与以前一样）。
- 同一 tag 重跑会覆盖资产（`--clobber`），不会因"已存在"失败。
- tag 已存在但指向别的提交时**直接失败**——避免发布出来的字节和 tag 上的代码不是一回事。
- 需要 `contents: write`（workflow 顶部已声明），否则要等 20 多分钟构建完才在最后一步报 `Resource not accessible`。
- 包**没有签名**，release 说明里已写 `apk add --allow-untrusted`。

## 9. 界面文案与中文翻译

界面上**所有**给用户看的文字都要走 `_()`，msgid 就是那句英文本身（LuCI 用 msgid 当键，没有别名字符串）。改文案时
`.po` 里那条旧 msgid 就失效了——它不会报错，只会安静地变回英文，所以有一条测试盯着这件事。

```sh
python tests/test_i18n.py        # 12 项：覆盖率、格式符、菜单标题、编译成 lmo 后逐条能查到
```

测试做四件事：视图里每个 `_()` 都在 `.po` 里有译文；`.po` 里没有源码已不再用的条目；`%s`/`%d` 在译文里个数与顺序一致；
编成 lmo 后每条 msgid 都能按哈希查到译文。加新文案的流程：写 `_('...')` → 把同一句英文抄进
`openwrt/luci-app-zte-mesh/po/zh_Hans/zte-mesh.po` 并翻译 → 跑上面的测试。

**它怎么到设备上**：`luci.mk` 把 `po/<lang>/` 编成独立的包 `luci-i18n-<app>-<lang>`，文件落到
`/usr/lib/lua/luci/i18n/zte-mesh.zh-cn.lmo`；`zh_Hans` 经 `LUCI_LC_ALIAS` 映射成 `zh-cn`。装应用包**不会**带上它——
`build-apk.yml` 因此单独编译它，并断言四个 apk 都在。页面从 `/cgi-bin/luci/admin/translations/<lang>` 拿到所有
`<lang>.lmo` 拼成的 JS，`cbi.js` 的 `_()` 查的就是它——所以"源码里有译文"和"设备上有 lmo"是两件事，缺一件页面就是英文。

手工装到运行中的设备（实验室的路由器是拷文件而不是装包）：

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
| `ns-run-x86_64` / `ns-run-aarch64` | 两份宿主架构的沙箱启动器 | 只剩 lab 主机 `wget` 取（包里是现编的） |
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
下次需要"只有某台机器用"的产物，先问它能不能用现编的方式解决，不要再开长期分支。
