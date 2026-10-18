# B.3：真实 multiapd 的隔离 Controller 初始化

> **状态更新（2026-10-12）：** 本文记录 B.3 里程碑（隔离 netns 里的控制器初始化）。之后：M2 加密/Authenticator 验证和子节点配网**已经完成并在真子路由上验证**（见 [REAL_CHILD.md](../REAL_CHILD.md)）；`P1905ComCfg.Enable` 现在由服务配置 `mesh.v2` 决定，**默认 0（关）**；`MultiAPCfg.CurHop` 启动时写 2；`VersionMode` 的含义见 [FINDINGS.md](../FINDINGS.md) 第 6 节。下文保留当时的实测记录，其中"M2 尚未完成""`controller_ready=false`"等描述是该里程碑当时的状态。

## 当前实测结果

这次执行的是原厂 **multiapd 本体**，不是只调用它的数据库 API。
在 `root@192.168.5.1` 上原生 ARM32 运行，40 秒后受控停止：

- 原厂 DB attachment、OSSInitConfig 和四个 OSS task socket 初始化推进成功。
- 真实 `SubShmSrvInit` 创建 key `0xabcb`、128 KiB pool，映射 `0x68000000`；
  链表初始状态、共享段尺寸及原厂锁均验证。不把 OSSStart 替换为空成功。
- 实际进入 `MASTER INIT`，原厂程序创建本地 `1905Device`。
- 在**无物理端口的私有网络 namespace** 中，原厂 AF_PACKET/RAW sockets
  打开成功：1905 `0x893a`，LLC `0x0004`。
- B.3.1 新增两个只读 CM profile reader；PSK 324-byte 输出与真实原厂 getter
  对照一致。真实 RAND_bytes、DH_generate_key 成功，未使用弱随机或成功 stub。
- B.3.2 通过完全私有的独立 lab veth 链路，真实 daemon 收到 Discovery/Query
  （CMDU 0/2），自行发出 284-byte Topology Response（CMDU 3），独立 peer
  校验 MID、地址、DeviceInfo、TLV/EOM 成功。见 [WIRE_PROTOCOL.md](WIRE_PROTOCOL.md)。
- B.3.3：独立 peer 校验双频 AP Autoconfiguration Response（CMDU 8），
  包含正确 MID、band 和 Controller service。公开 WSC M1 使用真实 RNG/DH，
  原厂处理后发出 CMDU 9，但目前只有 34 bytes、没有 WSC M2 配置 TLV。
  **M2 加密/Authenticator 验证和子节点配网均未完成**，报告保持 false。
- 限时停止为 SIGKILL（9），报告明确记录 `controller_ready=false`。
  未验证 stock 节点发现、配网或稳定运行。见 [CM_PROFILES.md](CM_PROFILES.md)。

完整构建、已有 DB/配置桥设备回归，以及新增真实启动实验均通过；这只表示
实验里程碑和隔离检查通过，**不表示功能完备**。

## 实际修复的平台边界

1. `_initPCBPool` 原先失败，并非已证明无线驱动缺失：原厂任务要在
   `/var/tmp/<task>_{asy,snd}` bind Unix sockets。私有 root 中将 `/var/tmp`、
   `/var/run`、`/var/log` 指向私有 tmpfs 后，真实任务创建继续执行。
2. `OssSubScribInit` 需要 CSP 平时建立的 subscriber pool；单独调用原厂
   server initializer 并验证状态即可。不启动整个 `cspd`。
3. 新增 `P1905ComCfg`（8 字段/328 bytes）、`IPInterface`（48 字段/2404 bytes）
   metadata，与同基线 constructor/type lengths 核对，**不读原厂 defaults**。
   另建立原厂请求的 `MULTIAPDOMAIN1..8`，均为未启用、空凭据的实验行。
   B.3.3 增加 `P1905_ACD`（9/456）、`P1905AcdBssCfg`（5/264）metadata；
   两条 BSS fixture 使用原厂 `DEV` 查询前缀和公开逻辑 radio 配置，非实际 RF 状态。
   本实验显式设置 `VersionMode=2`，不是推断或复用厂商产品默认值。
