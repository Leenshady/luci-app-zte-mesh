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
| `libcmapi.so` | 同上（固件版） | — | `289d6454a294fa9b20101d2c39aa311e5be5ab4eb114200563dccb2efe02b87e` |
| `multiapd` | 同上（固件版；第 6 节的地址用这一份） | — | `0733836ae67ffb4fab33022960a213279330b3724e41649b512531c520571817` |

**注意：仓库运行包里的 `libdb.so`/`multiapd`/`libmultiapcmapi.so` 与固件里的同名文件不是同一次构建**
（sha256 全不相同；`libmultiapcmapi.so` 的 `CmMultiAPWlanRenew` 在运行包是 `0x3780`，在固件是 `0x3034`）。
下文 `libdb`/`multiapd` 的地址（除第 6 节标明固件版外）属于**运行包**那一份（shim 真正加载的就是它），`cspd`、`libcmapi.so` 与
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

更新（2026-11-26）：单频段腿实为 `SSEND(0xa93e)`，见 6.4；偏移基准见 6.4 的说明（本节 `+0x40/+0x180/+0x20` 以 `r5` 为基址，与 6.4 的 `+0x44/+0x184/+0x24` 相差 4 字节）。

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

## 6. A 档补充取证（2026-11-26）：可选视图 schema、B15、VersionMode、Renew 触发点

对象仍是固件那一份构建：`cspd` `eb7062b9…`、`libcmapi.so` `289d6454…`、`libmultiapcmapi.so` `f81ff838…`、
`libmultiapcfapi.so` `46025f72…`、`multiapd` `0733836a…`。方法：这三个二进制都**未 strip**，用符号表定位后逐函数反汇编，
再加一个“受限 ARM 直线模拟器”截获构造函数的实参（不运行原程序、不执行 cspd 本体）；**本次没有用 Ghidra**。
原始数据留在 VM 的 `~/rea/views7.json`、`~/rea/a234.json`（只在 VM，不入库）。

### 6.1 七张可选视图的字段表（构造函数级）【确定】

| 视图 | 构造函数 | 字段数 | domain 字节 | 描述符里的名字 |
|---|---|---|---|---|
| `EthPortConfProduct` | `0x1b5348` | 24 | 1700 | `EthPortConfProduct` |
| `InterfaceStatus` | `0x1ba544` | 17 | 1224 | `InterfaceStatus` |
| `MultiAPDualBand` | `0x1d0390` | 4 | 340 | `MultiAPDualBand` |
| `P1905AcdVenderCfg` | `0x1cd6dc` | 3 | 272 | `P1905AcdVenderCfg` |
| `P1905StaBind` | `0x1cdff4` | 4 | 340 | `P1905StaBind` |
| `P1905ZTEMANUF` | `0x1cded8` | 2 | 204 | **`P1905ZTEMANUF`（全大写）** |
| `WLANAD` | `0x1c9988` | 53 | 3672 | `WLANAD` |

七张的构造函数结构完全一致：`memset` → `dbCreateTbl` → `dbCreateDomainNew`；domain 每项 68 字节
（`name_ptr @0`、`dtype @8`、`save @13`、`default_ptr @24`），最后一项 name 为空串作终止。
字段表（`名字:类型`）直接来自截获的 domain，不是从字符串猜的：

```
EthPortConfProduct -> ViewName:2 TR098ViewName:2 RViewName:2 IFName:2 Enable:15 PortIsolateEn:15
  FlowCtlEn:15 AutoNegEn:15 MaxBitRate:14 DuplexMode:14 InLoopBack:15 OutLoopBack:15
  MacLimitEn:15 MacLimit:13 MACAddress:18 SupportMaxBitRate:14 AliasName:3 LastChange:13
  LowerLayers:4 SupportUPstream:14 UpStream:15 EEECapability:15 EEEEnable:15 HideListView:15
MultiAPDualBand   -> ViewName:2 StaMac:18 LazyLegacyFailedLmt:13 InBlackTimer:13
P1905AcdVenderCfg -> ViewName:2 Subtlvtype:13 Enable:15
P1905StaBind      -> ViewName:2 STAMac:18 TargetMac:18 TargetBand:14
P1905ZTEManuf     -> ViewName:2 ManufName:2
```

要点【确定】：

- `P1905AcdVenderCfg` 只有 `Subtlvtype` + `Enable` 两列，**一行一个子 TLV 类型**；“启用清单”就是这张表的行集合，
  而不是一个位图字段。两列的默认值都是空串。
- `P1905ZTEMANUF` 的注册名是全大写，与符号 `dbCreateP1905ZTEManufTbl` 的大小写不同——引用这张表必须用全大写。
- `P1905StaBind` 的定位是 `{STAMac, TargetMac, TargetBand}`，与 D6“STA 绑定走 `0x2401` 本地命令、出厂 0 行”一致。
- `MultiAPDualBand` 只有 4 个字段（`StaMac` + 两个阈值/定时），不是一张“双频配置”表。
- `EthPortConfProduct` 是这批里少数带真实非空默认值的表：`RViewName='IGD.LD1'`、`Enable='1'`、`MacLimit='2048'`、
  `MACAddress='00:00:00:00:00:00'`。
