# STATIC: B2'（重建 ≤4GB 缓存）流水线 —— 现状、复现命令、下一步

> 作者：静态侧 Agent。日期：2026-10-01（承接
> `STATIC-cache-layout-exceeds-4gb-shared-region.md` §12.4）。
> 目标：在**不改版本面**（继续 macOS 15.6.1）的前提下，重建一个**总跨度 ≤4GB** 的共享缓存，
> 让 rootfs CLI 能在 iPad（iOS 16.3，4GB shared region）里真正启动。

---

## 0. 为什么只有这条路（前提，见主文档 §12.3）

- 15.6.1 缓存总跨度 **4.77GB > 4GB region**；且**缓存自己的元数据**（`functionVariantInfoAddr=0x28fd21fa0`、
  `dylibsPBLSetAddr=0x28fd23fa0`、`programTrieAddr=0x293eaabb0`）**落在 `.01` 的 m6 = 区外** ⇒
  dyld 必须映射那些页 ⇒ 必进内核守卫区 ⇒ 致命 `EXC_GUARD(DEALLOC_GAP)`（`code1=0x2ac75c000`）。
- 六次头字段补丁（含新 inode）+ 重启验证**全部无效**；判别实验显示"区外映射不止一个"。
⇒ 只能把**内容**缩到 ≤4GB ⇒ 重建缓存。

## 1. 已就绪的资产（本会话完成）

| 资产 | 状态 |
|---|---|
| 宿主机 | macOS **15.6.1 / 24G90**（与设备 rootfs **同一 build**）；clang 17 / CLT SDK |
| 官方构建器 | `/usr/bin/update_dyld_shared_cache`（84288B，`dyld-1286.10` = 设备 dyld 同版本） |
| 缓存源 | `analysis/dyld-cache-15.6.1/dyld_shared_cache_arm64e{,\.01}` —— 与设备**逐字节同源**（SHA256 双一致） |
| 提取器源码 | `analysis/dyld-dyld-1286.10/other-tools/dsc_extractor.cpp`（+ `dsc_iterator.cpp/.h`） |
| 依赖闭包工具 | `misc/dsc_cache_subset.py`（遍历 `LC_LOAD_DYLIB` 求闭包，fat-aware） |
| 设备侧 | 已回到**原版**状态（5 个缓存 inode/字段复原、dyld `9956…51a1`）；devfs/ptmx ✓；TC 复原脚本 ✓ |

## 2. 提取器：已定位全部坑，还差"补对象文件"这一步

失败历史（每一步都实测过）：

1. **不要用 Python 调 `/usr/lib/dsc_extractor.bundle`**：会崩（宿主 crash report
   `Python-2026-09-30-235239.ips`，`dsc_extractor.bundle!make_dirs` 空指针）。
2. **必须自己编译**（源码与 24G90 同版本）——编译要点（全部实测）：
   - `-std=c++20`（dyld 用 `consteval`/`std::span`/`<=>`）；
   - **`-fblocks` 必需**（源码大量 block 字面量：`dsc_extractor.cpp:365/523/596/664/1051/1080`；
     漏了它会**编译出空块 → 调用即跳 0x0**，实测崩溃点 `PC=0x0`，lldb 与自装 SIGSEGV 处理器均可见）；
   - 需要把树内**所有含头文件的目录**加进 `-I`（zsh 注意：**必须用数组** `${incs[@]}`，未加引号的
     字符串展开在 zsh 里**不会**分词）；
   - 缺两个**私有头**，需桩：`CommonCrypto/CommonDigestSPI.h`（`CCDigest` 用公开 `CC_SHA1/SHA256` 实现，
     语义正确）与 `CrashReporterClient.h`（弱符号 no-op）；另 `_simple.h` 可空桩。桩与 shim 见 `/tmp/dscmods/`。
   - `.c` 与 `.cpp` **分别**编译再链接（避免 C 函数被 C++ 改名）。
