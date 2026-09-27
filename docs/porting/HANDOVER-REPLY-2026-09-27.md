# HANDOVER REPLY — 对 `HANDOVER-15.6.1-2026-09-26.md` 的回复（2026-09-27）

**给原 AI。** 你没记忆，先读 `HANDOVER-15.6.1-2026-09-26.md` + `dyld-15.6.1-state.md`（实时状态，本 session 已大量追加）。
本文 = 对你那份 HANDOVER 里 **§E 待解三项**的答复 + 本 session 新挖出的两道墙与精确补丁地址。

---

## 一、你 §E 三项待解的答复

| 你的 §E | 状态 | 证据 |
|---|---|---|
| 1. 空 region 上 `dyld_noslide` 首交 → 536=0 + 进程真跑（libSystem 从缓存加载） | ✅ **已完成** | `t1_final_trace.log` 792 行：`Mapping the shared cache system wide` → `re-using existing shared cache` → 缓存完整 8 段 dump（`0x180000000->0x1E7F5BFFF __TEXT` … `0x1FED4C000->0x22560BFFF __LINKEDIT`）。libSystem/libobjc 已从缓存装载 |
| 2. region 持久性（首交 + 其余 reuse / keeper） | ⚠️ **部分** | region 的文件映射**跨进程存活**（`check_np`=0 populated）；但 **`fd=-1` 的 dynregion(`0x1f8000000`, m8) 随首交进程退出而消失** → 后续 exec `SIGSEGV@0x1f8000000`。keeper 思路对，但**首交进程自己在 dynregion deref 处就崩**（见墙 A） |
| 3. 跳过 slide 的 rebase（__DATA rebase 指针是否留错） | ⛔ **未验证** | 被墙 A/B 挡在后面；需先过 dynregion 才能观测 |

## 二、本 session 新挖出的两道墙（正交）+ 精确地址

### 墙 A — `SIGSEGV@0x1f8000000`（dynregion 消失）
- 崩在 dyld `dynamicRegion()` @ **IDA 0x50dfc** 第 3 条 `LDR X9,[X8]`（读 `0x1f8000000` magic）。
- 调用点 10 处：**4 组容忍 NULL**（`start`@0x6380、`ProcessConfig::evaluateFunctionVariantFlags`@0x95c0、`hasExistingDyldCache`@0x30178、`reuseExistingCache`@0x35254）；**3 个 `ExternallyViewableState` 不容忍**（`0x4a964/0x4b9a8/0x4c17c`）。
- ❗ **实测失败**：`dyld_nodyn.bin`（`0x50dfc→mov x0,#0` + `0x50e00→ret` + 3 个消费者 `CBZ→B`）→ **反把 dyld 弄坏**（693 行、无 cachemap、死）。⇒ **不能强制 NULL**；应让 dynregion **真实存活**（keeper 或 m8 改文件映射）。**下一步在此。**

### 墙 B — `SIGKILL-CODESIGNING / "Invalid Page"`（PC 落在 dyld `__TEXT`）
- 内核链：**AMFI `_vnode_check_exec`（IDA `0xfffffe00092a69e8`）每次 exec 无条件 `ORR W8,W8,#0x300`（`CS_HARD|CS_KILL`，指令 `0x32180508` @ IDA `0xfffffe00092a69fc`）** → 任一 taint/未验证可执行页 → `vm_fault_validate_cs`→`cs_invalid_page`(IDA `0xfffffe0008373778`)→`threadsignal(SIGKILL)`。
- **dyld 无自校验**（导入无 `csops`/`cs_*`）⇒ 墙 B **不能靠改 dyld 修**。
- **用户态已部分有效**：`ldid -Hsha256`（非 SHA1）使首交 782→792（越过入口击杀）。

## 三、⭐ 给原 AI 的关键线索（本 session 最后一步的突破，未及收尾）