- `InterfaceStatus` 与 `WLANAD` **全部字段 `save=0`**（状态类，不落盘）；`WLANAD` 的 53 个字段里含
  `MACAddr`/`IPAddr`/`IPV6Addr`/`MLOLinkMacAddr` 与 `CurrentMode`（类型 0，`schema.py` 的 `TYPE_LENGTHS` 未收录）。
- `WLANAD` 构造时有一个未建模调用 `WLAN_MAX_DWORD(0x80,0x400)`，它只写描述符 `+8`（在 `dbCreateTbl` 之前），
  不触碰 domain，所以字段与默认值仍然完整；它的业务含义未证。

### 6.2 B15：`CmWLANMapSsidCfgSync2Repeater`【确定】

- `libcmapi.so` `_CmWLANMapSsidCfgSync2Repeater @0x128e64`（1696 B）；公开壳 `@0x129504`（24 B，置 `force=1`）；
  `…ByCard @0x12951c`（612 B，`force=0`，循环 `idx 0..3`）。
- 签名 `(char *szInstIdentity, SSID_CFG *ptSsidCfg, int bRequireExtender)`；空指针返回 `-10`。
- 读 `WlanCfgDbRead`（WLAN 配置，用 bit `0x40` 判 enable）与 `WlanSTAProfileInstIdDbRead`（`STAPROFILE`），
  逐字段比较 SSID、AuthMode、EncryType、key 和分频段 SSID，**有变化才** `WlanSTAProfileInstIdDbWrite` 整块写回。
- **只写数据库**：没有 ASEND/SSEND、没有 `dbAPI*View`。返回 `0 / -1 / -6 / -8 / -9 / -10 / -257`。
- 调用者：`nm -D cspd` **零命中**；`multiapd` 导入（`U CmWLANMapSsidCfgSync2Repeater`），
  路径 `acdcConfigM2EncryptedSettings+0x388 ← acdcParseM2Msg ← _acdcRcvApAutoConfigWSCM2`。

含义：它发生在 **M2 应用**路径上，把主控下发的 SSID 同步进本机 `STAPROFILE`。沙箱里做成 no-op 不会漏事件，
但会漏掉“中继侧 SSID profile”的落库——如果以后 slave 角色出现“配置来了、日志说应用了、库里没有”的现象，先查这里。

### 6.3 `MapGetVersionMode` 的位语义

- `libmultiapcfapi.so` `MapGetVersionMode @0x13a4`（184 B）：静态缓存 `dwVersionMode @0x12ae4`，初值 **9（未初始化）**；
  为 9 时 `MapGetDB` 读 `MultiAPCfg +0x164`（356）作缓存，失败返回 **0**（不是错误码）。
- cspd 里只有 3 处调用：
  - `MapHandleWlanRenewMsg @0x172a5c` → `0x172a60 tst r0,#2`、`moveq r8,#2` / `movne r8,#0` ——**bit1 已证**：置位 0 s、清位 2 s（再挂 10 s）。
  - `handle_wlan_set_all @0x15717c` 把原值透传给 `set_multiap_version(&card+0x14c, ver)`（无掩码；失败日志 `call set_multiap_version fail`）。
  - `@0x157854` 只调用、丢弃返回值（刷新缓存）。
- **bit0 与 bit2 以上没有观察到任何掩码消费点**；`VersionMode` 是功能位掩码这一条不变，但“每一位是什么”仍未证（只有 bit1 有证据）。

### 6.4 `CmMultiAPWlanRenewNotify` 的两条腿与 16 个触发点【确定】

`libmultiapcmapi.so @0x314c`（440 B）：

1. `CmMultiAPGetDB` 失败 → `-8`；`buf+0x44 == 0` → `-17`（`mvn r0,#16`）。
2. `buf+0x184 > 1` → `ASEND(0x9c8a, p, 100, "cspd.cspd.multiapd_mgr")`；失败 → `-3`。
3. 否则 `buf+0x24 > 1`（即 `MultiAPCfg.Mode > 1`）→ 组 8 字节 `{band,0,0,0,0,0,0,0}`，
   `SSEND(0xa93e, blk, 8, timeout=1000, dest "multiapd.acd.acd")`；**SSEND 失败才回退**第 2 条那个 100 字节 ASEND；
   两条都不走 → `-17`。

> 偏移基准说明：本节以 `CmMultiAPGetDB` 的 memset 起点（`r9 = fp-0xa24`）为基址，得 `+0x44 / +0x184 / +0x24`；
> 第 3.2.1 节当年的 `buf[0x40] / buf+0x180 / buf+0x20` 是以 `r5 = fp-0xa20` 为基址的同一批字节，两者相差 4 字节。

调用点共 **16** 处（这批调用全部组 100 字节块：`memset 0x64` → `strh #0x2401 [blk+0x2c]` → `snprintf(blk+0x34,…)`，**没有单字节 band 形态**）：

