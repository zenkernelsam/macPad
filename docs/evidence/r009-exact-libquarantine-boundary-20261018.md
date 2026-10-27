# R009 只读审计：精确 Ventura libquarantine getter 与 103 边界

日期：2026-10-18。范围：使用精确 22F82
`analysis/r005-responsibility-inputs/libquarantine.dylib`（UUID
`A2D17FF6-CBC6-3D19-89E1-F5E57191E8A3`，SHA
`f5989c8f4afc9985e95795bf683679eed629af8b5ae288a0fb87317ba0e844f2`）
分析 getter/helper、policy/selector/struct、return/errno 与 103 归因。
**未改源码/测试/构建输入、未构建、未操作设备、未执行 Git 写入。**
只新增本文件与 `coordination/_relay/round-009-frontend-result.md`。

候选 HEAD：`7e5d771`。后端 R005 的 live generation 未被触碰。

## 1. 输入身份与有效性

| 项 | 值 |
| --- | --- |
| 精确 image | `analysis/r005-responsibility-inputs/libquarantine.dylib` |
| SHA-256 | `f5989c8f4afc9985e95795bf683679eed629af8b5ae288a0fb87317ba0e844f2` |
| UUID | `A2D17FF6-CBC6-3D19-89E1-F5E57191E8A3` |
| LC_BUILD_VERSION | platform 1 (macOS), minos/sdk 13.4 |
| `__TEXT` | vmaddr `0x18b31f000`, fileoff 0, filesize 0x3000 |
| getter | unslid `0x18b320b00` (runtime base+0x1b00) |
| helper | unslid `0x18b320b2c` (base+0x2c) |
| 32KB slice | `slices/libquarantine.dylib.slice` SHA `66a44633…5b99`; `llvm-objdump` **exit 1** “section 0 offset … extends past the end”; not a valid standalone disassembly input |
| full image | `__TEXT` code disassembles cleanly; other segments are sparse (see §5) |

`llvm-objdump -d` 对 full image 的 `__TEXT` 成功；以下地址均为 unslid
`__TEXT` 地址。

## 2. 精确 getter/helper

**Getter `0x18b320b00`：**

```text
18b320b00  pacibsp
18b320b04  sub sp,sp,#0x30
18b320b08  stp x29,x30,[sp,#0x20]
18b320b0c  add x29,sp,#0x20
18b320b10  ldp q0,q1,[x0]        ; copy 32-byte input audit_token_t
18b320b14  stp q0,q1,[sp]
18b320b18  mov x0,sp
18b320b1c  bl  0x18b320b2c      ; helper at +0x2c
18b320b20  ldp x29,x30,[sp,#0x20]
18b320b24  add sp,sp,#0x30
18b320b28  retab
```

**Helper `0x18b320b2c`：**

```text
18b320b2c  pacibsp
18b320b30  sub sp,sp,#0xe0
18b320b34  stp x20,x19,[sp,#0xc0]
18b320b38  stp x29,x30,[sp,#0xd0]
18b320b3c  add x29,sp,#0xd0
18b320b40  mov x19,x2            ; x2=0 from tccd
18b320b44  str xzr,[sp,#0xb8]
18b320b48  add x8,sp,#0x10
18b320b4c  movi.2d v0,#0
18b320b50..b6c  stur q0 ...      ; zero 0x38..0xc0 area
18b320b70  ldr w8,[x0,#0x14]     ; token val[5] = pid
18b320b74  ldr w9,[x0,#0x1c]     ; token val[7] = pidversion
18b320b78  stp x2,x8,[sp,#0x8]   ; [sp+8]=0, [sp+0x10]=pid
18b320b7c  stp x9,x1,[sp,#0x18]  ; [sp+0x18]=pidversion, [sp+0x20]=out token ptr
18b320b80  add x8,sp,#0x8
18b320b84  stp x8,x3,[sp,#0x28]  ; [sp+0x28]=&sp+8, [sp+0x30]=x3
18b320b88  adrp x0,0x18b321000
18b320b8c  add x0,x0,#0xe0b       ; string "Quarantine"
18b320b90  add x2,sp,#0x10        ; syscall arg struct
18b320b94  mov w1,#0xb6           ; selector 182
18b320b98  bl  0x18b321bc0        ; shared __auth_stubs policy entry
18b320b9c  cbz x19,0x18b320bac    ; x19=0: no local copy
18b320ba0  cbnz w0,0x18b320bac
18b320ba4  ldr x8,[sp,#0x8]
18b320ba8  str x8,[x19]
18b320bac  ... retab
```

**FACT（精确 22F82）：**

- Policy/service string = **`Quarantine`**，位于 `0x18b321e0b`
  （file offset `0x2e0b`）。
- Selector = **`0xb6` (182)**。
- Syscall arg `x2 = sp+0x10`；结构：
  - `+0x00` = pid（32-bit zero-extended）
  - `+0x08` = pidversion
  - `+0x10` = 调用方提供的 32-byte output token 指针（x1）
  - `+0x18` = 指向 `[sp+8]`（其值为 0）的指针
  - `+0x20` = x3（tccd 传 0）
  - `[sp+8]` = x2（0），位于结构之前。
