# B.1 / B.2：原厂数据库引擎与配置实验

> **状态更新（2026-10-12）：** 本文记录 B.1/B.2 里程碑（原厂数据库引擎实验）。之后又还原并建立了更多表（`P1905ComCfg`、`IPInterface`、`P1905_ACD`、`P1905AcdBssCfg`、`AclCfg`、`WLANWEP`、`P1905DeviceInfo`，见 [FINDINGS.md](FINDINGS.md) 第 12 节）。`libdbcspview.so` 仍然缺失，但不阻止现有表。

**通过的是 DB 服务端/客户端与八表逻辑配置边界，不是 Controller 启动验收。**
B.1 验证真实缺失配置错误；B.2.1/2 恢复 Cfg/Master/Domain；B.2.3 新增五张
WLAN/DevInfo 表 metadata，共 391 字段。WLAN 只使用公开 fixture 和纯 CF GET，
不读取原厂 default/凭据，不伪造成功，不运行 `cspd`/`multiapd`。
**当前实验命令运行八表扩展版**，最新验收见 [WLAN_PROFILES.md](WLAN_PROFILES.md)，
历史策略与 Cfg ABI 见 [POLICIES.md](POLICIES.md)、[SCHEMA.md](SCHEMA.md)。

## B.1 历史设备验收

`root@192.168.5.1`，已启用 ARM32 compat 的 Linux 6.18.38+：

```text
ZTE_DB_ENGINE server_initialized=1 shared_bytes=4194304 callbacks_available=0
ZTE_DB_ENGINE client_attached=1 allocator=1 missing_view_status=-2
ZTE_DB_ENGINE factory_map_query=1 config_missing=1 map_status=-1 payload_logged=0
ZTE_DB_ENGINE PASS shared_counter=2 native_engine=1 controller_ready=0
ZTE_DB_SANDBOX private_ipc=1 private_net=1 readonly_root=1 caps=IPC_OWNER
ZTE_MESH_SHIM call=dbAPIGetView view=MultiAPCfg
ZTE_MESH_SHIM return=dbAPIGetView status=-2
```

服务端调用原厂 `DBShmSrvInit`，初始化空表/视图链表与原厂哈希；
客户端经过 fork + exec，在新的 ARM32 地址空间调用原厂 `DBShmCliInit`。
随后验证原厂共享内存池的加锁/分配/释放、缺失视图错误以及真实
`MapGetDB → MapGetMultiAPCfgDB → dbAPIGetView` 调用。
服务端观测到两次跨进程读取计数；退出前移除实验共享内存。
宿主 `/proc/sysvipc/{shm,sem}` 和网络接口名称前后枚举一致。

`libdbcspview.so` 仍未提供，实验中 `g_pShmHandler == NULL`。
这次测试证明其缺失不阻止基础 DB/锁/分配器工作；**不代表需要 callback
的业务视图可以正常运行**。`dbcGetView` 对通用视图和 dlsym callback 视图
采用不同路径，仍需按真实 Controller 查询逐项恢复。

## 可重复运行

```sh
# 只编译/做离线回归，不会自动部署或接入设备网络。
sh port/mesh-shim/build.sh

# 显式进行设备侧数据库实验。
python3 port/mesh-shim/device-db-smoke.py --host root@192.168.5.1
```

主要产物：

- `out/mesh-shim/db-engine-probe`：ARM32 原厂 glibc 2.26 的 DB 实验程序。
- `out/mesh-shim/ns-run`：AArch64 静态隔离启动器。
- `out/mesh-shim/device-db-report.json`：最新报告；单表状态为 true，整体 schema/Controller 仍为 false。
- `out/mesh-shim/multiap-cfg-schema.json`：50 个字段的原始类型、默认值和视图 offset。
- `out/mesh-shim/device-db/`：校验、宿主前后状态和完整 stdout/stderr。

程序没有用交叉工具链默认 glibc 链接最终 ELF；显式用 crt objects 和原厂
库链接，实际只要求 `GLIBC_2.4`。所有强符号和 GNU 版本需求均通过闭包审计。
原厂 DSOs 的部分符号由 multiapd 的其他直接依赖提供，单靠各库自身的
DT_NEEDED 不足，因此实验程序仍加载审计过的 27 库闭包。

## 隔离与失败行为

`ns-run` 只允许固定的 `/bin/db-engine-probe` 命令，不允许启动原厂主程序：

