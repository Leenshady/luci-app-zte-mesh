# 原厂二进制取证：`dbAPIGetView` 与 WlanRenew 触发链（REA + Ghidra 12.2）

日期：2026-11-26
方法：`rea` 6.10.1（Ghidra 12.2 headless）在 vm_ubuntu 上对 ARM32 原厂 ELF 做伪代码提取；符号缺失处用自写的
`capstone`/`pyelftools` 反汇编脚本补证。所有地址均为**该文件自身的装载地址**（Ghidra 默认基址，与 `readelf` 的 `st_value` 一致）。

标注约定：**【确定】** = 有反编译/反汇编证据 + 地址；**【推测】** = 只有调用关系或字符串佐证。

## 0. 取证对象与身份

| 文件 | 来源 | 大小 | sha256 |
|---|---|---|---|
| `libdb.so` | 运行包（与本仓库 `out/` 中一致） | 92248 | `b8316a82f3726eddcf96dfa997c7a55bd08f825dc9808167600f751c302f7a1f` |
| `multiapd` | 运行包（与 shim 实际运行的一致） | 988140 | `a0ba6beeb1eb4e0c900b24d06e9d37e7174f9f640320d12803d7d80c4276280a` |
| `cspd` | 固件 `V1.0.0.1B5.8000` rootfs（`kmoduletmp/kmodule.img` 内 `bin/cspd`） | 3106388 | `eb7062b96f8885035a719fb428a6b100933d8be0c7fe5d65876550bd49106250` |
| `libmultiapcmapi.so` | 同上（固件版） | — | `f81ff838fa0f4fd0a4411866d0b0da0654e9b6ce00faed4ca827afc9e2f71d28` |
| `libmultiapcfapi.so` | 同上（固件版） | 16924 | `46025f72f6b461a5ba0908547fd8375d738a7dd792824012389ba62b5151763b` |

**注意：仓库运行包里的 `libdb.so`/`multiapd`/`libmultiapcmapi.so` 与固件里的同名文件不是同一次构建**
（sha256 全不相同；`libmultiapcmapi.so` 的 `CmMultiAPWlanRenew` 在运行包是 `0x3780`，在固件是 `0x3034`）。
下文 `libdb`/`multiapd` 的地址属于**运行包**那一份（shim 真正加载的就是它），`cspd` 与
`libmultiapcmapi.so`/`libmultiapcfapi.so` 的地址属于固件那一份。

固件获取路径（可复现）：`dump.bin`（134217728 B，sha256 `334ed096…`）→ `tools/firmware-layout.py` 取槽 rootfs
（23068672 B，位于 13500416）→ AES-128-ECB 解（密钥 `SR1010V102020030`，见研究仓库
`docs/reverse-engineering/current-firmware-dump-20260806.md`）→ `jffs2-extract.py` → `kmoduletmp/kmodule.img`
（SquashFS 4.0 / xz，18165760 B）→ `unsquashfs`。rootfs 顶层的 `bin`、`lib` 是指向 `kmodule/` 的符号链接，
**cspd 不在 rootfs 表层**，这是本次修正的前置判断（上一轮假设的“rootfs `bin/cspd` 直接可取”不成立）。

## 1. `dbAPIGetView`：按 32 字节视图名，在共享池锁下取整表

### 1.1 `dbAPIGetView` @ `libdb.so` `0x3e38`（284 B）【确定】

```c
int dbAPIGetView(undefined4 vid_or_ctx, int pViewName, int pOut)
{
  (*g_pGetCount)++;
  if (pOut == 0 || pViewName == 0)            return -0x15;   // EINVAL
  if (oss_strnlen_s(pViewName, 0xffff) >= 0x20) return -0x1a;  // 名字超过 31 字节
  if (*(int *)(pOut + 4) != 0x01020304)        return -0x13;   // 调用方结构体 magic 不对
  iVar1 = ShmPoolLock(30000);                                // 共享配置池锁，30 s
  if (iVar1 == 0) { iVar1 = -0x2d; goto fail; }               // 拿不到锁
  iVar1 = dbcGetView(vid_or_ctx, pViewName, pOut);            // 真正读表
  ShmPoolUnlock();
  if (iVar1 == 0) return 0;
fail:
  ProcUserLog("source/dbl_api.c",0x12f,"dbAPIGetView",6,0,0,
              "get view failed VID(%s) VN(%s) Err(%d)", vid_or_ctx, pViewName, iVar1);
  return iVar1;
}
```

要点【确定】：

1. 视图名上限 **32 字节含结尾 0**（`>= 0x20` 直接 `-0x1a`）——与 DB 里 `ViewName[32]` 的宽度一致。
2. 调用方必须传一个**带 `0x01020304` magic** 的结构体（`+4` 偏移），否则 `-0x13`。这就是 shim 里视图描述符的来历，
   不是随便传个缓冲区就行。
