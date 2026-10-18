# B.3.1：Controller-only CM profile 读取与私有 1905 RX

> **状态更新（2026-10-12）：** 本文记录 B.3.1 里程碑，当时只支持两个公开实验实例（AP1/AP9）。现在 `controller-profile.c` 对每个频段的槽位 0 到 3 都应答：没有自有行的槽位返回 `InstExist=0`（不是错误，否则 `acdGetWLANCFG` 整体失败、M2 带 Tear Down）；仍只接受 WPA2-PSK + AES，且要求 `ZTE_MESH_DB_SANDBOX=1`。整体说明见 [FINDINGS.md](FINDINGS.md) 第 1 节，入网门槛见 [REAL_CHILD.md](REAL_CHILD.md) 第 2 节。

## 结论及边界

在设备 `root@192.168.5.1` 上原生 ARM32 验证了两个受限 CM reader：
`CmWLANGetAPConfig`、`CmWLANGetPsk`。这是公开 fixture 的**逻辑配置视图**，
不是 OpenWrt radio/driver backend，也不宣称本地存在可工作的 BSS。

真实 multiapd 继续启动、执行真实 DH key generation，并从完全私有的 veth
链路收到 IEEE 1905 Topology Discovery（CMDU 0，60 bytes）。后续 B.3.2
已验证 Query RX、真实 284-byte Topology Response 与独立 peer 的 MID/TLV
校验；见 [WIRE_PROTOCOL.md](WIRE_PROTOCOL.md)。
报告保留 `controller_ready=false`，没有测试任何 stock 子节点。

## ABI 与实现

- `controller-profile.c`：只读两张已有私有 DB 表 WLANCfg/WLANPSK。
  避开原厂 `CmWLANGetAPConfig` 内部 WEP setter、CSP/SSEND 和 driver 查询。
  数据库错误、损坏 header/type/status、越界、无终止字符串仍返回错误。
- `cm-profile-evidence.py` 从 hash-pinned `libcmapi.so` 读取 ARM 指令，核对
  AP Getter 的 **43 个 payload copy**，生成 `cm-profile-layout.inc`。
  同时核对 multiapd 的 AP 2496-byte、PSK 324-byte caller buffer。
  只恢复指令/布局 metadata，不读取 factory credentials/defaults。
- AP getter 保留 caller 的 selection/mode records，按恢复的 payload offsets
  填字段及 write/read mask；填逻辑 band/index/BaseViewName。仅调用已检查为
  内存 classifier 的 `wlan_get_websec` / `wlan_set_wifi_ssid_type`，不调用原厂
  高层 CM AP getter，也不把 setter/driver 方法改成空成功。
- 只支持两个公开实验实例 AP1/AP9、WPA2-PSK/AES、8–63-byte passphrase。
  不支持的实例、安全模式或 key 类型拒绝。band 0/1 是批准的逻辑 slots，
  不是由当前设备的 RF capabilities 探测所得。
- PSK getter接受 AP 实例及原厂 caller 的 `%s.PSK1` 形式。返回 CM ViewName
  是 WLANPSK 的 **WLANViewName（AP 实例）**，而不是数据库 PSK row key。
  检查 AP/PSK row 关联。设备测试使用真实 factory getter，核对完整 324-byte
  输出逐字节一致；测试数据只有公开 SSID/key。
- Getter 在 staging buffer 中构造结果，失败不修改 output；临时 profile/key
  buffer 在返回前擦除。日志只有 API/status 和 `hardware_claimed=0`，无 payload。

独立库 `libzte_controller_profile.so` 仅由显式 `--controller-startup` 实验加载。
默认 probe 仍在 DB init 前退出 78。尚未从 UCI 导入实际配置，也未提供生产
认证、动态 reload、真实 WLAN 状态、driver setter 或 ubus helper。

## 真实熵源与 legacy crypto

旧 OpenSSL 需要 `/dev/urandom`。启动模式在私有 mount namespace 中，只 bind
这一项内核字符设备（major/minor 1:9），只读、nosuid/noexec；其余 host `/dev`、
MTD、WLAN/switch 设备不暴露。目标必须是包里的空 regular file，不能是 symlink。

设备先验证真实 `RAND_bytes(32)` 返回 1，并擦除 bytes；启动 trace 透传真实
`DH_generate_key`，观察到两次 status=1，不打印任何 key material。
**没有替换 RAND/DH 为固定值或伪成功**。这只解决初始化前置条件，不代表
obsolete vendor OpenSSL 已通过现代安全审计或适合直接暴露给不可信网络。

## 完全私有的 L2 实验链路

`controller-net.c` 仅在 netns inode 与宿主不同后创建：

```text
private br-lan（空桥，保留实验布局）
private br0（实际 route/AL identity，与独立 ingress 使用相同公开 MAC）
veth-lab ═ 独立私有 veth ═ lab-peer（公开报文 fixture，不 enslave 到桥）
```

没有移动/连接任何 host 接口。veth 支持在当前设备已存在；缺少该前置条件
则拒绝实验，不由测试安装或加载模块。新 veth 先创建 DOWN，两个端点注册后
再 UP，避免 Linux veth peer 尚未建立时的 ENOTCONN。

`controller-packet.c` 仅发送两帧固定、无配置 TLV 的公开报文：Topology Discovery
和 Topology Query，所有流量留在私有 netns。Query 目的 MAC 来自私有 br0 ioctl。
trace 只观察真实 AF_PACKET `recvfrom`/`sendto` header metadata，保留原厂结果。

后续 B.3.2 实测：Discovery/Query 到达实际私有 ingress 的 daemon socket
（本次 ifindex 5、EtherType 0x893a）；原厂 dispatch/Topology Response 与独立
peer 校验通过。**fixture send 或 RX 成功仍不能单独当作业务处理成功**。
Peer 只保留验证元数据，不捕获/保存网络配置或 key payload。

## 验证与重现

```sh
sh port/mesh-shim/build.sh
python3 port/mesh-shim/device-controller-smoke.py --host root@192.168.5.1
```

- 19 个 host CM 场景：正面视图、真实 caller key 形式、损坏 DB header/type/status、
  字符串/guard 越界、不支持安全模式、坏 key/row 关联、未知/NULL 参数、无 sandbox。
- 设备验证两个 AP 的 guards、caller selection/mode 保留、只读 DB snapshot、
  错误 output 保留，和 `%s.PSK1` caller 形式下的 factory PSK 完整输出对照。
- B.3.1 的 169 个离线场景仍通过；加 workspace 14 项和 B.3.2 packet parser
  62 个断言，共 245 项检查。CM/packet ASan/UBSan 与既有回归通过。
  设备前后 SysV IPC 与接口名称不变。
- 原厂进程 40 秒后受控 SIGKILL，外层 watchdog/process-group 清理逻辑不变。

报告：`out/mesh-shim/device-controller-report.json`；日志仍做公开 key redaction 检查。
报告区分 `rx1905_observed`、`tx1905_observed`、`native_dh_keygen_succeeded` 和
`controller_ready`。宿主快照不是完整网络状态审计，安全依赖 namespace/capability。

## 下一步

Discovery/Query/Response 闭环已完成；继续验证隔离 lab 内的
AP-autoconfiguration、必要 vendor TLV/WSC 与真实平台状态依赖。
其余 getter、STAIF/WLANSetting/WLANWEP/P1905_ACD/MultiAPRoamCatonCfg、外部 CSP
通知/业务服务仍不完整；错误没有被吞掉。不能因为两个 getter 和 RX 已通过，
就接宿主 LAN、部署 procd 长期服务或宣称 stock EasyMesh 互通。
