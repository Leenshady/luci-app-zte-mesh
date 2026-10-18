# 真实子路由联调：已验证能力、配置语法与风险

本文记录 SR1010 上的原厂 `multiapd`（Controller 角色，私有沙箱内）与一台真实 ZTE 子路由联调的结果。所有结论来自真机日志、抓包或对原厂二进制的反汇编；
没有验证过的地方明确写出。协议层的深入分析在 [FINDINGS.md](FINDINGS.md)，构建/测试/守则在 [DEVELOPMENT.md](DEVELOPMENT.md)，
控制器能下发什么在 [CONTROLLER_CAPABILITIES.md](CONTROLLER_CAPABILITIES.md)。

测试对象：ZXHN Z506，固件 V1.0.0.2B2.8000（小兴小方糖），局域网地址 `192.168.5.216`，MAC `d4:b7:09:0d:77:30`。
`MultiAPCfg.VersionMode=0`，`P1905_ACD.ACDRole=1`（Agent）。当前由用户设成：MESH 开、自组网 V2 关、角色自适应（选举后 `Mode=1` Slave、`SetRoleEnable=1`）。
**我们从未改过子路由的模式。**

## 1. 状态（2026-10-12 的表，2026-10-18 更新了后几行）

| 项目 | 结果 |
|---|---|
| 发现 / 拓扑 / 链路指标 / Higher Layer（CMDU 0、1、2、3、5、6、7、13、14） | 真机通过 |
| AP-Autoconfig + WSC M1/M2（真实 DH、HMAC、Key Wrap） | 真机通过，子路由 `config M2[0]` |
| 子路由实际应用 SSID / WPA2 密钥 | 真机通过（日志 + 用户确认 2.4G/5G 均为 `cc`） |
| 重复入网、改配置后 Renew | 真机通过 |
| 多 BSS（槽位 1 到 3）的 SSID / 密码 / 开关 | 真机通过 |
| BeaconInterval（含热更新） | 真机通过，已还原 |
| 信道与发射功率（`0x8006`，两个频段） | 真机通过，已还原 |
| ACL 名单与每 BSS 策略（含热更新） | 真机通过（**主 SSID 上**）：数据库与驱动读回一致；封禁一加手机后被拒绝接入，已还原；`allow` 与 5G 驱动读回未验证。**槽位上的规则和策略（`acl.N.bss>0`、`radioN.bssK.acl_policy`）以前被服务配置整份拒绝，从没经服务配置跑过**（2026-10-17 修了解析顺序，只在实验室验证） |
| 角色选举 | 真机：子路由自适应 + 重启后自行选举为 Slave，被我们的控制器接受（`Mode=1`、`SetRoleEnable=1`） |
| `CurHop` | 真机：修复后子路由读到 2（原先是 -2） |
| 自组网 V2：默认关（`mesh.v2=0`），`mesh.v2=1` 也可 | 真机：两种取值接关着 V2 的子路由都正常，节点识别为 `ZXHN Z506`、`zte:true` |
| 子路由 Web 的"受控后锁定" | 由子路由本地 `Mode`/`Enable` 决定，控制器只能让它成为 Slave（见 FINDINGS.md 第 8 节）；真机：锁定主/备用 Wi-Fi 和射频页 |
| 漫游阈值 | 能下发，Z506 固件没有解析代码，不会生效 |
| 频段引导、双频合一 | 控制器侧参数已写入；实际引导行为未验证 |
| 带宽 20/40/80/160 | 没有下发路径 |
| 重启子节点、指示灯开关 | 真机通过（2026-10-18，REPLAY_RESULTS 16.1）；steering 动作未做 |
| 访客限时、Wi-Fi 5 兼容、Wi-Fi 定时关闭 | 访客限时和定时（`scope=guest`）真机通过；Wi-Fi 5 兼容线上值正确，Z506 忽略；`scope=all` 只在实验室（REPLAY_RESULTS 16.4、16.5） |
| 30 分钟以上长稳、控制器崩溃后恢复 | 未测 |
| LuCI 应用、procd 服务化 | 已打包（apk），在真实 procd/rpcd 上用隔离网桥验证；未用安装的包接真子节点（SERVICE.md） |

## 2. 入网的必要条件（缺一不可）

控制器是原厂 `multiapd` 本身，子路由的接纳逻辑是一串门槛。每一项都经反汇编确认，并在真机上从失败走到成功。

