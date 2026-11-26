# REA 逆向与参考仓库的价值再评估（2026-11-26）

对象：
- 参考仓库：[`1234205a/zte-sr1010-research`](https://github.com/1234205a/zte-sr1010-research)
- REA 逆向：`rea` 6.10.1 + Ghidra 12.2，对固件/运行包里的原厂 ARM32 ELF 做伪代码与反汇编取证
- 本仓库：`zte-mesh`（SR1010 的 OpenWrt shim，自己实现配置 DB 与 renew 判定）

**一句话结论**：两者互补且价值已兑现大半——参考仓库解决“容器层”（固件/配置/NAND/升级），
REA 解决“二进制语义层”（函数契约、事件号、延迟/分支）；剩余价值只在取证与刷机安全网，
不再适合做全量逆向。

## 1. 参考仓库的价值矩阵

| 能力 | 对本项目价值 | 现状 |
|---|---|---|
| rootfs AES-128-ECB 解密 + JFFS2/SquashFS 提取（密钥 `SR1010V102020030`） | **高（一次性，最大）**：没有它就没有 cspd / libmultiapcmapi，REA 取证无从谈起 | 已兑现，见 `VENDOR_BINARY_EVIDENCE.md` 第 0 节，路径可复现 |
| NAND 双槽布局、槽头解析、`recovery-kit` | **中**：刷机/回滚安全网，也是固件来源说明 | 部分已内化到 `tools/firmware-layout.py` |
| `config.bin` Type-4 + AES-256-CBC + zlib 分块 + XML 往返 | **中低（当前）**：shim 不用原厂配置容器；读原厂机出厂配置做对照/迁移时有用 | 未使用，留作迁移/对照工具 |
| Web→cspd 配置导入调用链、CRC/分块 | **低**：只到调用链，没有 cspd 内部语义；shim 不做 Web 配置导入 | 仅作背景 |
| 启动链/升级签名/产品校验、Plugin/WireGuard/DDNS | **低**：做自定义固件/升级包才值钱，与 mesh shim 无关 | 不做 |

## 2. REA 逆向的产出矩阵

| 产出 | 对本项目价值 | 现状 |
|---|---|---|
| `dbAPIGetView` 契约：视图名 ≤31 字节、调用方 `+4` magic `0x01020304`、`ShmPoolLock(30000)`、`-2/-19/-45` 返回码 | **高**：直接解释 GAP P1 #4「少注册一张表 → 整次读视图失败」 | 已兑现（`VENDOR_BINARY_EVIDENCE.md` 第 1 节） |
| renew 全链：`0x9c8a → 0xa93e`、延迟 `6/2/10 s`、定时器 10 ms 单位、FIFO 队列、`msg+4 = MultiAPCfg.Mode` 分支 | **高**：证明 shim 直发 `0xa93e` 的设计正确，并把延迟与频段语义钉死 | 已兑现（第 3 节 + `PLAN.md` H-18 实测 `startup_renew=0`） |
| 事件号名字表、`0xa221` 是 cspd 内部事件（`wlan_handle_reset`） | **中**：避免把进程内事件误当跨进程协议；`0xa221` 已证否 | 已兑现（3.6 / 3.7） |
| 运行时闭环绕带发现 | **高（意外收益）**：见下节 | 已登记 `PLAN.md` H-18 |

## 3. 已兑现的具体收益（可核对）

1. **DB 读失败机制**：`dbcLstView → dbFindTbl` 失败返回 `-2`，不是空表；shim 少注册表会让原厂调用方整次读失败。
2. **renew 判据在 cspd 侧**：`_handleWlanRenew` 只转发 band，不做“该不该 renew”的判断；shim 用 `zte-meshctl renew-acd <band>` 直发 `0xa93e` 与原厂最后一步等价。
3. **延迟语义**：`Mode <= 1`（自适应/Slave）→ 6 s；`Mode > 1` → `VersionMode & 2` 决定 0/2 s，再挂 10 s 共用定时器。
4. **`0xa221` 不跨进程**：multiapd 内零命中；cspd 内是 `wlan_manage` 的卡级复位，不进入 multiapd。
5. **运行时验证（H-18）**：
   - `db-engine-probe` 自测断言过期（2026-11-26 已修）：`multiap-cfg.c` 把 `CurHop` fixture 写成 `2`，`db-engine-probe.c` 曾断言 `-2`（`rc=81`，`fail=public-master-fixture-values`）；现断言已对齐为 `2`，重编后在 owrt 上复跑 `rc=0`（2026-11-26）；
   - 未注册可选视图共 7 个：`EthPortConfProduct / InterfaceStatus / MultiAPDualBand / P1905AcdVenderCfg / P1905StaBind / P1905ZTEMANUF / WLANAD`（均 `-2`）；
   - 空拓扑下原厂 ACD 收到 `0xa93e` 但不发 Renew；带仿真 peer 时 `tx1905 cmdu=10 sent=42`、`renew_status=0 peer_received=1`。

## 4. 剩余价值与不再投入

- **参考仓库剩余价值**：① 刷机/回滚材料（NAND、槽头、recovery-kit）；② `config.bin` 往返，用于读原厂机配置做对照或迁移。
- **参考仓库不再投入**：Web 管理细节、启动链/升级签名、Plugin/WireGuard/DDNS——除非要发自定义固件。
- **REA 剩余价值**：只在“日志/抓包判不了、且答案会改 shim 行为或文档契约”时做点查询。
- **REA 不再投入**：全量反编译 cspd、复刻其状态机；凡是 shim 自己代码能读出的判断（哪个配置字段触发哪个频段），无需再开 Ghidra。

## 5. 风险与边界

- **版本绑定**：REA 结论强绑定二进制 sha256（运行包与固件里的 `libmultiapcmapi.so` 就不是同一次构建），引用结论必须带 sha256。
- **型号绑定**：参考仓库的密钥/偏移是 SR1010（ZX279133）专用；同平台的 E2633 用同样方法拿不到 rootfs 密钥（见 `E2633_FIRMWARE.md`），这反证了其稀缺性。
- **合规**：原厂二进制不入库、不随包分发；只保留地址、哈希与结论。
- **工具成本**：REA 工具链成本高（Ghidra 12.2 + JDK 25 + Node 24），每次开工前先确认问题值得。

## 6. 以后是否再开 REA：三条闸门

1. 运行时日志/抓包已经无法判定；
2. 答案会改变 shim 行为，或会改变一份对外文档里的契约；
3. 目标二进制的 sha256 已固定。

三条全满足才做，且只做“点查询”，不做全量反编译。

## 关联文档

- `VENDOR_BINARY_EVIDENCE.md`：本轮全部地址级取证（1–3 节）与工具链结论（第 4 节）
- `GAP_ANALYSIS.md`：P0/P1 缺口与本次结论的对应关系
- `FEATURE_MAP.md`：renew / AP-autoconfiguration 与原厂行为的对照
- `../PLAN.md` 第 5 节 H-18：运行时最小闭环的实测记录
