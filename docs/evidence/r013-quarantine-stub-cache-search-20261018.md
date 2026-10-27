# R013 只读取证：22F82 Quarantine 0xb6 stub provider 的本地 cache/fixup 搜索

日期：2026-10-18。范围：只搜索现有本地 **22F82** shared-cache/fixup
资料，尝试解析精确 `libquarantine.dylib` 中 stub `0x18b321bc0` 的
`__auth_stubs` GOT `0x1dfef8f88` 所指向的真实 provider。**未 SSH/操作设备、
未提取设备内容、未修改源码/测试/构建输入、未安装依赖、未写 Git history。**
只新增本文件与 `coordination/_relay/round-013-frontend-result.md`。

候选 HEAD：`c97e219`。

## 1. 输入与身份复核（FACT）

| 项 | 值 |
| --- | --- |
| exact image | `analysis/r005-responsibility-inputs/libquarantine.dylib` |
| SHA-256 | `f5989c8f4afc9985e95795bf683679eed629af8b5ae288a0fb87317ba0e844f2` |
| UUID | `A2D17FF6-CBC6-3D19-89E1-F5E57191E8A3` |
| arch / filetype | arm64e / `MH_DYLIB` |
| LC_BUILD_VERSION | macOS 13.4（另有 macCatalyst 16.5 同文件 build version） |
| `__TEXT` | vmaddr `0x18b31f000`, fileoff 0, filesize `0x3000` |
| file size | 1,346,822,272 B = `0x5046E080` |
| stub | `0x18b321bc0`，file offset `0x2bc0` |
| `__auth_stubs` | `0x18b321ba0`, size `0x1e0` = 30 × 16-byte stubs |
| GOT slot | `0x1dfef8f88` |
| cache | WS spindump：`BE59BB71-7B69-3EFC-86A8-5625A077AE2E`，slide `0x1ba40000`，slid base `0x19ba40000` → unslid base `0x180000000` |
| exact arm64e live slide | libquarantine `0x19d70b000 - 0x18b31f000` = `0x123ec000` |

以上由 `shasum`、`file`/`lipo -info`、`otool -l` 与 Python
load-command 解析直接得到。

**FACT（live arm64e image-list 一致性）：**
`coordination/artifacts/r006-lldb-pane.txt:83,102` 同时列出

```text
[ 65] A2D17FF6-… 0x000000019d70b000 /usr/lib/system/libquarantine.dylib
[ 84] 34A49B54-82B2-37A1-9314-F6A4A2BB3FF8 0x00000001927ab000 /usr/lib/system/libsystem_kernel.dylib
```

两个 image 的 runtime base 分别减去本地 exact image 的 unslid base
（`0x18b31f000` / `0x1803bf000`）都得到同一个 slide `0x123ec000`。
这独立证明本地 exact libquarantine 与 R005 metadata 中的 arm64e
libsystem_kernel 确实属于该 live arm64e 进程的 image 集。

**架构/身份差异（重要，不混用）：** WS spindump 是 arm64
WindowServer，列出 `/usr/lib/system/libsystem_kernel.dylib`
UUID `C76E6BED-4635-30C6-8F19-FB829BBE1AE1`，runtime base
`0x2021fa000`；它不是 R005/lldb 的 arm64e `34A49B54`。exact
`libquarantine.dylib` 是 arm64e，因此其 stub 应通过 arm64e cache
映射解析；WS 的 arm64 kernel 不能当作该 stub 的 target 证据。

## 2. 本地搜索范围与结果

搜索命令（见 §6）覆盖：

- `analysis/r005-responsibility-inputs/`：只有 sparse exact image、
  8 个 32KB dependency slices 和 `metadata.json`；**没有 cache/subcache、
  没有 fixup/bind 表、没有完整 dependency image**。
- `analysis/` 下 `dyld_shared_cache*`：只有
  `analysis/ios163cache/dyld_shared_cache_arm64e`（UUID
  `136ECC70-B2FF-3D53-8BBE-0C92B352EC77`, 376,832 B）和
  `...arm64e.19`（UUID `3C5E5C44-B1C3-3817-8511-05B373023A98`,
  115,867,648 B）。它们是 **iOS 16.3** cache，不是 22F82；其余命中是
  dyld 源码文件名包含 `dyld_shared_cache_util`，不是 cache 数据。
- `analysis/share-review-20261015/`、`analysis/share-review-20261018/`、
  `analysis/macPad-15.6.../`：15.6/24G90 分享包资料，不是 22F82；
  未用其 symbol/cache 结果。
- `analysis/device-leftovers-20261006/.../libsystem_kernel.dylib`：
  UUID `6E4A96AD-04B8-3E8A-B91D-087E62306246`，不是 exact arm64e
  Ventura dependency。
