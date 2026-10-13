# AGENT-ROLE — macPad「静态侧」Agent 角色/状态/知识 移植文档

> 用途：把当前 Agent 的角色、工作方式、已有结论与状态**完整移植**到新的 Qoder CLI 对话。
> 新 Agent 读完本文件（并按其 §0 的 bootstrap 提示启动）即等价于"第二个我"，可直接续做任务。
> 维护：本文件是**活文档**——每有重大结论请就地更新并 commit+push。

---

## 0. Bootstrap（用户把这段贴进新对话即可）
```
你是 macPad 项目（/Users/ciscohe/Desktop/macPad）「静态侧」Agent 的接棒者。
第一步：完整阅读 docs/porting/AGENT-ROLE-macPad-static-side.md，并按其内容工作。
要求：中文回复；证据驱动（RE-confirmed / 实测 confirmed / THEORY 三档标注）；先读文档与源码再动手；
每次 commit 后立即 git push origin main；不擅自碰设备（设备由另一条会话按"串行"约定使用）。
```

---

## 1. 我是谁 / 分工边界
- **项目**：macPad（= MacWSBootingGuide 的延伸工程）——在 **iPad13,11（M1, iPadOS 16.3, Dopamine rootless 越狱）** 上
  用 chroot + dyld interpose 跑 **macOS 15.6.1 的 WindowServer / GUI**。当前阶段目标：**先跑通 CLI**，再攻 WindowServer。
- **我的角色**：**「静态侧」Agent** —— 负责 XNU/dyld **源码级**与 **IDA 静态** 分析、写文档、给设备侧提供结论与靶点。
- **分工纪律**：与另一条「设备侧」会话**并行研究、串行设备**。我默认**不碰设备**；若需设备验证，先与用户确认设备空闲。
- **宿主很弱**（宿主就是那台 iPad）：任何编译/构建默认限流（并行度≈ncpu 的 70%，必要时 THROTTLE=<pct>）。

## 2. 工作方式（六条习惯，务必遵守）
1. **先读文档、再读源码、最后才 IDA**。项目文档 + `analysis/` 下源码往往已给答案，别重造轮子。
2. **证据分级**：结论必须能标 `RE-confirmed via <file>+<offset>`／`runtime-confirmed via <log>`／`THEORY（+ 如何验证）`。
3. **先搜后猜**：遇到错误串/符号，先 `grep docs/porting/*.md` 与 `analysis/` 源码；项目历史上踩过的坑都有记载。
4. **里程碑即落文档**：重要进展写 `docs/porting/*.md`（我常写 `STATIC-*.md` 与 `HANDOVER-REPLY-*.md`）。
5. **Git 纪律**：只 `git add` 明确路径；commit message 英文 ASCII；**commit 后立即 `git push origin main`**；绝不 force push。
6. **命名约定**：我的交付文档用 `STATIC-<主题>.md`；交接文档用 `HANDOVER-REPLY-<日期>-<主题>.md`。

## 3. 现有知识库地图（读这些就够了）
### 3.1 必读文档（按序）
| 文件 | 内容 |
|---|---|
| `docs/porting/dyld-15.6.1-state.md` | **单一事实来源**：patch ledger、fat-offset、根因、铁律、地址表（顶部有摘要；篇幅大，先读摘要） |
| `docs/porting/TOOLS-AND-PORTING.md` | 项目工具清单（sprobe / launchdchrootexec / libmachook / lldb 脚本 / extract_dyld_cache.py…）与移植流程 |
| `docs/porting/STATIC-536-EFAULT-enumeration.md` | **536 EFAULT(14) 全路径枚举** + dyld 入参逐字段 diff（我写的） |
| `docs/porting/STATIC-libsystem-cache-vs-shim.md` | **libSystem 用缓存还是用磁盘 shim** 的两个闸门（我写的） |
| `docs/porting/HANDOVER-REPLY-2026-09-28-populate.md` | 536 主任务（EINVAL）交付：真因、最小修复、验收输出、负结果清单 |
| `docs/porting/HANDOVER-REPLY-2026-09-29-536-enum.md` | 536 枚举任务的**提醒版**交接（含判定捷径与设备纪律） |
| `docs/porting/kernel-syscall536-re-handover.md` | 内核 536 站点全表（旧，但仍有参考价值） |
| `docs/porting/HANDOVER-DYLD-536-POPULATE-2026-09-28.md` | 任务书（含实测矩阵、血泪清单） |
| 仓库根 `CLAUDE.md` / `AGENTS.md` | 项目总纲：Patch Discipline / Evidence Discipline / IDA-first 规则 |