3. 读表**全程持有共享池锁**（`ShmPoolLock(30000)`），失败返回 `-0x2d`。
4. 本函数只做参数校验 + 加锁，真正取表在 `dbcGetView`。

### 1.2 `dbAPIGetViewInfo` @ `0x4794`（384 B）【确定】

```c
int dbAPIGetViewInfo(int pOut, uint *pLen)
{
  ...                                        // 0x8000 栈缓冲清零
  if (pLen == 0 || pOut == 0) return -0x15;
  req[0] = 0xb;                              // 请求码 11
  rc = _dbAPISSend(req, 8, g_dwTimeOut, reply, &replyLen);
  if (rc == 0 && reply[0] == 0) {
    if (replyDataLen <= *pLen) { oss_memcpy_s(pOut, *pLen, replyData); return 0; }
    rc = -0x10;                              // 给的空间不够
  }
  ProcUserLog("source/dbl_api.c",0x385,"dbAPIGetViewInfo",6,0,0,
              "get view info fail Err(%d)", rc);
  return rc;
}
```

说明：`db*API*` 走的是 `_dbAPISSend`（对 DB 进程的同步请求，请求码 `0xb` = 取视图信息；长度协商失败返回 `-0x10`）。

### 1.3 `dbcLstView` @ `0xc3cc`（272 B）【确定】

```c
undefined4 dbcLstView(id, arg2, arg3)
{
  tbl = dbFindTbl(id);
  if (!tbl) return -2;                       // 表/视图没注册
  view = *(int *)(tbl + 0x78);
  if (!view) return -4;
  if (*(int *)(view + 0x14)) {               // 视图自带的列举函数
     if (*(int *)(*(int *)(view + 0x14) + 0x10) && g_pShmHandler) {
        f = dlsym(...); if (f) return f(arg2, arg3);
     }
     return -0x19;                           // dlsym 失败/函数指针为空
  }
  if ((*(uint *)(view + 0x18) & 2) == 2) return dbViewGetList(tbl, arg2, arg3);
  return -4;
}
```

**这就是 GAP_ANALYSIS P1 #4 现象的机制**【确定→推论】：表没注册 → `dbFindTbl` 返回 0 → `-2`；
调用方（`multiapd` 的配置读取）拿不到行，只能按“空”处理，于是首条 `PolicyConfigReq` 的厂商体只剩 1 字节。
换句话说，shim 里“少注册一张表”的后果不是空表，而是**整个读视图调用失败**。

## 2. renew 的 AC 侧执行段（`multiapd`，运行包）

### 2.1 事件注册：`0xa93e → _handleWlanRenew`【确定】

事件表（`multiapd` 运行包，`0xe28d8` 起，8 字节一项 `{u32 event; void (*fn)()}`）：

```
0xe28e8: event=0x0000a93e -> _handleWlanRenew (0x6e0c8)
0xe28f0: event=0x000090d3 -> _handleWanPortListen (0x6e0b4)
0xe28f8: event=0x00001102 -> _acdsDiagnose (0x6d974)
0xe2900: event=0x00009092 -> _handleAcdcDo1905Packet (0x73680)
0xe2928: event=0x0000a96f -> _acdcMeshControllerRoleChangeHandle (0x7095c)
0xe2930: event=0x0000a92f -> _acd_handle_bss_status (0x70890)
```

### 2.2 `_handleWlanRenew` @ `0x6e0c8`（132 B）【确定】

反汇编（节选，注释为推断语义）：

```
0x6e0dc: ldrb   r5, [r0]              ; band = msg[0]（唯一的入参信息）
0x6e0e0: mov    r0, #0x34             ; sizeof(WLANCFG) = 0x34
0x6e0f0: mla    r0, r0, r5, r3        ; &g_wlancfg[band]
0x6e0fc: bl     acdGetWLANCFG         ; 取该频段当前配置（只用于日志/后续）
0x6e120..0x6e124: bl <log>(..., 0x735, ...)
0x6e128: sub    r0, fp, #0x18         ; 8 字节栈消息 {u8 band,0,0,0,0,0,0,0}
0x6e12c: bl     _acdsSendApAutoConfigRenew
0x6e130: mov    r0, r4                ; return 0
```

**关键否证**【确定】：`_handleWlanRenew` 只把 `msg[0]`（band）转发下去，**不做“哪个频段该不该 renew”的判断**；
判断在 cspd 一侧（第 3 节）。这与 GAP_ANALYSIS P0 #2 的结论一致，并且现在有地址级证据。

### 2.3 `_acdsSendApAutoConfigRenew` @ `0x6dc44`（404 B）【确定】

