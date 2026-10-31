# 实验室虚拟机（x86_64 Ubuntu）

在一台普通 x86_64 Ubuntu 上编译并运行这套代码，不需要 mac、不需要路由器、不需要原厂固件镜像。
沙箱、ARM32 交叉编译、`qemu-user` 模拟、控制器服务启动都已验证可用。

架构与运行原理见 [ARCHITECTURE.md](ARCHITECTURE.md)，真机流程见 [DEVELOPMENT.md](DEVELOPMENT.md)。本文只讲这台机器。

## 1. 这台机器能做什么、不能做什么

| 能做 | 说明 |
|---|---|
| 交叉编译全部产物 | `arm-linux-gnueabi-gcc` 编 ARM32（soft-float），`aarch64-linux-gnu-gcc` 编路由器侧静态工具 |
| 跑 ARM32 程序 | 内核对 ARM ELF 走 `binfmt_misc` → `qemu-arm-static`（`F` 标志让它跨 chroot 也有效） |
| 跑服务 | `ns-run` 的私有 IPC/网络/挂载命名空间在普通 VM 里工作正常，服务能完成启动序列 |
| 跑主机测试 | 13 个测试步骤里 10 个通过，其余 4 个只缺 `out/analysis/cspd`（见第 6 节） |

| 不能做 | 原因 |
|---|---|
| 生成可发布的运行包 | `openwrt/make-runtime.py` 会调 `prepare.verify_existing()`，它拿 `rootfs_sha256` 卡住，合成出来的 `out/` 一定被拒（这是有意的门槛） |
| `test_schema` / `test_policies` / `test_profiles` / `test_startup` | 这四个要 `out/analysis/cspd`，只有原厂 JFFS2 里有 |
| 真机/真子路由联调 | 没有网桥、没有 1905 对端，也没有 `192.168.5.x` |

## 2. 一次性准备

```sh
sudo apt-get update
sudo apt-get install -y --no-install-recommends \
  build-essential make git rsync curl ca-certificates file \
  gcc-arm-linux-gnueabi binutils-arm-linux-gnueabi libc6-dev-armel-cross \
  gcc-aarch64-linux-gnu libc6-dev-arm64-cross \
  qemu-user-static binfmt-support python3

sudo modprobe veth        # ns-run 要求 /sys/module/veth 存在，否则报 private-service-network
```

`qemu-user-static` 会注册 `binfmt_misc`；`ns-run` 走 binfmt 执行 ARM32 程序，所以这一步不能省。
检查：`cat /proc/sys/fs/binfmt_misc/qemu-arm` 应有 `flags: PF`。

不需要 Docker。文档里的 `container-make.sh` 在这台机器上用不了（docker 守护进程没起），用 `tools/lab/build.sh` 代替。

## 3. 日常流程

```sh
# 从 Windows/mac 工作区同步（本地有改动就同步一次）
LAB_HOST=10.23.90.131 LAB_USER=zzb LAB_PASS=… python3 tools/lab/push.py

# 首次或删过 out/ 之后：从运行包重建 out/runtime + manifest.json
python3 tools/lab/bootstrap-out.py

# 编译（ROUTERCC=cc 让 ns-run/exec-check 能在本机跑；要打包时才用 aarch64）
sh tools/lab/build.sh all database
sh tools/lab/build.sh ROUTERCC=cc out/ns-run out/ns-run-svctest

# 测试
sh tools/lab/build.sh test-host

# 跑服务（需要 root：unshare + bind mount）
sudo sh tools/lab/run-service.sh --seconds 20
```

在 VM 上远程执行的方法（不需要装 sshpass）：

```sh
export LAB_HOST=10.23.90.131 LAB_USER=zzb LAB_PASS=…
printf '%s\n' "$LAB_PASS" | python3 tools/lab/rsh.py --stdin 'sudo -S -p "" sh tools/lab/build.sh all'
```

`--stdin` 把本地标准输入转给远端命令，密码因此只出现在管道里，不在命令行中（`ps` 看不到）。

### 3.1 agent 模式（M1/M2）

```sh
sudo sh tools/lab/run-service.sh --agent --seconds 120
```