3. **当前障碍（已更正为更准确的结论）**：**手工编译 `dsc_extractor` 在本机不可行** ——
   `common/*.cpp` 需要多个**私有 SDK 头**（`sandbox/private.h`、`System/sys/fsgetpath.h`、
   `corecrypto/ccdigest.h`、`libc_private.h` …）与**内部编译宏**（例如
   `MachOFile::canBePlacedInDyldCache`、`objc_visitor::sharedCacheSelectorStringsBaseAddress`
   只有在 Apple 内部 `-D` 下才可见），而 `_simple_salloc/_simple_vsprintf/...` 也**不由
   libsystem_platform 导出**（实测 `nm -gU` 计数为 0）。本机只有 CLT（`xcode-select -p` =
   CommandLineTools ⇒ **无 `xcodebuild`**）。
   ⇒ **正确做法是用 dyld 自己的 Xcode 工程**（`analysis/dyld-dyld-1286.10/dyld.xcodeproj` 存在）：
   `xcodebuild -project dyld.xcodeproj -target dsc_extractor`（需**完整 Xcode**）。
   已尝试的机械修补（`-std=c++20 -fblocks`、全目录 `-I`、`-include Availability.h`、
   `CommonDigestSPI/CrashReporterClient/_simple/libc_private` 桩）把编译推进到"仅缺内部宏/私有头"，
   半成品对象保存在 `/tmp/dsc_o_*.o`，桩在 `/tmp/dscmods/`。

**已保存的半成品对象**：`/tmp/dsc_o_extract.o`（extractor，`-fblocks` 版）、`/tmp/dsc_o_main.o`
（CLI + SIGSEGV 处理器 + 真 progress block）、`/tmp/dsc_o_shim.o`（CCDigest/CR 弱符号）、
`/tmp/dsc_o_*.o`（common 中编译成功的部分）。

## 3. 复现/继续的具体命令（照抄）

```bash
cd /Users/ciscohe/Desktop/macPad/analysis/dyld-dyld-1286.10
incs=(); for d in $(find . -type f \( -name '*.h' -o -name '*.hpp' \) -not -path './.git/*' \
    -exec dirname {} \; | sort -u); do incs+=("-I$d"); done
SD=$(xcrun --show-sdk-path)
# 1) 修 common/*.cpp 的编译错误（逐个看 /tmp/err_*.txt），然后：
for f in common/*.cpp; do clang++ -std=c++20 -O1 -w -fblocks -c "$f" \
    -o "/tmp/dsc_o_$(basename $f .cpp).o" -I/tmp/dscmods "${incs[@]}" -I"$SD/usr/include" || echo "FAIL $f"; done
# 2) 链接（把上一步成功的 common 对象都带上）
clang++ -o /tmp/dsc_extract_fixed /tmp/dsc_o_extract.o /tmp/dsc_o_main.o /tmp/dsc_o_shim.o /tmp/dsc_o_common*.o \
    -framework Foundation -framework CoreFoundation
# 3) 提取（主缓存 + .01 各一次）
/tmp/dsc_extract_fixed ../dyld-cache-15.6.1/dyld_shared_cache_arm64e    /tmp/dsc_extract_1561
/tmp/dsc_extract_fixed ../dyld-cache-15.6.1/dyld_shared_cache_arm64e.01 /tmp/dsc_extract_1561
# 4) 依赖闭包（种子 = CLI 集合；种子二进制可从设备拷或先用缓存内 install name）
python3 misc/dsc_cache_subset.py closure /tmp/miniroot/bin/echo /tmp/miniroot/bin/sh /tmp/miniroot/bin/bash \
    --search /tmp/dsc_extract_1561 --out /tmp/dsc_subset.txt
# 5) 组沙箱 mini-root（按上表的 install name 路径放真实文件）+ SystemVersion.plist
# 6) 生成 ≤4GB 缓存（⚠️ -root 只能是 /tmp 沙箱，绝不可指向 /）
/usr/bin/update_dyld_shared_cache -root /tmp/miniroot_1561 -arch arm64e
```

## 4. 之后的部署与验证（设备侧，仍是"可逆 + 重启验证"）

1. 把新缓存（预计几百 MB）推到设备，替换 `$CR`/`$DST` 下的 `dyld_shared_cache_arm64e{,.01}`（`mv`-only，原件留隐藏名）。
2. 新缓存的 **UUID/cdhash 全变** ⇒ 重算 cdhash（其 CS blob 在 `codeSignatureOffset`）并入 TC；
   `cachereg` 重挂；`$R/dev` + `mountdevfs`。