```
0x6dc64: I5MessageInit(0, <模板>)                 ; 建 CMDU（AP-autoconfiguration Renew, msgtype 0x000A）
0x6dc74: ldrb r1, [r6]                            ; band
0x6dc7c: _acdsInsertRenewMsg(msg, band)           ; 插 TLV（含 A-L MAC 等）
0x6dc8c: I5MessageRelayMulticastSend(msg, 0, 0)   ; 组播发出
0x6dc9c: acdGetBasicInfo()                        ; 之后按 0x19c 步长遍历每频段/BSS 做本机记录
```

即：**Renew 是组播 CMDU，一次一个频段**（呼应 FEATURE_MAP 第 4 节“每频段一个 `APAutoconfRenew`”）。

## 3. renew 的判据与投递（`cspd`，固件 `V1.0.0.1B5.8000`）

### 3.1 模块事件表：`0x9c8a → MultiapMgrWlanRenew`【确定】

`MultiapMgrEvent` @ `0x28cb70`，24 字节一项：

```
0xa93c -> MultiapMgrEnableModeChange (0x171e00)     ; SetRoleEnable/Mode 变化 → 重启守护进程
0xa93d -> MultiapMgrRestart          (0x171da4)     ; P1905ComCfg 变化 → 重启守护进程
0x9c8a -> MultiapMgrWlanRenew        (0x171568)     ; WLAN 变化 → renew
0xa93f -> MultiapMgrWlanIsBackhalRestart (0x171500)
0xa96e -> MultiapMgrGetRestartCount  (0x171708)     ; ACD 取启动次数（shim 用 ZTE_MESH_START_COUNT 拦截）
0x1150 ~ 0x1153 -> MultiapMgrTimer1..4Event
```

**这张表直接证实了 SERVICE.md 里的两条既有结论**：`CmMultiAPWlanRenew` 的事件号是 `0x9c8a`；
`CmTopoSetP1905ComCfg`/`CmMultiAPSetMultiAPCfg` 的 `0xa93d`/`0xa93c` 确实落到“重启守护进程”，不是 renew。

### 3.2 `CmMultiAPWlanRenew` @ 固件 `libmultiapcmapi.so` `0x3034`（280 B）【确定】

```
0x3054: strb r0, [fp,#-0xa15]         ; 入参 u8 band 存栈
0x305c: memset(buf, 0, 0xa08)
0x3064: rc = CmMultiAPGetDB(buf)      ; 失败 → return -8
0x3074: ldrb r3, [fp,#-0x9d0]         ; buf+0x50 的使能字节
0x3078: cmp r3,#0 ; beq → log(0x116), return -17
0x30c0: cmp band,#2 ; bhi → return -10
0x30d8: movw r0, #0x9c8a              ; 事件 0x9c8a
0x30d4: sub  r1, r5, #9               ; 负载 = 1 字节 band
0x30d0: mov  r2, #1
0x30e0: bl   <ASEND>(0x9c8a, &band, 1, "cspd.cspd.multiapd_mgr")
```

结论【确定】：**“要不要 renew”在进入 cspd 之前就定了，消息本身只带 1 字节频段号**，频段合法值 `0..2`（2.4G / 5G / 6G），
越界直接 `-10`。这解释了为什么 shim 的 `zte-meshctl renew-acd <band>` 只需要一个 band 参数。

### 3.2.1 `CmMultiAPWlanRenewNotify` @ 固件 `libmultiapcmapi.so` `0x314c`（440 B）【确定】

```
0x31a0: memset(buf, 0, 0xa08);
0x31a4: if (p == 0)                 return -10;
0x31b8: rc = CmMultiAPGetDB(buf);   // 读 MultiAPCfg 到栈缓冲
0x31c4: if (rc)                     return -8;
0x31d0: if (buf[0x40] == 0)         return -17;      // 某个使能位
0x31e0: if (*(u32*)(buf + 0x180) > 1) {              // “整块配置”模式
            ASEND(0x9c8a, p, 100 /*0x64*/, "cspd.cspd.multiapd_mgr");  // 把调用方 100 字节结构体原样发走
            失败 → return -3;
        }
0x324c: if (*(u32*)(buf + 0x20) > 1) {               // 单频段模式
            band = *(u8*)p; ASEND(0x9c8a, {band, …}, 8, …)
        }
```

两种负载形态与 3.3 的 `len == 1` / `len == 0x64` 分支一一对应：**“整块 WLAN 配置（100 字节）”和“单频段”是同一事件的两种用法**，
所以 cspd 侧的 `MapHandleWlanRenewMsg` 才要按长度分派。

### 3.3 `MapHandleWlanRenewMsg` @ `0x17288c`（716 B）【确定】