### 3.2 源码（**本地齐全，优先读它们**）
- `analysis/xnu-xnu-8792.81.2/` —— XNU 全源码（关键：`bsd/vm/vm_unix.c`、`osfmk/vm/vm_shared_region.c`、`osfmk/vm/vm_map.c`）
- `analysis/dyld-dyld-1286.10/` —— dyld 源码（关键：`dyld/SharedCacheRuntime.cpp`、`dyld/Loader.cpp`、`dyld/ProcessConfig.h`）
- 二进制 IDB 素材：`analysis/kc_raw_16.3_T8112.bin`（内核）、`analysis/dyld_15.6.1_arm64e_thin`、`analysis/dyldwork/amfid_bin`

### 3.3 IDA Pro MCP 用法（强制）
- 三个实例：`ida-pro-mcp-Instance1/2/3`。**装载的二进制会变** ⇒ 每次先 `server_health` 确认 module/imagebase 再引用地址。
  （最近一次：I1=内核 `kc_raw_16.3_T8112`（base `0xfffffe0007004000`）、I2=`dyld_15.6.1_arm64e_thin`（base 0）、I3=`amfid_bin`）
- `py_eval` **必须带 `code` 参数**；`xrefs_to` 需 `addrs: []`；
- **`insn_query` 的 `op_any` 匹配的是操作数数值，不匹配内存位移** ✗（要找 `[X8,#0x12A]` 这类，改用全 `.text` 的 `idc.GetDisasm` 文本扫描）；
- **Hex-Rays 里常量是十进制**（例如 `+298` ⇒ 真实偏移 `0x12A`）——我因此多扫过两轮，务必换算；
- `idautils.XrefsTo` 在本版本有 bug，需用 `ida_xref.xrefblk_t().first_to(addr, ida_xref.XREF_ALL)`；
- 需要加载新二进制时：**把绝对路径告诉用户，请其加载**（不要自己启动 IDA）。

## 4. 已确立的核心结论（可直接引用，免重做）
### 4.1 536（`__shared_region_map_and_slide_2_np`）—— EINVAL 线
- **验收已通过**（runtime-confirmed）：`env -i PATH=/usr/bin:/bin DYLD_SHARED_CACHE_DIR=<cryptex dyld> chroot /var/mnt/rootfs /bin/echo HELLO`
  ⇒ `rc=0`、`notloaded=0`，且 `libSystem=<D161E41A>`（**来自缓存**，非 shim `<B90391D8>`）。
- **真因**：`/usr/lib/libSystem.B.dylib` 曾被改名 `libSystem.B.dylib.OFF` ⇒ **shim 缺失 ⇒ 536 对任何缓存都 EINVAL(22)**
  （铁律：**shim 必须存在且签名有效**；见 `dyld-15.6.1-state.md:2496`）。
- **最小修复**：恢复 shim + 重签 + 入 trustcache（`ldid -Hsha256 -S<ent>` → `misc/cdhash_slices.py` → `jbctl trustcache add`）。
- **步骤顺序铁律**：**先 FS 写（部署）后 cachereg**（否则挂上的 CS blob 失效）。
- 负结果：`dyld_sf0e.bin` 缺 `plataccept`（噪声源）；`[main]-only` 的 `filescount1+nodyn` 必须与 `mappings_count` 自洽，否则出现伪码 `5⇒22`。

### 4.2 536 —— EFAULT(14) 线（`STATIC-536-EFAULT-enumeration.md` 详版）
- **唯一映射点**：`bsd/vm/vm_unix.c:2716-2718`（switch：`KERN_INVALID_ADDRESS⇒EFAULT`；`NO_SPACE⇒ENOMEM`；`PROTECTION_FAILURE⇒EPERM`；其余⇒EINVAL）。
- **可产出 14 的站点（本输入下 3 条）**：
  ① `vm_shared_region.c:1611`（`fd==-1` 条目 `copyin(sms_file_offset,…,sms_size)` 返回 EFAULT）；
  ② `vm_map.c:2717`（`vm_map_enter` FIXED 越界：`start<min ∥ end>max ∥ start>=end`）；
  ③ `vm_shared_region.c:2615/2632`（slide_info 形态）。
  （`vm_shared_region.c:1024` 属 check_np；`vm_map.c:3605` 属 `vm_map_enter_fourk`=4K 页变体。）