3. **重启**（region 重建）后跑 `misc/post_reboot_cli_test.sh` 的同一套见证（F1 dyld + `/bin/echo HI`）。
   - 若 `HI` 打出 ⇒ **CLI 里程碑达成** ⇒ 继续 r1..r3 阶梯（`sh`/`cat`/`ls`）与 `launchdchrootexec` 的
     `proc_set_debugged` 修复。
   - 若仍失败 ⇒ 用 §12.2 的判别手法（换签名值观察地址是否随动）定位新缓存里下一个区外引用。

## 5. 安全与纪律（血泪）

- ⚠️ **`update_dyld_shared_cache` 的 `-root` 绝不能指向 `/`**（会重写宿主自己的缓存）——一律 `/tmp` 沙箱。
- ⚠️ 宿主 **Python 调 dsc_extractor.bundle 会崩**（已两次）——只用 §3 自编的独立工具。
- ⚠️ 设备实验保持：`mv`-only、原件保留、每轮复核 dyld SHA/inode；重启会杀掉 10.8GB 的 Virtualization VM。
- 本文件是这条流水线的唯一事实来源；每推进一步就更新它并 push。

---

## 6. **2026-10-01 进展：提取已解决，mini-root 就绪，只差 sudo 跑 builder**

### 6.1 提取器：**不需要自己编译** —— 用上一会话已写好的 `analysis/dyldwork/extract_host2.py`
它用 `_NSConcreteGlobalBlock` + `ctypes.CFUNCTYPE` 构造了**真正的 block**（正是我定位的崩溃点：
`dsc_extractor.cpp:990` 无条件调用 progress 块，传 NULL 就跳 PC=0x0）；而仓库里那份
`misc/extract_dyld_cache.py` 传的是 NULL ✗（两次 Python 崩溃就是它）。
实测（读取宿主自己那份 24G90 缓存，等价于我们的本地副本）：

```
python3 analysis/dyldwork/extract_host2.py /tmp/dsc_ho        # -> rc=0
# 结果：3257 个文件 / 4.4GB  （= 该缓存的全部镜像）
```
（`analysis/dyld-dyld-1286.10/dyld.xcodeproj` 的 `xcodebuild` 路线**不可用**：本机只有 CLT，
`xcodebuild` 直接报 "requires Xcode" ✗。）

### 6.2 依赖闭包 + mini-root（已完成）
- 种子：从设备取 `/bin/{echo,sh,bash,cat,ls,date}`（`/tmp/seeds/`）。
- 闭包：`python3 misc/dsc_cache_subset.py closure /tmp/seeds/{echo,sh,bash,cat,ls,date} \
  --search /tmp/dsc_ho --out /tmp/dsc_subset.txt` ⇒ **564 个 dylib / 870.2 MB**
  （仅 2 个良性未解析：`MLCompilerServices`、`libobjc-env.dylib`）。
- mini-root：`/tmp/miniroot_1561/` = 564 dylib（按 install name 路径）+ 6 个种子 + `SystemVersion.plist`
  ⇒ **570 文件 / 871.8 MB**（版本 24G90 ✓，**远小于 4GB**）。

### 6.3 下一步：**用 sudo 跑 builder**（唯一需要 root 的一步）
非 root 时 `update_dyld_shared_cache` **静默不做事**（rc=0、无输出、无产物 ⇒ 已实测两次）。
请在**宿主**上执行（⚠️ `-root` 只指向 /tmp 沙箱，**绝不可** `-root /`）：

```
sudo /usr/bin/update_dyld_shared_cache -root /tmp/miniroot_1561 -arch arm64e
ls -la /tmp/miniroot_1561/System/Library/Caches/com.apple.dyld/
```
预期产物：`/tmp/miniroot_1561/System/Library/Caches/com.apple.dyld/dyld_shared_cache_arm64e{,.01,...}`
（总跨度应 ≤4GB；可能耗时数分钟）。拿到产物后即可进入 §4 的部署与验证（推设备 → 重算 cdhash 入 TC →
`cachereg` → 重启 → 跑 `misc/post_reboot_cli_test.sh` 的同一套见证）。


### 6.4 builder 第一次 sudo 运行：**静默无产物**（已定位两个原因，待复跑）

`sudo update_dyld_shared_cache -root /tmp/miniroot_1561 -arch arm64e` → 无输出、无产物
（`<root>/System/Library/Caches/com.apple.dyld/` 不存在；miniroot 内所有文件时间戳都还停留在装配时刻）。
已排除/修正两点：