```c
int MapHandleWlanRenewMsg(void *in, int len)      // 由 MultiapMgrWlanRenew (0x171568) 调用
{
  msg = malloc(0x74); memset(msg, 0, 0x74);
  if (!in || len == 0) { log(0x111); return -1; }
  if (len == 1)        { msg->band = in[0]; msg->kind = 0; }          // 只给频段
  else if (len == 0x64) { msg->band = in[0]; memcpy(msg+8, in, 100); msg->kind = 1; }  // 整块 WLAN 配置
  else { log(0x12f); free(msg); return -1; }

  band = msg->band;
  if (band > 1) { log(0x136); free(msg); return -1; }                 // 只接受 0/1
  seq = ++g_bandRenewSeq[band].counter;                               // 12 字节/频段，跳过 0
  msg->seq = seq;
  if (*(u32 *)(msg + 4) <= 1) delay = 6;                              // msg+4 来自 MultiAPCfg 快照
  else {                                                              
     delay = (MapGetVersionMode() & 2) ? 0 : 2;
     if (MapGetProcessInfo()->pid > 0) EXTRA_SET_TIMER(100 * delay, 2, msg);
     delay = 10;
  }
  SET_TIMER(100 * delay, 1);                                          // 无 ctx：定时器槽 1
  log(band, seq, delay);
  ListAppendTail(&g_tWlanDelayedRenewMsgList, &msg->node);            // msg+0x6c 为链表结点
  return 0;
}
```

要点：

- 频段只有 **0 和 1**（与 `CmMultiAPWlanRenew` 允许 0..2 不同；6G 在这里被拒）【确定】。
- 每个频段一个自增序号（`g_bandRenewSeq[band]`，12 字节结构，`+0x14` 计数，`+0xc` 存 band），**跳过 0**【确定】。
- 延迟常量 **6 / 2 / 10**（单位见下），日志字符串 `Map Renew delay 10 s` 的 10 就来自这里。
  分叉依据是 `*(u32 *)(msg + 4)`（取自 `MapGetMultiAPCfgDB()` 的快照字段，**结论见 3.7：就是 `MultiAPCfg.Mode`**）：
  该值 `<= 1` → `delay = 6`；否则 `delay = (MapGetVersionMode() & 2) ? 0 : 2`，且在 `MapGetProcessInfo()->pid > 0`
  时先给这条消息注册一个 per-message 定时器（`EXTRA_SET_TIMER`，type 2 → `MultiapMgrTimer2Event` → `WlanRenewSend`），
  再把 `delay` 改成 `10` 注册共用定时器（`SET_TIMER`，type 1 → `MultiapMgrTimer3Event` → `WlanRenewToCtr`）。
- **时间单位已在 `liboss.so` 里确认是 10 ms**：`SET_TIMER`/`EXTRA_SET_TIMER` 都跳到 `_setCommonTimer`（`liboss.so 0x1589c`），
  它在 `0x15aa4` 处把入参拆成 `秒 = v / 100`、`微秒 = (v % 100) * 10000`，即 `v` 以 10 ms 计。
  所以 `100 * delay` 就是 **delay 秒**：`2` → 2 s、`6` → 6 s、`10` → 10 s。
  这与 FEATURE_MAP 第 4 节实测“约 2 秒后 `WlanRenewToCtr`”一致（走的是 `delay = 2` 那条）。
- 消息进 `g_tWlanDelayedRenewMsgList` 后由 Timer3 取出 → `WlanRenewToCtr`【确定，调用者扫描】。

### 3.4 `WlanRenewToCtr` @ `0x172730`（348 B）：真正发 `0xa93e`【确定】

```
0x1727d8: ldrb r3, [fp,#-0x9d8]      ; 某状态（0 = ?）
0x1727dc: ldr  r2, [fp,#-0x9f8]      ; 重试/计数
0x1727e4: beq  0x1727f0              ; 若 r3==0 → 只打日志，不发
0x1727e8: cmp  r2, #1 ; bhi → 0x172824  ; 否则只有 r2>1 才真正发
0x172824: ldrb r3, [r5]              ; band
0x172830: movw r0, #0xa93e           ; ← 事件号
0x172834: strb r3, [fp,#-0xa30]      ; 负载[0] = band
0x172838: ldr  r3, [r5,#8]           ; msg->seq
0x172840: str  r3, [fp,#-0xa2c]      ; 负载[4..7] = seq（大端/小端按本机 ARM LE）
0x172828: mov  r2, #8                ; 负载长度 8
0x17284c: mov  r3, #0x3e8            ; 同步超时 1000 ms
0x172850: bl   <SSEND>(0xa93e, buf, 8, 1000, &ack, &ackLen)
0x172854: cmp  r0, #0
0x17285c: mov  r2, r5 ; mov r1, #2 ; mov r0, #0x64
0x172868: bl   <EXTRA_SET_TIMER>(100, 2, msg)   ; 失败 → 1 s 后走 Timer2 的 reset 兜底
```