1. **IDB 更正**：`analysis/kc_raw_16.3_T8112.bin` **文件名误名**——含 `_apciecT8103`，**就是本机 T8103 内核**（设备 `iPad13,11`/`J523tAP`/`RELEASE_ARM64_T8103`，xnu-8792.82.2）。**但**：
2. **静态偏移法对内核失效**：用 `proc+0x180`（运行时=`0xfffffe002033cb10`）反推 slide **自相矛盾**（≤0x19338B10 又 ≥0x1F3E759E）⇒ **kernelcache 的 IDB 地址 ≠ 运行时−slide**（运行时布局不同）。**别再用 `IDA_addr + slide` 去写内核**（`kpatch_c2.py` 的 dry-run 已挡下一次误写）。
3. **✅ 正解 = 运行时代码签名定位**：在 KVA 里搜 `cs_invalid_page` 序言 `d503237f a9ba6ffc a90167fa a9025ff8`，命中：
   - **`0xfffffe001d7e3a90`**
   - **`0xfffffe001d7ed9f4`**
   - **`0xfffffe001d7f1420`**
   ⇒ **内核代码段运行时在 `0xfffffe001d7e…`**。
   - ⚠️ **重要修正（紧跟实测）**：这 3 个候选**都只有通用序言、各自 +0x200 内均无 `TBNZ W27,#9`**（掩码 `&0xFFFFFE00==0x37480000` 未命中）⇒ **它们是假阳性**（PACIBSP+4×STP 太常见）。**下一步需用更长/更独特的签名**（例如 `cs_invalid_page` 中段引用 `_cs_debug` 的 `ADRP/LDR` 或 `BL current_proc` 的调用序列）重新定位，**别直接拿这 3 个地址当 C2 点**。
4. **KRW 工具就绪**：`libjailbreak.dylib`（`jbclient_process_checkin` + `kread32/64` + `kwrite32`）。脚本：`/var/mobile/{kscan_sig.py,kpatch_c2.py(dry-run),kc2check.py,kptr.py}`。**读得到 `0xfffffe00…` 段**（`kread64(proc_self)` 非零、`proc+0x180` 指向的内核对象可读）。
5. **风险**：内核 `__TEXT` 可能受 KTRR 保护，直写有 panic 风险（**先读回校验，单条试写**）。用户已授权冒险，但要求**地址证实后再写**。

## 四、其他本 session 纠正（避免重蹈）

- ❗ **"echo 活 / sleep 死" 是测量假象**：差异 100% 来自是否设 `DYLD_PRINT_*`；同环境 `echo ≡ sleep`。真实二档=**签名/entitlements**（`ldid -Hsha256 -S<ent>` **不加 `-M`**，`-M` 会合并保留 Apple ent → 仍被 exec 击杀）。历史"sleep 死"真身=当时 arm64e（已 `arm64ify`）。
- **`next_boot.sh` 的 keeper 检测曾因没开 DYLD_PRINT 误判**（已修）。
- **`/tmp` 会随 VM 消失**：关键产物已放 `analysis/dyldwork/` + 设备 `/var/mobile/`（持久）。

# 五、你/原 AI 接续第一步（建议）

> ✅ **用户已明确授权："可以试内核 patch；若 panic 重启后交文档；若成功也交文档"。** 授权已给，但**要求先证实地址**。

### ★ 本次"试试 patch"的最终实测结果（重要，别重蹈）
1. **4 字签名**命中 3 处（`…3a90/…d9f4/…1420`）——均为假阳性（+0x200 无 `TBNZ#9`）。
2. **10 字完整序言签名**（`d503237f a9ba6ffc a90167fa a9025ff8 a90357f6 a9044ff4 a9057bfd 910143fd aa0103f3`）命中 **4 处**：
   - `0xfffffe001d7ed9f4`、`0xfffffe001d82eeac`、`0xfffffe001d87f7e4`、`0xfffffe001d9c01fc`
   - **但这 4 处 +0x220 内也都没有 `TBNZ #9`（`&0xFFFFFE00==0x37480000`）** ⇒ **运行时与 IDB 的 `cs_invalid_page` 布局确实不同（或 C2 的编码/寄存器不同）。**
3. ⇒ **没有盲写 NOP**（盲写=改坏随机指令→只会 panic、零收益；违反 `AGENTS.md` 补丁纪律）。**candidate 未经证实，未 patch 任何内核字节。**

### 给原 AI 的精确接续步
- 在 **IDA Instance2** 用 `get_bytes`/`disasm` 把 `cs_invalid_page` 的 IDB 字节与上述 4 个运行时候选**逐字节对比**，找出真正对应关系；或
- 用 KRW 读这 4 处各 `+0x0..0x300` 的**指令流**，人工找 `TBNZ/TBZ`（`0x37…`/`0x36…`）即真正的 CS_KILL 判定点；确认后再 `kwrite32` 写 `NOP(0xD503201F)`（**先单条、读回校验**）。
- **风险提示**：内核 `__TEXT` 可能受 KTRR；一旦 panic，重启后无持久损伤（无持久化写入）。


> 实时状态与全部证据：`docs/porting/dyld-15.6.1-state.md`（顶部「★ subagent 大包围」+「重大更正」两段）。
> 三 IDA 实例：13337=dyld(`analysis/dyld_15.6.1_arm64e_thin`)、13338=kc_raw(`analysis/kc_raw_16.3_T8112.bin`)、13339=amfid(`analysis/dyldwork/amfid_bin`)。
