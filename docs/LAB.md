# 实验室虚拟机（x86_64 Ubuntu）

在一台普通 x86_64 Ubuntu 上编译并运行这套代码，不需要 mac、不需要路由器、不需要原厂固件镜像。
架构与运行原理见 [ARCHITECTURE.md](ARCHITECTURE.md)，真机流程见 [DEVELOPMENT.md](DEVELOPMENT.md)。本文只讲这台机器。

## 1. 能做什么、不能做什么

| 能做 | 说明 |
|---|---|
| 交叉编译全部产物 | `arm-linux-gnueabi-gcc` 编 ARM32（soft-float），`aarch64-linux-gnu-gcc` 编路由器侧静态工具 |
| 跑 ARM32 程序 | 内核对 ARM ELF 走 `binfmt_misc` → `qemu-arm-static`（`F` 标志让它跨 chroot 也有效） |
| 跑服务 | `ns-run` 的私有 IPC/网络/挂载命名空间在普通 VM 里正常工作，能完成启动序列 |
| 跑主机测试 | 13 个测试步骤里 9 个通过，其余 4 个只缺 `out/analysis/cspd`（见第 6 节） |

| 不能做 | 原因 |
|---|---|
| 生成可发布的运行包 | `make-runtime.py` 会调 `prepare.verify_existing()`，用 `rootfs_sha256` 卡住，合成出来的 `out/` 一定被拒（有意的门槛） |
| `test_schema` / `test_policies` / `test_profiles` / `test_startup` | 要 `out/analysis/cspd`，只有原厂 JFFS2 里有 |
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

`qemu-user-static` 会注册 `binfmt_misc`，`ns-run` 靠它执行 ARM32 程序，不能省。检查：`cat /proc/sys/fs/binfmt_misc/qemu-arm`
应有 `flags: PF`。不需要 Docker（`container-make.sh` 在这台机器上用不了，用 `tools/lab/build.sh` 代替）。

## 3. 日常流程

```sh
# 从 Windows/mac 工作区同步（本地有改动就同步一次）
LAB_HOST=10.23.90.131 LAB_USER=zzb LAB_PASS=… python3 tools/lab/push.py

# 首次或删过 out/ 之后：从运行包重建 out/runtime + manifest.json
python3 tools/lab/bootstrap-out.py

# 编译（ROUTERCC=cc 让 ns-run/exec-check 能在本机跑；要打包时才用 aarch64）
sh tools/lab/build.sh all database
sh tools/lab/build.sh ROUTERCC=cc out/ns-run out/ns-run-svctest

# 测试 / 跑服务（需要 root：unshare + bind mount）
sh tools/lab/build.sh test-host
sudo sh tools/lab/run-service.sh --seconds 20
```

远程执行不用装 sshpass：`printf '%s\n' "$LAB_PASS" | python3 tools/lab/rsh.py --stdin 'sudo -S -p "" sh tools/lab/build.sh all'`
——密码只出现在管道里，`ps` 看不到。

### 3.1 agent 模式（M1/M2）

```sh
sudo sh tools/lab/run-service.sh --agent --seconds 120
```

`--agent` 换成 `db-engine-probe --agent-startup`：daemon 以 `Mode=1 / SetRoleEnable=0 / EnMasterOnly=0` 启动，探针这一侧
扮演合成主控，走完 Discovery → Topology Query → Search → Search Response → M1 → M2。关注前缀：`ZTE_AGENT_WIRE`（帧收发）、
`ZTE_AGENT_WLAN`/`ZTE_AGENT_PSK`（库读回）、`ZTE_TRACE_VALUE`（setter 返回值、`cfgChg`）。

`--seconds` 要给到 100 以上：一次完整运行约 50 秒，一半是"重启"阶段——两份 M2 落库后探针用 `controller_respawn_daemon()`
把 daemon 换掉（库与订阅池留在探针进程里，等同工厂 `multiapd_mgr` 的 restart），再观察新实例是否重新发 M1。
重启后 M1 要十几秒才来（通常两次 topology retry），**不要**因此判断成"它记得自己已配置"。

沙箱只给 daemon 一个环境变量（多传一个会让原厂 ld.so 断言），运行时开关一律用 `/run/zte-mesh` 下的 flag 文件（bind mount）：

| 文件 | 作用 |
|---|---|
| `/run/zte-mesh/trace-all` | 打开 daemon 日志火管（`ZTE_TRACE_ALL`，上限 2 万行） |
| `/run/zte-mesh/mm-off` | 关掉 `libzte_agent_mm.so` 的配置管理器替身，让两个 WLAN setter 报告原厂返回值（诊断用） |

