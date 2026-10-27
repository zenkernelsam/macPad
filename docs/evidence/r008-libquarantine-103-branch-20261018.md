# R008 只读审计：Ventura libquarantine getter 与 103 产生分支

日期：2026-10-18。范围：分析 `responsibility_get_responsible_audit_token_for_audit_token`
的真实 provider `/usr/lib/system/libquarantine.dylib`（Ventura UUID
`A2D17FF6-CBC6-3D19-89E1-F5E57191E8A3`，base+0x1b00）及其 direct callee
closure，定位 103 分支，并区分 native-iOS 与 Ventura tccd 的 import metadata。
**未改源码/测试/构建输入、未构建、未操作设备、未执行 Git 写入。**
只新增本文件、`coordination/_relay/round-008-frontend-result.md` 和
`coordination/_relay/round-008-required-inputs.json`。

候选 HEAD：`7e5d771`。后端 R004 的 live generation 未被触碰。

## 1. 本地输入检查

| 对象 | 身份 | 是否精确 Ventura |
| --- | --- | --- |
| `analysis/r004-responsibility/` | metadata + probe source only; no Apple binary | 否 |
| `analysis/device-leftovers…/dscq/usr/lib/system/libquarantine.dylib` | UUID `8CE8DAD0-52D6-3F46-98EF-161E1397FB48`，SHA `fd20c715…cb4b`，macOS 15.6，36,816 B | **否**；仅版本相邻 |
| `analysis/ios163cache/` | iOS 16.3 cache；libquarantine absent | 否 |
| `coordination/artifacts/R004-responsibility-*` | runtime controls/prologue only | 否；无完整 image |

**FACT：** 精确 22F82 libquarantine 的完整 image 不在本地。R004 提供的
`base=0x1a6d5f000`、symbol `0x1a6d60b00`、32-byte prologue 和两个 control
是现有全部精确输入。最小提取 manifest 见
`coordination/_relay/round-008-required-inputs.json`。

## 2. 15.6 libquarantine 的机器码（版本相邻，不是 22F82 证明）

`analysis/device-leftovers…/libquarantine.dylib`：UUID
`8CE8DAD0-52D6-3F46-98EF-161E1397FB48`，SHA `fd20c715…cb4b`，
`__TEXT` vmaddr `0x18da93000`、fileoff 0。

**Getter** `0x18da94750`（file `0x1750`）：

```text
18da94750  pacibsp
18da94754  sub sp,sp,#0x30
18da94758  stp x29,x30,[sp,#0x20]
18da9475c  add x29,sp,#0x20
18da94760  ldp q0,q1,[x0]        ; copy input audit_token_t
18da94764  stp q0,q1,[sp]
18da94768  mov x0,sp
18da9476c  bl  0x18da9477c      ; ___qtn_syscall_responsibility_get3
18da94770  ldp x29,x30,[sp,#0x20]
18da94774  add sp,sp,#0x30
18da94778  retab
```

**Helper** `___qtn_syscall_responsibility_get3` `0x18da9477c`（file `0x177c`）：

```text
18da94790  mov x19,x2            ; x2=0 from the getter
18da947bc  ldr w8,[x0,#0x14]     ; input token val[5] = pid
18da947c0  ldr w9,[x0,#0x1c]     ; input token val[7] = pidversion
18da947c4  stp x2,x8,[sp]        ; [sp]=0, [sp+8]=pid
18da947c8  stp x9,x1,[sp,#0x10]  ; [sp+0x10]=pidversion, [sp+0x18]=out token ptr
18da947d0  stp x8,x3,[sp,#0x20]
18da947d4  adrp x0, 0x18da95000
18da947d8  add x0, x0, #0xdd4    ; "Quarantine"
18da947dc  add x2, sp, #0x8      ; struct pointer
18da947e0  mov w1, #0xb6         ; selector 182
18da947e4  bl  0x18da95b2c       ; ____sandbox_ms stub
18da947e8  cbz x19, ...          ; x19=0: no local out copy
18da947ec  cbnz w0, ...
18da947f0  ldr x8,[sp]; str x8,[x19]  ; only if x19 != 0
18da947f8  ... retab
```

