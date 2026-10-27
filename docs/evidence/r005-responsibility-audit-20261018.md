# R005 只读审计：responsibility/audit 契约与现有适配

日期：2026-10-18。范围：离线审计 responsibility/audit 的真实契约、项目现有适配，
判断 `responsibility_get_responsible_audit_token_for_audit_token` 的失败由谁生产，
并给出窄修复候选与判别实验。**未连接设备、未改源码/测试/构建输入、未构建、
未执行 Git 写入。** 后端 R002 的设备事实为引用，不在本轮复测。

候选 HEAD：`7e5d771`。后端 R002 的设备 generation 由 backend 独占，本轮不触碰。

## 1. 检查身份

| 对象 | 身份 / SHA-256 |
| --- | --- |
| `launchdchrootexec/main.m` | `0e8d6504189188db4c8993194381abd6d459bbf01113374ef5ae010f606c110c` |
| `layout/usr/macOS/LaunchDaemons/com.macwsguide.tccd-system.plist` | `d2e8669d752a4face78537d2ac4f538ffc1431e0d92ca0ba3e8f7de406a24f2f` |
| `layout/usr/macOS/bin/macos_gui.sh` | `471fc752ed72d0a376ef747d834530607c484839b1d2af2585a4eb3fab6ebc9d` |
| `libmachook/mac_hooks.m` | `e8ae9dadfa5bb40701a681661cb7677e729f94b35807355f5ea5ae0af71eb08d` |
| `analysis/TCC_22F82`（Ventura 客户端） | UUID `1238D7CA-719A-36A4-94B1-2F387E6B3BF1`，SHA-256 `09b84137…f2de` |
| `analysis/SkyLight_22F82` | UUID `96676A53-B1E0-3D7E-B98B-B73873CD1880`，SHA-256 `13d7a0ca…6c61` |
| 分享包 `role-binaries/tccd-arm64e`（24G90，版本相邻） | UUID `8D17A864-8554-3BA0-9CC9-B92BD597F6AE`，SHA-256 `b616c04d…b057` |
| `analysis/xnu-xnu-8792.81.2`（iOS 16.3 内核源码；设备为 8792.82.2） | `kern_proc.c` `3ec596e3…f6ca`，`kern_fork.c` `1cac9e63…677f`，`proc_internal.h` `d271ba60…4b84`，`errno.h` `c8a527ee…4f36` |
| 分享包 `sequoia-hook-source/launchdchrootexec/main.m` | `1acf93d6…b0fb` |
| 分享包 `sequoia-hook-source/libmachook/mac_hooks.m` | `d6587240…6e84` |

Ventura tccd 本身不在本地；后端 R002 记录其设备副本 SHA-256
`42c388e875f5a479402a3a8318aa9c866101c1162d042ba0daafbf5d996c828d`、UUID
`9845D17F-0409-3884-8A17-260C729636E5`。

## 2. responsibility API 的调用契约

### 2.1 FACT：24G90 tccd 的精确调用序列

本地 `analysis/share-review-20261015/role-binaries/tccd-arm64e`（`__TEXT`
vmaddr `0x100000000`、fileoff `0`）：

```text
0x10005c878  stp q0,q0,[sp]             ; 输出 token 缓冲清零
0x10005c884  add x8,sp,#0x20; ...       ; 取发送方 audit token 到 [sp+0x20]
0x10005c898  add x0,sp,#0x20            ; x0 = input audit_token_t *
0x10005c89c  mov x1,sp                  ; x1 = output audit_token_t *
0x10005c8a0  mov x2,#0
0x10005c8a4  mov x3,#0
0x10005c8a8  bl _responsibility_get_responsible_audit_token_for_audit_token
0x10005c8ac  cbz w0, 0x10005c924        ; 0 = success；非 0 = errno
```

- 失败路径读取 `errno` 并调用 `strerror`，记录
  `"ERROR: responsibility_get_responsible_audit_token_for_audit_token() from PID[%d]: (#%d) %{public}s"`
  （格式串 `0x10005e67c`）。