1. **miniroot 缺 `/usr/lib/dyld`**（缓存构建必须把 dyld 本体放进 root）。更关键的是
   **dyld4 要求"缓存里的 dyld"与进程用的磁盘 dyld 一致**（否则整份缓存会被忽略），
   而宿主 `/usr/lib/dyld`（fat，2289328B，SHA `e371c8cb…`）**≠** 设备 rootfs 那份
   （thin，1239616B，SHA `9956915299c6e3e21c7e650242166bab05dc635da4acac2cbade4e646eec51a1`，
   被 fork 改签过）⇒ 已把**设备的 dyld 原样拷入** `/tmp/miniroot_1561/usr/lib/dyld` ✓。
2. 关于该工具的**要求**：CLI 包装器闭源（`cache-builder/update_dyld_shared_cache.cpp` 在开源树里是空壳），
   但构建引擎 `cache_builder/NewSharedCacheBuilder.cpp` 是开源的 —— 若复跑仍静默，下一步就从这里读它的
   root 前提（以及用 `log show --predicate 'process == "update_dyld_shared_cache"'` 抓它的 os_log 输出，
   **它的日志走 syslog 而不走 stdout**，这正是"看不到输出"的原因）。

**复跑命令**（宿主，⚠️ `-root` 只能指向 /tmp 沙箱）：

```
sudo /usr/bin/update_dyld_shared_cache -root /tmp/miniroot_1561 -arch arm64e 2>&1 | tail -20
ls -la /tmp/miniroot_1561/System/Library/Caches/com.apple.dyld/
# 若仍静默，抓它的 syslog：
log show --last 2m --style compact --predicate 'process == "update_dyld_shared_cache"' | tail -20
```

### 6.5 **结论：B2'' 在本机不可实现**（2026-10-01，证据确凿）

补上 miniroot 的 `/usr/lib/dyld`（设备那份，SHA 与记录一致）后**再次 sudo 复跑**：
仍然**无输出、无产物**；`log show --predicate 'process == "update_dyld_shared_cache"'` **一条日志都没有**。

⇒ 直接检查该二进制本身（`size` / `nm` / `strings`）：

```
$ size /usr/bin/update_dyld_shared_cache
__TEXT 16384   __DATA 0   __OBJC 0        ← 16KB 的桩
$ nm -gU /usr/bin/update_dyld_shared_cache
0000000100000000 T __mh_execute_header   ← 除入口外没有任何函数
$ strings -a /usr/bin/update_dyld_shared_cache | wc -l
4                                        ← 只有版本 banner
```

**`/usr/bin/update_dyld_shared_cache` 在 macOS 15 上就是一个空壳**
（与开源树里那份 `cache-builder/update_dyld_shared_cache.cpp` 的
`int main(...) { return 0; }` **完全对应**）。真正的缓存构建器
（`dyld_shared_cache_builder` / `dyld_shared_cache_util`）**随 Apple 内部构建系统发布，不在系统里**
（本机又只有 CLT、无 Xcode；`xcodebuild` 直接 `requires Xcode` ✗）。

**⇒ 因此"在 15.6.1 版本内重建 ≤4GB 缓存"这条路在本机不可实现**：
- 自建 builder = 重写整个 cache 构建器（fixup/PBL/trie/subcache），不可行；
- 官方 builder 本机不存在；
- 手工编译 `dsc_extractor` 亦因私有 SDK 头/内部宏而失败（§6.1 记录）。

**剩下的可行路线 = C（换 13.4 Ventura rootfs）**：13.4 的缓存约 2.5GB，天然 <4GB，
且 `pointer_format=12` 本内核支持（format-13 墙不存在）；作者已在该版本上跑通
（`prepare_ventura_windowserver.py`、`22F82` 的缓存 CDHash 都在仓库里）。
**需要用户提供一份 13.4 rootfs（DMG 或安装器）** —— 设备与宿主上目前**都没有**
（已搜：设备 `/var/mnt`、`/var/mnt/r2`、staging 目录；宿主 `~/Downloads`、`/Users/ciscohe/*.dmg`）。

---

## 7. 【更正 §6.5】不是"没有 builder"，而是"builder 缺 `ld/`"；本机其实有 Xcode 26.3

> 日期：2026-10-01（同日第二轮；触发：用户问"网上应该有开源工具？"并指出 `analysis/` 下就有
> `MacWSBootingGuide`）。级别：`RE-confirmed`（源码清单 + xcodebuild 实测日志）。

