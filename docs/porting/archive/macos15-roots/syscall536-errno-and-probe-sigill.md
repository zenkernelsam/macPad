# syscall 536 errno = EINVAL(22) + dyld 内探针 SIGILL 根因

日期：2026-09-26 ｜ 设备 iPad13,11 / iOS 16.3 (20D47) / T8112，Dopamine rootless
IDA：Instance1=dyld thin（base 0x0）｜Instance3=kernel `kc_raw_16.3_T8112.bin`（base 0xfffffe0007004000）

## TL;DR
- **536 真实返回值 = `22` (EINVAL)**。用“stub 拦截 + 写 stderr”探针实测：本回合 `/bin/echo` 3/3，历史累计 6/6，共 **9/9**。stderr 尾部 8 字节 = `16 00 00 00 00 00 00 00`。
- **探针 SIGILL(rc=132) 的根因 = 机器码字节序写反**：`build_dyld.py` 的 `P` 表把 arm64 指令按“反汇编器字序”的 hex(如 `d10043ff`) 直接 `bytes.fromhex` 写入文件，落盘即变成小端逆序字节 → 非法指令。**与 arm64e 认证无关，与 0x35698/0x38d08 偏移语义无关。**
- **当前状态下 536 最可能的失败门（排序）**：① 引擎层“region 已被填充”`KERN_FAILURE`→EINVAL；② setup 的 CS 覆盖门 `ubc_cs_blob_get`；③ `ubc_getobject` NULL。EPERM(1) 诸门与 Sandbox 门均被排除。
- **更正旧结论**：`docs/porting/kernel-syscall536-finding.md` 的“返回 40(EMSGSIZE) = Sandbox”对**当前**这个 vnode 不成立（该 vnode 已置 `VSHARED_DYLD` → sandbox 钩子短路返回 0）；当前实测是 22。

---

## ① 探针 SIGILL 的根因

### 逐字证据（IDA Instance1 + 设备 raw 字节）
在 thin 偏移 `0x38d08`（`__text` 内一段**零 xref 的 NOP 填充**，原始字节 = 连续 `1f 20 03 d5`）反汇编三份数据：

| 来源 | 0x38d08 前几条字节 | IDA `create_insn` 结果 |
|---|---|---|
| pristine（NOP 填充） | `1f 20 03 d5 …` | `NOP, NOP, …`（合法） |
| **`build_dyld.py` fwcave2**（字序 hex） | `d1 00 43 ff f9 00 03 e0 …` | **`DCB 0xD1`（len=0，非法）** |
| `_bx2_tmp.py`（真小端文件序） | `e0 0f 1f f8 e1 03 00 91 …` | `STR X0,[SP,#-0x10]! ; MOV X1,SP ; MOV X2,#8 ; MOV X0,#2 ; MOV X16,#4 ; SVC 0x80`（合法） |

- fwcave2 想写的是字 `0xd10043ff`（=`sub sp,sp,#0x10`）；但把 hex 串当文件字节写，得到的是 `d1 00 43 ff`，其小端字 = `0xff4300d1` → **非法指令**。
- `errprobe`（`0x35698`）同理：hex `12001c00d2800030d4001001` 落盘首字 = `0x001c0012` → **非法指令**（就是要写 `and w0,w0,#0xff` 的那个字被写反了）。
- 设备上正在跑的 `stubprobe.bin`（正确小端）反证：`0x76e00 = c2 07 ff 17`（=`b 0x38d08`），`0x38d08 = e0 0f 1f f8 e1 03 00 91 …`（合法）→ 正常执行。

### 证据矩阵（运行结果）
| 构建 | cave 字节序 | 结果 |
|---|---|---|
| cleanB（无探针） | — | **rc=0** |
| `stubprobe.bin`（_bx2，真小端） | 合法 | **rc=22**（cave 跑了，exit(x0&0xff)） |
| `_test_fw2`（build_dyld fwcave2，字序） | 非法 `DCB` | **rc=132 (SIGILL)** |
| `_test_err`（build_dyld errprobe，字序） | 非法 `DCB` | **rc=132 (SIGILL)** |

### 逐项排除用户给的三个假设
1. **不是 arm64e 认证/分支问题**：cleanB rc=0、stubprobe rc=22，arm64e thin dyld 在 arm64 主二进制下落得动。`__shared_region_map_and_slide_2_np` stub（0x76df8）里的 `PACIBSP/RETAB` 都在**错误分支**上，且探针进的是成功分支 0x76e00，与认证无关。
2. **不是偏移语义错**：同一个 `0x38d08`（以及 `0x35698`）只要是合法字节就能正确运行（stubprobe rc=22）。
3. **是探针补丁本身破坏了执行流**，且**破坏方式是字节序**——与状态文档 2026-09-26 记录的 launcher `0x46FC` 被写成 `0x001c0012`（同为 `12001c00` 的字序逆序）导致 SIGILL(132) 是**同一个 bug**。