**FACT（15.6）：** stub `0x18da95b2c` 在 `__auth_stubs`；其间接符号
（nlist index 116）是 `___sandbox_ms`。字符串位于 file `0x2dd4`，内容
`Quarantine`。因此 15.6 的 getter 直接调用
`__sandbox_ms("Quarantine", 0xb6, &struct)`；`__sandbox_ms` 的返回/errno
被原样传回。该 helper 内部**没有** `ENOPOLICY` 赋值，也没有
`p_responsible_pid` 直接读取。

**FACT（15.6 邻近库）：** `libsystem_kernel.dylib` 中
`___sandbox_ms` 与 `___mac_syscall` 是**同一地址** `0x1804335e8`
（两个导出名指向同一实现）。因此 15.6 的用户态调用边界是
`__mac_syscall`。

## 3. 103 的具体内核分支（本地 XNU 源码）

`analysis/xnu-xnu-8792.81.2/security/mac_base.c`（SHA
`f08d89d1…be2a`；设备内核 `xnu-8792.82.2`，补丁号相邻）：
`__mac_syscall()` 位于 `1567` 起：

```c
error = mac_proc_check_mac_syscall(p, target, uap->call);
if (error) {
    return error;
}

error = ENOPOLICY;                       /* line 1587 */

for (i = 0; i < mac_policy_list.staticmax; i++) {
    mpc = mac_policy_list.entries[i].mpc;
    if (mpc == NULL) continue;
    if (strcmp(mpc->mpc_name, target) == 0 &&
        mpc->mpc_ops->mpo_policy_syscall != NULL) {
        error = mpc->mpc_ops->mpo_policy_syscall(p,
            uap->call, uap->arg);
        goto done;
    }
}
/* dynamic policy loop follows; otherwise: */
return error;                            /* 103 if no match */
```

**FACT（源码范围）：** 103 是 `ENOPOLICY`（`bsd/sys/errno.h:256`），
`strerror(103)` = `Policy not found`。该分支在 **MAC policy dispatch**：
若没有名为 `target`（本调用为 `Quarantine`）且带
`mpo_policy_syscall` 的 policy，`__mac_syscall` 返回 103。
`mac_proc_check_mac_syscall`（`1535-1550`，调用点 `1582`）若失败会返回
其他错误；观测到 103 说明该 check 已通过，失败在 policy 查找。

**结论（FACT/受限）：** 对 15.6 的调用链，103 不是“目标进程缺少
responsibility relationship”的局部判断，而是 **`__mac_syscall` 的
"Quarantine" MAC policy/handler 未找到**。它发生在任何
`p_responsible_pid` 语义查询之前。此结论对设备内核是补丁号相邻的源码
证据；精确设备内核 build 仍需核对。

## 4. 精确 Ventura getter 的可见部分

R004 的 32 字节 prologue 解码：

```text
0x1b00  pacibsp
0x1b04  sub sp,sp,#0x30
0x1b08  stp x29,x30,[sp,#0x20]
0x1b0c  add x29,sp,#0x20
0x1b10  ldp q0,q1,[x0]
0x1b14  stp q0,q1,[sp]
0x1b18  mov x0,sp
0x1b1c  bl  0x1b2c
```

**FACT：** 22F82 getter 的可见 wrapper 与 15.6 getter **同构**，且
`bl` 目标为 base+0x2c，正好是 15.6 helper
`___qtn_syscall_responsibility_get3` 的偏移（+0x2c）。这强烈提示 Ventura
helper 也是同一 qtn-syscall wrapper；但 **helper body、selector/struct、
policy name 和 `__sandbox_ms` 绑定均未本地取得**，不能把 15.6 的
`0xb6`/`Quarantine` 直接写成 22F82 事实。

**THEORY（待精确 image 验证）：** Ventura helper 在 base+0x2c 以
`__sandbox_ms("Quarantine", selector, struct)` 进入内核 MAC dispatch；
103 来自同一 `__mac_syscall` 无匹配 policy 分支，或 Ventura 的 policy
name/selector 与 iOS 内核注册表不匹配。精确 selector/struct 可能不同。
这不是 per-target responsibility relationship 缺失的证明。

## 5. native-iOS 与 Ventura tccd metadata 区分

- R004 的 `tccd-22F82-arm64e`（SHA `039f64b7…`，UUID `9845D17F…`）是
  **Ventura** tccd；`dyld_info -imports` 显示
  `_responsibility_get_responsible_audit_token_for_audit_token (from libSystem)`。
  “from libSystem” 是**链接时 umbrella/re-export 名**，不是实现 image；
  chroot 的 `dladdr` 证明实际 provider 是
  `/usr/lib/system/libquarantine.dylib` UUID `A2D17FF6…`。
