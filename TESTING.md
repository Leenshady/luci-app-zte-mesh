# 构建、测试与真机联调手册

本文记录怎么构建、怎么跑控制器、怎么在真子路由上安全地测试，以及一路踩过的坑。逆向发现见 [FINDINGS.md](FINDINGS.md)，配置语法见
[REAL_CHILD.md](REAL_CHILD.md)。`tools/` 里是本文用到的脚本（见 [tools/README.md](tools/README.md)）。

## 1. 机器与连接

| 角色 | 怎么连 | 说明 |
|---|---|---|
| 开发机（本机） | — | x86_64 Alpine 虚机，工程在 `~/code/build-mesh`（不是 git 仓库，见第 8 节）。Docker 守护进程挂不上 sshfs 目录，所以本机不直接编译 ARM32 |
| 构建机（Mac mini） | `ssh 192.168.174.5`（端口 22，用户 `cnjn`） | 仓库在 `/Volumes/code/zx279133`（`port/mesh-shim/` 是这份代码的副本）；离线 Docker 构建镜像 `sr1010-mesh-shim-builder` |
| SR1010 路由器 | `ssh -p 2022 root@192.168.174.5` | **同一个 IP，端口 2022**。`dev-service-smoke.py` 默认 `--host root@192.168.174.5 --port 2022`。路由器上只写 tmpfs（`/tmp`、`/run`），测试结束自动清理 |
| 真子路由 ZXHN Z506 | telnet，`tools/child/ztetn.py` 默认 `192.168.174.5:2323` | 这是 Mac mini 上到子路由 telnet 的端口转发；子路由自身在局域网里是 `192.168.5.216`。账号口令只从环境变量 `ZTE_TN_USER`/`ZTE_TN_PASS` 读，**不要写进文件** |

路由器上没有 python3，只有 BusyBox 工具和 `tcpdump`（所以服务日志和 `state.json` 用 `grep`/`cut` 看，不要在路由器上写 python）。

## 2. 构建与打包

1. 在本机改源码（`port/mesh-shim/*.c|*.h|*.py|*.def|*.inc`）。
2. 拷到构建机：`scp -q <文件> 192.168.174.5:/Volumes/code/zx279133/port/mesh-shim/<文件>`。
3. 构建（离线容器、ARM32 软浮点、`-Wall -Wextra -Werror`）：`ssh 192.168.174.5 'sh /tmp/build8.sh'`（脚本副本：`tools/build/macmini-build.sh`），
   产出 `out/mesh-shim/db-engine-probe`、`zte-meshctl`。`zte-debugcmd` 用 `build9.sh`。
4. 取回产物并放进测试包：把 `db-engine-probe`、`libzte_controller_profile.so`、`zte-meshctl` 放到 `/tmp/svctest-repack/`，在 `out/mesh-shim/` 里运行
   `python3 -I /tmp/svctest-repack/repack.py`（副本：`tools/build/repack-svctest.py`），它替换 `zte-mesh-svctest.tar.gz` 里同名成员并重新生成包内 `SHA256SUMS`
   （包内有自校验，漏掉这步测试包会拒绝运行）；再 `mv zte-mesh-svctest.tar.gz.new zte-mesh-svctest.tar.gz`。
5. 完整的原厂运行包准备/审计用 `sh port/mesh-shim/build.sh`（需要 Docker、`unsquashfs`，从固件重建 `out/mesh-shim/runtime`，核对哈希）。

## 3. 跑控制器

控制器服务由 `dev-service-smoke.py` 在路由器上拉起（私有 netns + 到 `br-lan` 的 veth，`tcpdump` 抓包，运行目录 `/tmp/zte-mesh-svc`，状态 `/run/zte-mesh/state.json`，
配置 `/run/zte-mesh/config`，权限 0600，热更新就是原子改写这个文件）。真子路由的典型命令：

```
python3 dev-service-smoke.py --seconds 3000 \
  --expect-macs d0:11:e5:c5:cb:4a,d4:b7:09:0d:77:30,32:83:90:d9:0f:62,aa:8b:bc:9f:1f:c7,aa:c9:63:3e:7e:70 \
  --watch-mac d4:b7:09:0d:77:30 --config-file <配置> --version-mode 0 --no-change \
  --real-manufacturer ZTE --real-model "ZXSLC SR1010" --real-market-sign 8000 \
  --pcap-out run.pcap --log-out run.log
```