- cspd 3 处：`wlancfg_hdl_time_action @0x151248`、`handle_wlan_rf_msg.constprop.25 @0x15817c`、`wlan_debug_mapsync @0x159718`。
- `libcmapi.so` 13 处：`MultiAp_NOTIFY_1905_ACD @0x101648`、`wlan_restore_ap_cfg @0x10d35c`、`_CmWLANSetBaseConfig @0x115f44`、
  `CmWLANSetPowerEnhance @0x118880`、`wlan_dual_band_setting_sync @0x11a984`、`CmWLANSetWep @0x11b118`、
  `_CmWLANSetPsk @0x11badc`、`CmSetAclCfg @0x11c48c`、`__CmDelAclCfg @0x11cbc4`、`CmWLANSetAcl @0x11d0ec`、
  `_CmWLANSetAPConfig @0x120524`、`__CmWLANSetAclList @0x122ba8`、`@0x122ec0`。

**结论**：`zte-meshctl renew-acd <band>` 只等价于第 3 条腿（`SSEND 0xa93e`）；它**不等价于**原厂“配置变更 → Renew”的整体——
SSID/PSK/WEP/ACL/APConfig/功率/双频/恢复出厂/1905-ACD 等 13 个 setter 走的是 100 字节 ASEND `0x9c8a`，
而且都绕过了 Notify 自己的门控（`buf+0x44`、`buf+0x184`、`buf+0x24`）。shim 若直接改库而不发 `0x9c8a`，需要按场景自行补发。

### 6.5 对“要不要注册这 7 张视图”的影响

字段表与默认值已经拿到，但**决策不变（仍暂不注册）**：现在只解决了“表长什么样”，还没有解决
“谁在消费、0 行是否等价于未配置、注册后原厂会不会据此启用额外 vendor 行为”。`P1905AcdVenderCfg` 尤其危险——
它的行集合就是“启用哪些 vendor 子 TLV”，补一批行等于主动打开一组原本没开的厂商消息。
重开条件仍是 H-12/H-13 那条原厂机线路读到具体缺失字段或页面现象。

### 6.6 `STAProfile`：B15 写入的那张表（H-19 的 P0）【确定】

- 构造函数 `dbCreateWLANSTAProfileTbl @0x1cbda4`（`0x2f0` B）：**17 个字段、domain 1224 B（18×68）、2 行**
  （`dbDefWLANStaProfile` 里的 `cmp r5,#2`）。字段（均 `save=1`）：
  `ViewName:2 ESSID:2 BSSID:18 AuthMode:14 EncrypType:14 WPA3AuthMode:14 WPAPSK:3 WEPAuthMode:14
  DefaultKeyID:14 Key1:3 Key2:3 Key3:3 Key4:3 CardBind:13 Channel:12 BandWidth:14 Valid:15`
- 出厂默认：`AuthMode=6`、`EncrypType=2`、`WPA3AuthMode=2`、`WEPAuthMode=1`、`DefaultKeyID=1`、`BSSID` 全零；
  `WPAPSK` 的默认值不在镜像里，由 `CspHardCodeParamGet("/etc/enhardcodefile","STAProfile.1.WPAPSK",0x4b5374,65)` 注入。
- 实例：`dbDefWLANStaProfile` 用 `oss_snprintf_s` + `dbSetValCommByRowNo` 写成 `DEV.WIFI.STAPROFILE1/2`
  （格式串 `0x251612`，字段名 `0x2493d3/0x2515f5`），`CardBind` 分别为 `1/2`。
- 失败路径：`libcfapi` `WlanSTAProfileInstIdDbRead @0x5b908` 调 `dbAPIGetView`（调用点 `0x5b9e4`，表名
  `"STAProfile"`，view 头 `size=940`、magic `0x01020304`），**非 0 就返回 -1**；
  `_CmWLANMapSsidCfgSync2Repeater`（运行时包 `libcmapi 0x159cb8`；固件那份是 `0x128e64`）在 `0x159f1c` 判返回，
  失败走 `0x15a2b8 mvn r0,#7` → **返回 -7，不写库**。写侧 `WlanSTAProfileInstIdDbWrite @0x5ba74` 走
  `dbAPISetView`（调用点 `0x5bb2c`），失败同样返回 -1。
- 结论：只要 `STAProfile` 未注册，这条同步必然读失败（`-2` → `-1` → 同步 `-7`），**不会写库**；
  沙箱日志里若仍出现 “success, mesh sync”，以 H-19 P1 加的只读回读（`ZTE_AGENT_SYNC staprofile`）为准。
- 实现落点（H-19 P2）：`src/wlan_sta_profile.def` + `src/wlan-profiles.c` 的 `STA` 档案——17 字段、view 940 B
  （8 + 932，与 `libcfapi` 的 size 一致）、两行 `ViewName=DEV.WIFI.STAPROFILE1/2`、`CardBind=1/2`，字段默认值按仓库
  口径取 PUBLIC 零值（不复制上面的出厂默认）。

### 6.7 AL MAC 的生成链：`InterfaceStatus` → `i5_config`【确定（链路）/ 推测（列语义）】

2026-11-26 追加，回答“原厂怎么生成 AL MAC”。地址属固件 `multiapd`（sha256 `0733836a…`）；
运行时包对应符号是 `_baseALConfigInit 0x632b8`、`baseCheck1905Packet 0x6c910`，字符串与固件相同。

**装配（固件 `_baseALConfigInit 0x5e6a0`）**