- 这**不能**当作 native iOS tccd 的 import metadata。native iOS tccd 是
  不同 executable/UUID；本地 iOS cache 中 `libquarantine.dylib` 不存在，
  native helper 也无法 dlsym/直接 import 该符号。需精确 native tccd
  binary/UUID 才能把它的 import ordinal 与实现绑定。
- “native helper 拿不到符号”不等于 native tccd 没有该函数（platform
  client 可以私有链接）；只说明非 platform helper 的可用性边界。

## 6. 最窄的上游修正候选与判别实验

**修正候选（仅在精确 image/kernel 证据齐备后）：**

1. **首选、无源码补丁：** 若精确 Ventura helper 也走
   `__mac_syscall("Quarantine", …)` 且 iOS 内核未注册该 MAC policy，
   这是**平台 policy contract 缺口**。用户态不能注册 MAC policy；faithful
   的修正只能是使用一个**不经过该缺失 policy 的真实 responsibility
   查询**（若存在），或改变运行环境/provider，而不是 patch libquarantine
   或伪造结果。
2. **条件兼容候选：** 若精确 helper 的 selector/struct 对应一个 iOS 内核
   **实际注册**的 policy（不同名字/selector），可做窄的 ABI-preserving
   适配，把 Ventura ABI 转成该真实 policy 调用；必须先有精确 bytes。
3. **拒绝：** `responsibility_set_pid_responsible_for_pid` 或其他 setter、
   fake token/session、常数成功、TCC checker 绕过、复制 24G90/15.6
   selector 到 22F82。

**判别实验（backend 设备所有权）：**

1. 按 manifest 提取精确 22F82 libquarantine；反汇编 base+0x1b00/+0x1b2c，
   解析 `__sandbox_ms` 的间接符号，记录 policy name/selector/struct。
2. 用现有只读 helper 对**所有** responsibility getter
   (`..._for_audit_token`, `..._for_pid`, `_get_attribution`,
   `_uniqueid`) 做 raw return/errno/output 对照。若全部 103 且同一
   `__mac_syscall` 边界，则确认 policy dispatch 缺口；若某个 getter 成功，
   它是可用的真实替代。
3. 在设备内核上以只读方式确认 MAC policy 列表中是否有 `Quarantine`
   且带 `mpo_policy_syscall`（内核 build/source 匹配；不写内核）。
4. Native 侧只使用现有、已授权的 platform client；先把 native tccd
   的精确 UUID/hash/import 绑定，再比较 raw 结果。不扩 entitlement。

## 7. 命令与退出码

| 命令 | 退出 | 结果 |
| --- | --- | ---: |
| `otool -l`/`vtool -show-build` 15.6 libquarantine | 0 | UUID `8CE8DAD0…`，macOS 15.6，`__TEXT` 0x18da93000 |
| `llvm-objdump -d` 0x18da94750–0x18da94804 | 0 | getter/helper 见 §2 |
| Python nlist 解析 index 116 | 0 | `___sandbox_ms` |
| Python 读取 file 0x2dd4 | 0 | `Quarantine` |
| `nm -a` 15.6 libsystem_kernel | 0 | `___sandbox_ms` == `___mac_syscall` @0x1804335e8 |
| `sed` XNU `security/mac_base.c` | 0 | ENOPOLICY 分支 @1587 |
| `sha256sum` §1/§3 对象 | 0 | 哈希见表格 |
| `git log --oneline -1`（只读） | 0 | HEAD `7e5d771` |

未运行测试、构建、package、设备命令；未执行 commit/reset/stash/push；
无后台任务。

## 8. 限制

- 精确 Ventura libquarantine image 缺失；15.6 的 `Quarantine`/`0xb6` 只是
  版本相邻证据，不能写成 22F82 事实。
- 本地 XNU 是 iOS 16.3 `8792.81.2`，设备 `8792.82.2`；`__mac_syscall`
  分支是补丁号相邻的源码证据，不是设备内核二进制反汇编。
- 未证明设备内核缺少 `Quarantine` policy/vtable；只证明源码中 103 的来源
  是无匹配 policy 的 dispatch。精确 policy 列表需设备只读确认。
- Native getter 的 provider/ABI 仍未知；native tccd metadata 不能与
  Ventura tccd 混用。

(handover to codex now)
