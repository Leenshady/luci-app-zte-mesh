# 项目长期记忆（zte-mesh）

## 取证流程（2026-11-01 教训，勿再犯）

1. **上机 `sendcmd 1 DB p` 之前，先查 `docs/milestones/SCHEMA.md` 与 `POLICIES.md`。**
   B.2.1/B.2.2 已经有 `MultiAPCfg` 50 字段、`P1905ComCfg` 8 字段（含偏移与字节数）、`AclCfg`、
   Master/Domain 策略表的字段级还原。2026-11-01 我从 `cspd` 字符串表重新推断表边界，
   把 `P1905ComCfg` 切成四十多列（实际 8 列），把 `MultiAPCacheMac` 的 7 列记到了
   `MultiAPRoamCatonCfg` 上——两处都是重做已完成的活还做错了。
2. **表名不许猜。** 先用 `sendcmd 1 DB all` 列出全部表名（**不是 `DB l`，那个不存在**）。
   `IGD.P1905CFGn` 就是凭空猜的产物，实测 `table not find`；它后来被证实是 `P1905Cfg` 的**行名**。
3. **`RowCount=0` ≠ 表不存在。** 表在、行空是"入口存在但没配过"（`MultiAPStaCfg`/`EthVlan`），
   运行时填充（`MultiAPCacheMac` 每 MAC 一行）也一样；只有 `table(X) not find` 才是表名错。
   **也不要据此否定落点**：0 行可能是"按需建行"。
4. **列名以实测为准**：`cspd` 字符串表里列名连续排列、表名夹在中间当分隔，
   漏切一个表名就会把整段列名并进上一张表——离线切分只能当假设。
5. **"缺语义"不要当"缺数据"去 dump。** `SupportStrategies=251` / `EnRoamModes=19` 的值早在
   `MultiAPDomain` 里，缺的是"第 n 位是什么"；这类问题只能靠反汇编或 Multi-AP 规范正文，
   dump 再多表也解不出来（2026-11-01 第五轮，`MultiapCapability` 只有 2 个能力位，已证伪）。
6. **`isSave=0` 的列只能读状态，不能当配置比对。** 第六轮拿 `WLANSSID.Enable`（全表 `isSave=0`）
   去反证 `Brport.PortState`，结论是反的；换成 `WLANCfg.Enable` 后两表逐个吻合。
   正着用是"别写它"，反着用是"别信它当配置"（2026-11-01 第七轮）。

## 文档收尾惯例

改结论时四处同步：具体文档 → `E2631_DB_SNAPSHOT`/`E2631_ROOTFS`/`FEATURE_STATUS` 里受影响的表 →
`README.md` 索引（并核对"这 N 份"的数字）→ `.workbuddy/memory/YYYY-MM-DD.md` → `git commit`（不推送）。
提交前跑一次坏链与行尾检查（新文件用 LF）。

## 事实基线

- 真机：市售 AX3000，自报 `E2631`，2026-08 固件；`rootfs.jffs2` 明文，JFFS2 是 68 字节 inode 变体。
- 射频：Mediatek **MT7916** 一颗双频（`WLANBase` 实测，2×2 + 2×2）；`DEV.WIFI.RD1`=2.4G(ch 4)、`RD2`=5G(ch 40)。
  接口名：2.4G `wlan0`–`wlan3`、5G `wlan5g0`–`wlan5g3`（`WLANCfg.IfName`）。
- 出厂默认：**踢人走 `EnActiveDeauth`，不走 BTM**（`EnBtmKickoff=0`）；漫游缓存全关（`BtmCacheEn=0`）。
- 11r 不存在（`CONFIG_CSP_WLAN_SUPPORT_80211KV`），只有 11k/11v。