**结论【确定】**：

- cspd 发给 `multiapd.acd.acd` 的事件是 **`0xa93e`**，负载 **8 字节 = `{u8 band, 0, 0, 0, u32 seq}`**；
  `seq` 就是 3.3 里按频段自增的序号——这也是线上帧里能区分“两次 renew”的依据。
- 发送失败时调 `EXTRA_SET_TIMER(100, 2, msg)`：按第 3.3 节单位，**100 = 1 s**，槽号 2 →
  `MultiapMgrTimer2Event` → `WlanRenewSend`（即改用 3.6(c) 的 reset 路径兜底），不是“重试 2 次”【确定】。
  （这里 `#2` 是定时器槽号，不是次数——这是我上一版写错的地方。）
- `MultiapMgrTimer3Event` → `WlanRenewToCtr`、`MultiapMgrTimer2Event` → `WlanRenewSend` 两条边由调用者扫描得到【确定】。
  `WlanRenewSend` 另发 `0xa221`（见 3.6）。

### 3.5 “哪个频段要 renew”的判定点（`cspd` 的 wlan 模块）【确定】

`CmMultiAPWlanRenewNotify` 由 `cspd` 通过 PLT（`0x1d8e4`）调用，全程序只有 3 处（BL 目标扫描）：

```
CmMultiAPWlanRenewNotify (PLT 0x1d8e4)
  <- wlancfg_hdl_time_action+0x360     (0x151248)   ; Wi-Fi 定时开关到点
  <- handle_wlan_rf_msg.constprop.25+0x470 (0x15817c)
  <- wlan_debug_mapsync+0x114          (0x159718)   ; 调试用全量重同步
```

`handle_wlan_rf_msg` @ `0x157d0c`（1388 B）签名是 `(u16 id, int band)`：

```
0x157d14: mov  r4, r1                 ; band = 第 2 参数
0x157d40: cmp  r4, #1 ; bls  ok       ; band 只能是 0/1，否则打日志(0x1d86)返回
0x157d78: r0 = band ; bl  WlanBaseDbRead   ; 取该频段配置
0x158110..0x158128: memset(buf,0,100); buf[0] = band
0x15812c..0x158150: memset(buf+8,0,0x24); memcpy(buf+8, "cspd.wlan_manage.wlan_mgr", 0x23)
0x158154: strh 0x2401 -> buf+0x2c     ; CM 同步消息类型 0x2401
0x158178: r0 = buf
0x15817c: CmMultiAPWlanRenewNotify(buf)   ; 100 字节整块 → 0x9c8a
```

它的两个调用者都在 `wlancfg_hdl_key`（`0x158378`、`0x1583c0`），按**消息号**分派：

```
0xa417: 若 p[1]==1        → enabled = (p[1]==0) ; handle_wlan_rf_msg(id, band=r7)
0xa408: 若 p[1]==<旧值>   → 同上
0xa42c: ...（另有若干消息号）
```

**这就是完整的判据链**【确定】：

> wlan 侧按频段发来的配置变化消息（`0xa417`/`0xa408`/`0xa42c`…，每条自带 band）
> → `wlancfg_hdl_key` 逐条分派 → `handle_wlan_rf_msg(id, band)`（band 0/1 校验）
> → 组装 100 字节 `{band, 0…, "cspd.wlan_manage.wlan_mgr", 0x2401, …}`
> → `CmMultiAPWlanRenewNotify` → `ASEND(0x9c8a, 100B)` → `cspd.multiapd_mgr`
> → `MultiapMgrWlanRenew` → `MapHandleWlanRenewMsg`（挑 seq、挂延迟）→ `WlanRenewToCtr`
> → `ASEND(0xa93e, 8B {band, seq})` → `multiapd.acd.acd` → `_handleWlanRenew`
> → `_acdsSendApAutoConfigRenew` → 组播 CMDU 10。

也就是说：**“哪个频段变了”在 wlan 模块的入站消息里就已经逐条带上了 band，原厂没有做“整表差分再推频段”的动作**
（这与 FEATURE_MAP 第 4 节“webd 一次写 AP1/PSK1/RD1/RD2，结果两个频段都 Renew”一致——因为 wlan 侧为两个频段各发了一条消息，
不是 webd 自己算出来的）。shim 没有 cspd/wlan 模块，所以**必须自己比较并逐频段发 `0xa93e`**，这正是 GAP_ANALYSIS P0 #2 的形态。