```c
memset(i5_config, 0, 0x214);                 // 0x101d30，B 段全局
if (GetRouteIFInfo(ifname, buf) != 0) {      // PLT → libcfapi
    ProcUserLog("base get 1905 ALMAC fail"); // 串 @0xaf843
    return -1;                               // i5_config 保持全 0
}
memcpy(i5_config, buf + 0xAC, 6);            // AL MAC = 接口信息里的 6 字节
i5_config[6..7] = seed + seed / 0xFFFF;      // seed = u1905GetRandSeed()，不属 MAC
```

`baseMain+0x84`（`0x5e860`）调用后**不检查返回值**，失败只留一行日志。

**数据源（`libcfapi GetRouteIFInfo 0x34048`）**

- 先校验 32 字节接口名（`strlen` / `SafeStrncpy`）；
- 再 `GetInterFaceBaseInfo(ifname, base)`（`libcfapi 0x2c3a8`，692 B）填 0x4A8 字节结构；
- 最后把 `base+0x1F8` 或 `base+0x2BC` 的 6 字节拷到 `out+0xAC`，选择依据是该结构 `+0x280` 的字节，
  同时把选择写进 `out+0xB2`。

`GetInterFaceBaseInfo` 的调用表（PLT 解析）里与取数有关的是 `dbAPIGetView`（`0xe880`）与
`dbAPIQryView`（`0xecac`）；字符串表里有 `InterfaceStatus`、`INTERFACESTATUS table %s not exists`、
`INTERFACESTATUS table multi recodes(%s), count=%d`。**6.8 已更正**：`InterfaceStatus` 没有 MAC 列，
MAC 来自 `EthLink.MACAddress`；本节的“MAC 列”结论作废。

**调用点普查（ARM `BL` 编码扫描，GOT → `.rel.plt` → PLT 桩）**

| 文件 | 命中 | 说明 |
|---|---|---|
| 固件 `multiapd` | 7 | `_baseALConfigInit`、`get_local_macaddr` 是 AL MAC 的两条入口；其余 `i5DmGetSelfDevIpAddress`、`I5TlvIPV4TypeInsert`、`I5TlvIPV6TypeInsert`、`topoGet1905NeighborInfo`、`topoGetSelfDevIPv6Info` 只取地址 |
| 固件 `cspd` | 120 | 全是通用接口信息（ddns、DHCPv6、DNS、防火墙、route、samba、autoip）；唯一沾 1905 的 `MapCooperatorTopoUp`（`0xcc93c`）不读 `+0xAC`——cspd 侧没有 AL MAC 消费点 |

**冲突与自检**

- `baseCheck1905Packet`（固件 `0x5d3b4`）：`memcmp(i5_config, AL-MAC-TLV+3, 6)` 相等 →
  `The AL MAC in the received message[%#X] is itself,ignore.`（@0xaf510），返回 6（跳过）；
- 拓扑按 AL MAC 线性查（`i5DmGetTopoSockByAlMAC 0x87338` → `i5DmDeviceFind 0x86580`，
  `memcmp(dev+0x18, al_mac, 6)`）：两台设备同 AL MAC 会被**合并**，没有告警、没有重生成。

**影响（对我们）**【确定 + 推论】

`InterfaceStatus` 正是 shim 未注册的 7 张之一（H-18 的 `-2` 集合），因此 `dbAPIQryView` 失败 →
`GetInterFaceBaseInfo` 失败 → `GetRouteIFInfo` 失败 → `i5_config` 全 0：**vendor 侧 AL MAC 为
`00:00:00:00:00:00`**，而控制面用 init 派生的 `02:00:00:00:19:xx`，两边不是同一个 MAC。
故 H-18 的“7 张都不注册”需留一条例外候选：若真机日志证实，`InterfaceStatus` 必须注册（或提供最小一行：
键=接口名、MAC 列=br-lan 的 MAC），再与 init 派生值对齐。

**验证步骤（不改代码）**

1. 在 owrt（x86_64，现有包）看 vendor 日志是否出现 `base get 1905 ALMAC fail`；
2. 出现 → 按 6.1 的 17 字段注册 `InterfaceStatus` 一行，复看日志与 `CPE AL MAC:` / `_baseShowALMAC`；
3. 用 1905 抓包核对 AL MAC TLV、拓扑 self 节点 MAC 是否与 init 一致。

**现场验证（2026-11-26，owrt `10.23.90.222`）**

- 重启 controller 后启动同秒记录到 `ZTE_CONTROLLER_TRACE db_lst_view view=InterfaceStatus status=-2`，
  即 vendor 初始化时确实在读这张表，且它不存在；同秒还有 vendor 的 `get view failed` 日志格式。
- 但整个启动 156 行日志里 **0 条 vendor 业务日志**：`ProcUserLog` 在当前级别被过滤，
  **看不到 `base get 1905 ALMAC fail` 不能当作“初始化成功”的证据**。
- 手工注入 IEEE 1905 Topology Query（临时静态 x86_64 工具，源 `02:00:00:00:19:bb`，带 AL MAC TLV）：
  在主机 netns 的 `br-lan`、以及 multiapd 自己 netns（`net:[4026532527]`）内的 `br-lan`/`br0`/`veth-lab`
  各发 3 帧，**均无 Topology Response**；同刻 `/run/zte-mesh/peers/02:00:00:00:19:bb.seen` 生成，
  证明投递可达我们 controller，是 vendor 不回应人工 Topology Query（`dev_num=1`、无真实邻居）。
