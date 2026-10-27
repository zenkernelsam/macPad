# R011 只读取证：WS 主线程停滞 sample 的符号化与 pacing 判定

日期：2026-10-18。范围：使用后端复制的
`analysis/r007-ws-sample/ws-spindump-20261018.txt` 与
`loaded-images.json`，严格匹配 WS UUID `465422C7-3CFC-3E9F-9C04-813F3A265AA6`、
cache UUID `BE59BB71-7B69-3EFC-86A8-5625A077AE2E` slide `0x1ba40000`、
以及安装 libmachook hashes，判断主线程是否实际进入
`macws_coexist_wait_for_completion_slot` 的 poll/usleep、锁竞争、
SkyLight/TCC/Mach 或 unknown。**未改源码/测试/构建输入、未构建、未操作
设备、未执行 Git 写入。** 只新增本文件与
`coordination/_relay/round-011-frontend-result.md`。

候选 HEAD：`7e5d771`。

## 1. 输入身份

| 项 | 值 |
| --- | --- |
| spindump | `analysis/r007-ws-sample/ws-spindump-20261018.txt`，3,911,680 B，SHA `de7fe9fb50e1018b0aa3e91eab98f4ee07ff9b862e3ee6cc99332f35129a3d05` |
| sample window | 2026-10-18 16:42:01.780–16:42:04.779，150 samples ×20 ms |
| WS | PID 50574，UUID `465422C7-3CFC-3E9F-9C04-813F3A265AA6`，arm64 |
| cache | `BE59BB71-7B69-3EFC-86A8-5625A077AE2E` slid base `0x19ba40000`, slide `0x1ba40000` |
| loaded images | `analysis/r007-ws-sample/loaded-images.json` SHA `dcff278eac75241f2732a90b4517f023d39481768a956b39a71e175da10794b2` |
| SkyLight | `analysis/SkyLight_22F82` UUID `96676A53-…`, SHA `13d7a0ca010a9808234c71c34fd650b26329de99d1aceaeb5ddbb58f8b906c61` |
| kernelcache (local, T8103 iOS 16.3) | `analysis/kc_raw_16.3_T8112.bin` SHA `6673ce2889604cbfb7a9dc938000be0c48486874c9aebae0cdd4870c514f2b38` |
| installed libmachook | arm64e `9a511953…572819`, arm64 `151a2175…32bced` (R003/R008 deployed) |
| local Theos build | arm64e `2df74403…562a`, arm64 `80b5ae2d…1152` — **不同**，不作符号替代 |

## 2. 栈方向与 raw frames

该 spindump 是 **root-first**（顶部为线程入口，底部为当前 leaf）：
- CA 线程 `com.apple.coreanimation.display.primary` 的底部 leaf 明确写
  `blocked by turnstile waiting for WindowServer [50574] thread 0x978af`
  （142/150）；这是线程等待的 leaf，不是顶部。
- AccessibilityUIServer 主线程顶部是 `dyld + 104800`（入口），不是 leaf。

主线程 `Thread 0x978af ws_main_thread` 150/150 samples；关键 raw frames
（地址为 runtime，括号内为 spindump 的 image+offset）：

```text
0x19bae7f28  <983665BA…> + 2401042216          # root-side userland entry
0x1028433bc  WindowServer + 13244
0x1a0e62504  <983665BA…> + 2488575236          # SkyLight _SLXServer+0x534
0x1a0e626c4  <983665BA…> + 2488575684          # SkyLight _server_loop+0x8c
0x1a0e61bb8  <983665BA…> + 2488572856          # CGXRunOneServicesPass+0x57c
0x1a0e61f60  <983665BA…> + 2488573792          # post_port_data+0x118
0x1a0e23258  <983665BA…> + 2488316504          # non_coalesced_timer_handler+0x18
0x1a0e2314c  <983665BA…> + 2488316236          # run_timer_pass+0x1f8
0x102c5d098  <CC9C81E9…> + 233624              # injected dylib, unresolved
0x1a0daf264  <983665BA…> + 2487841380          # update_display_callback+0x558
0x102c5cfd0  <CC9C81E9…> + 233424              # injected dylib, unresolved
0x1a0dbcbe8  <983665BA…> + 2487897064          # WS::Updater::UpdateDisplays+0x33a0
0x1a0c9e210  <983665BA…> + 2486723088          # SLCADisplay::present_update+0x374
0x1a0c9d000  <983665BA…> + 2486718464          # SLCADisplay::finish_external_update+0x98
0x1a6a66434  <983665BA…> + 2585060404          # unknown cache caller
0x102c486b8  <CC9C81E9…> + 149176              # injected dylib, unresolved
0x19be08834  <983665BA…> + 2404321332          # libsystem_kernel +0x9834 (see §4)
0xfffffe0007f92f1c  <BFF3D243…> + 503580       # kernel leaf, 146 samples
```

