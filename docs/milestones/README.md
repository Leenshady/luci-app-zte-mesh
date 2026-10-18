# 早期里程碑（历史记录）

这些是 2026-10-10 到 10-12 各里程碑当时的验收记录，按时间顺序保留原文；每篇开头有一段状态更新。文中的
`controller_ready=false`、“未接入真实子节点”“M2 尚未完成”等都是当时的状态，现在的状态见仓库 [README](../../README.md)。

| 里程碑 | 文档 | 内容 |
|---|---|---|
| B.1 / B.2 | [DATABASE.md](DATABASE.md) | 原厂数据库引擎在私有命名空间里跑通 |
| B.2.1 | [SCHEMA.md](SCHEMA.md) | `MultiAPCfg` 50 字段还原、原厂读写与 CM 转换 |
| B.2.2 | [POLICIES.md](POLICIES.md) | Master/Domain 策略表 |
| B.2.3 | [WLAN_PROFILES.md](WLAN_PROFILES.md) | WLAN/DevInfo 表布局 |
| B.2.4 | [CONFIG_BRIDGE.md](CONFIG_BRIDGE.md) | ARM64 → ARM32 配置桥（服务后来没用这条管道，复用了它的校验） |
| B.3 | [STARTUP.md](STARTUP.md) | 真实 `multiapd` 在沙箱里初始化 |
| B.3.1 | [CM_PROFILES.md](CM_PROFILES.md) | CM 替身（AP/PSK 读取） |
| B.3.2 / B.3.3 | [WIRE_PROTOCOL.md](WIRE_PROTOCOL.md) | 1905 拓扑查询/应答、AP-Autoconfig 在私有链路上的线上校验 |

之后的服务模式（B.4 起）见 [../SERVICE.md](../SERVICE.md)，真子节点联调见 [../REAL_CHILD.md](../REAL_CHILD.md)。