- 拓扑 `dev_num=1`，`peers/` 里没有 `00:00:00:00:00:00`，也没有能区分 vendor 与 controller 的同 MAC 证据。
- 结论：AL MAC 的具体值只能靠**真实对端**触发应答，或注册 `InterfaceStatus` 后复看。
- 注册机制已查明：运行时 `libdb.so` 自身导出 `dbAPIQryView 0x406c`、`dbAPILstView 0x3f54`、
  `dbCreateTbl 0x7b20`、`dbCreateDomainNew 0x7d5c`，shim 侧不需要实现 QryView，注册表即可；
  前置是解 `dbCreateInterfaceStatusTbl`（cspd `0x1ba544`，`0x204` B）的 17 个 domain 元数据。

### 6.8 AL MAC 复核（2026-11-26 晚）：MAC 在 `EthLink`，`InterfaceStatus` 只是链路中间站【确定】

工具：`arm-linux-gnueabi-objdump` 逐指令读 `libcfapi.so`（固件 `fw/kmod/lib/libcfapi.so`，
`GetRouteIFInfo @0x34048`、`GetInterFaceBaseInfo @0x2c3a8`）+ `libdb.so`（`dbcQryView @0xcd24`、
`dbGetPmTypeValLen @0x68e4`）；视图元数据用 `views7.py` 同法（`views8.py`，输出 `views8.json`）。

**6.8.1 类型长度表（`libdb.so` 0xFAB6 / 0xFAEA，26 项 u16）**

| type | 2 | 3 | 5 | 7 | 12 | 13 | 14 | 15 | 16 | 17 | 18 | 22 | 23 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 记录长度 | 64 | 96 | 288 | 1056 | 32 | 32 | 32 | 32 | 32 | 44 | 36 | 36 | 44 |

`dbGetPmTypeValLen`=记录长度（0xFAEA），`dbGetPrmValLen`=值长度（0xFAB6，记录长度-28）。
其余类型 0/1/4/6/8/9/11/25 在本次涉及的表里未出现。

**6.8.2 `InterfaceStatus`（cspd `dbCreateInterfaceStatusTbl`）**

17 字段、**256 行**、`save=0`、记录 1768 B（8 字节表头 + 17 段）。字段顺序与默认值：

| # | 字段 | 类型 | save | 默认 | # | 字段 | 类型 | save | 默认 |
|---|---|---|---|---|---|---|---|---|---|
| 0 | ViewName | 2 | 1 | "" | 9 | IFName | 2 | 0 | "" |
| 1 | IF_ID | 2 | 0 | "" | 10 | ttyDevName | 2 | 0 | "" |
| 2 | LowerLayer | 7 | 0 | "" | 11 | UpMaxBitRate | 13 | 0 | "" |
| 3 | Enable | 15 | 0 | 0 | 12 | DownMaxBitRate | 13 | 0 | "" |
| 4 | WanLanPrority | 13 | 0 | 0 | 13 | UpCurrBitRate | 13 | 0 | "" |
| 5 | LinkType | 13 | 0 | 0 | 14 | DownCurrBitRate | 13 | 0 | "" |
| 6 | Alias | 3 | 0 | "" | 15 | PhyType | 13 | 0 | 0 |
| 7 | Status | 13 | 0 | 0 | 16 | IFType | 14 | 0 | 0 |
| 8 | LastChange | 13 | 0 | 0 | | | | | |

**没有 MAC 列**，所以“注册一行、MAC 列写 br-lan”的做法不成立。

**6.8.3 `GetInterFaceBaseInfo` 的查询方式（决定行怎么建）**

- `dbAPIQryView(view, cond)`：`view[0]` 必须等于表注册的记录长度（1768），`view[4]` 与 `cond[0]`
  都是 `0x01020304`（`dbAPIQryView 0x3f70` 校验，错则 `-18/-19`）。
- 条件靠**字段掩码**表达：把 `IF_ID` 段（记录偏移 72）的 mask 置 2，值区（72+28=100）放接口名；
  `cond = {magic, 0x100, 0, rowkey_buf}`（`0x100`=最多 256 行）。
- 结果必须**恰好 1 行**：0 行 → `INTERFACESTATUS table %s not exists`；≥2 行 → `multi recodes(%s) count=%d`。
  即 `IF_ID` 必须唯一。
- 取到行键后 `dbAPIGetView("InterfaceStatus", rowkey, view)`，把字段装进 0x4A8 结构：
  `+0x00` 请求的 ifname、`+0x20` LowerLayer、`+0x421` Enable、`+0x424` WanLanPrority、
  `+0x468` IFName、`+0x478` ttyDevName、`+0x49C` WanLanPrority、`+0x4A0` Status。

**6.8.4 `GetRouteIFInfo("DEV.IP.IF1", out)` 的实际链路**

`_baseALConfigInit`（固件 `0x5e6a0`）传的 ifname 是 `0xaa9a3` 的常量 `"DEV.IP.IF1"`，不是 br0。
之后：