- 输出 token 指针由调用方拥有（x1），helper 不分配/不 retain；成功时
  内核经该指针写入；失败时 R004 观测为全零。helper 的 `x19` 分支只在
  caller 传非零 x2 时复制 8 字节，tccd 路径不进入。
- 返回值：helper 返回 stub 的 `w0`；getter 原样返回。tccd 错误路径读取
  errno（R004 `0x1000514?` 格式）；libquarantine 在此 getter/helper 中
  **没有** 103 赋值或 ENOPOLICY 常量。

## 3. 其他 responsibility/Quarantine selector（同一 stub）

精确 `__TEXT` 中调用同一 stub `0x18b321bc0` 且使用同一 `Quarantine`
字符串的 selector 包括：

```text
0xb4 (180)  -> 0x18b320c5c  ; wrapper with 3 out pointers
0xb5 (181)  -> 0x18b320db4  ; two sxtw inputs
0xb6 (182)  -> 0x18b320b98  ; audit-token getter (this report)
0xb7 (183)  -> 0x18b320df0  ; token pid/pidversion
0xb8 (184)  -> 0x18b320e2c  ; token pid/pidversion
0xb9 (185)  -> 0x18b320e4c  ; x2=0
0xba (186)  -> 0x18b320e74  ; pointer input
0xbb (187)  -> 0x18b320ea8  ; pointer input
0x53 (83)   -> 0x18b3204e0  ; other Quarantine op
0x58 (88)   -> 0x18b320d34  ; other Quarantine op
0x59 (89)   -> 0x18b320d78  ; other Quarantine op
```

**FACT：** 这些 wrapper 全部把 `w0`/errno 原样传回；可见的局部错误常量包括
`0x16`（十进制 22，EINVAL）、`0x22`（十进制 34，ERANGE）和 `0xc`（12），
**没有 103**。`responsibility_get_responsible_for_pid` `0x18b320bbc`
的局部 null 分支在 `0x18b320bec` 经 `__error` 写入 `0x22`（34/ERANGE）
并返回 -1；不是 103。`grep #0x67` 仅命中 `cmp`（字符串代码），无
`mov w…,#0x67` 赋值。

## 4. 103 归因修正（关键）

**FACT（精确 getter/helper）：** 103 不在 libquarantine 的
getter/helper/可见 wrapper 内产生；它们只返回 stub 的 `w0`/errno。

**FACT（15.6 版本相邻，不是 22F82）：** 同一 helper 形状的 stub 在
`analysis/device-leftovers…/libquarantine.dylib`（UUID
`8CE8DAD0-…`，macOS 15.6）解析为 **`___sandbox_ms`**；同版本
`libsystem_kernel` 中 `___sandbox_ms` 与 `___mac_syscall` 是同一地址
`0x1804335e8`。这只能作为 Ventura 的 THEORY。

**THEORY（22F82）：** `0x18b321bc0` 很可能也是 `___sandbox_ms`/
`__mac_syscall`，但精确 stub provider 未解析：其 `__auth_stubs` GOT
slot 是 `0x1dfef8f88`，而该地址不被提取 image 的任何 segment 覆盖
（见 §5）；不能把 15.6 的名字写成 22F82 事实。

**FACT（本地 XNU 源码，相邻版本）：** `__mac_syscall`
（`security/mac_base.c:1567-1600`）在以下三种位置都可能返回/传播 103：

1. `mac_proc_check_mac_syscall(p, target, call)` 返回 ENOPOLICY（precheck）；
2. `error = ENOPOLICY` 默认值，遍历后无匹配 `mpc_name`/`mpo_policy_syscall`；
3. 匹配到的 `mpo_policy_syscall` 自身返回 ENOPOLICY。

因此 **103 不唯一证明“无 handler”**。R008 的“103 = 无匹配 handler”必须
撤回为“可能是三者之一”；精确 Ventura stub provider 和设备内核实际
policy/dispatch 未被本轮观测。

## 5. Sparse/invalid offsets 与现有替代

精确 image 的 load commands/segment 状态（FACT）：

| 项 | 值 |
| --- | --- |
| `__TEXT` | vmaddr `0x18b31f000`, fileoff 0, filesize 0x3000（有效代码） |
| `__DATA_CONST` | vmaddr `0x1d41ac958`, fileoff 1223219544, filesize 80 |
| `__AUTH_CONST` | vmaddr `0x1dd78c558`, fileoff 1346819416, filesize 272 |
| `__DATA` | vmaddr `0x1d8ca4fe0`, fileoff 1268277216, filesize 16 |
| `__LINKEDIT` | vmaddr `0x22193c000`, fileoff 1346822144, filesize 128 |
| `LC_SYMTAB` | symoff/nsyms/stroff/strsize = 0/0/0/0 |
| `LC_DYSYMTAB` | all fields 0; `indirectsymoff/nindirectsyms = 0` |
| `LC_DYLD_INFO`/`LC_DYLD_CHAINED_FIXUPS` | absent |
| `LC_DYLD_EXPORTS_TRIE` | dataoff `1143721272`, datasize 1648（位于声明的 `__LINKEDIT` 之前，cache-absolute/sparse） |
| stub GOT slot | `0x1dfef8f88`（stub `0x18b321bc0`），**不被上述 segment 覆盖** |
| `otool -Iv` | “entries extends past the end of the indirect symbol table” |

