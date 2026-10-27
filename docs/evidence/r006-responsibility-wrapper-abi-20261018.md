# R006 只读审计：Ventura responsibility wrapper/ABI 与真实系统边界

日期：2026-10-18。范围：离线确认 Ventura 22F82
`responsibility_get_responsible_audit_token_for_audit_token` 的实现/ABI 可从
哪些机器码得到、哪些 getter/setter 签名有依据、真实 syscall/Mach/XPC 边界与
一个 native-vs-chroot 判别实验。**未连接设备、未改源码/测试/构建输入、未构建、
未执行 Git 写入。** 唯一新增为本文件与
`coordination/_relay/round-006-frontend-result.md`。

候选 HEAD：`7e5d771`。后端 R003 的 fresh generation 已结束，本轮不重启、
不重复 launch generation 实验。

## 1. 先撤回 R005 的 launchd-hosting 结论

**FACT（backend R003）：** 在同一 fresh generation 中，WS 48891 的 TCC
请求由 tccd 48750 处理；失败 PID 是**目标 token** 的 PID（49186/49199），
不是 WS sender。临时 launchd-hosted probe 49211 以及项目 launchd-hosted
`iconservicesagent`（48579）也得到同样的
`responsibility_get... from PID[...]: (#103) Policy not found`、空字典。
因此 **“launchd hosting 本身足以建立 responsibility”已反证**；R005 候选 A
不能作为充分修复。目标进程的 responsibility 查找才是失败点。

**FACT（backend R003）：** Ventura tccd（`42c388e8…`，UUID
`9845D17F-0409-3884-8A17-260C729636E5`）在
`0x10004fb48 bl _responsibility_get_responsible_audit_token_for_audit_token`，
`0x10004fb4c cbz w0, 0x10004fbc4` 为 success；错误日志格式在
`0x100051a54`。失败后进入预创建的空 reply 路径。**该 callsite 只给出
调用/返回值控制流，没有给出寄存器参数设置。**

## 2. 本地输入与提取尝试（FACT）

| 本地对象 | 身份 | 是否可作 22F82 wrapper 证据 |
| --- | --- | --- |
| `misc/libSystem.B.dylib` | 16,800 B，macOS minos 14.0，UUID `7BD6C0A3-5CB9-3F91-A657-3246A754C05F`，SHA `5619e5c3…859e` | 否；构建 shim/stub，无 responsibility 符号 |
| `analysis/dyldwork/dsc_libSystem.B.dylib` | 16,384 B，macOS 15.6，UUID `072C7C60-2B6C-3E07-AA56-FD1750EADC2F`，SHA `f37e6a30…cb4c` | 否；stub |
| `analysis/device-leftovers…/dscq/usr/lib/system/libsystem_kernel.dylib` | macOS 15.6，339,296 B，SHA `799bf8d4…4084` | 否；无 responsibility 符号 |
| `analysis/ios163cache/dyld_shared_cache_arm64e` | 376,832 B，SHA `f3926e38…c3b9` | 否；主 cache 只有 `0x180000000+0x58000` 映射 |
| `analysis/ios163cache/dyld_shared_cache_arm64e.19` | 115,867,648 B，SHA `901389c1…ffb7` | 否；只有 `0x1bc38c000+0x6e48000` 映射 |
| `analysis/share-review…/role-binaries/tccd-arm64e` | 24G90 tccd，UUID `8D17A864-8554-3BA0-9CC9-B92BD597F6AE`，SHA `b616c04d…b057` | 仅 24G90 callsite 证据，**不是 22F82** |

本地 iOS 16.3 cache 的 image table 列出：

```text
/usr/lib/system/libsystem_kernel.dylib  @0x1c4126000
/usr/lib/libSystem.B.dylib              @0x1d485d000
```

两者都**不在**本地已存在的两个 mapping 内（主 cache 0x180000000–0x18058000；
`.19` 0x1bc38c000–0x1c2a14000）。用
`misc/dsc_extract_image.py`（SHA `adc2d39f…1b74`）提取：

```text
$ python3 misc/dsc_extract_image.py <base> /usr/lib/system/libsystem_kernel.dylib <out>
image: /usr/lib/system/libsystem_kernel.dylib @0x1c4126000
AttributeError: 'NoneType' object has no attribute 'seek'
exit 1
```

原因：`vm2foff(0x1c4126000)` 在本地缺失的 subcache 中。**本地没有
Ventura 22F82 或完整原生 iOS 的 libSystem/libsystem_kernel 实现**；不能
据 24G90 或 15.6 stub 推断 22F82 wrapper。

## 3. 有机器码依据的 API 声明（严格分版本）

### 3.1 24G90 tccd callsite（FACT，版本=24G90）

`__TEXT` vmaddr `0x100000000`、fileoff `0`；以下 VA 减 `0x100000000` 即
file offset。

**Responsible audit-token getter**（`0x10005c8a8`，file `0x5c8a8`）：