`--agent` 换成 `db-engine-probe --agent-startup`：daemon 以 `Mode=1 / SetRoleEnable=0 / EnMasterOnly=0`
（子路由）启动，探针这一侧扮演合成主控，走完 Discovery → Topology Query → Search → Search Response →
M1 → M2。关注这些前缀：`ZTE_AGENT_WIRE`（帧收发）、`ZTE_AGENT_WLAN` / `ZTE_AGENT_PSK`（库读回）、
`ZTE_TRACE_VALUE`（setter 返回值、`cfgChg`）。

daemon 每个 radio 各发一次 M1，探针按 `0x85` 里的 radio 标识去重后逐个回答（2.4G 给
`zte-agent-2g`，5G 给 `zte-agent-5g`），所以 `--seconds` 要给到 100 以上：一次完整运行约 50 秒，
其中一半是"重启"阶段——两份 M2 落库后，探针用 `controller_respawn_daemon()` 把 daemon 换掉
（库与订阅池留在探针进程里，等同工厂 `multiapd_mgr` 的 restart），重新做一次拓扑通告，
再观察新实例是否重新发 M1。`reboot-before` / `reboot-after` 两次读回对比就是这个实验的结论。
重启后 M1 要十几秒才来（通常要两次 topology retry），不要因此判断成"它记得自己已配置"。

沙箱只给 daemon 一个环境变量（多传一个会让原厂 ld.so 断言），所以运行时开关一律用
`/run/zte-mesh` 下的 flag 文件，host 与 sandbox 之间是 bind mount：

| 文件 | 作用 |
|---|---|
| `/run/zte-mesh/trace-all` | 打开 daemon 日志火管（`ZTE_TRACE_ALL`，上限 2 万行） |
| `/run/zte-mesh/mm-off` | 关掉 `libzte_agent_mm.so` 的配置管理器替身，让两个 WLAN setter 报告原厂返回值（诊断用；不建就是替身生效，见下） |

`libzte_agent_mm.so` 已经在 `spawn_daemon()` 的 `LD_PRELOAD` 里，属于运行时而不是实验开关：
它只接管 `CmWLANSetAPConfig` / `CmWLANSetPsk`，并且**先把刚写的行读回来比对**，只有库里已经是调用方
要的值时才把 -3 报成 0（`ZTE_AGENT_MM ... row_applied=1`）。所以 lab 里不再需要 setter 开关。

两处读库的坑，验证时最容易误判：

- `WLANPSK` 必须用 `dbAPIGetView("WLANPSK", "DEV.WIFI.AP1.PSK1", …)` 按实例 id 读（密钥在行内偏移 260）；
  用 `dbGetValComm()` 按行号读会一直得到空值——看上去像"没写进去"。
- `CmWLANSetAPConfig` 写库发生在 `cm_wlan_send_cmapi_set_msg` **之前**，所以没有 cspd 时库已经变了、
  返回值仍是 -3；两者要分开看。

## 4. 与真机构建的四处差异，都是环境导致的，不是代码退化

| 差异 | 为什么 | 在哪里 |
|---|---|---|
| `-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0` | Ubuntu 的 gcc 默认定义 `_FORTIFY_SOURCE`，把 `controller-profile.c:64` 忽略 `write()` 返回值变成 `-Werror` 错误；Debian 不会。顺手也避免 `memcpy` 走 `__memcpy_chk`，让生成的代码与受审计的构建一致 | `tools/lab/build.sh` |
| `ROUTERCC=cc` | `ns-run`、`exec-check`、`config-bridge-probe` 是 AArch64 静态程序，在 x86 上跑不了。实验室里编成 x86_64；**打包前必须用 aarch64 重编** | 命令行参数 |
| `ZTE_MESH_EMULATED_LAB=1` | 见下面两条 | `ns-run.c`、`shim.c` |
| `shim.c` 的 `shmat` 重试 | qemu 的 `shmat()` 对**固定地址**一律返回 `EINVAL`（`tools/lab/shm-fixed.c` 实测：`NULL` 可以、`fixed|SHM_REMAP` 可以、`fixed` 不行）。原厂数据库引擎把段挂在 `0x60000000` 并直接解引用返回值，于是段错误。预载库在 `EINVAL` 时补一次 `SHM_REMAP` | `shim.c` |

两条代码改动都只在 `ZTE_MESH_EMULATED_LAB` 存在时生效，真机路径不受影响：

