# 中兴 mesh 功能清单、完成度与推进顺序

> **状态（2026-10-29）：** 主控侧在真子路由（ZXHN Z506，原厂固件）上验证；子路由侧在实验室（合成主控）验证。
> 本表是唯一的功能总账：每完成一项就在“状态”列打勾并把证据补上，**不要**在别的文档里另起一份清单。
> 协议与逆向细节见 [CONTROLLER_CAPABILITIES.md](CONTROLLER_CAPABILITIES.md)、[REAL_CHILD.md](REAL_CHILD.md)、
> [research/AGENT_PROBE.md](research/AGENT_PROBE.md)；部署与测试见 [DEVELOPMENT.md](DEVELOPMENT.md)。

## 图例

| 记号 | 含义 |
|---|---|
| ✅ | 已实现，**真机**验证过 |
| ☑️ | 已实现，只在实验室 / 测试桩上验证（未上真机，或对端固件不支持） |
| ⬜ | 未做 |
| ➖ | 做不到：对端没有接收代码或没有下发通道（不是我们没写） |

难度：**1** 改几行、**2** 一天内可验证、**3** 需要新逻辑或新测试面、**4** 需要真机驱动侧配合、**5** 需要逆向或换硬件。

## A. 主控（Controller，本机是主路由）

| # | 功能 | 状态 | 证据 | 难度 |
|---|---|---|---|---|
| A1 | 原厂 `multiapd` 在私有命名空间里跑（IPC/net/mount、veth 到 `br-lan`） | ✅ | `ns-run` + `attach`，装包后真路由器、真 procd | — |
| A2 | 发现 / 拓扑 / 链路指标 / 高层数据（CMDU 0、1、2、3、5、6、7、13、14） | ✅ | REAL_CHILD §1 | — |
| A3 | AP-Autoconfig + WSC M1/M2（真 DH、HMAC、Key Wrap） | ✅ | 子路由 `config M2[0]` | — |
| A4 | 配置变更后 Renew（热更新，无需重启） | ✅ | 改配置约 4 秒生效 | — |
| A5 | 主 SSID / 密码 / 加密 / 隐藏 | ✅ | 2.4G、5G 均读回 | — |
| A6 | 多 BSS（访客、备用、隐藏回程，槽位 1–3）名称 / 密码 / 开关 | ✅ | REAL_CHILD §1 | — |
| A7 | BeaconInterval（含热更新） | ✅ | 120 → 读回 120 | — |
| A8 | ACL 名单 + 每 BSS 策略（关 / 拦 / 只放行） | ✅ | 一加被踢并拒绝重连；`allow` 与 5G 驱动读回未验证 | — |
| A9 | 信道与发射功率（`0x8006`） | ✅ | 两个频段验证并还原 | — |
| A10 | 重启子节点 / 指示灯开关 | ✅ | REPLAY_RESULTS 16.1 | — |
| A11 | 访客限时、Wi-Fi 定时关闭 | ✅ | `scope=guest` 真机；`scope=all` 仅实验室 | — |
| A12 | 自组网 V2（`mesh.v2`）与市场码门槛 | ✅ | 两种取值都正常入网 | — |
| A13 | 控制器角色 `master` / `auto`（含自适应选举替身） | ✅ | 第 40 轮 | — |
| A14 | MLO 开关下发（`0200fe`） | ☑️ | 线上帧逐字节一致；Z506 无该字段，等于无操作 | — |
| A15 | Wi-Fi 5 兼容标志（`ForceWiFi5`） | ☑️ | 线上值正确；Z506 不认 | — |
| A16 | 漫游 / 频段引导阈值下发 | ☑️ | 能发；Z506 固件没有解析代码 | — |
| A17 | 引导（BTM）/ 封锁 / 踢终端 | ⬜ | `0x8014` 等，接口已在 libcmapi | 3 |
| A18 | 拓扑页：回程 RSSI、树状图、每 AP 终端列表、固件版本 | ⬜ | 现在只有节点清单与动作 | 3 |
| A19 | 服务配置校验：`zte-mesh-timer` / ACL / 槽位连续性的错误定位 | ☑️ | init 69 项 | — |

## B. 子路由（Agent，本机是子节点）

