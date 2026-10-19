# 开发、构建、测试与真机联调

架构见 [ARCHITECTURE.md](ARCHITECTURE.md)，服务配置语法见 [REAL_CHILD.md](REAL_CHILD.md) 第 3 节，脚本见 [tools/README.md](../tools/README.md)。

## 1. 机器与连接

| 角色 | 怎么连 | 说明 |
|---|---|---|
| mac | `ssh 192.168.167.93`（mac 自己的 Wi-Fi，与路由器无关） | **仓库正本** `/Volumes/code/sr1010-mesh`（git，分支 `main`，没有远端）。编译在这里的离线 Docker 镜像 `sr1010-mesh-shim-builder` 里做；`out/`、`evidence/` 只在这里 |
| 开发机 | — | 克隆 `~/code/sr1010-mesh`（`git clone 192.168.167.93:/Volumes/code/sr1010-mesh`）。改完提交后 `git push`：mac 仓库设了 `receive.denyCurrentBranch updateInstead`，推送直接更新 mac 的工作区 |
| SR1010 路由器（主线 ImmortalWrt） | 从 mac：`ssh root@192.168.5.1`（LAN 侧，端口 22） | 实验室脚本默认 `--host root@192.168.5.1 --port 22`，都在 mac 上运行。路由器上没有 python3、没有 `pkill`，只有 BusyBox 和 `tcpdump` |
| 真子路由 ZXHN Z506 | 从 mac：telnet `192.168.5.216:23`（`tools/child/ztetn.py` 的默认值；地址由当时发 DHCP 的固件决定，路由器跑原厂固件时是 `192.168.5.3`，用 `--host` 或 `childread.sh` 的 `CHILD=` 改） | 口令只从环境变量 `ZTE_TN_PASS` 读（用户名 `ZTE_TN_USER`，默认 root），**不要写进任何文件** |
| 构建机 | `ssh -p 2022 192.168.223.19`，ImmortalWrt 树 `~/workspace/immortalwrt` | 只用来编 OpenWrt 包（第 8 节） |

## 2. 构建

在 mac 上（`orbctl start` 先把 Docker 拉起来）：

| 命令 | 做什么 |
|---|---|
| `sh build.sh` | 完整流程：从固件提取并审计运行环境（`prepare.py`，需要 `unsquashfs`）、核对表结构证据、建镜像、`make all database controller test-host`、离线加载测试 |
| `sh tools/build/container-make.sh <目标>...` | 在容器里编指定产物（`db-engine-probe`、`zte-meshctl`、`ns-run`、`runtime/lib/libzte_controller_trace.so` 等）或跑 `test-host`、`controller`。会先在容器里删掉旧产物，保留 make 的退出码 |
| `sh tools/build/runtime.sh svctest` | 实验室运行包 `out/zte-mesh-svctest.tar.gz`（启动器的根是 `/tmp/zte-mesh-svc/root`，路由器上只写 tmpfs） |
| `sh tools/build/runtime.sh install` | OpenWrt 包用的运行包 `out/zte-mesh-runtime.tar.gz`（根是 `/usr/lib/zte-mesh/root`） |

- 原厂固件镜像默认读 `../zx279133/vendor-reference/2b5/rootfs.jffs2`，`SR1010_VENDOR_ROOTFS` 可以改。重复准备会核对已有文件的哈希，不静默替换。
- **在 mac 上不要原地覆盖源码文件再编译**：容器经 virtiofs 读源码，原地变短的文件会报 `is shorter than expected`，变长的可能编出新旧混合的东西。
  用 git 更新（开发机推送）或先写临时名再 `mv`。产物同理：容器外删产物会让容器里看到旧缓存，所以 `container-make.sh` 在容器里删。
- 运行包内有 `SHA256SUMS` 自校验，`make-runtime.py` 还会审计 ARM32 ABI（强符号、GNU 符号版本）。
- x86_64 主机可用 `Dockerfile.cross`（`make ROUTERCC=aarch64-linux-gnu-gcc`）交叉编译。

## 3. 跑控制器

三种方式，都在 mac 上执行：

1. **实验室（不碰子节点）**：`python3 dev-service-smoke.py --no-attach --seconds 24`。路由器私有命名空间里跑服务，验证配置、热更新、守护进程重启，
   不把线接进 `br-lan`。`dev-config-check.py DIR` 批量喂配置看接受/拒绝，`dev-sched-check.py` 用真实时钟测定时窗口和访客限时（约 5 分钟）。
2. **真子节点**：`python3 tools/capture/mreplay.py --config <0600 配置> --expect-macs <允许的 MAC> --out <证据目录> --seconds N`。
   接进 `br-lan`，触发子节点重新入网（默认把子节点端口 down/up），抓两侧的包、服务日志和 `state.json`；`--swap 秒:文件` 中途换配置，
   `--action '秒:reboot <mac>'` 或 `'秒:light <mac> 0|1'` 发节点动作，`--al-last` 每次换一个控制器 AL MAC。