1. `dbAPIGetView("IPInterface", "DEV.IP.IF1", 0x964)` —— 失败即返回 -1（shim 已在
   `startup_identity` 注册这一行，`Name=br0/IsBridge=1`）。
2. `GetInterFaceBaseInfo("DEV.IP.IF1")` —— 需要上面的 `InterfaceStatus` 行（`IF_ID="DEV.IP.IF1"`）。
3. 沿 `LowerLayer` 向下爬：直到该值以 `DEV.BRIDGING` 开头（`0x5ed05`），且必须含 `.BRPORT`
   （`0x5fd31`，匹配点偏移 1..30），否则报错返回。
4. 从 bridge-port 名取到下层名后读 `EthLink`；PPP/VLAN 分支（`PPPIF` → `DEV.ETH.VLAN*` /
   `DEV.ETH.LINK*`，`0x34c68`–`0x34dc0`）也汇到同一处。
5. MAC 只出现在**两处相邻的拷贝**（`0x354d8` / `0x3551c`，dest=`out+0xAC`）：
   `EthLink.MACAddress`（视图偏移 `0x1e4`）或 `EthLink.UserCfgMACAddress`（`0x2a8`），
   由 `EthLink.MACAddressOverride`（视图 `+0x2cc`）选，选择结果写 `out+0xB2`。
   这是全函数**唯一的 MAC 来源**，与 `InterfaceStatus` 无关。

**6.8.5 需要补注册的表（都已在 `views8.json` 取到构造级元数据）**

| 视图 | 字段数 | 行数 | 记录 B | 用途 |
|---|---|---|---|---|
| InterfaceStatus | 17 | 256 | 1768 | 接口栈中间站（IF_ID/LowerLayer/IFName/Status） |
| EthLink | 16 | 17 | 752 | **MAC 来源**：`MACAddress`/`UserCfgMACAddress`/`MACAddressOverride` |
| Bridge | 13 | 1 | 520 | `DEV.BRIDGING.BRIDGE1` |
| Brport | 26 | 45 | 1800 | `DEV.BRIDGING.BRIDGE1.BRPORT1` → LowerLayers 指向 EthLink |
| EthVlan / PPPIF | 11 / 34 | 16 / 16 | 584 / 2440 | 仅 PPP/VLAN 回程路径，桥接路径可不建 |

EthLink 字段：`ViewName(2,save1)、Enable(15)、Status(14)、Alias(3)、Name(3)、LastChange(13)、
LowerLayers(3)、MACAddress(18)、PriorityTagging(15)、WanLanPrority(14)、VlanEnable(15)、WBMode(14)、
8021Priority(13)、UserCfgMACAddress(18)、MACAddressOverride(15)、DynamicLowyer(14)`。

**6.8.6 对 H-20 的影响与执行路径**

- 只注册 `InterfaceStatus` 一行 → 第 2 步通过，但第 3–5 步仍会在 `Brport`/`EthLink` 处失败，
  `out+0xAC` 保持 0，AL MAC 仍是全 0。**单表方案已排除。**
- 可选两条路：
  - **A（忠于原厂语义）**：按 TR-098 建最小图——`IPInterface.DEV.IP.IF1` →
    `InterfaceStatus.IF_ID=DEV.IP.IF1`（LowerLayer=`DEV.BRIDGING.BRIDGE1.BRPORT1`）→
    `Brport.DEV.BRIDGING.BRIDGE1.BRPORT1`（LowerLayers=`DEV.ETH.LINK1`）→
    `EthLink.DEV.ETH.LINK1.MACAddress=<目标 MAC>`。行名/字段语义需用探针逐轮确认。
  - **B（拦截边界）**：在 shim 里 interpose `GetRouteIFInfo`。不可取：同函数还被
    `i5DmGetSelfDevIpAddress`、`I5TlvIPV4/IPV6TypeInsert`、`topoGet1905NeighborInfo`、
    `topoGetSelfDevIPv6Info` 用于取地址，用桩返回 0 会把自身上报地址变成全 0。
- 验收仍按 H-20：①`base get 1905 ALMAC fail` 消失；②`out+0xAC`/AL MAC TLV 非 0 且等于
  init 派生值；③拓扑 self 节点 MAC 一致。迭代用 `db-engine-probe --getroute`
  （新增模式：`controller_startup()` 后直接调 `GetRouteIFInfo("DEV.IP.IF1", buf)` 并打印
  `rc` 与 `buf+0xAC` 六字节），先在沙箱里把行建对，再装机。

### 6.9 daemon 的 `GetRouteIFInfo` 被 `libzte_controller_route.so` 抢占【确定，2026-11-26 晚】

**证据一（真机进程环境）**：owrt 上 multiapd（pid 100513）的 `/proc/100513/environ`：

```
LD_PRELOAD=/lib/libzte_controller_profile.so:/lib/libzte_controller_route.so:/lib/libzte_controller_trace.so:/lib/libzte_mesh_shim.so:/lib/libzte_agent_mm.so
```

`route` 在 `libcfapi` 之前，且 `src/controller-route.c:61` 导出了 `GetRouteIFInfo`。multiapd 的
PLT 调用按此顺序解析，`_baseALConfigInit`（6.8.4 的第 1 步之前）拿到的是**我们的适配器**，
不是 libcfapi `0x34048` 的实现。该适配器成功时向 stderr 写一次
`ZTE_CONTROLLER_ROUTE actual_private_if=1 route_bytes=532 l3_supported=0`；真机自 17:26 起的
daemon 有两条（`ns-run[99442]`），说明适配器路径成功。