```text
0x10005c878  stp q0,q0,[sp]        ; output token buffer (32B) zeroed
0x10005c884  ... [sp+0x20]         ; input token copied from sender object
0x10005c898  add x0, sp, #0x20     ; arg0 = const audit_token_t *input
0x10005c89c  mov x1, sp            ; arg1 = audit_token_t *output
0x10005c8a0  mov x2, #0            ; arg2 = 0 (semantics unknown)
0x10005c8a4  mov x3, #0            ; arg3 = 0 (semantics unknown)
0x10005c8a8  bl _responsibility_get_responsible_audit_token_for_audit_token
0x10005c8ac  cbz w0, 0x10005c924   ; 0=success; nonzero reads errno
```

**PID setter**（`0x100008f60`，file `0x8f60`）：

```text
0x100008f58  mov x0, x22           ; pid (from "pid")
0x100008f5c  mov x1, x24           ; responsible_pid (0 -> pid)
0x100008f60  bl _responsibility_set_pid_responsible_for_pid
0x100008f64  mov x21, x0           ; return status
...
0x100008fb0  "responsibility_set_pid_responsible_for_pid(%d, %d): %d"
             ; w22=pid, w24=responsible_pid, w21=return
```

同型 callsite 在 `0x10005ca88`（x0=pid, x1=pid, 返回值 w0）。

**PID getter**（`0x10005ca9c`，file `0x5ca9c`）：

```text
0x10005ca94  mov x0, x19
0x10005ca98  bl 0x1000671c0        ; x0 = pid
0x10005ca9c  bl _responsibility_get_pid_responsible_for_pid
0x10005caa0  mov x25, x0           ; return used directly as PID
```

调用点**没有设置 x1 输出指针**，返回 x0 被当作 responsible PID 使用。

**Attribution getter**（`0x10002955c`，file `0x2955c`）：

```text
0x100029554  mov x0, sp            ; audit_token_t * (32B copy)
0x100029558  mov w1, #0            ; flags/arg1 = 0
0x10002955c  bl _responsibility_get_attribution_for_audittoken
0x100029560  str x0,[x19,#0x8]     ; returned object; null = failure
```

**Identity binary path**（`0x1000297b8`，file `0x297b8`）：

```text
0x1000297b4  ldr x0,[x19,#0x8]     ; identity object
0x1000297b8  bl _responsibility_identity_get_binary_path
0x1000297bc  cbz x0, fail          ; returned C string
```

因此，**仅就 24G90 callsite**，有机器码依据的声明是：

```c
int responsibility_get_responsible_audit_token_for_audit_token(
    const audit_token_t *input_token,
    audit_token_t *output_token,
    /* x2, x3: caller passes 0; semantics unproven */ ...);
int responsibility_set_pid_responsible_for_pid(int pid, int responsible_pid);
int responsibility_get_pid_responsible_for_pid(int pid); /* returns PID */
void *responsibility_get_attribution_for_audittoken(
    const audit_token_t *token, int flags); /* x1=0 at callsite */
const char *responsibility_identity_get_binary_path(void *identity);
```

限制：token getter 的 x2/x3 是否属于原型、是否 flags，未证明；attribution
getter 的第二参数语义未证明；上述**不是 22F82 声明**。

### 3.2 22F82 tccd（FACT，仅控制流）

后端 R003 的精确 22F82 tccd 反汇编只给出：

```text
0x10004fb48  bl _responsibility_get_responsible_audit_token_for_audit_token
0x10004fb4c  cbz w0, 0x10004fbc4
0x100051a54  error format string (#%d = errno, %s = strerror)
```

可证明：符号被调用；返回 `w0`；`w0==0` 为 success；非零走错误/空 reply。
**不能**从该片段证明 22F82 的 x0/x1/x2/x3 参数设置，也不能把 24G90 的
声明升级为 22F82 事实。

## 4. 真实系统边界

**FACT：** responsibility 查找是 tccd 内的同步 libSystem 调用，不是 XPC。
其后才使用预先创建的 XPC reply；`#103` 是 errno/strerror，不是 XPC 错误。
**THEORY/未知：** wrapper 进入内核的具体 syscall/Mach trap/proc_info
selector/flavor 不在本地 open-source XNU（`PROC_INFO_CALL_*` 仅到 `0x10`，
无 responsibility flavor）和本地 stub 中；不得猜测。输入/输出均为 32 字节
audit token 的证据来自 24G90 callsite；22F82 的 32 字节缓冲未由本地反汇编
确认。

**FACT（XNU 源码范围）：** `p_responsible_pid` 在 fork 时继承
（`kern_fork.c:1096`），setter 为 `proc_set_responsible_pid`
（`kern_proc.c:4698-4706`）；开放源码中没有 in-tree setter 调用者或
responsibility proc_info flavor。`ENOPOLICY` 表示该目标进程没有可解析的
responsibility policy。

## 5. 一个判别性 native-vs-chroot 对照（backend 独占）

目的：区分“wrapper/ABI 在 iOS 上不工作”与“chroot 目标没有 policy”。