### 为何“无 crash 报告”
SIGILL 发生在 dyld 内部，被 `launchdchrootexec` 折叠为退出码 132，CrashReporter 未为这条路径生产 `.ips`。设备上唯一的 `true-*.ips` 都是**下游**崩溃：`EXC_BAD_ACCESS @ 0x00000001f8000000`（=`0x180000000 + 0x78000000`，即 `dynoff` 补丁假设的 dynamic region 地址）——正因为 536 失败、region 从未映射，dyld 后来解引用该地址才 SIGSEGV。这**反证** 536 从未成功。

### 修正
探针机器码 hex 必须写成**小端文件序**（即指令字的字节逆序）。参考已工作的 `_bx2_tmp.py`（对每个 cavue 指令用 clang 汇编后取真字节，或用 `struct.pack('<I', word)`）。

---

## ② 可靠读出 536 errno 的方法（已跑通，值 = 22）

思路：避开会被内核中途 kill 的 exit 探针，改为**拦截系统调用 stub 的返回处**，用 `write(2,…)` 把原始 `x0`（8 字节）写到 stderr，再 `exit(x0&0xff)`。

- 拦截点：`__shared_region_map_and_slide_2_np` @ `0x76df8` 的
  ```
  76df8  MOV X16,#0x218
  76dfc  SVC 0x80
  76e00  B.CC locret_76E20     ; ← 把它改成 无条件 b 0x38d08
  76e04  PACIBSP …  BL _cerror_nocancel  ; 错误路径
  76e20  RET
  ```
  把 `B.CC`（**成功**分支）改成无条件 `b 0x38d08` 后，成功/失败两条路都汇入 cave。
- cave @ `0x38d08`（真小端 40 字节）：
  `STR X0,[SP,#-16]! ; MOV X1,SP ; MOV X2,#8 ; MOV X0,#2 ; MOV X16,#4 ; SVC`（write(2, &x0, 8)）；`LDR X0,[SP] ; ADD SP,SP,#16 ; AND W0,W0,#0xff ; MOV X16,#1 ; SVC`（exit）。

### 命令级流程
```sh
# 构建（本地）
python3 analysis/dyldwork/_bx2_tmp.py stubprobe.bin      # 写 /tmp/stubprobe.bin
# 签名 + 部署（设备）
ldid -Hsha256 -Cadhoc -S ent /tmp/stubprobe.bin
for a in arm64 arm64e; do jbctl trustcache add "$(ldid -arch $a -h /tmp/stubprobe.bin|sed -n 's/^CDHash=//p')"; done
rm -f /var/mnt/rootfs/usr/lib/dyld; cp -f /tmp/stubprobe.bin /var/mnt/rootfs/usr/lib/dyld; chmod 755 /var/mnt/rootfs/usr/lib/dyld
# 运行 + 取 errno
L=/var/jb/usr/macOS/bin/launchdchrootexec
$L 0 0 /var/mnt/rootfs /bin/echo X 2>/tmp/e.1; echo "rc=$?"   # → rc=22
od -An -tx1 -v /tmp/e.1 | tail -1                            # → 16 00 00 00 00 00 00 00  (=22)
```
**实测**：`/bin/echo` rc=22 ×3、`/bin/ls` rc=22 ×3；raw `x0` = `16 00 00 00 00 00 00 00` = **22 (EINVAL)**。

### 备选（更稳，若需继续）
- 拦截点改到 setup 之后、引擎之前无法区分；要区分门，需在**内核**侧：在 `sub_8459570` 各 EINVAL 站点（见 ③）读该分支是否命中（KRW 或断点）。
- 或改探针 cave 额外调 `shared_region_check_np`(294) 读 region 是否已填充（294 返回 0/12 可用于区分“region 空 vs 满”）。

---

## ③ 当前 536 失败门排序（源码行 + 内核 VA + 依据）

syscall 536 → 包装 `sub_FFFFFE0008459134` → setup `sub_FFFFFE0008459570` → 引擎 `sub_FFFFFE0008061EF0`（=`vm_shared_region_map_file`）。
包装层：`v9=setup(...)`，若为 0 则 `v33=引擎(...)`；`v33>3`（含 `KERN_FAILURE=5`）→ **22**。故 setup 与引擎都能产出 22。