**证据二（沙箱探针只 preload shim）**：`ns-run.c:220` 固定 `LD_PRELOAD=/lib/libzte_mesh_shim.so`，
所以 `db-engine-probe --controller-startup` 里的 `GetRouteIFInfo` 走的是 libcfapi 原厂实现，
6.8.4 的链路只在这个探针里被真正执行。逐轮补表后的实测：

1. 注册 `InterfaceStatus`（含 `IF_ID=DEV.IP.IF1`、`LowerLayer=…BRPORT1`、`IFName=br0`）、
   `Brport`（`LowerLayers=DEV.ETH.LINK1`）、`EthLink`（`MACAddress=br0`）后，
   `GetInterFaceBaseInfo` `rc=0`，但 `GetRouteIFInfo` 仍 `rc=-1`、`out+0xAC` 全 0。
2. 反汇编补出被漏掉的一步：函数在第 3 步之前调用 `GetDNSServersByIPIF`（`0x1b50c`）→
   `QryDNSServers` → `QryDNSServersWithEnable`（`0x1b2a4`），后者对
   `dbAPIQryView("DNSClientServer", …)` 的结果做判断，**只要该视图查询失败就返回 -1**，
   调用方 `0x3456c` 直接跳到函数入口的 `-1` 出口。
3. 注册 `DNSClientServer`（`dbCreateDNSClientServerTbl @0x1b3614`：8 字段
   `ViewName(2)/Enable(15)/Alias(3)/DNSServerV4(16)/DNSServerV6(17)/Interface(5)/Type(14)/Priority(13)`，
   记录 628 B、128 行、0 行即可）后，`GetDNSServersByIPIF` 实测 `rc=0`；但接线处
   `0x35618` 的 `dbAPIQryView("InterfaceStatus")`（mask 在模板 `+74`，即 `IF_ID` 字段）以
   **空 IF_ID** 查得 `count=0`，随后 `0x3568c` 跳过 `dbAPIGetView`，函数仍未取到 MAC。
4. 因而：即使把 6.8.5 的表全建好，原厂实现还会依赖运行时状态（该 qry 的键来自上级接口信息）
   才到 EthLink；这条链在 daemon 中**根本不执行**，继续扩图没有收益。

**结论**：H-20 的 AL MAC 只受 `src/controller-route.c` 影响。若要修，改适配器（或去掉它的
`br0`/本地管理位/`IFF_UP` 前置条件），不需要再扩 TR-098 图。`startup_DNSClientServer`
与 `src/al-graph.inc` 仅保留为沙箱探针能力，未装机。

### 6.9.1 清理与验证（2026-11-26 19:46）【确定】

**清理**（决定：不装机）：

- `src/controller-run.c`：删除 `controller_startup()` 里 `ZTE_MESH_EMULATED_LAB` 门控的整段诊断
  （`ZTE_CONTROLLER_IFBASE/GETROUTE/DNS/QRY/LST/KEY/ROW`），以及为它临时加的
  `GetRouteIFInfo/GetInterFaceBaseInfo/GetDNSServersByIPIF/dbAPIQryView/dbAPIGetView/dbAPILstView`
  文件级 extern。`al-graph.inc` 的 include、四个视图与 `DNSClientServer` 的注册保留。
- `src/controller-trace.c`：删除临时包装与打印——`GetRouteIFInfo` 包装及 `in_getroute` 标志、
  `dbAPIQryView` 包装、`walk_end/walk_lst/walk_get/walk_qry`、`qry_dump`、`al_graph_view`
  过滤，以及 `dbAPIGetView` 行里的 `caller=%p`；恢复为原来“仅失败行、按 view+status 去重”的日志
  （`IPInterface/MultiAPDomain/InterfaceStatus` 仍额外打印 instance）。
- 未动真机：设备 `/usr/lib/zte-mesh/root` 与 apk 包保持 17:26 起的版本。

**重编与冒烟**（vm_ubuntu 交叉编译，owrt 沙箱执行）：

| 项 | 结果 |
|---|---|
| `arm-linux-gnueabi-gcc` 编 `out/db-engine-probe` | 229012 B，无 warning |
| 编 `out/runtime/lib/libzte_controller_trace.so` | 35164 B（回到清理前基线大小） |
| 沙箱 `--controller-startup`（`/tmp/zte-mesh-db.CCBEcA`，ns-run + qemu-arm） | `rc=0` |
| 启动行 | `ZTE_CONTROLLER_STARTUP al_graph=1 eth_link_mac=02:00:00:00:19:06` |
| DB 引擎 | `server_initialized=1 shared_bytes=4194304` |

**H-20 验收（真机，daemon pid 100513，17:26 起）**：

- ① `logread` 覆盖从 10:55 起，全程无 `base get 1905 ALMAC fail`；17:26 启动有两条
  `ZTE_CONTROLLER_ROUTE actual_private_if=1`（适配器路径成功）。