- `ns-run.c`：该变量存在时把 `RLIMIT_AS` 放开（qemu 要预留整个 32 位客户机地址空间 + 自己的翻译缓冲，128 MB 上限必然失败），并把它传给 guest 环境。
- `shim.c`：该变量存在时，`shmat` 失败且 `errno == EINVAL` 就带 `SHM_REMAP` 重试一次。
- 两者都不改变原有权限模型；`ns-run` 本来就要 root 才能走到这里。

### 4.1 装在设备上时这两个开关由 init 决定

`ZTE_MESH_EMULATED_LAB` / `ZTE_MESH_QEMU` 不需要手工 export：`start_service()` 会先看
`uname -m`，arm64 上再读 `/proc/config.gz` 里的 `CONFIG_COMPAT`；只有内核确实跑不了 ARM32、
且私有根里存在 `qemu-arm-static` 时才把这两个变量传给 `ns-run`。`zte_mesh.main.emulator`
可以指定路径，`=none` 强制走原生路径（打过 `build-firmware.yml` 那套 `CONFIG_COMPAT=y`
固件之后就应该是这个状态）。

同一处还有 `zte_mesh.main.al_last`：AL MAC 的末字节，默认取 LAN 网桥 MAC 的末字节
（`02:00:00:00:19:XX`）。两台设备**必须**不同，否则 1905 栈会把对端当自己，见
[research/AGENT_PROBE.md](research/AGENT_PROBE.md) 第 12 节。

看"线上到底有没有帧"用 `tools/lab/spark.c`：

```sh
zig cc -target x86_64-linux-musl -static -Os -s -o spark-x86_64 tools/lab/spark.c
zig cc -target aarch64-linux-musl -static -Os -s -o spark-aarch64 tools/lab/spark.c
# 设备上：spark <iface> listen|discovery [秒]
```

注意网桥端口的 ingress 帧不会再投给绑定在该端口上的套接字：要抓"从线上进来的"用
`tcpdump -i <物理口>`，要抓"daemon 发了什么"用 `br-lan`。

### 4.2 ARM32 模拟器（`qemu-arm-static`）**不入包**

**结论（2026-10-31 定）**：`zte-mesh` 和 `zte-mesh-launcher` 都不装 `qemu-arm-static`，也不会自动下载、自动安装它。
包里只有原厂 ARM32 运行环境和用 `$(TARGET_CC)` 编出来的宿主架构工具；服务能不能起来，取决于设备内核能不能
直接跑 ARM32。这是有意的取舍，不是漏装。

为什么不进包：

| 理由 | 说明 |
|---|---|
| OpenWrt 不编它 | feed 里没有 `qemu-user`。要打进包就得在包里塞一个预编译的第三方静态二进制——那是二进制再分发，许可与安全维护要另行处理（README「约定」里明确过的红线） |
| 必须与**宿主**架构匹配 | 模拟器本身是宿主架构的程序：aarch64 设备要 arm64 版，x86_64 设备要 amd64 版。一份 apk 覆盖不了两种设备，而仓库里 `zte-mesh` 是 `arch: all` |
| 体积不划算 | 静态 qemu-arm 约 11 MB，比整个运行环境包还大；而目标设备（SR1010，以及刷了 `CONFIG_COMPAT=y` 固件的 arm64 板子）根本用不到它 |
| 只有一处真的需要它 | 需要模拟的只有 chroot 里的 `db-engine-probe`（原厂 ARM32）。`ns-run`、`mesh-search` 由 `zte-mesh-launcher` 用目标编译器现编，本来就是宿主架构，不受影响 |

init 怎么判定（`zte-mesh.init`，`emulator` 那一段）：

| 情况 | 判定 | 结果 |
|---|---|---|
| `aarch64` / `arm64` | 先读 `/proc/config.gz`（退化为 `/proc/config`）里的 `CONFIG_COMPAT=y`；**读不到就直接 exec 一次 `$ROOT/lib/ld-linux.so.3`**，能打印自己的 usage（匹配 `*sage:*`）就认定支持 | 支持 → 原生跑，不需要模拟器 |
| `x86_64` 及其他 | 一律不可能原生 | **必须**有模拟器，否则服务起不来 |
| 任意 | `zte_mesh.main.emulator=<路径>` | 跳过探测，用指定路径（相对私有根） |
| 任意 | `zte_mesh.main.emulator=none` | 跳过探测、强制原生路径。只用于诊断，不是解决办法 |