其他分支：
- 1 sample：`CC9C81E9 +0x245b4` / `+0x112a0` 路径，底部进入
  `IOWorkLoop::sleepGate+0x34` `0xfffffe00085a3420`，
  `IOCommandGate::runAction+0xcc` `0xfffffe00085a7714`，
  `lck_mtx_sleep+0xb8` `0xfffffe0007f7fe88`，以及 kernel leaf
  `0xfffffe0007f43388`.
- 3 samples：`CC9C81E9 +0xfde8` 路径，底部 kernel
  `0xfffffe0007f43388`.

## 3. 精确 SkyLight 映射（FACT）

cache slide 0x1ba40000 后，以下 unslid 地址与本地精确 SkyLight
`96676A53…` 的 local symbols 对齐（`nm -a analysis/SkyLight_22F82`）：

| runtime | unslid | 符号 |
| --- | --- | --- |
| `0x1a0e62504` | `0x185422504` | `_SLXServer+0x534` |
| `0x1a0e626c4` | `0x1854226c4` | `_server_loop+0x8c` |
| `0x1a0e61bb8` | `0x185421bb8` | `_CGXRunOneServicesPass+0x57c` |
| `0x1a0e61f60` | `0x185421f60` | `_post_port_data+0x118` |
| `0x1a0e23258` | `0x1853e3258` | `_non_coalesced_timer_handler+0x18` |
| `0x1a0e2314c` | `0x1853e314c` | `_run_timer_pass+0x1f8` |
| `0x1a0daf264` | `0x18536f264` | `update_display_callback+0x558` |
| `0x1a0dbcbe8` | `0x18537cbe8` | `WS::Updater::UpdateDisplays+0x33a0` |
| `0x1a0c9e210` | `0x18525e210` | `SLCADisplay::present_update+0x374` |
| `0x1a0c9d000` | `0x18525d000` | `SLCADisplay::finish_external_update+0x98` |

这些是 **display update/timer 路径**，不是 IOMFB begin/SwapEnd/
IOConnect pacing 调用点。

## 4. 未解析/限制

- `983665BA-B173-392C-9874-65213940AEF1`：spindump 路径为空；
  `loaded-images.json` 的 base `0x10c918000` 与 frame 的巨大 offsets
  （0x8f1cff28 等）不一致，不能作为有效 image 映射。旧 lldb pane 把它
  标为 `/Resources/CursorAsset` 属于另一 generation，不能套用。它是
  root-side/中间 cache 帧的 **unknown** UUID 标签。
- `CC9C81E9-9998-336F-A191-C68162587D54`：路径为空；地址
  `0x102c24000`，offsets 0x39098/0x38fd0/0x246b8/0x245b4/0xfde8/0x112a0
  均在一个 injected dylib 内。安装 libmachook hashes 已知，但本地没有
  匹配的 UUID/bytes；本地 Theos build 不能替代，所以不能把该 frame 证明
  为安装 libmachook，也不能把 offset 映射到具体函数。
- `0x19be08834` unslid `0x1803c8834` 位于 22F82 libsystem_kernel 的
  `__TEXT`（slice base `0x1803bf000`）+0x9834；但本地 slice 只有 32KB，
  +0x9834 超出 slice，无法在此反汇编该 wrapper。完整 dependency image
  在设备上；因此 `poll`/`usleep`/`__psynch_mutexwait`/`mach_msg` 的确切
  wrapper 未证明。
- kernelcache 本地为 T8103 iOS 16.3（6673ce28…），设备 kernel
  8792.82.2；1-sample 的 `IOWorkLoop::sleepGate`/`lck_mtx_sleep` 映射
  与符号相符，但 146-sample 的 leaf 最近符号
  `assert_wait_deadline_with_leeway+0x1df4` 是最近邻近似，不保证是同一
  函数。

## 5. pacing 判定（问题唯一答案）