### 7.1 §6.5 里被推翻的两条

1. **"本机只有 CLT、无 Xcode" → 错。** 实测
   `DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer xcodebuild -version`
   → **Xcode 26.3 (17C529)**。`xcode-select -p` 报 `requires Xcode` 只是因为它指向
   `/Library/Developer/CommandLineTools`；**用 `DEVELOPER_DIR` 即可绕过，无需 sudo**。
   另：本机 macOS = **15.6.1 (24G90)**，与设备 chroot 的 rootfs **完全同版本**。
2. **"官方 builder 不在系统里 ⇒ 没有可用的官方 builder" → 只对了一半。**
   `/usr/bin/update_dyld_shared_cache`（84288 B，fat x86_64+arm64）**确实是桩**：
   `nm -gU` 仅 `__mh_execute_header`、`strings` 仅版本 banner + `fffff`，与开源树
   `cache-builder/update_dyld_shared_cache.cpp`（全文 `int main(){return 0;}`）对应。
   **但那只是 1 KB 的 CLI 包装 —— builder 本体是完整开源的**（见 7.2）。

### 7.2 builder 源码清单（`RE-confirmed`，本地 `analysis/dyld-dyld-1286.10/`）

| 目录 | 关键文件（字节） |
|---|---|
| `cache-builder/` | `dyld_shared_cache_builder.mm`(58081)、`AppCacheBuilder.cpp`(286312)、`CacheBuilder.cpp`、`OptimizerBranches.cpp`(60220)、`OptimizerLinkedit.cpp`(46806)、`AdjustDylibSegments.cpp`(92904)、`kernel_collection_builder.cpp`、`update_dyld_sim_shared_cache.cpp` |
| `cache_builder/` | `NewSharedCacheBuilder.cpp/h`、`SubCache.cpp`(2458 行)、`Chunk.cpp`、`CacheDylib.cpp`、`Optimizers.cpp`、`IMPCaches.cpp`、`SectionCoalescer.cpp`、`ASLRTracker.cpp`、`BuilderOptions.cpp` |
| `shared_cache_linker/` | `SharedCacheLinker.cpp`(21740)、`.h`、`_private.h` |
| `mach_o/`、`mach_o_writer/` | 完整 |
| 编译脚本 | `build-scripts/update_dyld_shared_cache-build.sh`（内部即 `xcodebuild -target dyld_shared_cache_builder …`） |

`xcodebuild -project dyld.xcodeproj -list` 中**确有** `dyld_shared_cache_builder` 目标。

### 7.3 实测：编译推进到哪一步

```
# 1) xcodebuild -target dyld_shared_cache_builder -sdk macosx RC_ARCHS=arm64e
  ✗ lsl/Allocator.h:44:10: fatal error: '_simple.h' file not found
  #  修复：tmp/dscstub/_simple.h（补全 _simple_getenv/_simple_dprintf/_simple_vdprintf 原型）
  #        + CPATH=<该目录>；三符号均由 libSystem 导出（已在 MacOSX.sdk 的 libSystem.tbd 确认）

# 2)
  ✓ libmach_o_writer / libmach_o … 通过
  ✗ error: Build input file cannot be found: '…/ld/options/Options.cpp'
     '…/ld/passes/Inits.cpp' '…/ld/Relocations.cpp' '…/ld/PersistentAtom.cpp'
     '…/ld/options/Options_AtomInfo.cpp' '…/ld/options/Options_Output.cpp'
     (in target 'SharedCacheLinker.framework')
```

**根因**：`ld/` 目录**整体不在开源树里**（`ls -d ld` → No such file）。`ld/` 是 ld64 那套内部
链接器源码。而 `cache_builder/NewSharedCacheBuilder.cpp:54` 明确
`#include <SharedCacheLinker/SharedCacheLinker.h>`，`SharedCacheLinker.framework` 的 Sources
里就是 `ld/*.cpp` ⇒ **builder 强依赖 SLC，SLC 强依赖未开源的 `ld/`**。

### 7.4 结论（更新）

- **官方 builder 无法仅凭开源 drop 编译** —— 卡点是 `ld/`（ld64）源码缺失，**不是** `_simple.h`
  这类小件。（可选但昂贵：给 SLC 造桩 / 裁掉 closure 功能。）