"没有答案"不等于"不支持"：多数固件（ax3600 上的原版 ImmortalWrt 6.12.103 就是）不导出 `/proc/config.gz`，
早期版本因此一律走上模拟器，而包里没有模拟器，服务就每 5 秒退一次。所以探测改成上面那样：宁可问内核，也不猜。

私有根默认 `/usr/lib/zte-mesh/root`（`ZTE_MESH_ROOT` 可改）。模拟器是在 chroot **之后**被 exec 的，因此必须
放在私有根内部；init 依次找这三处：

```
/usr/lib/zte-mesh/root/qemu-arm-static
/usr/lib/zte-mesh/root/usr/bin/qemu-arm-static
/usr/lib/zte-mesh/root/bin/qemu-arm-static
```

找不到时不会静默失败：日志里有一条完整说明（含宿主架构和上面三条出路），随后 `execv errno=8`（`ENOEXEC`，
内核拒绝执行这个 ELF）。**看到 `errno=8` 先按这一节处理，不要当成代码 bug。**

三条出路，按推荐顺序：

1. **刷带 `CONFIG_COMPAT=y` 的固件**——真机唯一干净的解法，arm64 内核开了 COMPAT 就能原生跑 ARM32，
   不需要模拟器，`ZTE_MESH_EMULATED_LAB` 也不会被设置（少了 RLIMIT_AS 放开和 `shmat` 重试这两条仿真专用路径）。
   配置在 `.github/workflows/build-firmware.yml`。
2. **手工放一个模拟器**——实验室和临时验证用。取**与设备同架构**的一份：

   ```sh
   # 在一台同架构的 Debian/Ubuntu 上（架构不同就去 packages.debian.org 下对应架构的 deb）
   apt-get download qemu-user-static
   dpkg-deb -x qemu-user-static_*.deb /tmp/q
   scp /tmp/q/usr/bin/qemu-arm-static root@<设备>:/usr/lib/zte-mesh/root/qemu-arm-static
   ssh root@<设备> chmod 755 /usr/lib/zte-mesh/root/qemu-arm-static
   ```

   lab 主机上 `tools/lab/emulate.sh` 做的事一样：`cp /usr/bin/qemu-arm-static "$ROOT/bin/"`。
   这份文件不在任何 git 仓库里，也不随包分发。
3. **`emulator=none`**——强制原生路径，用来确认"这块板子到底能不能原生跑"，不是解决办法。

放好后怎么确认：

```sh
ls -l /usr/lib/zte-mesh/root/qemu-arm-static          # 存在且可执行
service zte-mesh restart; sleep 5
logread | grep -e qemu -e execv                       # 不该再出现 "no qemu-arm-static was found"
```

x86_64 上第 2 节装的 `qemu-user-static` + `binfmt-support` 只方便**在沙箱外**直接跑 ARM 程序；
服务是 chroot 后 exec，即便 binfmt 带 `F` 标志能跨 chroot 生效，解释器文件仍须在根内可见，
所以最终还是要把那一个文件放进私有根。

## 5. 从运行包重建 `out/`

`prepare.py` 是 `out/` 的权威来源，但它要求原厂 `rootfs.jffs2`（带钉死的哈希）。没有镜像时用
`tools/lab/bootstrap-out.py`：

1. 解开 `openwrt/zte-mesh/zte-mesh-runtime.tar.gz`，逐个核对包内 `SHA256SUMS`；
2. 核对 `multiapd` 与 `prepare.MULTIAPD_SHA256` 一致（`a0ba6bee…`）；
3. 用 `vendor_elf` 走一遍 `DT_NEEDED` 闭包（27 个库）+ 符号审计，结果与真机一致；
4. 按 `prepare.py` 的权限规则写出 `out/runtime`，并生成带 `"source": "runtime-bundle"` 的合成 `manifest.json`。

合成清单的 `rootfs_sha256` 为空，所以 `prepare.verify_existing()` 会拒绝它——打包流程不可能误用它。
想让 `make-runtime.py` 通过的唯一办法是拿到原厂镜像，然后跑真正的 `prepare.py`。