| 门槛 | 现象 | 做法 | 作用范围 |
|---|---|---|---|
| 设备身份 | 子路由 `reason=1` 拒绝 | `DevInfo.ManuFacturer/ModelName` 用 SR1010 自己的公开值（ZTE / `ZXSLC SR1010`），**不含序列号** | 仅 `--controller-service`，由环境变量 `ZTE_MESH_REAL_MANUFACTURER`/`ZTE_MESH_REAL_MODEL` 开启；实验室路径仍用占位名 |
| 市场码 | 子路由 `reason=2` 拒绝 | `P1905ComCfg.MarketConfEn=1`、`MarketSign` 与子路由一致（本机为 `8000`） | 环境变量 `ZTE_MESH_REAL_MARKET_SIGN` |
| 无线电状态 | M2 带 Tear Down | `MultiAPCfg.SSIDSyncRadio=1`（有线控制器绕过 `bRadioStatusOn()`） | 启动时无条件设置 |
| BSS 启用标记 | M2 带 Tear Down | `P1905AcdBssCfg.CustomTag=68`（0x44 = bit2+bit6，`VersionMode` bit1 清零路径；bit0 不置，启用位每次 Renew 从 `WLANCfg.Enable` 读，CM 适配层补了 `Enable` 字节，REPLAY_RESULTS 第 14 节；2026-10-17 前是 69） | 每个 BSS 行 |
| 槽位应答完整 | M2 带 Tear Down（`bMapAccessAttr=16`） | `CmWLANGetAPConfig` 必须对每个频段的槽位 0 到 3 都成功；`acdGetWLANCFG` 遍历 4 个槽位，任一失败就整体返回 -1，Tear Down 保持默认值 1 | `controller-profile.c`：无自有行的槽位返回 `InstExist=0`，不是错误 |
| 每 BSS 的 `MapAccessAttr` | fronthaul 与回程区分 | 主 BSS 行设 32（fronthaul） | `wlan-profiles.c` |
| 每 BSS 的 `ACLPolicy` | 见 §4.4 | 主 BSS 和访客 BSS 报 1，回程 BSS 报 0，与子路由原配置一致 | `wlan-profiles.c` |
| `VersionMode` | 决定是否发 ZTE 私有厂商 TLV | 控制器用原厂基线 128（bit1 清零才发 ZTE 私有 TLV；bit7 加 Wi-Fi 7 的 `0xdd`/`0xe0`） | 服务配置 `version_mode`（见 FINDINGS.md 第 6 节） |
| 控制器的 `CurHop` | 子路由日志 `Get hop -2` | 启动时写 `MultiAPCfg.CurHop=2`（等于 `MaxHop`） | `multiap-cfg.c`；原因见 FINDINGS.md 第 7 节 |
| 自组网 V2（`P1905ComCfg.Enable`） | 控制器是否严格检查邻居 | **默认 0（关）**，`mesh.v2=1` 开 | 服务配置；对外声明的身份和市场码不受它影响 |

注意：真子路由与 SR1010 的 `multiapd`/`libcmapi.so` **不是同一个构建**（SHA256 不同），M2 由 SR1010 一侧构造，
所以控制器行为必须读 SR1010 自己的二进制，子路由能否接收必须读子路由自己的二进制。

## 3. 服务配置语法

控制器服务读取 `/run/zte-mesh/config`（`root`、权限 `0600`、固定键、不允许重复；任何一行不合法整份配置被拒绝，日志 `config=rejected`，`fail=config-missing-or-invalid`）：

