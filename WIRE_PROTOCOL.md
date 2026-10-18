# B.3.2：真实 IEEE 1905 Topology Query/Response 闭环

> **状态更新（2026-10-12）：** 本文记录 B.3.2/B.3.3 里程碑（发现、拓扑、AP-Autoconfig 在私有 veth 上的线上校验）。之后控制器已与真子路由完成 M1/M2、Renew、信道选择（CMDU `0x8006`）、ACL 等，见 [REAL_CHILD.md](REAL_CHILD.md) 与 [FINDINGS.md](FINDINGS.md)；原厂守护进程之外，服务自己构造的唯一一类报文是 `0x8006`（`controller-packet.c`）。

## 已实测的边界

在 `root@192.168.5.1` 原生 ARM32、完全私有 network/IPC/mount namespace 中：

- 原厂 multiapd 收到公开 Topology Discovery（CMDU 0）及 Topology Query（CMDU 2）。
- 真实 OSS `ASEND` 将接收报文交给内部任务，观察到 event 37010、4-byte payload、status 0；只记录元数据，不解引用/输出内部消息指针。
- 原厂程序自行构造并 `sendto` 发送 Topology Response（CMDU 3，284 bytes）。
- 独立 `lab-peer` 接收该帧，校验公开源/目的 MAC、query MID `0x1202`、CMDU/fragment flags、TLV 边界、Device Information/ALMAC 和 EOM/padding。
- Peer 验证与 daemon TX/RX 都成为设备脚本的强制通过条件，不再把 fixture send 或 socket receive 当作完整业务成功。

```text
ZTE_CONTROLLER_TRACE rx1905 cmdu=0 bytes=60 ifindex=5 protocol=35130
ZTE_CONTROLLER_TRACE rx1905 cmdu=2 bytes=60 ifindex=5 protocol=35130
ZTE_CONTROLLER_TRACE tx1905 cmdu=3 bytes=284 sent=284
ZTE_CONTROLLER_PACKET fixture=topology-response private_only=1 peer_received=1 cmdu=3 mid_match=1 tlv_sane=1 device_info=1 bytes=284 body_logged=0
```

**这是公开合成 peer 与原厂 daemon 的真实基础协议闭环，不是 stock 节点互通，
也不是 EasyMesh 配网/WSC/M1/M2、真实 BSS 或可用 Controller 验收。**
报告仍为 `controller_ready=false`，40 秒后受控停止。

## 接收阻塞与 lab 平台适配

固定基线机器码显示：

- `multiapd:I5GetInterfaceMacByIfName @0x65370` 对非 WLAN 接口按 `eth`/`veth`、
  `plc`、`moca`、`lo` 前缀处理。`br0` 不是可用的 physical socket-record 名称。
- `_baseRecv1905PacketProc.isra.4 @0x639c0` 在 packet validation/dispatch **之前**
  要找到/建立接口 socket record；仅观察 `recvfrom` 不能证明已经进入消息处理。
- `baseAddSock @0x65868` 调用上述 MAC 获取函数，错误没有通过“返回成功”屏蔽。

最初桥端 RX 用真实 Linux `br0` ifindex，进入内部接口路径后无法推进。
将 raw socket 绑定到已 enslave 的 veth 端口也不能代替桥端绑定：当前 Linux
bridge RX handler 会消耗/重新归属 protocol-specific AF_PACKET 报文。

现在的 **lab-only** 网络为：

```text
br-lan：私有空桥，保留兼容实验布局
br0：私有空桥，DEV.IP.IF1/IFLink/ALMAC 的实际 Linux identity
veth-lab ═ 私有 veth ═ lab-peer：不 enslave 到任何桥的独立实验链路
```

`controller-net.c` 在独立 netns、尚有 NET_ADMIN 时创建以上对象：
`br0` 与 `veth-lab` 设置相同的公开、locally-administered MAC；两端先 DOWN
创建，再 UP。没有从宿主移动任何设备或连接宿主 LAN，随后丢掉 NET_ADMIN。

`controller-route.c` 中的受限 `bind` mapper：

- 只在 sandbox、AF_PACKET、精确 sockaddr_ll 尺寸、1905/LLC 协议且目标为
  实际私有 br0 ifindex 时，改绑到实际私有 `veth-lab` ifindex。
- 调用真正 Linux bind；接口不存在或 syscall 失败仍报错。不会改写
  `recvfrom`、MAC ioctl、原厂 PDU、caller sockaddr 或其他协议绑定。
- GetRouteIFInfo 实际读取 `br0` 的 MAC/flags/MTU；与 IPInterface Name、
  MultiAPCfg IFLink、原厂 ALMAC 一致。不是虚构无线驱动状态。
- 设备验证 getsockname 的实际绑定、输入保留、坏 fd 的真实 EBADF，以及
  IPv4 协议仍绑定原始 br0；均是启动强制检查。

这不是生产 `br-lan`/DSA mapper，也不能直接用来接宿主 LAN。
生产实现仍需明确 Linux bridge/物理 ingress 与原厂接口模型的对应关系。

## 日志、验证与资源

- Packet/crypto/dispatch 元数据有独立 64-line budget；普通 DB/启动日志上限
  160。普通错误耗尽配额不会遮蔽 Query/Response；报告分别记录配额耗尽。
- Trace 透传原始函数结果、errno，不设置 OSS task verbosity/业务状态，不输出
  日志 varargs、网络 TLV body、SSID/PSK 或 DH material。
- `controller-packet.c` 在发送 Query **前**绑定私有 peer RX，最多等待 4 秒、
  处理 512 帧；不写 pcap/raw frame 文件，仅输出验证元数据。
- Peer parser 有 62 个断言、100,000 次确定性变异，ASan/UBSan 通过；覆盖
  截断、错误 MID/MAC/header、缺失/重复 DeviceInfo、畸形长度/接口记录及 EOM。
- 当前离线合计 245 项检查通过（包含 14 项 workspace 清理测试与新 62 个
  parser 断言；两套各 100,000 次 codec 变异不算入检查数量）。
- 设备回归确认公开 credential canary 不出现在日志、临时目录自动删除、
  宿主 SysV IPC 与接口名称快照未变化。快照不是完整网络状态审计；隔离
  依赖 namespace、capability、只读 root、watchdog/process-group 边界。

```sh
sh port/mesh-shim/build.sh
python3 port/mesh-shim/device-controller-smoke.py --host root@192.168.5.1
```

主要证据：`out/mesh-shim/device-controller-report.json`、
`out/mesh-shim/device-controller/probe.{stdout,stderr}`。

下一步仍留在隔离 lab：验证 AP-autoconfiguration/必要的 ZTE vendor TLV、
WSC/M1/M2 及真实 profile/identity getter 依赖。不启动 cspd，不恢复厂商
硬编码 credentials，不部署 procd 或接入 stock 子节点，直到平台与协议
状态机边界经独立验证。
