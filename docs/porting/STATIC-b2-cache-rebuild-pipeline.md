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