- **B2''（自建 ≤4GB 缓存）仍未打通**，但**卡点已精确**，且**"本机没有工具链"的疑问已消除**。
- **第三方开源工具**（下一轮评估，用户提示的方向）：
  - `DyldExtractor`（Python，`pip install dyldextractor`）—— 抽出并**修复**成接近可加载的 dylib；
  - `nfzerox/VirtualMacOniPad` 的 **`uncache.py`** —— 专门把"有损抽取物"补成可加载
    （重建 chained fixups / GOT+auth-GOT / ObjC sel·protocol·相对方法列表 / 跨段 PC-relative /
    平台标记改写）；**其致谢名单里明确列有 `MacWSBootingGuide`（本项目）**；
  - `dsce`、`iOS-run-macOS-executables-tools`、`macmade/dyld_cache_extract`、
    `keith/dyld-shared-cache-extractor`。
- 若这些工具能把 dylib 变成"可直接 dlopen"的形态，可考虑**不走共享缓存**
  （`cacheMode="avoid"`）——但该模式受 AMFI 门禁（§T4/T5 已证被拦），需先解门禁或改为 dyld 侧补丁。

### 7.5 附带：dyld 源码里与"chroot / 老内核"直接相关的两处开关（`RE-confirmed`）

1. `dyld/DyldProcessConfig.cpp:235-239` —— Apple 亲笔注释：
   ```cpp
   // hack to allow macOS 13 dyld to run chrooted on older kernels
   if ( (this->dyldCache.addr == nullptr) ||
        (this->dyldCache.addr->header.mappingOffset <= offsetof(dyld_cache_header, cacheSubType)) )
       this->process.pageInLinkingMode = 0;
   ```
   我们的缓存是**新格式**，条件为假 ⇒ 该 hack **不生效**（这正是 goal 里"逼 dyld 走进程内 fixup"的现成钩子）。
2. `pageInLinkingMode`（0/1/2/3）与 `DYLD_PAGEIN_LINKING`：
   - `Loader.cpp:2043`：`canUsePageInLinkingSyscall = (mode>=2) && !libSystemInitialized() && !sandboxBlockedPageInLinking()`
   - `DyldProcessConfig.cpp:1339`：`opts.usePageInLinking = (mode>=2) && !sandboxBlockedPageInLinking()`
   - **但** `SharedCacheRuntime.cpp:984` 的 `mmap(…, MAP_FIXED|MAP_PRIVATE, …)` 是**无条件**执行的
     （与 `usePageInLinking` 无关），页内 fixup 才由它分流 ⇒ **单改 mode 很可能不解决 m3 写错误**
     （THEORY，待设备验证）。
   - `SharedCacheRuntime.cpp:917`：`uint8_t* buffer = (uint8_t*)SHARED_REGION_BASE;`（**编译期常量**
     0x180000000）⇒ 缓存落址**无法**靠改 header 的 `sharedRegionStart` 平移。
3. `SharedCacheRuntime.cpp:926 / 1455-1468` `deallocateExistingSharedCache()`：
   ```cpp
   uint64_t existingCacheAddress = 0;
   if ( __shared_region_check_np(&existingCacheAddress) == 0 ) {
       (void)__shared_region_check_np(NULL);   // <rdar://problem/73957993> remove the shared region sub-map
   }
   ```
   在 mmap 之前**先移除内核既有的共享区子映射**。若这一步在 iOS 上失败/部分生效，
   后续 `MAP_FIXED` 就会撞上残留映射（待设备侧验证）。

---

## 8. 第三方工具实测：`DyldExtractor` **不支持** macOS 15 缓存（2026-10-01）

### 8.1 环境（宿主 = macOS 15.6.1）

- 宿主 Python 3.14.7（homebrew）；**PEP 668** 拦截 → 用 venv `tmp/dscvenv`
- `pip install dyldextractor` → **2.2.2**（`arandomdev/dyldextractor`，PyPI 最新），带 `capstone 4.0.2`
- 两个已解的坑：
  - `capstone 4.0.2` 需要 `distutils`（Python ≥3.12 已移除）→ `pip install setuptools`
  - 新版 setuptools 又移除 `pkg_resources` → 固定 `setuptools<81`
- CLI 是 **`dyldex` / `dyldex_all`**（不是 `python -m dyldextractor`）

### 8.2 它能解析 macOS 15.6.1 缓存（能读 images 列表）

