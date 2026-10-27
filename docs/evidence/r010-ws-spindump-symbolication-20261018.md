# R010 只读审计：WS spindump 主线程停滞符号化边界

日期：2026-10-18。范围：用已保存的 WS spindump 与精确 UUID/slide 符号化
主线程停滞，判断是否进入 coexist pacing wait。**未改源码/测试/构建输入、
未构建、未操作设备、未执行 Git 写入。** 只新增本文件与
`coordination/_relay/round-010-frontend-result.md`。

候选 HEAD：`7e5d771`。后端 R005 的外部 generation 未被触碰。

## 1. 本地输入状态（FACT）

- 后端 R005 的 spindump 路径：
  `/var/mobile/macws_r005_evidence_20261018/ws-spindump-20261018.txt`
  （3s/20ms，3.8 MB，WS 50574）。**本地没有该文件的副本**；
  对 `/tmp`、`~/Downloads`、`~/Desktop` 与仓库的 `*spindump*` 搜索
  只命中 iOS 的 `libspindump.dylib`，没有报告文本。
- 本地 `coordination/artifacts/r005-lldb-pane1.txt`（SHA
  `85b7b706…78e5`）是 **2026-10-15 00:39、WS PID 55506** 的 lldb pane，
  且每次停止原因均为 `breakpoint 1.1`（`WSAuditTokenHoldsCachedProcessRight
  + 200`）。它是**断点诱导**的旧 generation，不是 R005 的被动 spindump，
  不能用于本次主线程停滞结论。
- `coordination/artifacts/R005-icon-services-and-pacing-20261018.json`
  （SHA `6fd2b6a9…f462`）只有摘要：`main thread deep unsymbolicated chain;
  com.apple.coreanimation.display.primary blocked on WS main thread 0x978af
  for 142/150 samples`；没有原始 PC/二进制镜像/slide。
- **结论：** 本地无法把 R005 的 main-thread 链解析到函数；必须取得完整
  spindump（及加载镜像/slide）后才能判断 pacing。不能用旧 lldb pane 代替。

## 2. 需要的精确文件（最小清单）

1. **完整 spindump**：
   `/var/mobile/macws_r005_evidence_20261018/ws-spindump-20261018.txt`
   （3.8 MB，原文件，不是摘要）。它应包含 main thread 的 raw frames 与
   `Binary Images` 段（UUID、load address、slide/offset）。
2. **WS 50574 的加载镜像/slide 表**（若 spindump 没有 raw PC/Binary
   Images）：从同一 generation 的 `image list`/`vmmap` 读取，至少包含：
   - SkyLight：UUID **`96676A53-B1E0-3D7E-B98B-B73873CD1880`**
   - WindowServer 主镜像：UUID 需重新核对（旧 pane 的
     `465422C7-…` 属于 10-15 generation，不能直接套用）
   - libmachook：安装产物的 UUID/load address/slide（不是本地构建）
   - 每个 frame 对应的 image index + offset 或 raw PC。
3. **精确安装的 libmachook 产物**（或其 dSYM/符号表）：
   - arm64e SHA `9a5119534d3dd40c3353efa8cbf888392b3a020c63785377a498cc466c572819`
   - arm64  SHA `151a2175c0b9bacb571cd9798c7204e4cd17922ed875823dc014593dd332bced`
   - 本地 `.theos` 构建的 arm64e/arm64 是
     `2df74403…562a` / `80b5ae2d…1152`，与安装产物**不同**；不能用本地
     build 的偏移冒充安装产物的符号。
4. SkyLight 精确 image 已在本地：
   `analysis/SkyLight_22F82`（UUID `96676A53-…`，SHA
   `13d7a0ca…6c61`），可直接反汇编；但仍需 spindump 的 slide/offset。
5. 若需要核对 `poll`/`usleep` 的调用者之外的 libsystem_kernel 栈帧，需
   22F82 libsystem_kernel 的精确 image/UUID；当前未在本地。

## 3. 源码层的 pacing wait 与可判定栈（FACT）

`libmachook/mac_hooks.m:27319` `macws_coexist_wait_for_completion_slot`：

- `pthread_mutex_lock(&pace_lock)`（27324）；
- `macws_coexist_interaction_wake_socket()`（27325）；
  `macws_coexist_wait_while_workspace_sleeping`（27326）；
- 循环内 `poll(wake_fd, POLLIN, remaining_ms)`（27343-27354）；
- `poll` 失败/无 fd 时 `usleep`（27357/27361）；
- 末尾更新 `last_completion_ns`。

调用点：

- `macws_iomfbserver_begin_skylight_update`（19896）
- `MacwsIOMobileFramebufferSwapEnd_new`（27437）
- `IOConnectCallStructMethod_new` selector-5 fallback（27518）

**判定表（拿到 spindump 后使用）：**