| # | 门 | 源码 | errno | 内核 VA |
|---|---|---|---|---|
| 1 | mappings 溢出 | `vm_unix.c:2237` | EINVAL 22 | 0x84596c4 |
| 2 | **无 shared region**（`vm_shared_region_trim_and_get`==NULL） | `vm_unix.c:2245` | EINVAL 22 | 0x8459780 |
| 3 | root-dir 不匹配 | `vm_unix.c:2264` | EPERM 1 | 0x8459764 |
| 4 | fd==-1 且 mappings>1 / 未对齐 | `vm_unix.c:2288 / 2303,2311` | EINVAL 22 | 0x8459d74 / 0x8459d04 |
| 5 | 非 FREAD | `vm_unix.c:2334` | EPERM 1 | 0x8459d38 |
| 6 | v_type!=VREG | `vm_unix.c:2357` | EINVAL 22 | 0x8459d50 |
| 7 | `mac_file_check_mmap`（Sandbox/AMFI） | `vm_unix.c:2372` | 透传 | 0x8459a08 |
| 8 | uid!=0 | `vm_unix.c:2456` | EPERM 1 | 0x8459d64 |
| 9 | 卷不匹配（非根卷/非 Cryptexes） | `vm_unix.c:2514` | EPERM 1 | 0x8459d28 |
| 10 | `ubc_getobject`==NULL | `vm_unix.c:2599` | EINVAL 22 | 0x8459ce0 |
| 11 | **CS 覆盖**（`ubc_cs_blob_get` 为 NULL/不足） | `vm_unix.c:2620` | EINVAL 22 | 0x8459cbc |
| 12 | **引擎：region 已填充**（`sr_first_mapping != -1`） | `vm_shared_region.c:1464` | KERN_FAILURE→EINVAL 22 | `sub_8061EF0` |

### 排序与依据
1. **引擎“region 已填充”**（`vm_shared_region.c:1464` → `KERN_FAILURE` → 22）。依据：setup 的诸门（含 CS 覆盖）用同一缓存文件在同一参数下**曾经通过**——状态文档记录的“空 region 时 536 返回 0（成功）”（`dyld-15.6.1-state.md` exit-probe 段：`@0x35698 → T1=0`）即 setup 全绿。iOS 上 shared region 在启动/`vm_map_exec` 时已被**系统缓存填充**，用 `hasexisting/prereuse` 补丁强制走 map 路径就会命中 `sr_first_mapping != -1` → 22。
2. **setup gate11 CS 覆盖**（`vm_unix.c:2620`）。外来 macOS 缓存挂在 rootfs（其 `/System/Library/dyld/dyld_shared_cache_arm64e` 是指向 `../../Volumes/Preboot/Cryptexes/OS/...` 的**符号链接**）；若该文件未带内核可解析的 CS blob 或所提交区间不被 blob 覆盖 → `ubc_cs_blob_get` 返回 NULL → 22。次于 #1，因 #1 的“空 region 曾返回 0”说明该门在被测缓存上能过。
3. **setup gate10 `ubc_getobject`==NULL**（`vm_unix.c:2599`）。APFS 常规文件一般有 UBC 对象，可能性较低。
4. **setup #2 无 shared region**（`vm_unix.c:2245`）。正常 exec 会建立，可能性低。

**已排除**：uid(门8)、卷(门9)、root-dir(门3)、非可读(门5) 都产出 **EPERM(1)**，与实测 22 不符；Sandbox（门7）会产出其自身 errno（旧文档的 40），均与当前 22 不符——且该 vnode 已置 `VSHARED_DYLD`（KRW 观察），sandbox 钩子在 `!VSHAREDCACHE` 分支前就 `return 0`。

### 与旧文档的冲突（明确更正）
`kernel-syscall536-finding.md` 的“536=40(EMSGSIZE)”是**当时另一配置**的实测；其字节串本身也是字序写法（同一字节序 bug），可测性存疑。当前配置实测恒为 **22 (EINVAL)**，且与“region 已填充/引擎 KERN_FAILURE”一致。

---

## 命令速查
```sh
# 现役可跑 dyld（本回合验证）
ssh ... 'md5sum /var/mnt/rootfs/usr/lib/dyld'   # fbd6c127497e287b93ced5d5130ca0ca = /var/mobile/stubprobe.bin
# 复现 errno
ssh ... '/var/jb/usr/macOS/bin/launchdchrootexec 0 0 /var/mnt/rootfs /bin/echo X 2>/tmp/e.1; echo rc=$?'
# 还原干净 dyld（去掉探针）
ssh ... 'rm -f /var/mnt/rootfs/usr/lib/dyld; cp -f /var/mobile/dyld_restore.bin /var/mnt/rootfs/usr/lib/dyld; chmod 755 /var/mnt/rootfs/usr/lib/dyld'
```
