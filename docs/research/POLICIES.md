# B.2.2：Master / Domain 策略表与跨进程写入

> **已冻结（2026-11-03）**：取证存档，记的是那天在哪台机器上跑什么、看到什么、用的什么方法。结论以 [FEATURE_STATUS](../FEATURE_STATUS.md) 与 [PLAN](../PLAN.md) 为准，本文不再随结论更新——新结论写进那两份，不要回头改这里。

> **照抄前要补的两条（2026-11-06）**：本文的脚本名按 2026-10-12 那天的布局裸写（`schema.py`、`device-db-smoke.py`），这批在 2026-11-04 的目录整理里挪进了 `tools/dev/`——照抄要加前缀，例如文末那段回归命令是 `python3 tools/dev/device-db-smoke.py`；`sh build.sh` 不变。另一条：`--host root@192.168.5.1` 是当时那台机器的默认值，那台已经不在，现在按 [LAB](../LAB.md) 第 2.1 节的三台填，原因见 [DEVELOPMENT](../DEVELOPMENT.md) 第 1 节。本文的策略表结论与偏移量不受这两条影响。

> **状态更新（2026-10-12）：** 本文记录 B.2.2 里程碑（Master/Domain 策略表）。之后服务配置 `roam.*`、`band_steer.*` 会写入 `MULTIAPDOMAIN1`/`MultiAPMaster`；**Z506 固件没有解析漫游阈值 TLV 的代码，漫游阈值不会在这台子路由上生效**（见 [FINDINGS.md](FINDINGS.md) 第 11 节、[REAL_CHILD.md](../REAL_CHILD.md) 4.5 节）。

本文保留 B.2.2 三表验收；当前命令扩展到八表/391 字段、counter=45，逐项状态看 [FEATURE_STATUS](../FEATURE_STATUS.md)。
**本阶段验证三表共 211 字段，仍不是可用 Controller。**
本阶段继续使用同基线原厂 libdb、CF/CM API；只运行隔离的 DB 实验程序。
没有启动 cspd/multiapd、安装服务、配置真实无线或接入 LAN。

## 恢复的原厂布局

| 表 | 字段 | 视图 bytes | 原厂最多行数 | 测试行数 |
| --- | ---: | ---: | ---: | ---: |
| MultiAPCfg | 50 | 2952 | 1 | 1 |
| MultiAPMaster | 55 | 2312 | 1 | 1 |
| MultiAPDomain | 106 | 3464 | 32 | 2 |

静态证据仍是 SHA256
`8b8477e2598660653c27d796fcaaa5484f879cd263fb6caa5e0a982b1cee5ac1`
的 cspd；不执行其初始化函数。

- Master constructor：`0x1f2218`，55 个 68-byte domain + 终止项（3808 bytes）。
- Domain constructor：`0x1f2900`，106 个 domain + 终止项（7276 bytes）。
- view registration：Master `0x1f27e8`、Domain `0x1f348c`。
  两者按原厂传入 flags=63、第五参数=1024、callback=NULL；核对这些参数的
  原始机器码，不猜测未恢复参数的业务含义。
- 两表第一字段都是 type=2、primary=1 的 `ViewName`；原厂 CF/CM Master
  getter 使用固定 instance `MULTIAPMASTER`，Domain getter 使用调用者的 key。

`schema.py` 的数据流解码只允许 Cfg/Master/Domain 三个固定 constructor，
拒绝调用 `dbDefMultiAPMaster` 等 runtime default 函数。新的 `.def` 保留
原厂字段顺序、类型、primary/save 标志和注册默认值；完整逐项核对 161 个
描述符的零值/reserved/bounds，以及原厂注册函数的参数设置。

新增 type=2：32-byte 字符串容量、value_length=36、record_length=64。
其余视图 header、参数结果和 write mask 的布局与 [SCHEMA.md](SCHEMA.md)
相同。`ViewName` 的特殊 GET 会填充自己的元数据名称；其他字段按位置匹配。
共享 ARM32 domain ABI 集中在 `vendor-db.h`，不得直接作为 ARM64 RPC。

## 策略表不等于无线 profile

Master 包含漫游、band/device steering、统计报告、重传、发射功率上限等
策略；Domain 包含 ESSID 对应的 RSSI/load/timeout/steering 策略。
**这里没有恢复 WPA/PSK、安全模式及各频段远端 BSS profile。**

所有新增字符串注册默认值经验证均为空；解析/生成程序明确拒绝任何非空
原厂字符串默认值，避免把原厂配置/凭据带入运行包或日志。没有执行
`dbDefMultiAPMaster` / `dbDefMultiAPDomain` 的产品相关默认初始化。

公开 fixture 仅存在私有数据库中：

- Master key=`MULTIAPMASTER`，EnMultiDomain=1、EnRoam=1、报告间隔=30。
- Domain A/B 的 key 分别为 `ZTE_SHIM_DOMAIN_A/B`，ESSID 使用明确的
  `zte-shim-domain-a/b` 实验文本，两行 RSSI 分别为 -65/-78。
- 两行 DomainUsed=1，仅用于验证原厂过滤查询，不启用真实无线漫游。

这些不是生产默认值，也不是正在广播的 SSID。实验 semantic setter 禁止
改 primary key、错误字段/类型/capacity、u8 溢出和超出保守字符串限长；
它只是 ARM32 内部辅助函数，尚不是 ubus/UCI/RPC 配置桥。