| main-thread 栈特征 | 归属 | 是否进入 pacing wait |
| --- | --- | --- |
| `poll`/`usleep` 的返回地址落在安装 libmachook 的 pacing 函数 | 进入 wait 的 interval 循环 | **是** |
| `__psynch_mutexwait` 在 pacing 函数入口（`pace_lock`） | 竞争 pace mutex | 是“到达函数”，但不是 interval sleep |
| `mach_msg2_trap`/`__psynch_mutexwait` 落在 SkyLight `WSAuditTokenHoldsCachedProcessRight`、TCC 或 LaunchServices | 另一类阻塞 | **否** |
| libmachook 帧未符号化 | 未知 | **无法判定**；需精确产物/符号 |

后端摘要的 “no macws_coexist_wait symbol observed” 不能证明没进入 pacing：
摘要同时说 main thread 是 **unsymbolicated**；符号缺失可能只是 libmachook
没有符号表，而不是函数不在栈上。CA thread 142/150 等待 main 也只能说明
main 长时间不返回，不能区分 poll/usleep、mutex、XPC/Mach 等等待。

## 4. 符号化配方（拿到文件后执行）

1. 从 spindump 的 `Binary Images` 取每个 image 的 UUID、load address 与
   slide；校验 SkyLight UUID 必须是 `96676A53-…`，libmachook UUID/哈希
   必须匹配安装产物（`9a511953…`/`151a2175…`），否则**fail closed**，
   不要用本地构建或旧 generation 的 UUID/偏移。
2. 对 SkyLight 帧：`unslid = runtime_pc - slide`；用
   `analysis/SkyLight_22F82` 的 `nm -a`、function starts 和
   `llvm-objdump` 在该 unslid 地址反汇编；记录 exact offset 与原始字节。
3. 对 libmachook 帧：同样 `unslid = runtime_pc - slide`；优先用安装产物
   的符号/dSYM。若产物 stripped，按精确产物字节反汇编并用调用点特征
   （`poll`/`usleep`/`pthread_mutex_lock`、`macws_coexist_*` 字符串、
   三个调用者的调用形状）定位；不要用本地 build 的 `macws_coexist_wait…`
   偏移。
4. 对每个 main-thread frame 输出：image UUID、unslid offset、函数名/原始
   反汇编、调用者。只有落在 §3 判定表第一行才记为“进入 pacing wait”。
5. 如果 spindump 只给 `image + offset` 而没有 raw PC/slide，直接用该
   offset 对精确 image 反汇编；仍要校验 UUID。

## 5. 条件性 enqueue→wait→dispatch→response 实验

**本轮不准备执行该实验**：没有 spindump，无法确认 sample 支持它。只有在
符号化后 main thread 明确位于 pacing 的 `poll`/`usleep`（判定表第一行）时，
才按以下最小设计执行（后续 packet）：

- 一个 owned fresh generation；一个有限、可归因的请求；
- 在 `macws_coexist_wait_for_completion_slot` 的调用点做只读时间戳/
  计数器：请求 enqueue 时间、wait 开始/结束、dispatch、response；
- 关联 frame sequence、WS idle CPU、wait interval；
- 不新增 receiver/fence/forced completion，不因 timeout 延长或重启；
- 若 main thread 实际在 SkyLight/TCC/其他锁，先查该阻塞，不做 pacing 适配。

## 6. 责任边界保留（R009）

精确 22F82 libquarantine 已确认 policy `Quarantine`、selector `0xb6` 和
helper 结构；但 stub 的 GOT/bind（`0x1dfef8f88`）在稀疏提取中缺失，
actual 103 分支（precheck / no-match / matched handler）仍未归属；
不能写成“无 policy”。本轮不重复 adjacent-version 分析，不实现 setter。

## 7. 命令与退出码

| 命令 | 退出 | 结果 |
| --- | --- | ---: |
| `find /tmp ~/Downloads ~/Desktop macPad -iname '*spindump*'` | 0 | 仅 `libspindump.dylib`；无报告 |
| `grep -l coreanimation.display.primary coordination/artifacts/*.json/*.txt` | 0 | 仅 R005 摘要 |
| `grep -n macws_coexist_wait_for_completion_slot libmachook/mac_hooks.m` | 0 | 定义 27319，调用 19896/27437/27518 |
| `shasum -a 256` 本地证据/镜像 | 0 | 见 §1/§2 |
| `git log --oneline -1`（只读） | 0 | HEAD `7e5d771` |

未运行测试、构建、package、设备命令；未执行 commit/reset/stash/push。

## 8. 限制

- 没有本地 spindump，无法符号化或判定 pacing；结论是“无法判定”，不是
  “未进入 pacing”。
- 旧 lldb pane（10-15，PID 55506）是断点诱导 sample，不能作为被动停滞或
  本次 generation 的证据。
- 安装 libmachook 的字节/符号不在本地；本地构建偏移不可替代。
- 不声称 message starvation、前端/provider 原因或 TCC 原因。

(handover to codex now)