`libzte_agent_mm.so` 已在 `spawn_daemon()` 的 `LD_PRELOAD` 里：它只接管 `CmWLANSetAPConfig` / `CmWLANSetPsk`，并且
**先把刚写的行读回来比对**，库里已是调用方要的值才把 -3 报成 0（`ZTE_AGENT_MM ... row_applied=1`）。

两处读库的坑：`WLANPSK` 必须按实例 id 用 `dbAPIGetView("WLANPSK","DEV.WIFI.AP1.PSK1",…)` 读（密钥在行内偏移 260），
用 `dbGetValComm()` 按行号读永远是空值；`CmWLANSetAPConfig` 写库发生在 `cm_wlan_send_cmapi_set_msg` **之前**，
所以没有 cspd 时库已经变了、返回值仍是 -3，两者要分开看。

## 4. 与真机构建的四处差异，都是环境导致的，不是代码退化

| 差异 | 为什么 | 在哪里 |
|---|---|---|
| `-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0` | Ubuntu 的 gcc 默认定义它，把 `controller-profile.c:64` 忽略 `write()` 返回值变成 `-Werror` 错误（Debian 不会）；也避免 `memcpy` 走 `__memcpy_chk` | `tools/lab/build.sh` |
| `ROUTERCC=cc` | `ns-run`、`exec-check`、`config-bridge-probe` 是 AArch64 静态程序，x86 上跑不了。**打包前必须用 aarch64 重编** | 命令行参数 |
| `ZTE_MESH_EMULATED_LAB=1` | 见下两条 | `ns-run.c`、`shim.c` |
| `shim.c` 的 `shmat` 重试 | qemu 的 `shmat()` 对**固定地址**一律返 `EINVAL`（`NULL` 可以、`fixed|SHM_REMAP` 可以、`fixed` 不行）。原厂数据库把段挂在 `0x60000000` 并直接解引用返回值 → 段错误。预载库在 `EINVAL` 时补一次 `SHM_REMAP` | `shim.c` |

两条代码改动都只在 `ZTE_MESH_EMULATED_LAB` 存在时生效，真机路径不受影响：`ns-run.c` 放开 `RLIMIT_AS`（qemu 要预留整个
32 位客户机地址空间 + 翻译缓冲，128 MB 上限必然失败）并把它传给 guest；`shim.c` 在 `shmat` 失败且 `errno == EINVAL` 时带
`SHM_REMAP` 重试一次。两者都不改变权限模型（`ns-run` 本来就要 root 才走到这里）。

### 4.1 装在设备上时这两个开关由 init 决定

不需要手工 export：`start_service()` 先看 `uname -m`，arm64 上再判断内核能否原生跑 ARM32；只有确实跑不了、
且私有根里存在 `qemu-arm-static` 时才把这两个变量传给 `ns-run`。`zte_mesh.main.emulator` 可指定路径，
`=none` 强制走原生（刷过 `CONFIG_COMPAT=y` 固件之后就是这个状态）。

同一处还有 `zte_mesh.main.al_last`：AL MAC 的末字节，默认取 LAN 网桥 MAC 的末字节（`02:00:00:00:19:XX`）。
两台设备**必须**不同，否则 1905 栈会把对端当自己（[AGENT_PROBE](research/AGENT_PROBE.md) 第 12 节）。

看"线上到底有没有帧"用 `tools/lab/spark.c`：

```sh
zig cc -target x86_64-linux-musl -static -Os -s -o spark-x86_64 tools/lab/spark.c
zig cc -target aarch64-linux-musl -static -Os -s -o spark-aarch64 tools/lab/spark.c
# 设备上：spark <iface> listen|discovery [秒]
```

注意网桥端口的 ingress 帧不会再投给绑定在该端口上的套接字：抓"从线上进来的"用 `tcpdump -i <物理口>`，
抓"daemon 发了什么"用 `br-lan`。

### 4.2 ARM32 模拟器（`qemu-arm-static`）**不入包**

`zte-mesh` 和 `zte-mesh-launcher` 都不装它、也不自动下载安装。包里只有原厂 ARM32 运行环境和用 `$(TARGET_CC)` 编出来的
宿主架构工具；服务能不能起来，取决于设备内核能不能直接跑 ARM32。**这是有意的取舍，不是漏装。**