- 成功后 `0x10005c924` 用输出 token 构造 process；若 responsible token 的
  pid 与输入不同，走 `0x10005cb6c` 的 responsible-process 分支；否则
  `0x10005ca00` 起把该 token 作为 attribution 结果。
- 失败时 `0x10005c910` 清空 attribution 对象并跳 `0x10005cc84`；后续即
  “Failed to create attribution chain”。这与后端 Ventura 日志的
  `(#103) Policy not found` + 空字典路径一致。

### 2.2 FACT：`#103` 是 `ENOPOLICY`，不是 TCC 策略拒绝

- macOS SDK `sys/errno.h:256`：`#define ENOPOLICY 103 /* No such policy registered */`。
- 本机 `strerror(103)` 输出 `Policy not found`。
- 因此后端日志的 `(#103) Policy not found` 是 responsibility API 的
  **errno 返回**，表示被查询进程没有注册 responsibility policy；它发生在
  TCC 服务策略评估之前，不能解读为 TCC 的 `result=false` 或授权拒绝。
- 24G90 tccd 的失败路径没有写任何 reply key；后端 Ventura 二进制在
  `0x100006500` 创建 reply、`0x1000069cc..e0` 原样发送空字典，二者结构一致。

### 2.3 FACT：内核侧的 responsibility 字段与继承

iOS 16.3 XNU 源码（`analysis/xnu-xnu-8792.81.2`；设备
`xnu-8792.82.2`，补丁号相邻但非逐字节同源）：

- `bsd/sys/proc_internal.h:359`：`pid_t p_responsible_pid;`
- `bsd/kern/kern_fork.c:1096`：`child_proc->p_responsible_pid = parent_proc->p_responsible_pid;`
- `bsd/kern/kern_proc.c:4698-4706`：`proc_set_responsible_pid()` KPI 仅写入该字段。
- 全树中该字段的赋值只有 fork 继承和该 setter；**没有** in-tree 的 setter
  调用者，也没有开放源码中的 proc_info responsibility flavor。
- 该字段被 MAC launch-constraint（`security/mac_process.c:909`）、socket/NECP
  使用，说明它是内核真实的 process policy 输入，而不是 TCC 私有缓存。

**契约解释（FACT + THEORY）：** API 输入是目标进程的 32 字节 audit token，
输出应是该进程的 responsible audit token；若内核没有该进程的 responsibility
policy，wrapper 返回 `ENOPOLICY`。FACT 是调用序列、返回码/errno 和内核字段
语义；**THEORY** 是 macOS libSystem wrapper 在 Ventura 上如何查询该字段
（本地 libSystem/kernel 提取是 stub，无实现）。

## 3. 项目现有适配是否改变输入/结果

### 3.1 FACT：项目没有任何 responsibility/audit-session 设置调用

在 `launchdchrootexec`、`libmachook`、`macwshostd`、`MacWSHost`、`layout`、
`misc` 中对以下 API 的源码引用计数均为 **0**：

- `responsibility_get_*`
- `responsibility_set_*`
- `responsibility_spawnattrs_*`
- `setaudit*`

因此项目没有创建/覆盖 responsible policy，也没有 fake token/session。

### 3.2 FACT：audit token accessor interpose 不能改写调用者的 token

`libmachook/mac_hooks.m:1685-1686` 的声明是**按值**：

```c
extern au_asid_t audit_token_to_asid(audit_token_t atoken);
extern uid_t audit_token_to_auid(audit_token_t atoken);
```

实现 `13565-13575` 只修改形参的本地副本/返回标量：

```c
au_asid_t audit_token_to_asid_new(audit_token_t atoken) { ... }
uid_t audit_token_to_auid_new(audit_token_t atoken) { return atoken.val[0] = 501; }
```

C 按值语义保证形参写入不会改调用者持有的 `audit_token_t`；interpose 位于
`15293-15296`，无进程门。后端 R002 的 tccd 日志 `sender_auid=501` 是
accessor 的返回值，不是 token 被改写；请求里的 `target_token auid:-1`
仍是原始内核 token。