3. **已安装的 OpenWrt 包**：`uci set zte_mesh.main.enabled=1` 等，`/etc/init.d/zte-mesh start`；状态 `ubus call luci.zte-mesh status`。
   只想验证 procd/rpcd 而不碰子节点时，建一个空网桥（`ip link add br-zmtest type bridge`），把 `zte_mesh.main.bridge` 指过去。

- 配置文件里有 Wi-Fi 密钥：放在仓库外，0600，跑完删除（证据目录里也不要留）。
- 服务运行目录：配置 `/run/zte-mesh/config`、状态 `/run/zte-mesh/state.json`、节点动作 `/run/zte-mesh/actions/`、子节点摘要 `/run/zte-mesh/peers/`。
- 调试日志级别（原厂守护进程）：`sendcmd multiapd.acd.acd -l 8`（还有 `base`、`topo`、`slave`），重启后复位，结束用 `-l 7` 还原。

## 4. 测试

- 主机测试全在容器里：`sh tools/build/container-make.sh test-host`（C 编解码含 10 万次变异、证据处理、表结构、策略、profile、启动视图、
  init 64 条、rpcd 15 条、preload、沙箱）。看到 `make: *** ... Error` 就是失败，退出码非 0。
- LuCI 页面的 JS 测试：`node tests/test_luci.js`（模拟 LuCI 的 `form`/`uci`/`rpc`；mac 上没有 node，在开发机上跑）。
- 单个 Python 测试按脚本方式跑，第一个参数是产物目录：`python3 tests/test_schema.py out`（`unittest discover` 不适用）。
- 容器把 `/tmp` 挂成 noexec：测试不能执行自己写到 `/tmp` 的脚本（`tests/test_rpcd.sh` 用 shell 函数和 `sh -c`），dash 的 `.` 不接受额外参数（先 `set --`）。

## 5. 真机测试守则

向真机推配置会真实修改并持久化它的设置。

- **每轮前后读回子节点基线**：`ZTE_TN_PASS=… sh tools/child/childread.sh`，8 行 `WLANCfg` 与 [REAL_CHILD.md](REAL_CHILD.md) 里的基线逐行比较。
  改槽位的实验先在实验室验证（`device-controller-smoke.py` 的 `map_teardown` 必须为 0），真机运行时轮询子节点 `WLANCfg`，一出异常立刻用“全部槽位 + 一次 Renew”恢复。
- **永远不要向真机推占位值。** 额外 BSS 的 `enabled/ssid/key` 必须三项给齐（代码强制）；同一无线电的槽位必须是 1..k（M2 里的 BSS 按位置对应）。
- **不要改子路由的模式**（`Mode`、自组网 V2、`VersionMode`、MESH 开关），这些由用户在子路由上设置；用户明确要求时才配合。
- **接入保护**：网桥转发表里出现不在 `--expect-macs` 里的 MAC 就拒绝接入。长期放行：控制器主机 `d0:11:e5:c5:cb:4a`、子路由 `d4:b7:09:0d:77:30`、
  一加 `32:83:90:d9:0f:62`、iPhone `aa:8b:bc:9f:1f:c7`、Mac `aa:c9:63:3e:7e:70`、用户电脑 `e8:bf:b8:9b:40:a1`（`cnjn-Code01-Ver2-1`，在子节点后面）。
  新出现的 MAC 先看 `/tmp/dhcp.leases` 确认是普通客户机，不确定就问用户。一加还会用私有 MAC `42:be:ab:fd:c3:88`（不在长期名单里）。
- 会让用户设备掉线的测试（关主 SSID、ACL 封禁真实终端、信道/功率、改 SSID、定时 `scope=all`）必须先得到用户同意，测完立即还原。
  用户的电脑可能就接在子节点后面。
- 还原办法：信道先发原信道再发 `0`（自动）；SSID 被重配后掉到隐藏默认名时，改成别的名字再改回；ACL 删光所有 `acl.N.*`；功率设回最大档。
- 子路由原始值备份在 mac 的 `evidence/build-mesh/config_z506.xml`（含明文密钥，不外传）。
- 不要在局域网里有别的真实 mesh 节点时把沙箱线接到 `br-lan` 用测试配置。

## 6. 踩过的坑

- **守护进程环境里只能有一个变量**（`ZTE_MESH_START_COUNT`）：多一个 `setenv` 原厂 ld.so 就断言失败（`dl-minimal.c: 126: realloc`，exit 127）。给 preload 库传状态走数据库。
- **路由器没有 `pkill`**：脚本按命令行找进程再杀（`mreplay.py` 曾经每轮漏两个 `tcpdump`，攒到 66 个、占 130 MB 内存）。每轮后看 `ps | grep tcpdump`。
- **不要用 `kill multiapd` 逼子路由重新入网**：反复这样做后子路由日志出现 `restart unexpected N times`、`SubScribPublish ... already exist`，`cspd` 收不到上线通知（FINDINGS 3.4）。
  改用重启控制器、`zte-meshctl renew-acd <band>`、`mreplay.py` 的端口 down/up，或重启子路由。