```
version_mode=128                # 控制器的 MultiAPCfg.VersionMode；原厂基线是 128（bit7 加 Wi-Fi 7 的 0xdd/0xe0 TLV，REPLAY_RESULTS 第 7 节），旧文档里的 0 已过时
radio0.enabled=1                # 必填；0 会真的关掉子节点这个频段的主 SSID（2026-10-17 起；以前被忽略）
radio0.ssid=<名称>
radio0.key=<8 到 63 个字符>
radio1.enabled=1
radio1.ssid=<名称>
radio1.key=<...>
radio0.hidden=0|1               # 可选：主 SSID 隐藏，默认 0；热更新（只 Renew 该频段）。**热更新路径没有试过**（会动用户真实的 Wi-Fi），启动值已用
radio0.beacon_type=0|3|6|10|12  # 可选：主 SSID 安全模式，默认 3（原厂基线是 12）；含义同 bssK.beacon_type；热更新
radio1.hidden=0|1
radio1.beacon_type=0|3|6|10|12

# 可选：自组网 V2（控制器自己的 P1905ComCfg.Enable：严格检查邻居是否为中兴/市场码一致）。默认 0 = 关；改动时服务写 P1905ComCfg.Enable 并重启守护进程（不重启服务，数据库保留）：子节点会掉出拓扑约 8 到 58 秒再重新入网（15 次实测；原厂一次样本 41 秒）
mesh.v2=0|1

# 可选：设备角色（原厂"主路由 / 自适应"页，GAP 第 7 项）。master = SetRoleEnable 0；auto = SetRoleEnable 1（路由模式下选举结果是 Master，Mode 仍是 2）。
# 不支持 slave（需要守护进程当 agent）；其它取值整份配置被拒绝。键的出现/消失需重启服务；master→auto 热更新 = 守护进程重启两次（相隔约 3 秒，原厂一样），
# auto→master 只清 SetRoleEnable、不重启。不写这个键时数据库保持固定值（Mode 2、SetRoleEnable 1）。
mesh.role=master|auto

# 可选：额外 BSS 槽位 K（1 到 3），三项必须同时给出，缺一项整份配置被拒绝。同一无线电上配置的槽位必须是 1..k（槽位 3 不能没有槽位 2，否则整份配置被拒绝：
# M2 里的 BSS 条目按位置对应子节点的槽位，中间有空会把槽位 3 的设置套到槽位 2 上并关掉槽位 3，第 44 轮）。
# 改 enabled 热更新（只 Renew 那个频段）；增加/去掉槽位热更新 = 写表后重启守护进程（子节点掉出拓扑一阵）；去掉的槽位不再出现在 M2 里，子节点会把它关掉
# （守护进程启动时才加载每个槽位的开关位，REPLAY_RESULTS 11.7）；已存在槽位的 ssid/key/hidden/beacon_type 改动热更新（只 Renew 该槽位所在频段）
radio0.bssK.enabled=0|1
radio0.bssK.ssid=<名称>
radio0.bssK.key=<8 到 63 个字符>
radio0.bssK.hidden=0|1          # 可选：隐藏 SSID；默认只有槽位 3 为 1（原厂的隐藏回程 BSS）
radio0.bssK.access_minutes=<1..10080>  # 可选：访客限时。槽位配置为开之后过这么久，有效使能位变 0（配置里仍是开）并 Renew 该频段；把 enabled 关掉再打开、或改这个值会重新计时；
                                # 被定时关闭压着的时候不计时；服务重启后重新计时。真机验证（备用槽位 2，1 分钟）
radio0.bssK.force_wifi5=0|1     # 可选：Wi-Fi 5 兼容网络开关，写 WLANCfg.ForceWiFi5（厂商查询回复的子 TLV 0x46）；Z506 的 multiapd 忽略它
radio0.bssK.beacon_type=0|3|6|10|12  # 可选：安全模式（原厂界面的五个值）：0 = 不加密、3 = WPA2、6 = WPA+WPA2、10 = WPA3、12 = WPA3+WPA2，默认 3。
                                # M2 里给子节点的认证类型是它与子节点 M1 所报类型的交集：Z506 没有 SAE，12 到它那里等同 3，
                                # 10（纯 WPA3）交集为空，Z506 忽略该槽位的这项（保持原值）；0 是开放网络，M2 短 16 字节。
                                # 只在停用的槽位 2 上验证过；用在主 SSID 上之前想清楚（0 = 真实网络变成开放网络）。密钥仍必须给

# 可选：Wi-Fi 定时关闭（服务里的调度器，REPLAY_RESULTS 第 16.5 节）。最多 8 条窗口；有 timer.N.* 就必须给 timer.utc_offset（沙箱没有时区，宿主传入路由器当前的 UTC 偏移）
timer.utc_offset=<-720..840>    # 分钟，东为正
timer.N.start=HH:MM             # N = 1..8，必填；窗口开始（路由器本地时间），与 end 不能相同
timer.N.end=HH:MM               # 必填；end 不晚于 start 表示跨过午夜
timer.N.days=<7 位 0/1>         # 可选，默认 1111111（周一到周日，窗口"开始"的那一天）
timer.N.scope=all|guest         # 可选，默认 all：所有主 SSID 和槽位 1、2 的有效使能位置 0（隐藏回程槽位 3 不动）；guest：只关槽位 1。热更新，只 Renew 变化的频段。
                                # 真机只验证了 guest 范围（all 会断开真实的 Wi-Fi，没试）

# 可选：无线电 BeaconInterval（毫秒，20 到 1000；不给则为 100，与子路由出厂值一致），可热更新
radio0.beacon_interval=<20..1000>
radio1.beacon_interval=<20..1000>

# 可选：信道和发射功率（不写 = 不动子路由现有设置；写了就在子路由上线约 15 秒后下发，可热更新）
radio0.channel=<0|1..13>        # 0 = 自动信道；其它 = 固定信道（子路由同时关掉自己的"自动信道"）
radio1.channel=<0|36..165>      # 5G：36 40 44 48 52 56 60 64 100..144 149 153 157 161 165
radio0.tx_power=<0..30>         # EIRP dBm，子路由映射到 5 档（见 §4.3）
radio1.tx_power=<0..30>

# 可选：MAC 过滤（ACL）。规则最多 16 条，N = 1..16；改动可热更新（改文件后自动 Renew）
acl.N.mac=<aa:bb:cc:dd:ee:ff>   # 必填；单播、非全零
acl.N.name=<名称>               # 可选，最多 20 个字符 [A-Za-z0-9_.-]，默认 aclN
acl.N.radio=0|1|both            # 可选，默认 both
acl.N.bss=0|1|2|3               # 可选，默认 0（主 SSID）；大于 0 的槽位必须已配置 bssK.*
radio0.acl_policy=disabled|ban|allow   # 可选：主 SSID 的过滤策略；不写 = 保持子路由原值（ban）
radio1.acl_policy=...
radio0.bssK.acl_policy=...              # 可选：额外槽位 K 的策略（该槽位必须已配置）

# 可选：双频合一 / 频段引导 / 漫游阈值 / 弱信号强制切换。给出的键改值是热更新（写库后发原厂 CM 通知，见 SERVICE.md "CM 通知层"）；
# 一个键从无到有或从有到无需重启服务（启动时才准备策略域的行）
wifi.dual_band_sync=0|1        # 1：5G 的名称和密码跟随 2.4G，此时不能再给 radio1.ssid/key（矛盾配置被拒绝）；radio1.enabled 仍要给。
                               # 0 或不写：radio1.ssid / radio1.key 可以省略——省略的 radio1.ssid = radio0.ssid + wifi.essid_suffix（原厂规则：
                               # 双频合一关闭时 5G 名称 = 2.4G 名称 + WLANSetting.ESSIDSuffix，FEATURE_MAP 第 4.1 节），省略的 radio1.key = radio0.key（基线就是这样；
                               # 原厂也能单改 5G 密码）。派生出的名称超过 31 字节整份配置被拒绝。
                               # 改值 = 写控制器自己的 WLANSetting.DualBandSync（读回）并 Renew 两个频段，不重启守护进程（2026-10-17 在真子节点上用相同名称验证：
                               # sync 1→0→1，名称没变，Renew 对 + 相同大小的 M2）。**不会**同时关频段引导：原厂在同一步把 MultiAPMaster.EnBandSteer 1→0，这里要自己写 band_steer.enabled
wifi.essid_suffix=<1 到 8 个可打印字符>   # 可选，默认 -5G（WLANSetting.ESSIDSuffix）；热更新（派生出的 5G 名称随之改变）。**派生名在真子节点上没有试过**（会改用户真实的 5G 主 SSID；
                               # 实验室验证了名称长度和 Renew，改名路径与显式 radio1.ssid 相同）
band_steer.enabled=0|1         # 写主控侧 MultiAPMaster.EnBandSteer
band_steer.rssi24=<-110..-30>  # 写 MultiAPDomain.BsRssiLmt24G / BsRssiLmt5G（两项同时给）
band_steer.rssi5=<-110..-30>
roam.rssi24=<-110..-30>        # 写 MultiAPDomain.RoamRssiLmt24G / RoamRssiLmt5G（两项同时给）；Z506 不解析，不会生效
roam.rssi5=<-110..-30>
roam.force=0|1                 # 弱信号强制切换（原厂 MESH 高级配置的开关）：MultiAPDomain.EnLegacyStaRoam（8 个域）
                               # + MultiAPMaster.EnBtmKickoff / EnRoamKick；不给 = 不动库里的值
mlo.enable=0|1                 # WLANSetting.MLOEnable，默认 1；热更新（两个频段 Renew + 0200fe 推送）
```

