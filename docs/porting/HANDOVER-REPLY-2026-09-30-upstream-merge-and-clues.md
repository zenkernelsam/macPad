# HANDOVER-REPLY 2026-09-30 — 上游合并结果 + 作者方法论里对下一轮 goal 的线索

> 请求方：用户（"把源头的新更改看看会不会有冲突，然后 merge 到我的 fork；再读原作者的 methodology 找下一轮线索"）。
> 本文件两部分：**§1 合并记录**（已 push），**§2 线索清单**（供下一轮预算分配）。

---

## 1. 上游合并记录（已完成并 push）

| 项 | 值 |
|---|---|
| 远端 | `upstream` = `https://github.com/DCMMC/macPad.git`（原作者）；`origin` = 你的 fork |
| 合并点 | merge-base `0106674`（"Harden fresh-device package installation"）→ 上游 13 个新提交 |
| 新提交 | `024c0fb` M1 iPadOS 16.4 部署、`0ed9db7` 宿主信任/游戏输入延迟、`bf269a0` iPadOS 16.2 MPS 原生 LLVM 身份、`ea35463` Office provisioning + 7DTD 节奏探针、`28be53d` fresh rootfs launch 修复、`83b263b` 7DTD 全屏/Shift、`98f3601` 7DTD runtime 复用、`fa0ad0e` 可见 drawable 输入计量、**`51ff7fa` power/memory/120Hz 修复**、`6e4ad7c`/`8e32f6d` 功耗与 pacing、`cc91e22` direct drawable receive、`bda24de` VS Code webview |
| 影响面 | 70+ 文件：`MacWSHost/*`（iOS 宿主展示链路）、`libmachook/*`、`layout/usr/macOS/bin/*`、`misc/*` 探针与测试、`docs/evidence/*`、`Makefile`、新增 `mountdevfs/`、新增头 `include/macws_{power_lifecycle,process_ancestry}.h` |
| 冲突 | **仅 1 个文件 1 处**：`libmachook/mac_hooks.m` 的 `IOConnectCallStructMethod_new` |
| 合并提交 | `a9b9e68`（已 push 到 `origin/main`） |

### 1.1 冲突的语义与解法（重要，供复核）

双方**各自修了同一个 AGX device-info 尺寸不符**（macOS 请求 0x78 / iOS 16.x UC 硬校验 0x70）：

- **我方**：调用**前**把 `*outStructCnt` 从 0x78 夹到 0x70；
- **上游**：保留原生 0x78 请求，**调用后**若返回 `kIOReturnBadArgument` 再以 0x70 重试，并把协商结果记入
  `g_macws_agx_device_info_abi`（`NATIVE_78` / `LEGACY_70`）。

⇒ **取上游版**（信息量严格更大：不丢原生尝试、且能识别真实 ABI），**保留我方正交块**：
`kern_SwapEnd` 的 swap ID 从 `inStruct+0x98` 重定位到 iOS 16.3 ABI 槽 `inStruct+0x50`。
依据（符号计数）：`inStructSwapIdOffset`/`g_macws_iomfb_abi` 在**上游 0 次**；
`g_macws_agx_device_info_abi`/`MACWS_AGX_DEVICE_INFO_ABI_*`/`deviceInfoRequestedSize` 在**我方 0 次**。

**验证**：全树无冲突标记；该函数体内花括号 13/13、圆括号 68/68 配平；两族符号均在位。
⚠️ 宿主机 `clang -fsyntax-only` **不足以定论**（`CydiaSubstrate`/`MSImageRef` 是设备专有模块、SDK 漂移），
**真正的编译校验需在设备上跑 Theos 构建**（`THEOS=/var/jb/var/mobile/theos bash misc/build_on_ios.sh`）。

---

## 2. 作者方法论中与下一轮直接相关的线索（按价值排序）

### 2.1 【最高】内存回收政策：**不要与 XNU memorystatus 对抗**
`layout/usr/macOS/bin/macos_gui.sh:365-369`（上游）：
> "Do not gate or stop the GUI on `memory_pressure -Q`. iOS deliberately uses otherwise-idle RAM for
> caches and reclaimable objects, so a free-percentage threshold is not a reliable pressure-state
> boundary. The former 58% policy produced a **runtime-confirmed false stop** during an otherwise
> healthy launch and is retired. **XNU/iOS memorystatus remains the authority for reclamation.**"

同文件 `:5057`/`:5140`："memory: guard disabled (managed by iOS/XNU memorystatus)"。

⇒ **我们 §9.6 定位的 VM-reclaim EXC_GUARD 就是这位"权威"在起作用**（`vm_reclaim.c:592-604`）。
⇒ **下一轮不要用"空闲内存百分比"当判据或门控**（作者实测会误杀）；要动的是**登记侧**（见 2.3）。

### 2.2 【高】jetsam/内存足迹的"见证"写法（作者已用过）
`libmachook/Compatibility/MacWSSteamProcess.m:1550-1557`：
> "The historical **Jetsam witness** attributed **600180 resident 16-KiB pages (9.16 GiB)** to that exact
> gpu-process role ... then reached **648584 resident 16-KiB pages before Jetsam**."