- **计数结论**：`vm_map_enter` 内只有 2717 一处 INVALID_ADDRESS；`vm_map_enter_mem_object_helper` **0 处** ⇒ **file-backed 不可能产 14**。
- **边界判据是 `VA − sr_base`**（非 file_offset）；未对齐 ⇒ 12，越界 ⇒ 14。dyn 条目 `VA−sr_base=0x12c75c000 > 4GB` ⇒ **结构性 14**。
- **判定捷径**：内核会打印 `for fd==-1 copyin() failed, errno=…` ⇒ 出现=来源①，不出现却 -14=来源②。
- **最小修复（dyld 侧）**：把 dyn 条目 VA 挪进 4GB region（或去掉 dyn 条目）+ `init_prot` 修回 `VM_PROT_READ`。

### 4.3 libSystem 来源（缓存 vs 磁盘 shim）
| 闸门 | 位置 | 来源 | 可控性 |
|---|---|---|---|
| ① | `ProcessConfig+0x208`（= `DyldCache+0xA8`） | ctor `0x9420` ← `PathOverrides::dontUsePrebuiltForApp()`@0x950c ← **任一 `DYLD_*` path-override env** | ✅ 用户态可控 |
| ② | `ProcessConfig+0x12A`（= `Security+0x1A`） | ctor `0xB2BC` ← **AMFI 信息字 bit9** | ❌（本机实测=0） |
- 判定链：`getLoader@0x1f018` → `block_invoke@0x1f788`（`0x1fd78` 读闸门② → `0x1fd84 isProtectedLibSystemPath@0xcb88`，保护表 `protectedPaths@0x9c638` **含 `/usr/lib/libSystem.B.dylib`**）。
- 结论：**直接 `chroot`（仅 `DYLD_SHARED_CACHE_DIR`）⇒ 缓存赢**；**`launchdchrootexec`（带 `DYLD_INSERT_LIBRARIES`）⇒ 磁盘 shim 赢**（故"补 shim"与"走缓存"两条线各自成立）。
- `ProcessConfig` 布局：`Process+0x10 / Security+0x110 / Logging+0x130 / DyldCache+0x160 / PathOverrides+0x240`。

## 5. 设备纪律（血泪清单，违反=白跑一趟）
- **永不**把 `/usr/lib/libSystem.B.dylib` 改名/移走（`.OFF` 就是那个坑）。
- **FS 写必须在 cachereg 之前**；`cachereg` 的 **fd 必须保持打开**（`READY ok=1` 才算挂上）。
- **设备串行**：`/usr/lib/dyld`、shim、cachereg、trustcache、region 都是共享状态；同一台设备不要两条线同时做实验。
- **重启会清 trustcache** ⇒ 恢复脚本/`restore_env.sh` 重跑；半失败会**永久污染 region**（`vm.shared_region_destroy_delay` 默认 120，别乱改）。
- 判据用 `DYLD_PRINT_LIBRARIES` 看 UUID：`<D161E41A>`=缓存、`<B90391D8>`=shim。
- 设备访问：`sshpass -p cisco ssh -o StrictHostKeyChecking=no -o PreferredAuthentications=password -o PubkeyAuthentication=no -p 2222 root@192.168.64.1 '<cmd>'`
  （PATH 前缀 `/var/jb/usr/sbin:/var/jb/usr/bin:/var/jb/sbin:/var/jb/bin:`；IP 历史上有过变化，不通就扫子网的 2222）。

## 6. 当前状态快照（截至 2026-09-30）
- ✅ **536 主任务（EINVAL）**：真因定位 + 最小修复 + 验收输出齐备（见 §4.1 与其文档）。
- ✅ **536 EFAULT 枚举 + dyld diff**：完成（§4.2 与其文档）。
- ✅ **libSystem 来源两闸门**：完成（§4.3 与其文档）。
- ⏳ 未做：`ProcessConfig+0x12A`/AMFI bit9 的**语义确认**；"注入 libmachook 且用缓存 libSystem"的**单点补丁**（靶子：`0x9420` 写入 或 `0x950c` 返回值）；
  `vm_map.c` 其它函数里的 INVALID_ADDRESS 站点（与本路径无关，未逐一读）。
- 🧭 设备侧（另一条会话）：`cat`/`sh` 在 shim 生效下跑通 + `sh` 的 SIGKILL 待查；WindowServer 仍远（`.01` 4.77GB > 4GB region 的几何问题仍在）。

## 7. 回复风格（用户偏好）
- **中文**回复；技术标识/代码保持原文；简洁、先给结论再给证据；表格化罗列；不堆废话。
- 主动：里程碑即写文档 + commit + **立即 push**；发现自己的旧结论错就**明确更正**并说明原因。
- 交付报告可用 Canvas（`.canvas.tsx` 写到 `~/.qoder-cn/projects/<encoded-workspace>/canvases/`，只 import `qoder/canvas`）。
