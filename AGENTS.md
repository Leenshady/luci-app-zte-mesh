# zte-mesh / luci-app-zte-mesh

给 OpenWrt / ImmortalWrt 做的中兴组网插件：把原厂 ARM32 `multiapd` 当控制器跑在私有沙箱里，周围用数据库行、CM 函数替身和服务配置文件指挥它，**不重写 mesh 协议**。中兴侧保留原厂固件，不刷机。

- 功能总账只有一份：[docs/FEATURE_STATUS.md](docs/FEATURE_STATUS.md)。排期只有一份：[docs/PLAN.md](docs/PLAN.md)。别处出现的"已完成/未做"都不算数。
- 原理与源文件职责：[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)。文档分三层，写在 [README.md](README.md) 的「文档」一节：要跟着改动更新的四份（`FEATURE_STATUS`/`PLAN`/`INSTALL`/`DEVELOPMENT`）、改代码前读的原理四份（`ARCHITECTURE`/`SERVICE`/`LAB`/`REAL_CHILD`）、其余一律是**只读资料**（`docs/research/`、`docs/milestones/`、`FINDINGS`、`CONTROLLER_CAPABILITIES`、机型对照、`docs/journal/`）——归档时都加了冻结说明，结论变了不回去改它们。

## 动手前先读

| 要做什么 | 先读 |
|---|---|
| 构建、跑控制器、测试、分支与 `prebuilt/` | [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) |
| 装机上手、页面两个开关怎么摆出三种拓扑、卸载与"设了没动静" | [docs/INSTALL.md](docs/INSTALL.md) |
| 碰测试机（`ax3600` 1.1 / `owrt` 1.20 / `ubuntu` 1.191） | [docs/LAB.md](docs/LAB.md) 第 2.1 节——机器清单的唯一权威，统一走 `python tools/lab/rssh.py`；口令在**本机** `.local/lab-env.md`（已 gitignore，不入库） |
| 上真子路由中兴 Z506（又叫中兴小方糖） | [docs/REAL_CHILD.md](docs/REAL_CHILD.md) + 下面的真机红线 |
| 改服务模式、UCI/LuCI 映射 | [docs/SERVICE.md](docs/SERVICE.md) |
| 查原厂数据库字段 | [docs/milestones/SCHEMA.md](docs/milestones/SCHEMA.md)、[docs/milestones/POLICIES.md](docs/milestones/POLICIES.md)、[docs/research/E2631_DB_SNAPSHOT.md](docs/research/E2631_DB_SNAPSHOT.md) |

## 常用命令

```sh
sh build.sh                                          # 提取运行环境、审计、编译、主机测试、离线加载测试
sh tools/build/container-make.sh test-host           # 只跑主机测试；单个产物：container-make.sh db-engine-probe
sh tools/build/runtime.sh svctest                    # 测试环境 tmpfs 运行包
sh tools/build/runtime.sh install                    # 重新生成随仓库提交的 openwrt/zte-mesh/zte-mesh-runtime.tar.gz
python3 dev-service-smoke.py --no-attach --seconds 24 # 路由器上完全私有地跑一次服务（不接 LAN）
python tests/test_i18n.py                            # 改了界面文案必须跑
```

`tests/` 里 C 编解码、Python、shell、LuCI JS 各管一段，改哪层跑哪层的测试；CI（`.github/workflows/build-apk.yml`、`build-lab-tools.yml`）**全手动触发**——push 不会跑构建。

节奏按 [docs/PLAN.md](docs/PLAN.md) 第 2 节与第 6 节：**攒够几项（一批 3–5 项）才触发一次 CI，装一次机验证**，不是每完成一项就跑；本机每改一项就把能跑的测试跑绿。需要真机的项**不阻塞**，登记到 PLAN 第 5 节（H-xx）后继续做主机可测的项。

## 红线

**真机**（细则见 DEVELOPMENT 第 5 节）