⇒ 下一轮应对我们的 child 做同样的**绝对足迹计量**（resident 16K 页数 / 映射总量），
而不是只看"free %"；并与 9.16 GiB / 648584 页这两个已记录的规模比对。

### 2.3 【高】fatal EXC_GUARD 的处置范式：**在请求侧抢跑，消除前置条件**
`docs/runtime-switches.tsv:94`（上游，默认 **on**）：
> `MACWS_CRASHPAD_IMMOVABLE_TASK_PORT_COMPAT` — "Returns the kernel-equivalent
> `MACH_SEND_INVALID_RIGHT` for Crashpad's exact nonfatal CPsx task-port telemetry request
> **before iPadOS raises fatal EXC_GUARD**; **real exceptions are unchanged**."

⇒ 作者对"iOS 会抛 fatal EXC_GUARD"的既有解法是：**在被请求的那一步返回内核等价错误码**，
让上层走正常错误路径，而不是压制守卫。对应到我们的 reclaim-over-gap：
正解应是 **让登记的"可回收区间"不含洞**（或不去登记跨洞区间），从源头消除
`vm_reclaim.c:595-603` 触发 `KERN_INVALID_VALUE` 的条件——**不是**去清 `TASK_EXC_GUARD_VM_FATAL`。
同理 `Metal_hooks.x:1181` 也有一条 "EXC_GUARD INVALID_NAME → COPY_SEND" 的处置先例。

### 2.4 【中】大 VA 保留的可观测性
`docs/runtime-switches.tsv`：`MACWS_STEAM_VA_DIAGNOSTICS` — "Records only Steam's **GiB-scale
virtual-memory reservations**" ⇒ 与我们的 ">4GB 高区几何" 同源问题，作者有现成观测思路可借。

### 2.5 【中】libSystem 来源 / root 命名空间（直接对应"搞定 libSystem"）
- `MACWS_CHROOT_HOST_ROOT`（auto）："Canonical host path captured before chroot and used to translate
  kernel file-ID paths into the process-visible root namespace"；
- `MACWS_APP_MOUNT_COMPAT`（**已退役**）："real chroot metadata and root filesystem identity now select
  the mandatory **logical-root namespace** for every process without an opt-in"；
- 上游本次改动包含 `layout/usr/macOS/bin/ensure_jb_usr_bind.sh`。

⇒ 作者已经把"chroot 内 root 命名空间"统一成不变量，并专门维护 `/usr/lib` 绑定脚本。
**下一轮应优先读 `ensure_jb_usr_bind.sh` + logical-root 相关代码**，这很可能直接解释我们的
`/usr/lib/libSystem.B.dylib` 到底解析到哪一份（我们目前连"见证"都还没校准）。

### 2.6 【背景/战略】作者的版本面与"已跑通"形态
- 新增 `layout/usr/macOS/bin/prepare_ventura_windowserver.py`、iPadOS 16.2 MPS 原生 LLVM 身份、
  M1 iPadOS 16.4 部署支持 ⇒ **作者的支持面是 macOS Ventura 13.x + iPadOS 16.2/16.4**；
- `MacWSHost/*`（catalyst drawable receiver/`MacWSFinalCompositePublisher`/power monitor）+ `macwsdisplayd`
  ⇒ 作者的**展示链路是"chroot 内原生 AGX 渲染 → 宿主 App 接收合成结果"**；
- `docs/runtime-switches.tsv`：`MACWS_AGX_NATIVE` = **on 且无需环境变量**（"Defaults on"），
  `macos_gui.sh` 亦称 "The production profile enables native AGX" ⇒ 原生 AGX 已是其**生产默认**。

⇒ 战略含义：**我们的 15.6.1 移植所撞的 format-13 缺口，是 13.x 上不存在的**（13.4 的 dyld 提交
`ARM64E_USERLAND24`=12，本内核支持）。下一轮预算应显式评估：
**继续 15.6.1（已用 F1 越过 m3，现卡内存回收）** vs **回退/并行 13.x（作者已验证的 target）**。
两条路的判据：① 13.x 是否可用作者的 `prepare_ventura_windowserver.py` 等现成设施；
② 15.6.1 是否只剩"内存回收 + 几何"两个环境类障碍（若是，继续更划算）。

---

## 3. 给下一轮 goal 的预算建议

1. **第一优先（解当前卡点）**：治"跨洞区间被登记为可回收"——先用人（2.2）的绝对足迹计量确认规模，
   再找登记源（macOS malloc/VM-reclaim 注册），把区间化整。
2. **第二优先（搞定 libSystem）**：读 `ensure_jb_usr_bind.sh` + logical-root 机制，
   建立**可靠见证**（rootfs shim UUID 实测为 `85f01a16…`/`db301ad9…`，见 STATIC 文档 §9.3），
   再判定 chroot 进程到底加载哪一份 libSystem。
3. **第三优先（路线）**：按（2.6）的判据决定 15.6.1 继续 vs 13.x 回退，避免预算压在错误的版本面上。
4. 任何 libmachook 改动落地前，**必须在设备上跑一次 Theos 构建**（本次合并的语义解只能算"结构核验通过"）。