- ② `/run/zte-mesh/state.json`：`al_mac="02:00:00:00:19:03"`，非 0，等于本机 br0 派生值。
- ③ 同目录 `peers/02:00:00:00:19:03.seen` 存在，拓扑 self 与本机 AL MAC 一致；`daemon_restarts=0`。

**遗留**：owrt `/tmp` 下的沙箱副本（`zte-mesh-db.*`、`run*.log`）与 vm `~/rea` 的反汇编/解码脚本
（`getroute.asm`、`strs.py`、`dnsview.py`、`dns_view.json`）留作取证材料，不属于交付物，可随时删除。

### 6.9.2 代码复核：注册未做沙箱门控（2026-11-26 复核；2026-11-27 按 A 修复）【已决】

6.9/6.9.1 的措辞是“`al-graph.inc`/`DNSClientServer` 仅保留为**沙箱探针能力，未装机**”，但复核
`src/controller-run.c` 的实际实现，二者不符：

- 注册发生在 `startup_identity()`（[controller-run.c:431](file:///f:/dev/zte-mesh/src/controller-run.c#L431)）
  里，而 `startup_identity()` 由 `controller_boot()` 无条件调用；`controller_boot` 的两条调用链都到它：
  - `controller_service()`（[controller-service.c:2018](file:///f:/dev/zte-mesh/src/controller-service.c#L2018)，
    **生产路径，lab=0**）
  - `controller_startup()`（[controller-run.c:915](file:///f:/dev/zte-mesh/src/controller-run.c#L915)，
    沙箱观察路径，lab=1）
- 注册块与 `local_mac("br0")` 没有任何 `ZTE_MESH_EMULATED_LAB` / `ZTE_MESH_DB_SANDBOX` 门控。也就是说，
  下次把当前源码构建并装机后，**真机也会注册这 5 张视图 + 合成行**（`InterfaceStatus/EthLink/Bridge/Brport`
  的 `DEV.IP.IF1 → BRPORT1 → ETH.LINK1` 链 + 空 `DNSClientServer`），与“未装机/沙箱探针”的说法相反。

**无启动级风险（已核实）**：`local_mac("br0")` 不会在开机时失败——`ns-run.c:105` 先调
`controller_private_network()`/`_service()`（[controller-net.c:135](file:///f:/dev/zte-mesh/src/controller-net.c#L135)
创建 `br0` 与 `veth-lab`），之后才 exec daemon 走到 `controller_boot → startup_identity`。所以这不是
“开不了机”，而是**语义错配 + 未决策的生产行为变更**：

1. 5 张视图从“未注册（`-2`）”变成“注册了合成行”。AL MAC 本身不受影响（走 `controller-route.c` 适配器），
   但任何其他读这些视图的原厂路径会从 `-2` 变成读到合成值——这个影响面尚未逐条排除。
2. `VALUE_ASSESSMENT.md` 仍写着“未注册可选视图… 决策仍是暂不注册”，其中就包含 `InterfaceStatus`，
   与现状矛盾。

**已决（2026-11-27，按 A）**：只做沙箱探针，生产库不注册。

- **门控不能用 `ZTE_MESH_EMULATED_LAB`**：init 在模拟器路径上把它写成
  `ZTE_MESH_EMULATED_LAB=1`（[zte-mesh.init:1108](file:///f:/dev/zte-mesh/openwrt/zte-mesh/files/zte-mesh.init#L1108)），
  而 owrt 是 x86_64、永远走 qemu，所以装机后该变量**恒为 1**，用它做门等于没门。
- 实际门控是 `controller_boot()` 已有的 `lab` 参数：`startup_identity()` 新增第三参数 `int lab`
  （[controller-run.c:431](file:///f:/dev/zte-mesh/src/controller-run.c#L431)），`al_graph = lab`。
  `controller_startup()`/`controller_agent_startup()`（观察路径）传 1，`controller_service()`
  （[controller-service.c:2018](file:///f:/dev/zte-mesh/src/controller-service.c#L2018)，生产路径）传 0。
- 5 张视图的注册与 `local_mac("br0")` 行写入整块包进 `if (al_graph)`；未注册时这 5 个表在真机上
  仍是 `-2`，与 6.9/6.9.1 的“未装机”口径一致，`VALUE_ASSESSMENT` 的“暂不注册”继续成立。

**验证（2026-11-27，owrt 沙箱，临时根 `/tmp/zte-mesh-db.NpkPhb`，未装机）**：

- 交叉编译 `make out/db-engine-probe`：`-Wall -Wextra -Werror` 通过，229012 B。
- `--controller-startup`（lab=1）：`ZTE_CONTROLLER_STARTUP al_graph=1 eth_link_mac=02:00:00:00:19:06`，
  `roam_caton_view=1 wlan_setting_view=1 cache_mac_view=1`、`real_multiapd=1 private_db=1 host_network=0`
  全部不变。
- 生产路径（lab=0）无法在临时根上跑：ns-run 只接受 `/usr/lib/zte-mesh/root` 配 `--controller-service`
  （防误用），所以“lab=0 不注册”由源码链路确证（`controller_service → controller_boot(...,0) →
  startup_identity(...,0) → al_graph=0`，且 `startup_identity` 只有这一个调用点）。
- 临时根已删除，设备安装目录与 `state.json` 未改动。