- `analysis/r007-ws-sample/ws-spindump-20261018.txt` 与
  `loaded-images.json`：只有 cache UUID/slide 与 image base，**没有
  cache bytes、GOT 或 fixup 记录**。
- `coordination/artifacts/r006-lldb-pane.txt`、
  `r007-lldb-pane-full.txt`：只有 image list 和线程栈，没有
  `0x1dfef8f88`/`1f22e4f88` 的内存读或寄存器。
- 本机 `analysis/VirtualMac`、`analysis/dyld-cache-15.6.1` 等：不存在
  （15.6 cache 目录已按 2026-10-04 manifest 删除；22D68 host cache
  只在历史 inventory 文档中出现，且是 13.2.1，非 22F82，未使用）。
- 对本地小 cache/image 候选搜索 `BE59BB71`：`SkyLight_22F82`,
  `TCC_22F82`, `ios163cache/*`, `device-leftovers libsystem_kernel`
  均为 0；只有 spindump/文档文本命中。

**结论（FACT）：本地没有 22F82 的 `dyld_shared_cache_arm64e` /
`.01` / `.a2s` / `.map` 或对应 fixup 记录。**

## 3. 对 exact sparse image 的结构检查

### 3.1 stub 指令

`xcrun llvm-objdump -d --start-address=0x18b321ba0
--stop-address=0x18b321d80`（exit 0）给出：

```text
18b321bc0: f02a5eb1  adrp x17, 0x1dfef8000
18b321bc4: 913e2231  add  x17, x17, #0xf88
18b321bc8: f9400230  ldr  x16, [x17]
18b321bcc: d71f0a11  braa x16, x17
```

即 stub 结构确实固定指向 GOT `0x1dfef8f88`，但指令本身不含符号名；
`braa` 的目标在运行时由该 GOT slot 的 authenticated pointer 决定。

### 3.2 segment/section 覆盖

`otool -l` / Python 解析（exit 0）：

| segment | vmaddr | vmsize | 覆盖 0x1dfef8f88? |
| --- | --- | --- | --- |
| `__TEXT` | `0x18b31f000` | `0x3000` | 否 |
| `__DATA_CONST` | `0x1d41ac958` | `0x50` | 否 |
| `__AUTH_CONST` | `0x1dd78c558` | `0x110` | 否 |
| `__DATA` | `0x1d8ca4fe0` | `0x10` | 否 |
| `__LINKEDIT` | `0x22193c000` | `0x80` | 否 |

image 自己的 `__AUTH_CONST,__auth_got` 是
`0x1dd78c578 + 0xf0`，`__DATA_CONST,__got` 是
`0x1d41ac958 + 0x10`；两者都**不是** `0x1dfef8f88`。读取 sparse
文件中这两个区（file offsets `0x5046d578`、`0x48e8d958`）全是 0，
说明本地提取也没有保留 image-local relocation target。

### 3.3 file offset / subcache

unslid cache base = `0x180000000`（由 WS spindump
`0x19ba40000 - 0x1ba40000` 直接得到）。

```text
slot cache offset = 0x1dfef8f88 - 0x180000000 = 0x5fef8f88
sparse file size  = 0x5046e080
delta beyond file = 0xfa8af08
```

因此 slot 不在本地 sparse 文件中。它属于 22F82 arm64e cache 的
main/subcache 中的某个 mapping；具体是哪一个 subcache 需要 22F82
cache header/mapping table，不能猜。

### 3.4 relocation/symbol metadata

- `LC_SYMTAB` = `(symoff, nsyms, stroff, strsize) = (0,0,0,0)`。
- `LC_DYSYMTAB` 全部 18 个字段为 0（包括
  `indirectsymoff`/`nindirectsyms`）。
- 无 `LC_DYLD_INFO`/`LC_DYLD_INFO_ONLY`；无
  `LC_DYLD_CHAINED_FIXUPS`。
- `LC_DYLD_EXPORTS_TRIE`：dataoff `0x442bcd38`，datasize `0x670`；
  该范围虽在文件内，但字节全为 0，`dyld_info -exports` 报
  `LINKEDIT overlap of start of LINKEDIT and exports trie`。
- `otool -Iv`：`Indirect symbols for (__TEXT,__auth_stubs) 30 entries
  (entries extends past the end of the indirect symbol table)`，exit 0。
- `nm -a`：`no symbols`，exit 0。
- `grep -a -c -o`：`sandbox_ms`=0，`mac_syscall`=0，
  `___sandbox_ms`=0，`Quarantine`=2（仅 policy 字符串）。
- `__auth_stubs` 的 30 个 stub 只给出 30 个 GOT 地址；没有名称表。

**两个相互独立的检查都失败：**

1. **地址/文件覆盖检查**：slot 不属于任何声明的
   segment/section，且超出本地 sparse 文件（`0x5fef8f88 >
   0x5046e080`）。