1. 取得**精确匹配**的 native iPadOS 16.3 20D47 `libsystem_kernel.dylib`/
   `libSystem.B.dylib`（完整 cache 集）与 Ventura 22F82 同名镜像；
   `otool -l LC_UUID` + SHA 记录，确认符号存在。
2. 用同一份只读 helper 源码分别构建 native 与 chroot 版本；helper 只调用
   getter，不调用 setter：
   - `task_info(mach_task_self(), TASK_AUDIT_TOKEN)` 取自身 token；
   - `responsibility_get_pid_responsible_for_pid(getpid())`；
   - `responsibility_get_responsible_audit_token_for_audit_token(&self,&out)`；
   - 打印 return/errno、输出 token 的 pid/auid/euid、`dladdr` 的 libSystem
     UUID/path。
3. 两者都用**真实 launchd 作业**运行（native 作业与 chroot 作业各一），
   避免 SSH 父进程差异；不使用 setter、不创建 policy、不改 TCC。
4. 判别结果：
   - native 有效、chroot `ENOPOLICY` → wrapper/ABI 可用，差异在 chroot
     目标的 responsibility producer/launch 契约；
   - 两者都无效 → iOS 内核/launchd 未提供该 policy，macOS wrapper 依赖的
     contract 在目标环境不成立；
   - native libSystem 无该符号、chroot 有 → API 为 macOS-only；边界是
     原生缺失的 contract，不是 chroot 配置。
5. 这是唯一需要的原生对照；不重复 fresh WS generation 实验。

## 6. 精确缺失输入与提取配方

**缺失二进制/UUID：**

- Ventura 22F82 `libSystem.B.dylib` 与 `libsystem_kernel.dylib`（根因
  wrapper 实现所在；UUID/SHA 待提取）。
- Ventura 22F82 tccd 的 `0x10004fb00–0x10004fb60` 完整反汇编（x0–x3 设置）
  与 `nm -m` 的 provider/ordinal；tccd SHA/UUID 已知（`42c388e8…` /
  `9845D17F…`）。
- Native iPadOS 16.3 20D47 的完整 cache 子文件，覆盖
  `libsystem_kernel` @`0x1c4126000` 与 `libSystem.B` @`0x1d485d000`；
  UUID/SHA 待提取。
- 22F82 wrapper 的实际 syscall/Mach trap/proc_info selector/flavor 与
  输出缓冲/flags 语义。

**提取配方（backend 设备所有权）：**

```bash
# 1) 记录并复制完整 cache 集（主文件 + 所有 subcache），不要只取主文件。
ls -l /var/mnt/rootfs/System/Library/dyld/
shasum -a 256 /var/mnt/rootfs/System/Library/dyld/dyld_shared_cache_arm64e*

# 2) Host 侧单镜像提取（工具只扫描 .01.. .15）：
#    若真实 subcache 编号 >= .16，在 /tmp 建临时目录，把真实 subcache
#    链接成 .01.. .15 名称；不要改仓库工具。
python3 misc/dsc_extract_image.py <cache_base> \
    /usr/lib/system/libsystem_kernel.dylib /tmp/ventura_libsystem_kernel.dylib
python3 misc/dsc_extract_image.py <cache_base> \
    /usr/lib/libSystem.B.dylib /tmp/ventura_libSystem.B.dylib

# 3) 设备侧可选：misc/extract_dyld_cache.py <cache-path> <out-dir>
#    （使用 /usr/lib/dsc_extractor.bundle）。

# 4) 身份与反汇编：
file /tmp/ventura_*.dylib
otool -l /tmp/ventura_*.dylib | grep -A2 LC_UUID
shasum -a 256 /tmp/ventura_*.dylib
xcrun llvm-objdump -d --symbolize-operands /tmp/ventura_libsystem_kernel.dylib \
    | grep -n -A40 -B10 "responsibility_get_responsible_audit_token_for_audit_token"
```

本地 iOS cache 的 `.19` 已经证明工具编号假设需要显式处理；上述步骤也把
`libSystem.B`/`libsystem_kernel` 的实际 UUID 与 24G90/15.6 区分开。

## 7. 命令与退出码

| 命令 | 退出 | 结果 |
| --- | --- | ---: |
| `shasum -a 256 <§2 本地对象>` | 0 | 见 §2 |
| `file`/`vtool -show-build` 本地 stub | 0 | macOS 14.0/15.6 stub，非 Ventura |
| 自定义 mapping 读取脚本（主 cache + `.19`） | 0 | 两个 mapping，见 §2 |
| `python3 misc/dsc_extract_image.py …libsystem_kernel…` | **1** | `image @0x1c4126000`，missing mapping → `NoneType.seek` |
| `xcrun llvm-objdump -d tccd-arm64e` | 0 | 24G90 callsites 见 §3.1 |
| `git log --oneline -1`（只读） | 0 | HEAD `7e5d771` |

未运行测试、构建、package、设备命令；未执行 commit/reset/stash/push；临时
`/tmp/ioscache_extract` 已清理，无后台任务残留。

(handover to codex now)
