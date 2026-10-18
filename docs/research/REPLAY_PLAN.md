# 回放对比：原厂控制器 vs shim 控制器的冷启动入网

目标：用同一台真子节点（Z506）、同一份配置（尽量贴近原厂最初基线），让 shim 的控制器做一次冷启动入网，把它发出的帧与原厂控制器的冷启动基准逐帧逐 TLV 对比，
得到"哪些字段真的到了线上、哪些不一致"的清单。先**不**做通用的"喂整库"加载器：先用 shim 现有配置语法跑，差异里出现什么才补什么。

## 对比基准与工具

- 基准：`vendor-master-coldjoin-eth4.pcap` 的 12:33:37.5 到 12:33:50（原厂最初基线配置，MLO 开、双频合一开、V2 开、阈值 -60/-70，子节点由物理拔插触发）。
- 对比器：`tools/mcompare.py A.pcap B.pcap --a-win T0 T1 --b-win T0 T1 [--drop-periodic]`。归一化：丢弃 MID、时间戳、WSC 加密字节、设备身份和指标的内容（只比长度）；
  保留方向、CMDU 类型、TLV 类型与长度、M2 各分片长度、策略与厂商子 TLV 的值（`4a 4c 5a 4d 60 4f`、`0100/0101` 各查询回复等）、`0200fe` 的模块名与值。
- **噪声底线（已标定）：** 原厂不同事件的单频段 Renew 轮次之间，经归一化后 17 条消息逐条完全一致（差异为 0）。因此对比出的差异不是抓包噪声。
  原厂整套冷启动入网只有一份样本（第二份样本没拿到：原厂上 `ip link set eth4 down/up` 不能让子节点重新入网，只有物理拔插有效），
  所以冷启动阶段的噪声底线不能直接标定；`TopoResponse` 的长度会随终端上下线变化（337/374/409/461），对比时按"内容类型"看，不按长度。

## 预期差异（设计上就不同，对比报告里单独标出）

| 项 | 原厂 | shim | 原因 |
|---|---|---|---|
| AL MAC | `7c:7d:21:b9:c1:3a` | `02:00:00:00:19:06` | shim 固定，待改成可配置 |
| 厂商 TLV `0x40`（设备信息，195/166 字节） | 含序列号、设备名、版本 | 无序列号和友好名 | shim 有意不带序列号 |
| 槽位 3 回程 BSS（`ZMESH-…_5G`，隐藏、使能、`MapAccessAttr=64`） | 有 | 没有（`InstExist=0`） | shim 不配置回程 BSS；影响 M2 长度和 12 对厂商查询的槽位 3 回复 |
| 槽位 1、2（访客、Wi-Fi 5 兼容，均关闭） | 有，名称为原厂值 | 无（不推占位值） | 不向真机推占位密钥 |
| `WLANSetting` 表（双频合一、MLO、`ESSIDSuffix`） | 有；基线 MLO 开 | 未注册 | 影响 M2 第 2 分片（MLO 开 1383，关 1353）和 `0200fe`（值 = `MLOEnable`） |
| `0200fe` 跨设备模块消息 | 有（入网序列里，`PolicyConfigReq` 之前） | 无 | 来自 cspd 的 `wlan_mgr` |
| `version_mode` | 库里 128 | 0（真子节点路径用 0） | 后续再试 128 |
| cspd 发起的流量（`37025` 灯状态、`acap_iptv` 同步） | 有 | 无 | 没有 cspd |
| 入网时 `PolicyConfigReq` 次数 | 冷启动里 3 次（62、92、92 字节各一） | 启动后约 25 到 80 ms 内一次（前期文档） | 待对比确认 |

## 配置（尽量贴近原厂基线，语法见 `REAL_CHILD.md` 第 3 节）

```
version_mode=0
radio0.enabled=1
radio0.ssid=cc
radio0.key=<原厂 2.4G 密钥，不进文档>
radio1.enabled=1
wifi.dual_band_sync=1        # 5G 跟随 2.4G，不再给 radio1.ssid/key
mesh.v2=1                    # 原厂基线 P1905ComCfg.Enable=1
band_steer.enabled=1         # EnBandSteer=1
band_steer.rssi24=-60        # BsRssiLmt24G
band_steer.rssi5=-110        # BsRssiLmt5G
roam.rssi24=-60              # RoamRssiLmt24G
roam.rssi5=-70               # RoamRssiLmt5G
```
身份通过环境变量：`ZTE_MESH_REAL_MANUFACTURER=ZTE`、`ZTE_MESH_REAL_MODEL="ZXSLC SR1010"`、`ZTE_MESH_REAL_MARKET_SIGN=8000`（`mreplay.py --identity` 默认值）。