1. 在 fork 后创建新的 IPC、网络、mount namespace，并检查三个 namespace
   inode 与宿主不同；失败退出 80，绝不退回宿主 IPC。
2. mount propagation 设 private；实验根目录 bind-remount 为只读，提供
   私有 tmpfs `/tmp` 和 `/dev/shm`，不挂载 `/proc`、硬件设备或 MTD。
3. 关闭额外 fd，清空继承环境，限制内存/CPU、关闭 core dump，启用
   no-new-privileges；只保留 `CAP_IPC_OWNER`。
4. 原厂 DB 创建共享内存的权限位为 000，因此实验需要 `CAP_IPC_OWNER`
   来使用真实引擎；不保留 SYS_ADMIN、NET_ADMIN、NET_RAW 或 SYS_RAWIO。
5. 固定加载本实验根目录内的 ARM32 垫片，显式开启 trace；此时已有真实
   后端，所有 DB 调用继续原样透传，不伪造返回值。
6. 15 秒总时限，结束或超时清理整个实验进程组。IPC namespace 销毁时
   回收其中遗留的 semaphore/shared-memory 对象。

新增 9 个隔离启动器回归场景，覆盖错误路径、symlink 逃逸、禁止启动
multiapd/cspd、命令/库路径替换、额外参数、shell 文本与 namespace 被拒绝。
这些测试不是完整沙箱安全审计；此启动器目前仅适用于此受控 DB 实验。

## 已恢复的 ABI 证据

基线文件：`out/mesh-shim/runtime/lib/libdb.so`，SHA256
`b8316a82f3726eddcf96dfa997c7a55bd08f825dc9808167600f751c302f7a1f`。

| 项目 | 恢复结果 / 位置 |
| --- | --- |
| `DBShmSrvInit` | `0x6cf0`；key 65535，4 MiB，flags `0x600` |
| `DBShmCliInit` | `0x6e80`；同 key，大小/flags 为 0 |
| attach 地址 | `0x60000000`；原厂未检查 shmat 失败 |
| 全局指针初始化 | `0x6b48`；表头 +0，视图头 +8，get counter +2164 |
| pool | base +2200；free-list、统计、原厂 allocator |
| DB lock | semaphore key `0x30004`；`libcommfun.so:cspLockInit @0x63b8` |
| table/view hash | `dbInitHashTble @0xee5c`；600/500 buckets |
| get ABI | size at +0，magic `0x01020304` at +4；缺表返回 -2 |
| MultiAPCfg 查询 | `libmultiapcfapi.so:MapGetMultiAPCfgDB @0x13c4`，2952 bytes，空 instance |
| 高层 query | `MapGetDB @0x14e0`；DB query 失败转为 -1 |

两个 init 在成功路径最终返回共享内存中的地址，而不是约定的 0 成功码。
因此实验用实际 mapping、全局指针、锁和计数验收；垫片保留原返回值，
但只记录 `returned=1`，不把它作为 status 打印，不泄漏指针。
同一进程重复 attach 固定地址会失败，故客户端必须 exec 新地址空间。

## B.2 八表逻辑 fixture 已通过；下一步为配置桥与 CM facade

已定位同基线 `cspd` 中的生成式注册函数，仅做静态分析，不运行它：

```text
cspd SHA256: 8b8477e2598660653c27d796fcaaa5484f879cd263fb6caa5e0a982b1cee5ac1
0x1f16c0 dbCreateMultiAPConfigTbl
0x1f1274 dbDefMultiAPCfg
0x1f2218 dbCreateMultiAPMasterTbl
0x1f1cd8 dbDefMultiAPMaster
0x1f2900 dbCreateMultiAPDomainTbl
```

Cfg/Master/Domain 的注册字段、原厂类型、长度、primary/save 标志和测试行
已恢复并原生验收；源机器码逐项核对见 [SCHEMA.md](SCHEMA.md) 和
[POLICIES.md](POLICIES.md)。后续新增五张 WLAN/DevInfo 表布局和纯 CF 逻辑
查询，见 [WLAN_PROFILES.md](WLAN_PROFILES.md)。仍需 UCI/helper、安全模式/
真实身份映射及 Controller-only CM facade。不能把原始 ARM32 结构直接当成
RPC，也不能复制原厂凭据或执行含硬件/无线操作的默认初始化函数。
完整 OSS/internal task/socket 启动和节点管理尚未验收。