2. **relocation/命名检查**：无 symtab/dysymtab、无 bind/chained-fixup
   记录、无 indirect symbol 名称、无 provider 字符串；image-local
   GOT/auth-GOT 也是零值。即使有 8 字节 slot 的原始值，也需要
   fixup/bind 解释，不能仅凭 raw bytes 命名。

因此本地 sparse image 不能解析 exact provider。**不能把 15.6 exact
image 的 `___sandbox_ms` 写成 22F82 FACT；它仍是版本相邻 THEORY。**
也不能从 stub 名称反推 103 分支；R009 的三种 `__mac_syscall` 103
来源仍未被精确观测。

## 4. 需要从设备获取的最小 exact artifact/range

按优先级，任意一项足够继续（但仍需两项结构检查）：

**A. cache 侧（首选、只读）：**

1. 22F82 arm64e cache 的 `dyld_cache_header` + mapping table
   （UUID `BE59BB71-7B69-3EFC-86A8-5625A077AE2E`，build 22F82），
   用来把 unslid `0x1dfef8f88` 唯一映射到
   `dyld_shared_cache_arm64e` 或 `.01` 的 exact file offset；记录
   subcache SHA-256。
2. 该 offset 所在的 cache page（至少 slot 的 8 字节；page size 以
   22F82 cache header 为准，观测到的 Ventura cache 为 16KB）以及覆盖该
   slot 的 **chained-fixup/bind 记录**：target VA/import ordinal/name。
   仅有 raw 8 字节不够——cache 上可能是未绑定/链式 fixup 占位。
3. 若 fixup 只给出 target VA：目标 image 的 UUID + symbol table/
   export trie/`.a2s`/`.map` 中该地址的符号范围。当前 exact arm64e
   dependency 候选是 libsystem_kernel `34A49B54-…`（R005 的
   device_full 在
   `/var/mobile/macws_r005_evidence_20261018/responsibility-inputs/deps/libsystem_kernel.dylib`），
   但未经该 fixup 证明，不能先假定。
4. 缓存身份：cache UUID、22F82 build、subcache 文件名/大小/SHA-256。

**B. live 只读替代（需要 device 权限，本轮不做）：**

在 owned arm64e chroot generation 上，以只读断点/内存读观察
stub 入口/返回，读取 `x17` 或直接读 `0x1f22e4f88`（slide
`0x123ec000`）的 8 字节，strip PAC 后 `image lookup -a`/`dladdr`
得到目标 image/symbol；不做 setter、不写内核、不伪造 object。
这能直接回答 provider 名称，但不能替代 cache 结构证据。

## 5. 命令与退出码

| 命令 | 退出 | 结果 |
| --- | --- | ---: |
| `find analysis -maxdepth 5 -type f -name 'dyld_shared_cache*'` | 0 | 只有 `ios163cache` 两个文件 |
| `rg -n -S 'BE59BB71|1dfef8f88|18b321bc0|...'` 定向文本 | 0 | 只有 spindump/文档引用，无 cache 文件 |
| `grep -a -c BE59BB71` 对本地 cache/image 候选 | 0 | 全部 0 |
| `shasum -a 256` exact image | 0 | `f5989c8f…44f2` |
| `file` / `lipo -info` exact image | 0 | arm64e `MH_DYLIB` |
| `otool -l` segment/section | 0 | 见 §3.2 |
| `otool -Iv` | 0 | 30 entries extend past indirect table |
| `nm -a` | 0 | no symbols |
| `xcrun llvm-objdump -d` stub range | 0 | 见 §3.1 |
| `grep -a -c -o` provider strings | 0 | sandbox/mac_syscall=0; Quarantine=2 |
| Python file-size/offset arithmetic | 0 | slot beyond file by `0xfa8af08` |
| `git log --oneline -1`（只读） | 0 | HEAD `c97e219` |

未运行测试、构建、package、设备命令；未执行 commit/reset/stash/push。

## 6. 限制

- 本地没有 22F82 cache/subcache/fixup 数据；22D68 host cache 是
  13.2.1，15.6/24G90 share 资料和 iOS 16.3 cache 都不是 exact 输入。
- sparse image 只保留 exact `__TEXT` 代码；data/GOT/LINKEDIT 被稀疏化
  或清零，无法替代 cache。
- arm64（WS `C76E6BED`）与 arm64e（R005/lldb `34A49B54`）
  libsystem_kernel 是不同 image；exact stub target 需要 fixup 记录
  才能归属，不能按名称猜测。
- 未观测设备 cache mapping、fixup/bind、policy 或 103 分支；
  不声称 `___sandbox_ms`、`__mac_syscall` 或任何 103 来源为 22F82
  事实。

(handover to codex now)