```
$ tmp/dscvenv/bin/dyldex -l -b -f libSystem analysis/dyld-cache-15.6.1/dyld_shared_cache_arm64e
libsystem_platform.dylib … libSystem.B.dylib …（0.29s 完成）
```

### 8.3 但**抽取必失败**：它只支持 slide info **v2/v3**，而本缓存**全是 v5**

```
>>> sorted(DyldExtractor.converter.slide_info._SlideInfoMap.keys())
[2, 3]
```
```
$ tmp/dscvenv/bin/dyldex -e /usr/lib/libz.1.dylib -o tmp/dscextract analysis/dyld-cache-15.6.1/dyld_shared_cache_arm64e
File ".../DyldExtractor/converter/slide_info.py", line 460, in processSlideInfo
  mappingInfo = _getMappingInfo(extractionCtx)
File ".../DyldExtractor/converter/slide_info.py", line 313, in _getMappingInfo
  logger.error("Unknown slide info version: " + slideInfoVer)
TypeError: can only concatenate str (not "int") to str
（根因：line 312 `if slideInfoVer not in _SlideInfoMap` → 313 行把 int 当 str 拼）
```

同一次排查顺带用 `struct` 直读 `mappingWithSlide`（结构体为
`<QQQQQQII>` = 56 字节，`include/mach-o/dyld_cache_format.h:141`），**独立复现**了主文档 §1 的映射表：

```
main (映射 8 条, mappingWithSlideOffset=0x3e8)
 m0 0x180000000 +0x067f5c000 -> 0x1e7f5c000   (无 slide)
 m1 0x1e7f5c000 +0x001e90000 -> 0x1e9dec000   slideInfoVer=5 pageSize=0x4000
 m2 0x1ebdec000 +0x00239c000 -> 0x1ee188000   ver=5
 m3 0x1ee188000 +0x000024000 -> 0x1ee1ac000   ver=5   ← 历史 m3 受害者
 m4 0x1ee1ac000 +0x01200000  -> 0x1ef3ac000   ver=5
 m5 0x1ef3ac000 +0x07cc4000  -> 0x1f7070000   ver=5
 m6 0x1f9070000 +0x05cdc000  -> 0x1fed4c000   (无 slide)
 m7 0x1fed4c000 +0x268c0000  -> 0x22560c000   (无 slide)
.01 (映射 7 条)
 m0 0x22560c000 +0x54808000  -> 0x279e14000   (无 slide)
 m1 0x279e14000 +0x021c4000  -> 0x27bfd8000   ver=5
 m2 0x27dfd8000 +0x038b4000  -> 0x28188c000   ver=5   ← 跨 0x280000000 边界
 m3 0x28188c000 +0x00d90000  -> 0x28261c000   ver=5
 m4 0x28261c000 +0x045d4000  -> 0x286bf0000   ver=5
 m5 0x288bf0000 +0x001dc000  -> 0x288dcc000   (无 slide)
 m6 0x288dcc000 +0x23990000  -> 0x2ac75c000   (无 slide)  ← 异常地址
```

### 8.4 结论

- `dyldextractor 2.2.2`（PyPI 最新）**不能用于 macOS 15.6.1**；要它工作需自行实现
  slide-info v5 的 rebaser（自研工作量，等同重写其 `converter/slide_info.py`）。
- ⇒ "用 DyldExtractor 把缓存 dylib 变成可加载 → 走 no-cache 路线" **在本版本上不成立**。
- 下一步候选：`nfzerox/VirtualMacOniPad` 的 `uncache.py`（面向新系统，可能支持 v5）；
  `dsce`、`iOS-run-macOS-executables-tools`。

---

## 9. `uncache.py`（VirtualMacOniPad）—— 找到了"缺失的那一步"，但它同样只支持 slide info **v3**

### 9.1 项目定位与兼容性

- 仓库：`nfzerox/VirtualMacOniPad`（README 用 `curl` 取得；**WebFetch 被网关 403，curl 可用**）
- **要求 iPadOS 14–16.3.1**（我们的 iPad13,6 / iOS 16.3 在范围内）—— 但它是**虚拟机路线**
  （Hypervisor + UTM 式），跑的是自己的 macOS 内核，**与我们的 chroot 路线不同**。
- 致谢名单含 `MacWSBootingGuide`、`DyldExtractor`、`dsce`、`iOS-run-macOS-executables-tools`。

### 9.2 三个关键文件