- 它是**测试脚本**，不是常驻服务：`--seconds` 到点就停（procd 服务化按约定放在最后）。后台跑，输出写文件。
- 停止：给它发 `SIGINT`，**写成 `pkill -INT -f "dev-service-smoke[.]py --seconds 3000"`**（方括号避免 pattern 匹配到执行 `pkill` 的那条命令自己，见第 6 节）。
  停止后确认路由器上 `/run/zte-mesh`、`/tmp/zte-mesh-svc` 已删除。
- `--no-attach` 只做配置校验/启动自检，不把 veth 接进 `br-lan`；`--version-mode` 覆盖配置里的值；`--no-change` 跳过实验室用的中途改配置（**对真子路由永远加上**）。
- `--real-*` 三个参数只在真机路径开启真实身份和市场码；不给就是实验室占位身份。
- 配置键见 REAL_CHILD.md 第 3 节。`mesh.v2` 默认 0（自组网 V2 关）。
- 抓包：`pcap-out` 是控制器线侧，`<name>.port.pcap` 是子路由所在端口；`tools/child/pcap1905.py` 解析。

## 4. 离线测试

在 `port/mesh-shim` 里**按脚本方式**逐个运行，第一个参数是产物目录：

```
for t in tests/test_*.py; do python3 $t ../../out/mesh-shim; done
```

2026-10-12 的结果：`test_policies.py`（20）、`test_profiles.py`（28）、`test_schema.py`（16）、`test_startup.py`（6，已加入 `AclCfg` 的期望）全部通过。
`test_device_workspace.py`（参数约定不同）、`test_evidence.py`（缺 `jffs2` 模块路径）、`test_preload.py`、`test_sandbox.py`（需要容器/qemu/权限）
在这台开发机上因环境原因不能直接跑，与代码改动无关；它们由 `build.sh` 的容器流程和设备端 smoke 脚本覆盖。
`python3 -m unittest discover` 不适用（测试读 `sys.argv[1]`）。C 侧的主机测试在 `make test-host`（容器里）。

## 5. 真机测试守则

向真机推配置会真实修改并持久化它的设置。

- **每次测试前**确认子路由原值（备份文件 `config_z506.xml`，含明文密钥，**不要提交或外传**），测完核对还原。原始值：2.4G 信道 1/自动/功率档 1，5G 信道 36/自动/功率档 1，
  BeaconInterval 100，两个频段 SSID `cc`，访客 SSID `ZTE-tsEG65_GUEST`（关闭），`AclCfg` 0 行，`ACLPolicy` AP1/AP2/AP3/AP5 = 1、AP4 = 0。
- **永远不要向真机推占位值。** 额外 BSS 的 `enabled/ssid/key` 必须三项给齐（代码强制）。
- **不要改子路由的模式**（`Mode`、"自组网 V2"、`VersionMode`、MESH 开关）——这些由用户自己在子路由上设置；我们只读。用户明确要求试某个模式时才配合。
- **`dev-service-smoke.py` 的接入保护**：网桥转发表里出现不在 `--expect-macs` 的 MAC 就拒绝接入。用户长期放行的 MAC：控制器主机 `d0:11:e5:c5:cb:4a`、子路由
  `d4:b7:09:0d:77:30`、一加 `32:83:90:d9:0f:62`、iPhone `aa:8b:bc:9f:1f:c7`、Mac `aa:c9:63:3e:7e:70`。**其他未知 MAC 每次都要先问用户。**
  一加还会用另一个私有 MAC `42:be:ab:fd:c3:88`（按用户要求封禁时用过，**不在长期放行名单里**）。
- 会让用户设备掉线的测试（ACL 封禁真实终端、信道/功率变更、改 SSID）必须先得到用户同意，并在测完后立即还原。
- 还原办法：信道先发原信道再发 `0`（自动）；SSID 被重配后掉到隐藏默认名时，改成别的名字再改回；ACL 删光所有 `acl.N.*`；功率设回最大档。
- 不要在局域网里有真实 mesh 节点时把沙箱线接到 `br-lan` 用测试配置（会重配那些节点）。