**结论：** 这份 1.3GB 文件保留了精确 **`__TEXT` 代码**，但不是自包含
standalone image；stub 的 GOT/bind 数据在缺失的 cache segment 中。
精确 provider 名称因此未解析。

**现有替代：**

- 对 `__TEXT` 反汇编，使用 full image（`llvm-objdump` 成功）；32KB slice
  因 section offset 越界被 `llvm-objdump` 拒绝，不能作为 standalone 替代。
- 对 stub 名称，只能使用 15.6 exact image 的 `___sandbox_ms` 作为**版本
  相邻**证据；不能当 22F82 事实。
- 缺失的精确范围：GOT slot `0x1dfef8f88`（8 bytes）及其 chained
  fixup/bind 记录，和覆盖该地址的 cache segment/mapping。不要猜 bytes。

## 6. Native 与 Ventura tccd metadata

- 后端 R005：Ventura tccd（fat `42c388e8…`，arm64e thin `039f64b7…`，
  UUID `9845D17F…`）导入该 getter；native iOS tccd（SHA `67669ed7…`，
  UUID `0120CE08-6868-352E-B7EC-819850D57587`，CDHash `1ba18ff5…`）
  **没有 responsibility imports**（`nm -m`/`dyld_info -imports`）。
- R004 的 “Native import contract: tccd-22F82-arm64e …” 实际描述的是
  **Ventura** tccd；native helper 的 dlsym/flat-import 失败只是 helper
  的可用性事实，native platform/entitlement gating 仍是 THEORY。
- Ventura tccd 的 `from libSystem` 是链接 umbrella；实际 provider 已由
  chroot `dladdr` 定位为 libquarantine UUID `A2D17FF6…`。

## 7. 一个证据支持的 next control

**实际 policy/selector 边界观察（首选）：** 在 owned generation 上，以
只读断点/trace 观察一次 probe audit-token getter 的 `__mac_syscall`/
`Quarantine` policy 边界，记录：

- caller PID、policy 字符串 `Quarantine`、selector `0xb6`；
- `mac_proc_check_mac_syscall` 的返回；
- 是否匹配 policy（`mpc_name`）及其 `mpo_policy_syscall` 返回；
- 调用者 errno/输出 token 是否写入。

这能唯一区分 precheck / no-match / matched-handler 三种 103 来源，且使用
精确 image 已确认的 policy/selector/struct。若 debugger 不可用，回退为
一个 ABI-preserving 的只读 helper，分别调用精确 image 中 selector
`0xb4`、`0xb6`、`0xb7` 的 wrapper，比较 raw return/errno/output；
不进入 setter、不写内核、不伪造对象。

**剩余未知：**

- 精确 stub provider 名称（GOT/bind 缺失）；
- 设备内核实际 `Quarantine` policy 列表/handler；
- 三种 103 来源的实际归属；
- 成功路径的输出 token 写入/部分写契约；
- 是否存在不同 selector 的可用 genuine alternative。

## 8. 命令与退出码

| 命令 | 退出 | 结果 |
| --- | --- | ---: |
| `shasum -a 256` 精确 image/slice/metadata | 0 | 见 §1 |
| `otool -l` UUID/`__TEXT`/segments/symtab/dysymtab | 0 | 见 §1/§5 |
| `llvm-objdump -d` full image 0x18b320b00–0x18b320bb8 | 0 | 见 §2 |
| `llvm-objdump -d` 32KB slice | **1** | section offset past end |
| Python 读取 file 0x2e0b | 0 | `Quarantine` |
| Python 解析 stub/GOT/segments | 0 | GOT slot 0x1dfef8f88 不在 segment 内 |
| `grep #0x67` / 局部 103 扫描 | 0 | 仅 `cmp`，无 `mov #0x67` |
| `sed` XNU `mac_base.c` | 0 | ENOPOLICY 三处可能来源 |
| `git log --oneline -1`（只读） | 0 | HEAD `7e5d771` |

未运行测试、构建、package、设备命令；未执行 commit/reset/stash/push。

## 9. 限制

- 精确 `__TEXT` 代码分析为 FACT；stub provider/GOT 因 sparse image 缺失，
  `___sandbox_ms` 仅为 15.6 版本相邻 THEORY。
- 本地 XNU 为 `8792.81.2`，设备内核 `8792.82.2`；`__mac_syscall` 分支是
  源码结构证据，不是设备内核执行证明。
- 未观测设备 policy 列表/handler；103 的三种来源仍未唯一归属。
- Native tccd 无 responsibility imports；其 provider 与 gating 仍是
  THEORY，未用 helper 的 dlsym 失败替代 daemon 行为。

(handover to codex now)