热更新与重启：主 SSID/密钥、已有槽位 K 的 ssid/key/hidden/beacon_type、主 SSID 的 hidden/beacon_type、BeaconInterval、信道/功率、ACL、
`wifi.dual_band_sync`、`roam.*`、`band_steer.*`、`roam.force`、`mlo.enable` 随文件改动热更新（WLAN 类触发只 Renew 变化了的频段；策略类写库后发 CM 通知，
子节点收到新的 `PolicyConfigReq`）；**`mesh.v2` 改动 = 写库后重启守护进程；`mesh.role` master→auto = 重启两次（第二次是监督进程替 cspd 的 cooperator 做的模拟，见 REPLAY_RESULTS 第 13 节）**（`v2_update` + `daemon_respawn`，原厂 `multiapd_mgr` 的做法，见 REPLAY_RESULTS 第 12 节；
`state.json` 的 `daemon_pid` 换新、`daemon_restarts` 加 1，服务不退出）；`version_mode`、
策略键的出现/消失（`policy_keys_changed`）只会打印 `..._changed restart_required=1`，需要重启服务。
槽位 `enabled` 和增减槽位也已热更新（REPLAY_RESULTS 第 14 节：`enabled` = 写 `WLANCfg.Enable` + 那个频段 Renew，不重启守护进程；增减 = 写表 + 重启守护进程）。
访客槽位 1 的开关（两个频段一起，`bands=3`）已在真子节点上验证（REPLAY_RESULTS 第 15.4 节，第 49 轮：AP2、AP6 的 `Enable` 0→1→0，其余行不变，结束后 `WLANCfg` 全部 8 行读回与基线逐行相同）。
**未验证：主 SSID 的 `radioN.enabled` 热更新**（代码路径与槽位相同，但会动真实的主 Wi-Fi，没在真子节点上试）、主 SSID 的 hidden 热更新。