### 3.6 消息号名字、定时器单位、延迟队列（本节补齐 3.6 旧版的三个缺口）

**（a）消息号有名字表。** `cspd` 里有两张 8 字节一项的 `{char *name; u32 event}` 表
（`0x284c60` 起、`0x284e90` 起），用它们把上面那些裸数字对上了名字【确定】：

| 事件号 | 名字 |
|---|---|
| `0xa408` | `EV_KEY_WLAN_ONOFF` |
| `0xa40d` | `EV_KEY_WLAN_ONOFF2` |
| `0xa417` | `EV_KEY_WLAN_OFF` |
| `0xa418` | `EV_KEY_WLAN_ON` |
| `0xa42c` | `EV_KEY_WPS_ACTIVE` |
| `0xa40e` | `EV_KEY_WPS_ACTIVE2` |
| `0xa221` | `EV_WLAN_RESET_BY_CARDID` |
| `0xa96a` | `EV_WLAN_MULTIAP_ROAM_CATON_PARA_CONFIG` |
| `0x9613` | `EV_INTERFACE_UP` |

交叉验证：`0xa96a = EV_WLAN_MULTIAP_ROAM_CATON_PARA_CONFIG` 与 SERVICE.md 里“`SetRoamCatonCfg` 的 `0xa96a`”一致，
说明这张表就是全局事件号表，名字可信。

**（b）定时器单位 = 10 ms。** `SET_TIMER`（`liboss.so 0x16240`）和 `EXTRA_SET_TIMER`（`0x16278`）都跳到
`_setCommonTimer`（`0x1589c`）；后者在 `0x15aa4` 处把入参拆成 `秒 = v / 100`、`微秒 = (v % 100) * 10000`，
即 `v` 以 10 ms 为单位。于是 `100 * delay` = **delay 秒**，`EXTRA_SET_TIMER(100, …)` 的重试间隔 = 1 s。
`SET_TIMER` 的第二个参数是定时器槽号（`_setCommonTimer` 里 `cmp r?, #7` 限制 0..7），
cspd 用 1 号（`MultiapMgrTimer1Event`）和 2 号（`MultiapMgrTimer2Event`）。

**（c）延迟队列是 FIFO 工作队列。** `g_tWlanDelayedRenewMsgList` 是环形双向链表，节点嵌在消息的 `+0x6c`（next）
和 `+0x70`（prev）；`MapHandleWlanRenewMsg` 在尾部追加【确定】。`WlanRenewSend`（Timer2 路径）从表头 `next`
开始 FIFO 遍历，处理完就把节点摘链并 `RetUB`（即 free），所以它是**排空队列**的那条路径。

`WlanRenewSend` 内部按 `msg[1]`（kind）分叉【确定】：

```
if (node[1] == 0) {                       // 只带频段的消息
    band = node[0];
    ASEND(0xa221, &band, 1, "cspd.wlan_manage.wlan_mgr");   // EV_WLAN_RESET_BY_CARDID，异步
} else if (node[1] == 1) {                // 100 字节整块配置
    if (node[0x38] != <常量>) {
        if (strcmp(node+0x10, <旧 SSID/模块名>) == 0) {
            CmSendMsg2MM(node+0x10, node[0x34], node[0x38], node+0x3c, …);   // profile 下发
        }
    }
}
if (node[4] <= 1) ResendSetStaProfileMsg2Wlan();          // 重新下发 STA profile
```

两条路径的发送方式不同，值得记一笔：**renew 用 `SSEND`（同步，超时 1000 ms）发 `0xa93e`，
wlan reset 用 `ASEND`（异步）发 `0xa221`**。原因应在事件注册表：`0xa93e` 在同步请求表里，`0xa221` 不在。

### 3.7 两个缺口的结论（2026-11-26 补齐）

**（a）`*(u32 *)(msg + 4)` = `MultiAPCfg.Mode`**【确定】

1. `MapHandleWlanRenewMsg` 在 `0x1729f0` 处 `ldr r3,[fp,#-0xa00]`、`0x1729f8` 处 `str r3,[r4,#4]`：
   `msg+4` 是从 `MapGetMultiAPCfgDB` 填好的快照缓冲区里**原样拷贝**的。该缓冲区基址 = `fp-0xa24`，
   所以源字段 = 快照 `+0x24`。
2. 快照基址的约定用 `MapGetVersionMode`（`libmultiapcfapi.so 0x13a4`）锚定：它把快照 `+0x164` 的值缓存为
   VersionMode（`0x13ec: ldreq r3,[fp,#-0x8b0]`，缓冲区基址 `fp-0xa14`，差值正好 `0x164`）。
   而 `docs/milestones/SCHEMA.md` 记录 `MultiAPCfg` 的 `VersionMode` 偏移 = 356 = `0x164`、字段基准 = 36。