| 不进包的理由 | 说明 |
|---|---|
| OpenWrt 不编它 | feed 里没有 `qemu-user`。打进包就得塞一个预编译第三方静态二进制——二进制再分发，许可与安全维护要另处理 |
| 必须与**宿主**架构匹配 | aarch64 设备要 arm64 版，x86_64 设备要 amd64 版。一份 apk 覆盖不了两种设备，而 `zte-mesh` 是 `arch: all` |
| 体积不划算 | 静态 qemu-arm 约 11 MB，比整个运行环境包还大；目标设备根本用不到 |
| 只有一处真的需要 | 只有 chroot 里的 `db-engine-probe`（原厂 ARM32）。`ns-run`/`mesh-search` 由 launcher 现编，本来就是宿主架构 |

init 怎么判定（`zte-mesh.init` 的 `emulator` 段）：

| 情况 | 判定 | 结果 |
|---|---|---|
| `aarch64` / `arm64` | 先读 `/proc/config.gz`（退化 `/proc/config`）的 `CONFIG_COMPAT=y`；**读不到就直接 exec 一次 `$ROOT/lib/ld-linux.so.3`**，能打印自己的 usage（匹配 `*sage:*`）就认定支持 | 支持 → 原生跑 |
| `x86_64` 及其他 | 一律不可能原生 | **必须**有模拟器 |
| 任意 | `zte_mesh.main.emulator=<路径>` | 跳过探测，用指定路径（相对私有根） |
| 任意 | `emulator=none` | 强制原生。只用于诊断，不是解决办法 |

**"没有答案"不等于"不支持"**：多数固件（ax3600 上的 ImmortalWrt 6.12.103 就是）不导出 `/proc/config.gz`，早期版本因此
一律走上模拟器，而包里没有模拟器，服务就每 5 秒退一次。所以探测改成问内核，而不是猜。

私有根默认 `/usr/lib/zte-mesh/root`（`ZTE_MESH_ROOT` 可改）。模拟器是在 chroot **之后**被 exec 的，必须放在私有根内部；
init 依次找 `/usr/lib/zte-mesh/root/{,usr/bin/,bin/}qemu-arm-static` 三处。找不到会打一条完整说明（含宿主架构与三条出路），
随后 `execv errno=8`（`ENOEXEC`）。**看到 `errno=8` 先按这一节处理，不要当成代码 bug。**

三条出路，按推荐顺序：

1. **刷带 `CONFIG_COMPAT=y` 的固件**——真机唯一干净的解法：arm64 内核开了 COMPAT 就能原生跑 ARM32，不需要模拟器，
   `ZTE_MESH_EMULATED_LAB` 也不会被设置（少了两条仿真专用路径）。配置在 `.github/workflows/build-firmware.yml`。
2. **手工放一个模拟器**——实验室和临时验证用，取**与设备同架构**的一份：

   ```sh
   apt-get download qemu-user-static && dpkg-deb -x qemu-user-static_*.deb /tmp/q
   scp /tmp/q/usr/bin/qemu-arm-static root@<设备>:/usr/lib/zte-mesh/root/qemu-arm-static
   ssh root@<设备> chmod 755 /usr/lib/zte-mesh/root/qemu-arm-static
   ```

   lab 主机上 `tools/lab/emulate.sh` 做的是一样的事。这份文件不在任何仓库里，也不随包分发。
3. **`emulator=none`**——确认"这块板子到底能不能原生跑"，不是解决办法。

放好后确认：

```sh
ls -l /usr/lib/zte-mesh/root/qemu-arm-static      # 存在且可执行
service zte-mesh restart; sleep 5
logread | grep -e qemu -e execv                   # 不该再出现 "no qemu-arm-static was found"
```

第 2 节装的 `qemu-user-static` + `binfmt-support` 只方便**在沙箱外**直接跑 ARM 程序；服务是 chroot 后 exec，即便 binfmt
带 `F` 标志能跨 chroot 生效，解释器文件仍须在根内可见。

## 5. 从运行包重建 `out/`

`prepare.py` 是 `out/` 的权威来源，但它要求原厂 `rootfs.jffs2`（带钉死的哈希）。没有镜像时用 `tools/lab/bootstrap-out.py`：
解开 `openwrt/zte-mesh/zte-mesh-runtime.tar.gz` 并逐个核对 `SHA256SUMS` → 核对 `multiapd` 与 `prepare.MULTIAPD_SHA256`
一致（`a0ba6bee…`）→ 用 `vendor_elf` 走一遍 `DT_NEEDED` 闭包（27 个库）+ 符号审计 → 按 `prepare.py` 的权限规则写出
`out/runtime` 与带 `"source": "runtime-bundle"` 的合成 `manifest.json`。