`mesh.v2` 的校验（真机环境的 `--no-attach` 自检）：`0`、`1` 接受（启动日志 `ZTE_CONTROLLER_STARTUP p1905_v2=0|1`），`2`、空值、`yes`、重复键整份配置被拒绝；不写则为 0。

## 4. 各功能的验证记录

### 4.1 多 BSS：一次 M2 带走所有槽位
真机日志证明：子路由应用 M2 时，会把每个频段所有已声明槽位的 SSID、密码、开关一并应用。槽位含义（本机子路由）：0 主 Wi-Fi，1 访客，2 备用 SSID，3 回程隐藏网络；其他型号不一定相同。
**永远不要向真机推占位值**：第一次测试用占位名和公开占位密码，覆盖了子路由本地的访客 SSID 并把它打开了；现在每个槽位 `enabled/ssid/key` 三项必填。
入网 vendor 阶段另有 SSID 查询通道（索引 0 到 3）：子路由只应用其中的开关和 ACL 策略（`acdcSetSSIDConf`），其余子类型报 `unknown sub type`，无害。当前控制器对索引 1 到 3 答"没有这个 SSID"，不影响 M2 路径。

### 4.2 BeaconInterval
启动时写入控制器的 `WLANBase` 行；热更新时改行并对该频段发 Renew（日志 `ZTE_MESH_SERVICE beacon_interval radio=N value=V set=0 renew=0`），子路由在重新入网的 vendor 阶段取走新值。
**真机：** 2.4G 设 120 后子路由 `WLANBase.BeaconInterval` 读回 120（5G 不变，DTIM、Fragment 不变），删除该键后热更新回 100 并读回 100；改它会让该频段射频重启约 4 秒。
vendor 消息只带 BeaconInterval、MaxHop 和一个 STA 绑定支持标志，**没有 Fragment、TxPower、带宽、信道**。