3. 因此 `+0x24` = 36 = **字段序号 0 = `Mode`**（0 自适应 / 1 Slave / 2 Master / 3 两者）。
4. 交叉验证：`WlanRenewToCtr`（`0x1727d8`/`0x1727dc`）从同一个快照读 `+0x44`（= 68 = `Enable`，字段 1）
   与 `+0x24`（= 36 = `Mode`，字段 0），两个偏移与 `SCHEMA.md` 完全对应。
5. 语义也吻合：`0x172a4c: ldr r3,[r4,#4]; cmp r3,#1; movle r5,#6` —— `Mode <= 1`（自适应 / Slave，
   无 Master 角色）走 6 s；只有 `Mode > 1`（Master / 两者）才进入 `MapGetVersionMode() & 2` 的 0 s / 2 s 快慢分支。

顺带确认 `MapGetMultiAPCfgDB`（`libmultiapcfapi.so 0x119c`）写进缓冲区的头 8 字节：
`+0: 0xa08`（缓冲区长度）、`+4: 0x01020304`（`dbAPIGetView` 要求的 magic，字面量在 `0x1240`，已读）。
这两项落在 `MultiAPCfg` 记录 36 字节头的范围内，所以“字段从 36 起算”对 API 缓冲区同样成立
（上述 `+0x24`、`+0x44`、`+0x164` 三个偏移的实测结果也要求记录基址 = 缓冲区基址）。

**（b）`0xa221` 不跨进程：它是 cspd 内 `wlan_manage` 模块的自处理事件**【确定】

- 在 `multiapd`（运行包）里搜 `0xa221` **零命中**；cspd 里只有两处：名字表 `0x284dec`
  （`EV_WLAN_RESET_BY_CARDID`）与分发表项 `0x238a74`。
- cspd `0x238914` 起是一张 63 项的 `{u32 event; void *handler}` 表，表前紧邻 `wlan_manage.c`、`wlan_mm_main`
  两个字符串（即模块 `cspd.wlan_manage.wlan_mgr`，与 `WlanRenewSend` 的 `ASEND` 目的串一致）。
  表项 `0x238a74` = `{0xa221, 0x162d30}`。
- `0x162d30 = wlan_handle_reset`（124 B）：

```c
if (!payload)              { band_byte = 3; mask = 3; }        // 无参数 → 两个频段都复位
else {
    band = *(u8 *)payload;
    if (band > 1)          { band_byte = 3; mask = 3; }
    else                   { mask = 1 << band; band_byte = band + 1; }
}
WlanRegisterToSntp();                                          // 复位前重新注册 SNTP/时间策略通知器
ASEND(0xa276, &mask, 4, "cspd.cspconfig.wlan_config");          // 按位掩码复位对应频段的 WLAN 卡
wlan_handle_report_event(0x9c8a, &band_byte, 1);                // 向 wlan 模块的订阅者广播 1 字节 band
return 1;
```

补充两条（2026-11-26 补证，原两处“保留”已消）：

- `0x162c2c` 就是 `WlanRegisterToSntp`【确定】，不是“符号名与行为不符”：`WlanTimeCfgDbRead(buf)` →
  `wlan_register_timepolicy_setrule(buf[0x24], *(u32 *)(buf + 0x44))` → `strncpy(buf, "WlanRegisterToSntp", 0x23)`
  → `RegisterSntpNotifier(buf)`（先 enable=0，再 =1）。串名就取自 `0x238542` 的 `"WlanRegisterToSntp"`。
  它放在复位前调用，作用是**在卡复位前把 SNTP/时间策略通知器重新注册**，避免复位把驱动侧规则清掉（后半句为【推测】）。
- `wlan_handle_report_event(0x9c8a, …)` **不是**直发 multiapd_mgr【确定】：它遍历 wlan 模块自己的订阅者链表
  （head `0x4b8138`，项为 `{char dst[36]; u32 mask; next}`，由 `WlanMgrRegisterEvent`（`0x161a34`）用 malloc+strncpy 增删），
  对 mask 命中 bit3 的订阅者逐个 `ASEND(0x9c8a, data, 1, dst)`；订阅者集合运行时决定，静态文件里查不到收件人。
  cspd 内能确认直发 `0x9c8a → "cspd.cspd.multiapd_mgr"` 的是另两处：
  `wlancfg_hdl_guestoff_timer+0x26c`（`0x155d14`）与 `handle_wlan_set_all+0x430`（`0x156930`）；
  两者 payload 都是 **100 字节**（对应 3.3 的 `len == 0x64` 整块配置形态），日志 `wifi change ASEND EV_WLAN_RENEW FreqBand=%d ret=%d`。