**因此：** `audit_token_to_auid/asid` 的 interpose **不能改变**
传给 `responsibility_get_responsible_audit_token_for_audit_token` 的 token
内容；也不能注册/删除 responsibility policy。它们可能影响 tccd 的日志或
后续以标量为输入的策略判断，但不是本处 `ENOPOLICY` 的输入。**不能**把
“auid 不匹配”直接当作 Policy not found 的原因（符合任务要求）。

### 3.3 FACT：启动适配保留了 launcher 的 policy，但没有声明新 policy

`launchdchrootexec/main.m`：

- `103-113`：只有 `getppid()==1 && XPC_SERVICE_NAME` 时才设置
  `CS_LAUNCH_TYPE_SYSTEM_SERVICE`；SSH/孤儿启动保持 ordinary launch type。
- `119`：`spawn_flags = POSIX_SPAWN_SETEXEC`。
- `141`：`posix_spawn(..., POSIX_SPAWN_SETEXEC)` 让目标替换 launcher，
  保留 launchd 给 launcher 的 task/PID 与 responsibility 记录。
- `47-55`：在 exec 前 `setgid/setuid`；源码范围内未找到 exec/setuid 重置
  `p_responsible_pid` 的代码（FACT，限于 `analysis/xnu-xnu-8792.81.2`），
  但**THEORY**：launchd 的 policy 是否与 uid 绑定、SSH 会话链是否有
  policy，仍需实测。
- 没有任何 `responsibility_*` 调用。

其他“responsible process”用法（`macwshostd/main.m:3047-3056`、
`MacWSHost/main.m:6379-6385`、`macos_gui.sh:1504`）是让前台
UIKit/FrontBoard owner 执行 spawn，以保留 iOS 的真实 ancestry；不是对
chroot 进程直接调用 responsibility API，不能直接复用于 tccd。

### 3.4 FACT：分享包同样没有 responsibility 修复

- 分享包 `launchdchrootexec/main.m:133,146,178` 使用相同的
  `CS_LAUNCH_TYPE_SYSTEM_SERVICE` 条件与 `POSIX_SPAWN_SETEXEC`，没有
  responsibility 设置。
- 分享包 `libmachook/mac_hooks.m:12504-12508,14203-14204` 的
  `audit_token_to_asid/auid` interpose 与当前源码相同。
- 分享包仍安装 `_audit_token_check_tcc_access → hooked_return_1`
  （`mac_hooks.m:5689`）。它的桌面成功依赖该恒真 checker，而不是修复
  responsibility 或空 reply；不可作为 upstream 修正复用。

## 4. 窄修复候选

### A（首选，无源码改动）：用真实 launchd 作业承载需要 TCC 归属的客户端

- **探针**：当前 `macwstccdprobe` 由 SSH 中的 `launchdchrootexec` 启动；
  launcher 不是 launchd 作业，责任链可能为空。改为一次性 launchd job
  （同一 rootfs/uid/请求），让 launchd 自己建立 responsibility。
- **WS**：现有 `com.apple.WindowServer.plist` 已经是 launchd 作业；先在
  fresh generation 中确认它是否已获得 policy。若已获得，则无需任何源码修复。
- 这是“复用真实 launch 契约”的最小路径，不修改 TCC、audit token 或 checker。

### B（条件性源码候选）：仅对真实 launchd 作业声明 self-responsibility

若 A/B 显示 **真实 launchd 作业** 也没有 policy，而
`responsibility_set_pid_responsible_for_pid` 在该内核可用，则可在
`launchdchrootexec` 的 `getppid()==1 && XPC_SERVICE_NAME` 分支中、SETEXEC
之前调用：

```c
responsibility_set_pid_responsible_for_pid(getpid(), getpid());
```

- 这是内核真实 policy（self-responsible daemon），不是 fake token/constant
  success；只对顶层 launchd 作业生效，**绝不**应用到 SSH/ad-hoc 探针。
- 仅在诊断证明 launchd 未设置 policy 且 API 返回 0 时成立；若返回
  `EPERM/ENOTSUP`，则无用户态窄修复，应报告平台契约缺口。