### 4.3 信道与发射功率
服务进程自己构造标准 Multi-AP 信道选择请求（CMDU `0x8006`），从控制器的私有口发给子路由，等 `0x8007` 应答（日志 `ZTE_MESH_SERVICE radio_select ... response_code=0`）。
子路由上线后等 15 秒再发（更早的消息它会丢弃），失败最多重试 4 次；值变化、子路由重新上线时再发；键被删除时子路由保持当前设置。
**真机：** 2.4G 信道 6 + 15 dBm → 子路由 `WLANBase` 读回 `Channel=6`、`AutoChannelEnabled=0`、`TxPower=3`，`iwconfig` 信道 6；5G 信道 44 + 20 dBm → `Channel=44`、`AutoChannelEnabled=0`、`TxPower=2`。
还原：先发原信道再发 `0`（自动），功率设回最大档；两个频段均已还原到原始数据库值，子路由随后自行重新选信道。信道/功率实际生效会让该频段射频重启几秒，值不变时不重启。
非法值（2.4G 的 14、5G 的 50 等非法信道、功率大于 30、非数字）整份配置被拒绝。
功率 dBm → 档位（`TxPower`，1 为最大，来自子路由 `mutiap_power_convert`）：2.4G：>19 为 1；16–19 为 2；12–15 为 3；8–11 为 4；其余 5。5G：>21 为 1；20–21 为 2；16–19 为 3；10–15 为 4；其余 5。
**带宽（20/40/80/160）没有下发路径**（见 FINDINGS.md 第 10 节）；硬件上限：2.4G MT7592（20/40）、5G MT7613（20/40/80），不支持 160。

### 4.4 ACL
规则写进控制器的 `AclCfg` 表（服务预建 32 行并就地修改，运行中不增删行），守护进程在子路由入网/Renew 的 vendor 阶段把它做成 ACL 消息；策略是每个 BSS 的 `WLANCfg.ACLPolicy`：
0 disabled、1 ban（拦截名单里的 MAC）、2 allow（只放行名单里的 MAC），由子路由 `cspd` 映射到驱动 `AccessPolicy` 2、1、0。内部细节见 FINDINGS.md 第 9 节。
- **真机（数据库读回）：** 启动时配 1 条规则，子路由入网后表里出现规则；热更新加第 2 条、再删除全部，子路由表同步增加并清空；`radio0.acl_policy=disabled` 后 AP1 策略变 0，删键后恢复 1。
- **Z506 的实际行为：** 规则不分接口——每条规则（即使只给 `radio=0`、`bss=0`）都出现在 AP1、AP2、AP5、AP6 上，5G 主 SSID 上名称带 `_5G` 后缀；策略变化会连带改 AP3。
- **真机拦截（一加手机，用户同意）：** 驱动 `wlan0` 的 `iwpriv wlan0 set ACLShowAll=1` 输出 `Policy is 2 : ... rejected` 并列出 MAC；加入一加当前 MAC 后它立刻被踢下线，用户确认"被拒绝接入"；删除规则后重新连上，驱动名单清空。
- **手机的私有 MAC 会变：** 一加用过 `32:83:90:d9:0f:62` 和 `42:be:ab:fd:c3:88`，只封旧的无效；封某台设备要先看它此刻实际用的 MAC（子路由站点表 / DHCP 租约）。
- 没验证：`allow` 策略（名单不全会把设备挡在外面）、5G 驱动里的名单读回。
- 已修复的回归：控制器曾不设 `ACLPolicy`，默认报 0，子路由把原来的 1 改成了 0。现按子路由原值报告（主/访客 1，回程 0）。测试用的假 MAC 和一加的 MAC 已全部删除，`AclCfg` 回到 0 行。

### 4.5 漫游阈值与频段引导
- `roam.*` 写 `MULTIAPDOMAIN1`，控制器发出 ZTE vendor 策略 TLV（RCPI = (RSSI+110)×2，抓包：配 `-62/-72` 时线上字节 `60 4c`，默认 `-60/-70` 是 `64 50`），子路由回 CMDU 层 ACK。
  **Z506 没有解析这个 TLV，不会生效**；早先"子路由确认"的说法是错的。
- 标准 Metric Reporting Policy（`0x8a`）会生效：子路由 `rssithr` 读回随之变化。公开零默认值曾把 STA 上报阈值推成 RCPI 220（子路由 `rssithr` 变 0），现在启动时设 -110。
- 入网时控制器的策略消息在启动后约 25 到 80 ms 内发一次；子路由只接受它仍记得的控制器的消息。**（2026-10-17 更正）热更新有可靠手段：**
  原厂 CM 设置函数写库后给 `multiapd.map.master` 发同步 `SSEND(0x2401, {u32 0x10206|0x10205, name[32], len 0})`，此前"域事件 `0x10206` 无效"的尝试
  没有成功（当时的发送方式没有记录，推测是把 `0x10206` 当成了事件号；它其实是 `0x2401` 同步消息里的消息号）。现在 `zte-meshctl notify-master <id>...` 发的就是这条；子节点对每条热更新的 `PolicyConfigReq` 在 2 ms 内回 CMDU ACK
  （REPLAY_RESULTS 第 11 节，第 26 轮：与原厂 FEATURE_MAP 第 1、2、3 节的线上帧逐字节相同）。