合成清单的 `rootfs_sha256` 为空，`prepare.verify_existing()` 会拒绝它，打包流程不可能误用；想让 `make-runtime.py` 通过的
唯一办法是拿到原厂镜像跑真正的 `prepare.py`。**注意顺序**：`bootstrap-out.py` 会重建 `out/runtime`，把已编好的
`libzte_*.so` 一起清掉，之后再跑一次 `sh tools/lab/build.sh all database`（`mkroot.sh` 缺 shim 会直接报错）。

## 6. 当前测试状态

| 步骤 | 结果 |
|---|---|
| `controller-wsc-codec`、`controller-packet-codec`、`cm-profile-codec`、`bridge-codec` | 通过 |
| `test_evidence.py`、`test_device_workspace.py` | 通过 |
| `test_init.sh`、`test_rpcd.sh`（CRLF 要转 LF，`push.py` 已处理） | 通过 |
| `test_preload.py`、`test_sandbox.py` | 通过（沙箱真的 unshare 并 chroot 进 ARM32 root） |
| `cm-profile-evidence.py --verify cm-profile-layout.inc` | 通过：44 处 payload、2496 字节，与 `libcmapi.so`/`multiapd` 实际布局一致 |
| `test_schema`、`test_policies`、`test_profiles`、`test_startup` | 失败，全是同一个 `FileNotFoundError: out/analysis/cspd` |

补上这四个的唯一办法是把原厂镜像放到 VM 上，然后 `python3 prepare.py --rootfs <镜像> --analysis-cspd`；
没有镜像时它们只影响 `cspd` 证据，不影响别的。

## 7. 踩过的坑

| 现象 | 原因和解法 |
|---|---|
| `arm-binfmt-P: Could not open '/lib/ld-linux.so.3'` | 直接执行 ARM 程序时 binfmt 按绝对路径找解释器。改为显式给 loader：`out/runtime/lib/ld-linux.so.3 --library-path … <程序>`；`ns-run` 是先 chroot 再 exec，没这个问题 |
| `ZTE_DB_ENGINE fail=not-launched-by-sandbox` | 正常：`db-engine-probe` 只接受 `ns-run` 的启动方式 |
| `ZTE_DB_SANDBOX error=private-service-network errno=2` | 没加载 `veth` 模块。`sudo modprobe veth` |
| `arm-binfmt-P: allocate 134217728 bytes for jit buffer` | `RLIMIT_AS=128 MB` 装不下 qemu。设 `ZTE_MESH_EMULATED_LAB=1`（`run-service.sh` 已设） |
| `ZTE_DB_ENGINE fail=namespace-not-empty` | 在共享 IPC 命名空间里跑，库里有残留段。只能用沙箱 |
| `qemu: uncaught target signal 11` + 日志里有 `emulated=shmat-fixed-address-retry` | 正常，重试成功后继续。没有那一行仍段错误就看 `emulate.sh --strace` 的最后几条系统调用 |
| `trap: EXIT: bad trap` / `case … in` 语法错误 | 脚本被 CRLF 污染。`push.py` 现在会转 LF |
| `wrong ELF class: ELFCLASS32`（LD_PRELOAD） | 只在沙箱外用 binfmt 跑时出现（宿主 ld.so 看到 ARM 库），无害 |
| `cannot be preloaded` 之后出现 `ZTE_MESH_SHIM loaded` | shim 在 `root/lib` 里但 `out/runtime/lib` 已被 bootstrap 清掉。重跑 `build.sh all` |
| `chroot: failed to run command '/bin/sh'` | 手写沙箱时用了宿主 `/bin/sh`；lab root 里只有原厂二进制，用 `chroot ROOT ./bin/...` |

## 8. 下一步

- 有原厂镜像时补 `out/analysis/cspd`，四个测试步骤即可全绿。
- 验证 `WlanCfgDbWrite` 在 `db-engine-probe` 的库上是否成功（[AGENT_PROBE](research/AGENT_PROBE.md) 第 6 节的运行时那一半）：
  `shim.c` 记录全部 `CmWLAN*` 调用，沙箱内可长时间跑服务，`emulate.sh --strace` 可跟踪任意系统调用。
