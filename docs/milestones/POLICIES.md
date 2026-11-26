# B.2.2：Master / Domain 策略表与跨进程写入

> **状态更新（2026-10-12）：** 本文记录 B.2.2 里程碑（Master/Domain 策略表）。之后服务配置 `roam.*`、`band_steer.*` 会写入 `MULTIAPDOMAIN1`/`MultiAPMaster`；**Z506 固件没有解析漫游阈值 TLV 的代码，漫游阈值不会在这台子路由上生效**（见 [FINDINGS.md](../FINDINGS.md) 第 11 节、[REAL_CHILD.md](../REAL_CHILD.md) 4.5 节）。

本文保留 B.2.2 三表验收；当前命令扩展到八表/391 字段、counter=45，
最新结果见 [WLAN_PROFILES.md](WLAN_PROFILES.md)。
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

## B.2.2 历史原生设备验收

设备 `root@192.168.5.1`，启用 COMPAT 的 Linux 6.18.38+：

```text
ZTE_DB_ENGINE policy_tables=2 master_fields=55 domain_fields=106 domain_rows=2 test_only=1
ZTE_DB_ENGINE master_get_set=1 domain_get_set=1 domain_rows=2 cmapi_policy_get=1 cmapi_domain_list=1 domain_filter=1 row_isolation=1 payload_logged=0
ZTE_DB_ENGINE PASS shared_counter=21 native_engine=1 multiap_cfg_ready=1 policies_ready=1 controller_ready=0
```

验证内容：

1. 原厂 `MapGetMasterDB`、`MapSetMasterDB` 和原厂 Domain GET/SET 正常，
   逐项检查新 161 字段的类型/结果。
2. 原厂 `CmMultiAPGetMasterCfg`、`CmMultiAPGetDomainCfg` 完成 CM 转换，
   非零策略/实验 ESSID 与实际数据库值相符。
3. 原厂 `CmMultiAPListDomain` 经过真实 QRY 返回两行 key；暂将 B 的
   DomainUsed 改为 0 后只列出 A，再恢复 B。容量 1 无法容纳两行时返回 -12。
   此 CM 函数的内部查询最多八行，不能把它宣称为完整 32 行枚举接口。
4. masked SET 更新 Master 报告间隔、Domain A RSSI/ESSID；B 的值保持不变。
5. child exec 退出后，**服务端再次读取三张表**，确认写入确实存在于共享
   数据库，而非客户端临时缓冲区。最终 GET counter=21。
6. 不存在的 row 返回 -5；错误长度/magic 分别返回 -15/-19，未伪造成功。
7. CM/DB/list buffer guard 未被覆盖，包括较短 Master 视图后的空间。
8. 私有 IPC/网络/mount namespace、只读根、仅 IPC_OWNER；宿主 IPC 和接口
   名称快照前后一致。测试包不含 cspd，不使用真实无线/硬件设备。

当前报告 `out/device-db-report.json` 中三张表的 ready、读写、
Domain filter/row isolation 和跨进程写入验收为 true；整体
`schema_ready`、`wlan_profile_ready`、`controller_ready` 仍为 false。

## 构建、回归与剩余工作

```sh
sh build.sh
python3 device-db-smoke.py --host root@192.168.5.1
```

`out/mesh-policy-schema.json` 记录 Master/Domain 的完整字段/default/
primary/save/offset。源码为 `mesh-policies.c`、`multiap_master.def`、
`multiap_domain.def`；生成器只在显式调用时创建新定义，不覆盖现有源码；
正常 build 只校验，不重写。

新增 20 个离线测试，覆盖布局、主键、原厂 constructor/registration、
default 边界、禁止原厂非空字符串、错误表及生成器不覆盖。加上已有
17+16 个测试、5 个 preload 场景、9 个沙箱场景，共 67 个回归场景通过。

后续 WLAN/DevInfo 布局与纯 CF/DB 逻辑查询已恢复，见
[WLAN_PROFILES.md](WLAN_PROFILES.md)。仍需受限原生配置桥、真实身份和安全
模式映射、CM/driver facade、变更通知和持久化。策略表恢复不能替代这些
内容；OSS 完整启动、br0 接口适配与原厂子节点互通仍未验收。