| 文件 | 大小 | 作用 |
|---|---|---|
| `VirtualMac/vz/uncache.py` | 84544 | **把 DyldExtractor 的"仅供 RE"产物变成可加载的 arm64e `LC_DYLD_CHAINED_FIXUPS`** |
| `VirtualMac/patches/dyldextractor-2.2.2-arm64e.patch` | 4623 | 修现代缓存 ObjC 相对方法选择器基址（`__objc_opt_ro` 取代已消失的 `__objc_scoffs`） |
| `VirtualMac/vz/ipsw_patches/dyld_a2sb.go` | 1692 | 给 `ipsw` 加批量 `dyld a2sb`（批量 地址→符号） |

`uncache.py` 头注释（原文摘录）：
```
uncache: regenerate loadable arm64e LC_DYLD_CHAINED_FIXUPS for a cache image.
Takes DyldExtractor's (RE-only) output and makes it loadable:
  - collect the image's own slide-info-v3 fixups (location-filtered)
  - classify rebase (in-image) vs bind (cross-image); resolve binds via `ipsw dyld a2s`
  - emit DYLD_CHAINED_PTR_ARM64E_USERLAND chains (auth-preserving), weave into __DATA*
  - add LC_DYLD_CHAINED_FIXUPS, clear MH_DYLIB_IN_CACHE
Validate with `dyld_info -fixups`, then stamp iOS + sign.
Usage: uncache.py <main-cache> <image-substr> <dyldextractor-output> <final-output>
```

### 9.3 卡点：同样是 **slide info 版本**

`uncache.py:1305-1306`：
```python
for info in slide_info._getMappingInfo(ectx):
    if info.slideInfo.version == 3:
```
**只处理 v3**；我们的缓存是 **v5**（见 §8.3）。

### 9.4 实测：dyldextractor 能抽出 Mach-O，但那是"仅供 RE"形态

先修它自带的日志 bug（`slide_info.py:313` 把 int 当 str 拼 → `str(slideInfoVer)`），
之后 5 条 `Unknown slide info version: 5` 只记录、不再崩：

```
$ tmp/dscvenv/bin/dyldex -e /usr/lib/libz.1.dylib -o tmp/dscextract/libz.1.dylib \
      analysis/dyld-cache-15.6.1/dyld_shared_cache_arm64e
$ file tmp/dscextract/libz.1.dylib
Mach-O 64-bit dynamically linked shared library arm64e        (102734 bytes)
```

但**不可加载**（符合预期：dyldextractor 产物是给 IDA 看的，不是给 dyld 加载的）：
```
$ install_name_tool -id /tmp/libz_test.dylib /tmp/libz_test.dylib
fatal error: file not in an order that can be processed (function starts data out of place)
$ python3 -c "import ctypes; ctypes.CDLL('/tmp/libz_test.dylib')"
OSError: dlopen(...): mmap(addr=0x359F879E0, size=0xB8) failed with errno=22
```
段地址仍是**缓存里的原值**（`__TEXT vmaddr=0x18e619000`、`__DATA_CONST 0x1e85a09e0`、
`__AUTH_CONST 0x1f057cf30`、`__LINKEDIT 0x1fed4c000`）⇒ 无 `LC_DYLD_CHAINED_FIXUPS`、
无重定位、vmaddr 未归零 —— 正是 `uncache.py` 要补的那一段。

### 9.5 结论

- **工具找对了，但整条第三方工具链（dyldextractor 2.2.2 + uncache.py）只覆盖 slide info v2/v3**，
  而 macOS 15.6.1 用 v5 ⇒ 对 15.6.1 **全线不可用**。
- v5 并非"改个常量"：`dyld_cache_slide_pointer5` 用的是现代
  `dyld_chained_ptr_arm64e_shared_cache_{rebase,auth_rebase}` 编码，与 v3 的
  `dyld_cache_slide_pointer3`（51 位 pointerValue + 11 位链偏移 + 2 位 unused）**完全不同**。
  两版 `dyld_cache_slide_info` 头部相同（`version/page_size/page_starts_count/+u64/page_starts[]`，
  见 `include/mach-o/dyld_cache_format.h:374,533`）。
  ⇒ 要么自己补 v5 支持（dyldextractor 的 rebaser + uncache.py 两处），要么换版本。
- **路线 C（13.4）的相对优势进一步明确**：作者在该版本上跑通，且第三方工具链正好工作在那代格式上。