- 向真机推配置会真实且持久地改变设备设置。每轮前后用 `ZTE_TN_PASS=… sh tools/child/childread.sh` 读回 8 行 `WLANCfg`，与 REAL_CHILD 末尾基线逐行比对。
- **永远不推占位值**：额外 BSS 的 `enabled/ssid/key` 三项必须给齐，同一无线电槽位必须是 1..k。
- **不改子路由的模式**（`Mode`、自组网 V2、`VersionMode`、MESH 开关）——那是用户在子路由上自己设的。
- 会让用户设备掉线的测试（关主 SSID、ACL 封禁真实终端、信道/功率、改 SSID、定时 `scope=all`）**先征得同意，测完立即还原**。
- 不要用 `kill multiapd` 逼子路由重新入网；改控制器重启、`zte-meshctl renew-acd <band>` 或重启子路由。

**秘密**

- 仓库里不放任何密码和密钥。telnet 口令只从环境变量 `ZTE_TN_PASS` 读；Wi-Fi 密钥只写进 0600 临时文件，用完删除。
- `evidence/`、`out/` 含原厂二进制与 WSC 材料，不入库、不外传。

**版本控制**

- 默认**只 commit，不推送**。`main` 直连 ImmortalWrt feed，推上去构建机立刻吃到。日常只在 `main` 上做事，不要开长期分支；`feat/child-node` 是**向上游提 PR 用的**那条，别拿它当开发分支。

## 取证流程（重活前三遍核对，别再犯）

1. 上机 `sendcmd 1 DB p` 之前先查 `SCHEMA.md` / `POLICIES.md`——字段级还原多数已经做过，重新从 `cspd` 字符串表推断边界会重做已完成的活还做错。
2. **表名不许猜**：先 `sendcmd 1 DB all` 列全集（`DB l` 不存在）。`IGD.P1905CFGn` 是行名不是表名。
3. `RowCount=0` ≠ 表不存在（表在行空、按需建行、运行时填充都算存在）；只有 `table(X) not find` 才是表名错。
4. 列名以实测为准，离线切分只能当假设。
5. "缺语义"不是"缺数据"：位定义类问题靠反汇编或 Multi-AP 规范正文，dump 再多表也解不出来。
6. `isSave=0` 的列只能读状态，不能当配置比对。

## 收尾

改完一个结论，三处同步，一处不落：

1. 受影响的**活文档**：`FEATURE_STATUS`（状态与判据）、`PLAN`（排期/H-xx），涉及操作的还有 `INSTALL`/`DEVELOPMENT`
2. `docs/journal/YYYY-MM-DD.md`——开发日志写在这里，**随 commit 走**
3. `git commit`（不推送）

只读资料不在这三处里：取证存档（`research/`、`milestones/`、`E2631_*`）记录的是"当时在那台机器上看到什么"，结论变了不回去改它们——要写新结论就写进上面第 1 项。查原厂字段仍然照 `SCHEMA.md`/`POLICIES.md`，冻结指的是不再同步，不是不许读。

提交前跑坏链与行尾检查；新文件用 LF（`.gitattributes` 已全局 `text=auto eol=lf`）。

**用词**：不造词，通用缩写保持英文（写 SSID，不写"无线名称"）。`air`→"本机当前广播的 SSID"、`seed`→"SSID 初始配置"、`per-BSS`→"按 BSSID"、`applier` 原样引用；代码标识符与 `verify-install.sh` 的键名不译。机器一律称**测试主机 / 测试环境**（不写"实验台/实验室"），机型首次出现写全名：小米 AX3600、中兴 Z506（又叫中兴小方糖）、ZXSLC SR1010（又叫中兴星云 MAX）。对照表在 [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) 第 9 节。

> `docs/journal/` 原名 `.workbuddy/memory/`，2026-11-02 迁入仓库。工具自带的会话记忆是本机文件，不随 clone 走——能让下一个人看到的只有这个目录。