- `band_steer.*` 的 `BsRssiLmt` 不在发给子路由的任何策略 TLV 里，是控制器自己的判断阈值。Z506 没有 MLO 代码，无法验证 MLO。当前只有一个 AP，没有漫游目标。

### 4.6 角色选举、自组网 V2、CurHop 的真机结果
细节在 FINDINGS.md 第 3、5、7 节。真机观察：
- 手动设 Slave：选举没发生，子路由照常入网。
- 改成自适应后 `multiapd` 不重启：子路由不发 Search，"组网失败"；`sendcmd multiapd.base.base roleplay` 手动开始选举后，入网成功、`Mode` 0→1。
- 用户设"MESH 开 / V2 关 / 自适应"并重启子路由：自行选举为 Slave，`CurHop=2`、`P1905DevNum=2`；主/备用 Wi-Fi 配置和射频页被锁。
- 控制器 `mesh.v2` 取 0（默认）或 1，接关着 V2 的子路由都在线，节点识别为 `ZXHN Z506`、`zte:true`。
- **没验证：** 子路由开着 V2 时、控制器 `mesh.v2=0` 的组合（需要用户在子路由上打开 V2）。

## 5. 联调流程

1. 准备：确认局域网里除子路由和控制器外没有未登记的终端（`dev-service-smoke.py` 读网桥转发表，不在 `--expect-macs` 里的 MAC 拒绝接入；手机连着子路由 Wi-Fi 时也在表里，断开后要等老化约 5 分钟）。放行某个未知 MAC 必须由用户明确同意（长期放行名单见 DEVELOPMENT.md 第 5 节）。
2. 运行：见 DEVELOPMENT.md 第 3 节的命令；真机始终加 `--no-change`。
3. 触发重新入网：**不要 `kill` 子路由的 `multiapd`**（会留下残留状态，见 FINDINGS.md 3.4）。用重启控制器、`zte-meshctl renew-acd <band>` 或让用户重启子路由。
4. 判断成功（子路由 `logctrl -r -g 1 -n` 日志）：`Tear down` 为 0；`bMapAccessAttr is 32`；`parse M2 success[0]`；`config M2[0]`（无变化）或 `config M2[1]`（已应用新配置）。
   控制器侧：`state.json` 里节点 `online:true`、`model:"ZXHN Z506"`、`zte:true`；子路由 `sendcmd multiapd.map.slave dev` 里有 `02:00:00:00:19:06`。
5. 收尾：路由器上 `/run/zte-mesh`、`/tmp/zte-mesh-svc` 已删除，`zmesh0` 不存在，无残留 `ns-run`/`db-engine-probe`/`tcpdump`；子路由配置读回并还原。

## 6. 风险与教训

- 向真机推配置会真实修改并持久化它的设置。每次测试前用 `config_z506.xml` 之类的备份确认原值，测后核对还原。该文件含明文密钥，不要提交或外传。
- 短时间连续重配 2.4G，子路由的 2.4G 出现过掉到隐藏的默认 SSID；把 SSID 改成别的再改回即可恢复，恢复后再加一次验证。
- `iwconfig` 在刚重启或已关闭的接口上读回的 SSID 不可靠。
- 代码路径里"实验室"和"真机"必须分开：身份、市场码只在 `--controller-service` 路径通过环境变量开启。
- 不要在未确认语义前改 ACL 策略；不要切换子路由的 Mesh 模式；不要向真机推占位值。
- 完整坑清单见 DEVELOPMENT.md 第 6 节。

## 7. 未完成

1. 子路由开着 V2 且控制器 `mesh.v2=0` 的真机验证；`Policy=2` 的限制行为、`allow` 策略、5G 驱动 ACL 读回；槽位上的 ACL。
2. 关主 SSID 的路径（`radioN.enabled=0`、定时 `scope=all`）的真机验证（会断开用户的 Wi-Fi，要先征得同意）。
3. 终端动作（踢、封、引导）、拓扑页的终端列表和回程 RSSI、MaxHop/上报参数等小项配置。
4. 长稳、多个子节点、用已安装的 OpenWrt 包接真子节点。

## 槽位 1–3 的原厂配置（2026-10-17 在真子节点 Z506 上复现）