结论：`0xa221` 的语义是“**按频段复位 WLAN 卡（band → 位掩码），并向 wlan 模块订阅者广播一次 band**”，
是 cspd 内部 `wlan_manage → cspconfig.wlan_config` 的卡级动作，**不经过 multiapd**；真正把 renew 事件送到
multiapd_mgr 的是 WLAN 配置变化路径（`handle_wlan_set_all` / `wlancfg_hdl_guestoff_timer` 的 100 字节直发）。
shim 没有 wlan 驱动/卡级复位，这条支路在 shim 里没有对应实现；需要对齐 renew 语义的仍是 3.2–3.4 那条 `0x9c8a → 0xa93e` 链。

## 4. 工具链结论（给后续复现的人）

1. **REA 6.10.1 只接受 Ghidra ≥ 12.2，且要求 JDK 25**。Ghidra 11.4.2 + JDK 21 会被 `unsupported_version` 直接拒。
   实测组合：Ghidra `12.2`（`ghidra_12.2_PUBLIC_20261016.zip`）+ Temurin `25.0.5.1+2` + Node `24.22.3` + `rea@6.10.1`。
2. **Ghidra 的 Linux x86_64 原生反编译器**是硬依赖，Windows 侧的 REA 只能处理 PE，所以 ARM ELF 必须在 Linux 上跑。
3. 小目标（`libdb.so`，428 函数）`rea inspect` + 3 次 `rea decompile` 在 2 核 / 3.8 GB 虚拟机上约 3 分钟；
   大目标（`cspd`，3.1 MB）`rea inspect` 约 10 分钟；之后每次 `rea` 调用都会**重新分析**一遍，所以 cspd 上不要指望交互式地反复问。
4. 名字歧义要显式给地址：`dbAPIGetView` 在 `.symtab`/`.dynsym` 各出现一次，`rea decompile` 会报
   `procedure name is ambiguous: dbAPIGetView; [0x13344, 0x13e38]`，用 `0x3e38` 这种入口地址才唯一。
5. 磁盘：Ghidra 12.2 解压 885 MB，JDK25 约 300 MB，固件 rootfs + squashfs 约 300 MB，VM 根分区只剩 ~3 GB，需边用边删。
6. **PLT 桩解析（省时间的一条）**：这批 ARM ELF 的 `.plt` 是 “20 字节 PLT0 头 + 12 字节/桩”，桩顺序与 `.rel.plt` 顺序一一对应，
   所以 `stub(i) = plt_addr + 20 + 12*i`。校验：`CmMultiAPWlanRenewNotify` 在 `.rel.plt` 的 idx=499 →
   `0x1c180 + 12*499 = 0x1d8e4`，与 REA 报的候选地址完全一致；`memset`(idx 240) → `0x1ccc0` 也吻合。
   按 20 字节/桩去猜会得到 `inet_ntop`、`KILL_TIMER` 这类明显错误的映射（我踩过）。

## 5. 对 shim 的三条直接结论

1. **视图/表必须真的注册**：`dbcLstView` 在 `dbFindTbl` 失败时返回 `-2`，调用方不会拿到“空表”而是“调用失败”，
   于是 `PolicyConfigReq` 的厂商体退化成 1 字节。shim 启动时注册 `MultiAPRoamCatonCfg`/`WLANSetting`/`MultiAPCacheMac`
   是对的，而且任何一张漏注册都会以同样的方式失败。
2. **renew 的 band 由 cspd 的 wlan 模块逐条给出、由 `multiapd` 执行**：`multiapd` 侧 `_handleWlanRenew` 不判断频段；
   shim 直接发 `0xa93e`（8 字节 `{band,0…}`）在语义上等价于 cspd 的 `WlanRenewToCtr`（后者多带一个 `u32 seq`），
   这一点现在有地址级证据（`cspd 0x172824-0x172868`）。
3. **“哪个频段变化”在原厂是 wlan 模块的入站消息属性，不是一次整表差分**（第 3.5 节）。
   shim 没有 cspd，所以要么在写库前按字段变化自己算频段、逐频段发 `0xa93e`；
   要么把“整表差分”当成近似实现——后者会在一次提交改动两个频段时把两个频段都 renew，
   在只改一个频段时也只 renew 一个（两者结果相同），但在“改了 5G 专属字段而 2.4G 配置未变”的情形下，
   原厂只发 5G，差分实现也只会算出 5G，行为一致。**所以 shim 现有的“按频段摘要比较”做法与原厂等价**，
   前提是摘要覆盖的字段集合和 wlan 模块关心的字段集合相同（`BeaconType`、`ESSID`、隐藏、PSK、使能、ACL 等，见 GAP_ANALYSIS P0 #3）。