## 流程

1. （原厂仍在运行时）读出 2.4G 密钥写入 mac 上 0600 临时文件，用完删除；这样子节点的 Wi-Fi 配置在测试中保持不变。
2. 重启进主线（下次启动是主线 slot 1：`candidates-fallback-20261016`，f66c96d8）。**这是该构建第一次冷启动**（其中的"IKEY 用尽回退"提交只在 kexec 上测过），串口会话 `sr1010-serial` 盯着。
3. 主线上先确认：`veth` 模块可加载、`tcpdump` 可用、子节点的 `0x893a` 帧能到达 CPU（在 LAN 口和 `br-lan` 上各抓一次），再启动 shim。
4. `tools/mreplay.py`（在 mac 上运行）：站点保护（`--expect-macs`）、上传包、写配置、起 `ns-run`、抓 `br-lan`/子节点端口/`zmesh0` 三处、触发重新入网（`--trigger auto` 把子节点所在端口管理性 down/up 6 秒；若子节点没收到链路掉线，改 `--trigger manual` 请用户拔插）、收集、清理、核对路由器状态未变。
5. `tools/mcompare.py` 对比；差异按"预期/意外"分类，意外项逐个定位。
6. 之后：留在主线继续迭代，或回原厂（需要 `fw_flashing` 重写 slot 1 才能再回主线）。

## 风险

- 控制器真实向 LAN 发 1905 帧，可能重配真子节点。守则同 `TESTING.md` 第 5、6 节：未登记 MAC 先问用户；只用配置里的显式值；测完用原厂控制器的一次入网把子节点还原。
- 主线冷启动失败需要用户给板子断电重启。
- 站点保护依赖网桥转发表，手机连着子节点 Wi-Fi 时也在表里。

## 实施记录（2026-10-16 晚）

环境事实（后面复现用得上）：
- 主线 f66c96d8 首次冷启动成功；子节点在主线 `lan1`（原厂的 eth4），网桥 `br-lan` 含 `lan1/2/3`，`lan4` 是之前实验留下的 WAN2 PPPoE。主线 `ip link set lan1 down/up` 能让子节点看到链路变化（第一轮子节点在链路 up 后发了 Discovery 与 Search）。
- 磁盘上的 `dev-service-smoke.py` 是 10 月 11 日的短时版本，带 `--real-*` 的真机驱动已丢失，改用 `tools/mreplay.py`。
- **打包要用 `out/mesh-shim/ns-run`，不要用 `ns-run-svctest`**：后者是 10 月 11 日 19:21 的旧构建，没有 `ZTE_MESH_REAL_MARKET_SIGN` 转发（市场码始终为空，`P1905ComCfg.MarketConfEn` 保持 0，影响 `PolicyConfigReq` 厂商 TLV `60` 的第二字节）；`ns-run` 已带 `SERVICE_ROOT=/tmp/zte-mesh-svc/root` 并支持市场码。
- 重新编译 ARM32 的 `db-engine-probe`/`zte-meshctl`：mac 上要先 `orbctl start`（OrbStack 默认没运行）；`/tmp/build8.sh` 先在宿主上 `rm -f` 产物，OrbStack 的挂载缓存会让容器里的 `make` 误判"已是最新"而不编译，宿主上的产物就消失了。改用 `/tmp/build10.sh`（在容器内删除再编译）。
- 新增的 profile 加载器（本次改动，文件在 mac `port/mesh-shim/`：`mesh-policies.c`、`mesh-policies.h`、`controller-run.c`）：启动时读 `/run/zte-mesh/db-profile`（行 `表 行号 字段 值`），覆盖 `MultiAPMaster`、`MultiAPDomain`（行 0 到 7 → 内部行 2 到 9）、`P1905ComCfg`；显式服务配置在其后应用，仍然优先。由 `tools/mkprofile.py` 从原厂快照生成（875 行）。备份在 mac `/tmp/shim-bak-20261016/`。

第一轮（无 profile）与原厂冷启动的差异、第二轮（有 profile）的变化见 `REPLAY_RESULTS.md`。