| # | 功能 | 状态 | 证据 | 难度 |
|---|---|---|---|---|
| B1 | agent 模式启动（`Mode=1` / `SetRoleEnable=0` / `EnMasterOnly=0`） | ☑️ | 实验室：`role=slave mode=1 set_role_enable=0 en_master_only=0` | — |
| B2 | 发 M1、收 M2、凭据落 `WLANCfg` / `WLANPSK` | ☑️ | 两个频段各一份，`m2_match=1` | — |
| B3 | setter 兜底（配置管理器返回 -3 时读回校验再报 0） | ☑️ | `libzte_agent_mm.so`，沙箱专用 | — |
| B4 | 库 → 计划的发布（`agent-sync` → `/run/zte-mesh/wlan-plan`） | ☑️ | `published=1 rows=2` | — |
| B5 | 计划 → `/etc/config/wireless`（`wlan-sync`，幂等、只写真变化；plan 启用某 band 时把关着的 radio 设备打开，只写 0） | ☑️ | 86 项测试 | — |
| B6 | 回程 BSS：`MapAccessAttr&64` → `multi_ap=2` 独立段，主 SSID `multi_ap=1` | ☑️ | 86 项测试含开关双向复原 | — |
| B7 | UCI / init / LuCI 暴露 `mesh_role` + `backhaul` | ☑️ | init 69、rpcd 16 | — |
| B8 | **子路由免本地 profile**：没有配置时沿用本机正在广播的网络 | ☑️ | 本轮；init `role-slave-borrow` | — |
| B9 | **LuCI 按角色只显示有效设置**（子路由隐藏 Wi-Fi/漫游/额外网络/子路由清单） | ☑️ | 本轮；`test_luci.js`（主控 31 项 / 子路由 4 项） | — |
| B10 | rpcd `status` 返回角色 | ☑️ | 本轮；rpcd 18 项 | — |
| B11 | 子路由侧应用额外槽位（slot 1–3 现在只记日志） | ⬜ | `wlan-sync` 明确跳过 | 3 |
| B12 | **应用结果回传**：`wlan-sync.status` + `status.sync` + 状态页行 | ☑️ | 本轮；wlan-sync 86 项、rpcd 18 项 | — |
| B13 | 无线回程的数据面：本机作为 backhaul STA 去关联主控的回程 BSS | ⬜ | 只做了 AP 侧 | 4 |
| B14 | 真机：本机当子路由，接真主控（有线回程） | ⬜ | 只在合成主控下验过 | 2 |
| B15 | `CmWLANMapSsidCfgSync2Repeater` 对齐（libcmapi 内部调用，替身命中不到） | ⬜ | 沙箱里是 no-op | 5 |
| B16 | `CurHop` 在子路由侧的真实更新 | ⬜ | 实验室用 `-2` 顶替 | 2 |

## C. 打包与分发

| # | 功能 | 状态 | 证据 | 难度 |
|---|---|---|---|---|
| C1 | 单个 apk（runtime + init + wlan-sync + rpcd + LuCI），4.1 MB | ✅ | 装在真路由器上，procd/rpcd 全通 | — |
| C2 | 拆包：`zte-mesh-runtime` + `zte-mesh-controller` + `zte-mesh-agent` | ⬜ | 方案已定（见 AGENT_PROBE §11.7），未动手 | 2 |
| C3 | 统一行尾（`.gitattributes`，`* text=auto eol=lf`） | ⬜ | 现在工作区 CRLF，测试要转 LF 才能跑 | 1 |

## D. 没有通道的（换子路由固件才谈得上）

| # | 功能 | 原因 |
|---|---|---|
| D1 | 带宽 20/40/80/160 | Z506 不写 `BandWidth`，也不关“自动信道” |
| D2 | 访客限速 | 1905 里没有这个字段 |
| D3 | 信道扫描 / CAC | Z506 无接收代码 |
| D4 | VBSS（虚拟 BSS 创建/销毁/迁移） | Z506 无 |
| D5 | VLAN / 流量分离 / Profile-2 | Z506 无 |
| D6 | STA 绑定 | Z506 无 |
| D7 | 802.11r/BTM 踢人（部分） | Z506 固件不支持 FT |

## 推进顺序（只做下一项，做完打勾再往下）

按难度从 1 到 5，同难度按“能立刻验证”优先：

| 顺序 | 项 | 难度 | 为什么排这里 |
|---|---|---|---|
| 1 | ~~B8 / B9 / B10~~ | — | **2026-10-29 已完成** |
| 2 | ~~B12 应用结果回传~~ | — | **2026-10-29 已完成** |
| 3 | C3 统一行尾 | 1 | 一次改动，之后所有测试不用再转 LF；影响整个仓库，**需要单独确认** |
| 4 | B14 子路由接真主控（有线） | 2 | 决定 B15 要不要补，信息量最大，需要两台机器 |
| 5 | C2 拆包 | 2 | 方案已定，属于分发形态，不阻塞功能 |
| 6 | B16 `CurHop` 真机确认 | 2 | 需要真机，可并入第 4 项一起看 |
| 7 | B11 额外槽位在子路由侧应用 | 3 | 要先把 plan 的槽位与 hostapd 段对上，且有 A6 真机作参照 |
| 8 | A18 拓扑页 | 3 | 需要状态字段（RSSI、终端数）先落到 `state.json` |
| 9 | A17 引导 / 封锁 / 踢终端 | 3 | 接口已存在，缺的是策略与 UI |
| 10 | B13 无线回程 STA | 4 | 要真机看 `4addr`，且与 wpad 变体绑定 |
| 11 | B15 `CmWLANMapSsidCfgSync2Repeater` | 5 | 逆向或真机 strace，只有前 4 项都过了才值得动 |