原厂基线（REPLAY_RESULTS 第 10 节）每个无线电 4 个槽位：槽位 0 主 SSID；槽位 1 访客（`GuestSsid=1`、`VapIsolationEnable=1`、关闭、`BeaconType 12`）；
槽位 2（关闭、`BeaconType 6`）；槽位 3 隐藏回程（开启、隐藏、`ACLPolicy 0`、`MapAccessAttr 64`、`BeaconType 6`）。复现用的服务配置（`<KEY>` 为用户指定的 Wi-Fi 密码，仓库里不写）：

```
radio0.bss1.enabled=0  radio0.bss1.ssid=ZTE-tsEG65_GUEST  radio0.bss1.key=<KEY>  radio0.bss1.beacon_type=12
radio0.bss2.enabled=0  radio0.bss2.ssid=cc2-wifi5         radio0.bss2.key=<KEY>  radio0.bss2.beacon_type=6
radio0.bss3.enabled=1  radio0.bss3.ssid=ZMESH-c4FsH5_5G   radio0.bss3.key=<KEY>  radio0.bss3.beacon_type=6  radio0.bss3.hidden=1
radio1.bss1.enabled=0  radio1.bss1.ssid=ZTE-tsEG65_GUEST  radio1.bss1.key=<KEY>  radio1.bss1.beacon_type=12
radio1.bss2.enabled=0  radio1.bss2.ssid=cc2-5Gc-wifi5     radio1.bss2.key=<KEY>  radio1.bss2.beacon_type=6
radio1.bss3.enabled=1  radio1.bss3.ssid=ZMESH-c4FsH5_5G   radio1.bss3.key=<KEY>  radio1.bss3.beacon_type=6  radio1.bss3.hidden=1
```

SSID 名称来自子节点自己保存的值（原厂控制器当时下发的）。额外槽位的 `MapAccessAttr`（槽位 3 为 64，其余 32）和 `VapIsolationEnable`（只有槽位 1 为 1）由程序按原厂基线设置，不是配置项。
**注意：** 第 49 轮访客槽位 1 开着时子节点读回的 `VapIsolationEnable` 仍是 0（控制器的槽位 1 行里是 1）。访客网络的隔离行为没有验证，不要当作和原厂一致。

## 子节点 `WLANCfg` 基线（2026-10-17 第 49 轮前读回，之后每轮结束都与它逐行相同）

`tools/child/childread.sh` 的输出（只有非密钥字段；8 行的 `KeyPassphrase` 都是用户指定的 Wi-Fi 密码，不在这里写）：

```
AP1 InstExist=1 Enable=1 ESSID=cc BeaconType=3 ESSIDHideEnable=0 VapIsolationEnable=0 GuestSsid=0 MapAccessAttr=32 ACLPolicy=1
AP2 InstExist=1 Enable=0 ESSID=ZTE-tsEG65_GUEST BeaconType=3 ESSIDHideEnable=0 VapIsolationEnable=0 GuestSsid=1 MapAccessAttr=32 ACLPolicy=1
AP3 InstExist=1 Enable=0 ESSID=cc2-wifi5 BeaconType=6 ESSIDHideEnable=0 VapIsolationEnable=0 GuestSsid=0 MapAccessAttr=32 ACLPolicy=1
AP4 InstExist=1 Enable=0 ESSID=ZMESH-c4FsH5_5G BeaconType=6 ESSIDHideEnable=1 VapIsolationEnable=0 GuestSsid=0 MapAccessAttr=64 ACLPolicy=0
AP5 InstExist=1 Enable=1 ESSID=cc BeaconType=3 ESSIDHideEnable=0 VapIsolationEnable=0 GuestSsid=0 MapAccessAttr=32 ACLPolicy=1
AP6 InstExist=1 Enable=0 ESSID=ZTE-tsEG65_GUEST BeaconType=3 ESSIDHideEnable=0 VapIsolationEnable=0 GuestSsid=1 MapAccessAttr=32 ACLPolicy=1
AP7 InstExist=1 Enable=0 ESSID=cc2-5Gc-wifi5 BeaconType=6 ESSIDHideEnable=0 VapIsolationEnable=0 GuestSsid=0 MapAccessAttr=32 ACLPolicy=1
AP8 InstExist=1 Enable=1 ESSID=ZMESH-c4FsH5_5G BeaconType=6 ESSIDHideEnable=1 VapIsolationEnable=0 GuestSsid=0 MapAccessAttr=64 ACLPolicy=0
```

另外 `LEDCfg.EnableLEDCfg=1`（指示灯开）。