**注意顺序**：`bootstrap-out.py` 会重建 `out/runtime`，把已编好的 `libzte_*.so` 一起清掉。之后再跑一次
`sh tools/lab/build.sh all database`。`mkroot.sh` 会检查 shim 是否存在，缺了直接报错。

## 6. 当前测试状态

| 步骤 | 结果 |
|---|---|
| `controller-wsc-codec`、`controller-packet-codec`、`cm-profile-codec`、`bridge-codec` | 通过 |
| `test_evidence.py`、`test_device_workspace.py` | 通过 |
| `test_init.sh`、`test_rpcd.sh`（需要在远端把 CRLF 转成 LF，`push.py` 已处理） | 通过 |
| `test_preload.py`、`test_sandbox.py` | 通过（沙箱真的 unshare 了命名空间并 chroot 进 ARM32 root） |
| `cm-profile-evidence.py --verify cm-profile-layout.inc` | 通过：44 处 payload、2496 字节，与 `libcmapi.so`/`multiapd` 实际布局一致 |
| `test_schema`、`test_policies`、`test_profiles`、`test_startup` | 失败，全部是同一个 `FileNotFoundError: out/analysis/cspd` |

补上这四个的唯一办法是把原厂镜像放到 VM 上（`../zx279133/vendor-reference/2b5/rootfs.jffs2`），
然后 `python3 prepare.py --rootfs <镜像> --analysis-cspd`。没有镜像时它们不影响除 `cspd` 证据以外的任何事情。

## 7. 踩过的坑

| 现象 | 原因和解法 |
|---|---|
| `arm-binfmt-P: Could not open '/lib/ld-linux.so.3'` | 直接执行 ARM 程序时 binfmt 按绝对路径找解释器。改成显式给 loader：`out/runtime/lib/ld-linux.so.3 --library-path … <程序>`；`ns-run` 是先 chroot 再 exec，所以没这个问题 |
| `ZTE_DB_ENGINE fail=not-launched-by-sandbox` | 正常：`db-engine-probe` 只接受 `ns-run` 的启动方式 |
| `ZTE_DB_SANDBOX error=private-service-network errno=2` | 没加载 `veth` 模块，`stat("/sys/module/veth")` 失败。`sudo modprobe veth` |
| `arm-binfmt-P: allocate 134217728 bytes for jit buffer` | `RLIMIT_AS=128 MB` 装不下 qemu。设 `ZTE_MESH_EMULATED_LAB=1`（`run-service.sh` 已设） |
| `ZTE_DB_ENGINE fail=namespace-not-empty` | 在共享 IPC 命名空间里跑，库里有残留段。只能用沙箱（`run-service.sh`/`emulate.sh`） |
| `qemu: uncaught target signal 11` + `emulated=shmat-fixed-address-retry` 出现在日志里 | 正常：重试成功后继续。若没有那一行而仍然段错误，看 `emulate.sh --strace` 的最后几条系统调用 |
| `trap: EXIT: bad trap` / `case … in` 语法错误 | 脚本被 CRLF 污染（Windows 检出）。`push.py` 现在会把文本文件转成 LF |
| `ERROR: ld.so: object '/lib/libzte_mesh_shim.so' from LD_PRELOAD … (wrong ELF class: ELFCLASS32)` | 只在沙箱外用 binfmt 跑时出现（宿主 ld.so 看到 ARM 库），无害 |
| `ERROR: ld.so: object … cannot be preloaded` 之后 `ZTE_MESH_SHIM loaded` | shim 在 `root/lib` 里但 `out/runtime/lib` 里已被 bootstrap 清掉。重跑 `build.sh all` |
| `chroot: failed to run command '/bin/sh'` | 手写沙箱时用了宿主 `/bin/sh`；lab root 里只有原厂二进制，直接用 `chroot ROOT ./bin/...` |

## 8. 下一步

- 有原厂镜像时补 `out/analysis/cspd`，四个测试步骤即可全绿；
- 要验证 `WlanCfgDbWrite` 在 `db-engine-probe` 的库上是否成功（[research/AGENT_PROBE.md](research/AGENT_PROBE.md) 第 6 节的运行时那一半），
  现在这台机器已经具备条件：`shim.c` 记录全部 `CmWLAN*` 调用，沙箱内可长时间跑服务，`emulate.sh --strace` 可跟踪任意系统调用。