- 该候选仍需设备 A/B，不在本包实施。

### C：调整作业域/uid 让 launchd 建立 policy

系统域/用户域或目标 uid 的不同可能改变 launchd 的 responsibility 记录；
这是 plist/作业配置实验，不是源码补丁。

### 明确拒绝

- 对任意探针/WS 调用 setter 伪造 attribution；
- 伪造 audit token/session、恒定 checker、TCC grant/DB 修改；
- 把分享包的恒真 TCC hook 作为修复。

## 5. 判别性设备 A/B（backend 独占）

1. **主实验（launch 上下文）：** 同一 `macwstccdprobe preflight` 分别由
   (a) SSH `launchdchrootexec` 与 (b) 一次性 launchd job（同 domain/uid）
   启动；采集 tccd oslog（`REQUEST`、`responsibility_get_responsible`、
   attribution）与 reply count/keys，并在探针内只读记录
   `responsibility_get_pid_responsible_for_pid(getpid(), &rpid)` 返回/errno。
   若 (b) 成功、populated reply，则探针失败是 launch 上下文问题。
2. **WS 适用性：** 在 fresh WS generation 的首个 TCC 请求上采集同样字段。
   若同样 `#103 Policy not found`，说明 WS launch 契约是共同根因；否则
   probe 失败不能外推到 WS。
3. **条件实验 B：** 仅当 1/2 显示真实 launchd 作业也无 policy 时，用一次性
   `launchdchrootexec` 构建在真实作业分支调用 self-responsibility setter，
   A/B reply；确认只影响真实作业，不影响 SSH 探针/TCC 策略。
4. **不优先：** 关闭 `audit_token_to_auid/asid` interpose 的 A/B。按 §3.2
   的按值语义，它不应改变 responsibility 输入；只有在前述实验显示
   token 本身被改写时才需要。

## 6. 精确缺失证据

- Ventura/macOS 13.4 `libSystem`/`libsystem_kernel` 中该 wrapper 的实际实现
  与 UUID；本地 15.6 提取只有 stub（`dsc_libSystem.B.dylib` 16KB；
  `libsystem_kernel.dylib` 的 symtab 无 responsibility 符号）。
- Ventura tccd 本地副本与 responsibility 调用点反汇编（后端设备上有副本；
  本轮只有后端 R002 的偏移与 24G90 结构对照）。
- launchd-hosted probe / fresh WS 的 responsibility 返回与 reply。
- iPadOS launchd 是否为 MacWS 作业设置 `p_responsible_pid`。
- `responsibility_set_pid_responsible_for_pid` 在 `launchdchrootexec` 上下文
  是否被允许/实现。
- probe 与 WS 的 `XPC_SERVICE_NAME`、launch type、responsible PID、作业域。
- `setuid/setgid` 是否影响 launchd 已建立的 policy（内核源码未见重置，但无
  直接运行证据）。

## 7. 命令与退出码

| 命令 | 退出 | 结果 |
| --- | --- | ---: |
| `sha256sum <§1 文件>` | 0 | 哈希见 §1 |
| `otool -l … | grep -A2 LC_UUID` | 0 | UUID 见 §1 |
| `otool -l tccd-arm64e`；`llvm-objdump -d tccd-arm64e` | 0 | `__TEXT` vmaddr/fileoff；调用点 `0x10005c8a8` |
| `cc /tmp/errno103.c`；`/tmp/errno103` | 0 / 0 | `103=Policy not found` |
| `grep -rn "responsibility_get_\|responsibility_set_\|responsibility_spawnattrs_\|setaudit" <项目源>` | 0 | 各计数 0 |
| `dyld_info -exports` / `nm -a` / `strings` 本地 libSystem 提取 | 0 | 仅 stub，无 wrapper 实现 |
| `git log --oneline -1`（只读） | 0 | HEAD `7e5d771` |

未运行测试、构建、package、设备命令；未执行 commit/reset/stash/push。

(handover to codex now)