## 6. 踩过的坑

- **不要用 `kill $(pgrep multiapd)` 逼子路由重新入网。** 反复这样做后日志里出现 `restart unexpected N times`、`SubScribPublish ... already exist`、
  `ACD Notify ... to 0 clients`，`cspd` 收不到 ACD 上线通知（见 FINDINGS.md 3.4）。改用：重启控制器、`zte-meshctl renew-acd <band>`、或让用户重启子路由。
- **`pkill -f "<模式>"` 会杀掉自己：** 命令行里含同样字符串的那个 shell 也会被匹配。用 `[.]` 之类的方括号写法，或先 `pgrep` 再按 pid 杀。
- **zsh：** 变量不自动分词（`for s in $sites` 只循环一次，用 `bash -c` 或 `${=var}`）；以 `=====` 开头的词会被当成命令展开，别在 `echo` 里用它做分隔符。
- **子路由 telnet 会话不能叠加。** 同时开两个会话抓日志会被截断；日志抓取（`logctrl -r`）期间不要再开别的会话。
- **子路由重启会清掉 `/var/tmp/busybox`**；自带的只有 `grep tr cut sed awk pgrep wget tftp`。需要 `dmesg`/`od`/`dd` 时再传一份 busybox。
- **`iwconfig` 在刚重启或已关闭的接口上读回的 SSID 不可靠。**
- **短时间连续重配 2.4G** 会让子路由的 2.4G 掉到隐藏的默认 SSID（驱动配置里 `SSID1=MT7592AP1`、`HideSSID=1`），改名再改回即可触发 `Card 0 need radio config`。
- 向子路由推的策略消息只在控制器启动后约 25 到 80 ms 内发一次；子路由只接受它仍记得的控制器的消息（约 1 到 2 分钟）。重启控制器后要在这个窗口内。
- 工具不会自己重试带副作用的操作；`pkill` 之后的运行要等 15 到 25 秒让清理完成再起新的。
- 调试日志级别：`sendcmd multiapd.acd.acd -l 8`（还有 `base`、`topo`、`slave`），重启后重置，结束用 `-l 7` 还原。

## 7. 验证清单（改了控制器代码之后）

1. 本地改完 → 构建机构建（零警告）→ 重新打包。
2. 配置校验：`--no-attach --seconds 6` 跑合法值和非法值（`mesh.v2=0|1|2|空|yes|重复` 的结果见 REAL_CHILD.md），确认被拒绝的整份配置不启动。
3. 真子路由：用默认配置起控制器，约 70 秒后看 `state.json` 里节点是否 `ZXHN Z506`、`zte:true`、`online:true`；子路由
   `sendcmd multiapd.map.slave dev` 里有控制器 `02:00:00:00:19:06`；`MultiAPCfg` 的 `P1905DevNum=2`、`CurHop=2`。
4. 涉及配置下发的改动：读回子路由数据库（`tools/child/child-state.sh`、`child-acl.sh`）并还原。
5. 结束：停止控制器，确认路由器无残留（`/run/zte-mesh`、`/tmp/zte-mesh-svc`、`zmesh0`、`ns-run`、`db-engine-probe`、`tcpdump`）。

## 8. 版本控制状态（需要注意）

`~/code/build-mesh` 不是 git 仓库。构建机上的 `/Volumes/code/zx279133` 是 git 仓库（分支 `build`，远端 `git@github.com:cnjn/linux-mainline-zte-zxslc-sr1010.git`），
但 `port/mesh-shim/` 在里面是**未跟踪**（`??`）。也就是说这个项目的所有历史改动目前只有两份文件拷贝，没有提交记录。要提交时在构建机仓库里新开分支只加 `port/mesh-shim/`，
不要动 `build` 分支上与本项目无关的未提交改动（内核、rootfs、IDA 数据库）。推送到 GitHub 之前先确认：目录里有原厂固件分析材料（`analysis/`、`out/`、`vendor-reference/` 不应进库）。
