# 早期里程碑（历史记录，已冻结）

> **已冻结（2026-11-03）**：这个目录是 2026-10-10 到 10-12 各里程碑的取证存档，记录"那天在哪台机器上跑什么、
> 看到什么"。结论变了不回来改这里——逐项状态看 [FEATURE_STATUS](../FEATURE_STATUS.md)，排期看
> [PLAN](../PLAN.md)；只有当有人要复查某个当年的判据时才会打开它。

| 文档 | 内容 |
|---|---|
| [EARLY_ACCEPTANCE.md](EARLY_ACCEPTANCE.md) | B.1 – B.3.3 的验收记录合一份：数据库引擎、WLAN/DevInfo 布局、配置桥、沙箱里初始化真实 `multiapd`、CM 替身读取、1905 拓扑与 AP-Autoconfig 的线上校验 |
| [SCHEMA.md](SCHEMA.md) | `MultiAPCfg` 50 字段的还原（偏移、原厂读写、CM 转换）——**查原厂字段时仍然先读这份** |
| [POLICIES.md](POLICIES.md) | Master/Domain 策略表的还原——同上 |

之后的服务模式（B.4 起）见 [../SERVICE.md](../SERVICE.md)，中兴原厂路由器联调见 [../REAL_CHILD.md](../REAL_CHILD.md)。