`macws_coexist_wait_for_completion_slot`（`mac_hooks.m:27319`）的调用形状
（root-first 下从 leaf 向上）应为：

```text
SkyLight IOMFB begin / SwapEnd / IOConnect caller
  -> libmachook hook (macws_iomfbserver_begin_skylight_update /
     MacwsIOMobileFramebufferSwapEnd_new / IOConnectCallStructMethod_new)
    -> macws_coexist_wait_for_completion_slot
      -> poll / usleep (libsystem_kernel)
        -> kernel wait
```

即 pacing 路径至少应有**两个相邻的 libmachook frame**，且外层由 SkyLight
IOMFB 函数调用。观察到的近 leaf userland 形状是：

```text
SkyLight display update (finish_external_update/present_update/UpdateDisplays)
  -> cache-image/unknown frames
    -> CC9C81E9 +0x246b8 (single libmachook frame, caller is cache image 983665BA)
      -> libsystem_kernel +0x9834
        -> kernel wait / IOWorkLoop lock
```

以及另一条 `CC9C81E9 +0x39098` 由 SkyLight `run_timer_pass` 调用、
再调用 `update_display_callback` 的 display callback 路径。

**FACT/结论：**

- 没有采样的 main-thread PC 位于 `macws_coexist_wait_for_completion_slot`
  的 poll/usleep 调用形状：pacing 需要由 libmachook hook 直接调用 pacing
  函数，而观察到的 libmachook frame 的 caller 是 SkyLight timer/display
  或 cache image，不是 IOMFB begin/SwapEnd/IOConnect；只有单个
  libmachook frame 紧邻 libsystem_kernel，且其 caller 不是 libmachook
  hook。`CC9C81E9 +0x39098` 明确是 display callback 路径，不是 pacing。
- **至少 1 sample** 的 leaf 明确是 **IOKit kernel lock**
  (`IOWorkLoop::sleepGate`/`IOCommandGate::runAction`/`lck_mtx_sleep`)；
  **146 samples** 的 leaf 是 **kernel wait**（最近符号
  `assert_wait_deadline_with_leeway`，精确 subtype 受 §4 限制），不是
  userland `poll`/`usleep`。
- 因此分类为 **SkyLight display-update/timer → kernel wait/lock**，而
  **不是 pacing poll/usleep**。`com.apple.coreanimation.display.primary`
  的 142/150 turnstile waiting for main 只证明 main 是阻塞者，不证明
  pacing；未据此推断消息饥饿或前端/provider 原因。
- 不能证明 146-sample leaf 的具体 wrapper（poll vs psynch/mach/IOKit）
  或 `CC9C81E9` 具体函数；这属于 §4 的 exact limitation，不改变
  “pacing 调用形状不存在”的结论。

## 6. 责任边界保留（R009）

精确 22F82 libquarantine 已确认 policy `Quarantine`、selector `0xb6` 和
helper 结构；stub GOT/bind（`0x1dfef8f88`）与 actual 103 分支
（precheck/no-match/matched handler）仍未知，不是“无 policy”。本轮不
重复 adjacent-version 分析，不实现 setter。

## 7. 命令与退出码

| 命令 | 退出 | 结果 |
| --- | --- | ---: |
| `shasum -a 256` sample/loaded-images/SkyLight/kernelcache | 0 | 见 §1 |
| `grep` main-thread/CA/Binary Images | 0 | 150 samples；CA 142/150 turnstile |
| Python 解析 frame 地址/unslid/bisect SkyLight symbols | 0 | 见 §3 |
| Python bisect kernelcache symbols | 0 | 1-sample IOWorkLoop/lck_mtx；146 nearest assert_wait |
| `git log --oneline -1`（只读） | 0 | HEAD `7e5d771` |

未运行测试、构建、package、设备命令；未执行 commit/reset/stash/push。

## 8. 限制

- 983665BA/CC9C81E9 的精确 image 路径/UUID→artifact 映射缺失；本地无法
  证明 injected dylib 是安装 libmachook，也无法把 +0x246b8/+0x39098 映射
  到具体函数。
- 22F82 libsystem_kernel +0x9834 超出本地 32KB slice；完整 image 在设备。
- 本地 kernelcache 与设备 kernel 补丁版本相邻但不完全相同；146 leaf 的
  最近符号是近似。
- 不声称 message starvation、TCC 或 provider 原因；不推断可见输入/像素。

(handover to codex now)