4. Controller-only `GetRouteIFInfo` 适配器只接受 `DEV.IP.IF1`，实际通过
   Linux ioctl 获取私有 `br0` 的 MAC、flags 和 MTU，失败返回错误。
   B.3.2 另将实验 raw socket 的 br0 alias 绑定到真实独立 veth ingress；
   实际 bind/getsockname、失败及其他协议保留均验证。不是生产 LAN mapper。
   不模拟 WAN、IP、DNS、路由；其余接口返回 -1，且不修改 caller buffer。

路由结构边界来自固定基线 `multiapd:_baseALConfigInit @ 0x632b8`：
初始化 532-byte output，`+172` 为复制到 ALMAC 的六字节地址。
设备端另验证 532-byte 前后 guard、实际 MAC 与未知接口拒绝/缓冲保留。
这个受限适配器**仅用于私有 lab netns 实验，不是生产 LAN mapper**。

## 隔离与日志

- 启动器先 `unshare` IPC/network/mount namespace；无 host network bridge/veth
  移入，没有连接子节点或向宿主 LAN 发送报文。
- 仅此模式在新 netns 中建立 `br-lan`/`br0` 与一对完全私有的 lab veth。
  B.3.2 的 veth-lab/lab-peer 是独立链路，不 enslave 到桥，也不连接宿主接口；
  再丢掉 NET_ADMIN，保留 IPC_OWNER、NET_RAW。
  veth 支持须已存在；不会由测试安装或加载模块。runtime 不保留 SYS_ADMIN。
- root 保持只读，唯一字符设备例外是只读 bind 的内核 `/dev/urandom`（1:9），
  用于真实 legacy crypto。不映射宿主 /dev 目录、MTD、无线/switch 等硬件设备。
- 只读 root、私有 tmpfs、资源上限；子程序 40 秒观察，外层 50 秒 watchdog。
  退出后按原有 process-group 清理和 PID 固持逻辑回收后代。
- 逐文件校验上传包 SHA256。不会安装服务、修改 UCI/flash 或加载厂商模块。
- trace 透传真实 OSS/DB 返回值，仅记录 API/status、固定错误格式和 packet
  header 元信息；不展开格式中的 MAC、实例名、PSK、SSID、serial 或 payload。
- 启动 profile 是公开 fixture，报告检查 fixture credential 未出现在日志；
  **没有读取 OpenWrt UCI/实际凭据或厂商硬编码 defaults**。
- 设备测试前后核对宿主 SysV IPC 与接口名称；私有临时目录已清理。
  该快照并非完整网络状态审计，安全依赖 namespace/capability 边界。

## 重现

```sh
sh build.sh
python3 device-controller-smoke.py --host root@192.168.5.1
```

结果：`out/device-controller-report.json`；限量日志位于
`out/device-controller/probe.{stdout,stderr}`。
默认加载探针仍停止在 DBShmCliInit（78）；需要上述显式启动命令。

## 仍然阻塞可用 Controller 的部分

真实运行观察到这些 DB 请求失败，错误未被吞掉：
`STAIF`、`WLANSetting`、`WLANWEP`、`MultiAPRoamCatonCfg`，
以及实验 WLANCfg 缺少的部分实例。另有硬编码 radio/interface 查询、外部
CSP/SNTP 消息与 change-notify 缺服务；一些 CM getter 仍可能发 setter/SSEND。

Controller-only AP/PSK profile reader 已部分完成，不能据此宣称全部 CM/backend
可用。私有 Discovery/Query/Response 和双频 AP-autoconfiguration 已验证；
WSC M1 已进入真实处理，但尚未生成带配置的 M2。下一步修复必要
getter/platform 状态并验证 M2 认证/解密与公开 profile，而不是
盲目恢复全部 CSP 表或伪造成功。只有上述业务状态进一步确认后，才能谈 stock 节点互通。
在这些完成前，不应把当前包直接接入宿主 LAN 或交给 procd 长期运行。