- **不要自己发 ACD 事件 2**：守护进程在收到第一个 1905 帧时自己发，提前发一次会用掉一次性的 `g_AcdsRenewStat`。
- **`pkill -f "<模式>"` 会杀掉自己**（命令行里含同样字符串的 shell 也匹配）：用 `[.]` 之类的写法。
- **zsh**：变量不自动分词；以 `=====` 开头的词会被当成命令展开。
- **子路由 telnet 会话不能叠加**；两个 `sendcmd` 同时跑会串话。子路由重启会清掉 `/var/tmp/busybox`，自带的只有 `grep tr cut sed awk pgrep wget tftp`。
- 子路由行缓冲超过约 1 KB 会截断：长内容按 500 字符分块 base64 发（`tools/capture/vt.sh` 的做法）。
- `iwconfig` 在刚重启或已关闭的接口上读回的 SSID 不可靠；短时间连续重配 2.4G 会掉到隐藏默认 SSID。
- 测试路由器没有授时，时钟可能差十几个小时：定时窗口按路由器 `date -u` 算。

## 7. 改了控制器代码之后的检查

1. 开发机提交并推送 → mac 上 `container-make.sh` 编改动的产物（零警告）→ `test-host` 通过 → `runtime.sh svctest`。
2. 实验室：`dev-service-smoke.py --no-attach`；涉及配置解析的跑 `dev-config-check.py`；涉及调度的跑 `dev-sched-check.py`。
3. 真子节点（需要时）：`childread.sh` 基线 → `mreplay.py` → 看 `state.json` 节点 `model:"ZXHN Z506"`、`zte:true`、`online:true` → `childread.sh` 读回。
4. 结束：路由器无残留（`/run/zte-mesh`、`/tmp/zte-mesh-svc`、`zmesh0`、`ns-run`、`db-engine-probe`、`tcpdump`）；mac 上含密钥的配置已删。

## 8. OpenWrt 包与版本控制

**feed**：本仓库是 ImmortalWrt 的一个 feed，包在 `openwrt/` 下（`zte-mesh`、`luci-app-zte-mesh`，中文翻译包 `luci-i18n-zte-mesh-zh-cn`
由 `luci.mk` 自动生成）。构建机的 ImmortalWrt 树（25.12，目标 `zte/zx279133`，apk 包管理，分支 `sr1010-zx279133`）里登记了两处：

| 文件 | 内容 | 用途 |
|---|---|---|
| `feeds.conf.default`（已提交） | `src-git zte_mesh https://github.com/cnjn/zte-mesh.git` | 别人拉树时从 GitHub 取（要有仓库的访问权限；没钉版本，每次 `feeds update` 取最新的 `main`） |
| `feeds.conf`（不受 git 跟踪，存在时优先） | `src-link zte_mesh /home/cnjn/workspace/zte-mesh/openwrt` | 构建机上直接用本地克隆，改代码不用重新拉 |

**打包**：

```sh
# mac：生成运行包（含原厂二进制，不在 feed 里）
sh tools/build/runtime.sh install                       # -> out/zte-mesh-runtime.tar.gz
# 构建机
cd ~/workspace/immortalwrt
./scripts/feeds update zte_mesh
./scripts/feeds install -a -p zte_mesh                  # -> package/feeds/zte_mesh/{zte-mesh,luci-app-zte-mesh}
cp zte-mesh-runtime.tar.gz dl/                          # 从 mac 拷来，用完删掉
# .config 里加 CONFIG_PACKAGE_zte-mesh=m、CONFIG_PACKAGE_luci-app-zte-mesh=m、CONFIG_PACKAGE_luci-i18n-zte-mesh-zh-cn=m，再 make defconfig
make package/feeds/zte_mesh/zte-mesh/compile package/feeds/zte_mesh/luci-app-zte-mesh/compile
# 产物：bin/targets/zte/zx279133/packages/zte-mesh-*.apk（nonshared，按目标放），
#       bin/packages/aarch64_cortex-a53/zte_mesh/luci-*zte-mesh*.apk（按 feed 名放）
```

- `dl/zte-mesh-runtime.tar.gz` 不在时，选了这些包的整树编译会在 `zte-mesh` 解包这一步失败。不编这些包时就别在 `.config` 里选它们。
- 大小：`zte-mesh` 4.1 MB（装上后占 jffs2 约 4.9 MB，解压约 8 MB，大头是原厂库），LuCI 和中文包合计约 11 KB。
- 包没签名：`apk add --allow-untrusted`。卸载：`apk del luci-i18n-zte-mesh-zh-cn luci-app-zte-mesh zte-mesh`。
- 运行包含原厂二进制，`zte-mesh` 是 `PKG_FLAGS:=nonshared` 的私有包，不进公开的软件源。

**版本控制**：仓库在 GitHub `cnjn/zte-mesh`；mac 上的 `/Volumes/code/zte-mesh` 是主要工作副本，构建机的 `~/workspace/zte-mesh` 是 feed 用的克隆。
`out/`、`evidence/`、pcap、`*.i64`、apk、tar.gz 在 `.gitignore` 里，提交前确认没有原厂二进制、证据或密钥混进来。
