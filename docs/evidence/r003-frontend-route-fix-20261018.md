# R003 前端：AppInput exact-window F1 修复（2026-10-18，离线）

范围：只修 `coordination/reviews/R002-frontend-route-audit-20261018.md` 的 F1，
即 `GlobalSystemSurface` 不得绕过 `ConfigureWindow`/`CloseWindow` 的
exact-window 要求。未修改 provider、`AppInputBridge.m`、package/build 输入，
未启动设备，未构建或部署。

候选代码 HEAD：`4bcf20d`（父提交 `119e182` 为后端并行 docs 提交，不属于本包）。

## 1. 文件与哈希

| 文件 | 状态 | SHA-256 |
| --- | --- | --- |
| `include/macws_app_input_route.h` | 修复前 | `d21f6b421ab5357723c1a3facd9b876b5d40fb7b122a9ca74107902ca1daeb71` |
| `include/macws_app_input_route.h` | 修复后 | `29d7e17defb694cd3346c5b1868c0784f6c9dda98f8a4cf856d841e5c164440d` |
| `misc/test_app_input_route_contract.py` | 修复前 | `91dbfdedb5cb9c9f5ce979ca97f816c08c0f13ac3603ba7ea773d8b109dce5c2` |
| `misc/test_app_input_route_contract.py` | 修复后 | `11b5d302abb903b69425c5fd9240830829ede2b3180f1fb20ca2aaf3b39ab483` |

修复前哈希与 R001 证据记录一致，确认父提交中的输入就是被审计的版本。

## 2. 修复

原顺序（F1）：`RelativePointer` → `SystemGesture` → **generic global** →
exact-window window==0。因此带 `MacWSInputFlagGlobalSystemSurface` 的
`ConfigureWindow`/`CloseWindow`（window=0）被判为 `GlobalSystemSurface`。

新顺序：

```c
if (kind == RelativePointer) { ... }        // 不变
if (kind == SystemGesture)  { ... }         // 不变
if (MacWSAppInputRouteKindRequiresExactWindow(kind)) {
    if (globalSurface) return RejectGlobalOwner;      // 不兼容 flag
    if (windowID == 0) return RejectWindowRequired;   // 缺失 ID
    return ExactWindow;
}
if (globalSurface) return GlobalSystemSurface;        // 指针/global 路由不变
return windowID ? ExactWindow : ProcessScoped;
```

- `ConfigureWindow`/`CloseWindow` 现在无论 window 是否非零，只要带 global
  flag 即拒绝（flag 与该 kind 的语义不兼容）。
- 普通指针/global 路由（Touch*/Hover/Tap/SecondaryTap/Scroll 等）未改变。
- `RelativePointer` 与 Dock-only `SystemGesture` 分支未改变。
- `AppInputBridge.m` 未改动；其下游 `MacWSWindowWithNumber(0)=nil` 的
  fail-safe 行为不再是唯一防线。

## 3. 修复前后行为（host 纯 C probe，verbatim）

修复前（R002 审计记录）：

```text
kind=15 win=0 flags=0x40 -> 2 (Global) windowOut=0
kind=16 win=0 flags=0x40 -> 2 (Global) windowOut=0
kind=15 win=0 flags=0 -> -6 (RejectWindowRequired) windowOut=0
kind=16 win=0 flags=0 -> -6 (RejectWindowRequired) windowOut=0
```

修复后：

```text
kind=15 win=0 flags=0x40 -> -7 (RejectGlobalOwner) windowOut=0
kind=16 win=0 flags=0x40 -> -7 (RejectGlobalOwner) windowOut=0
kind=15 win=0 flags=0 -> -6 (RejectWindowRequired) windowOut=0
kind=16 win=0 flags=0 -> -6 (RejectWindowRequired) windowOut=0
kind=27 win=0 flags=0x40 -> -7 (RejectGlobalOwner) windowOut=0
kind=25 win=0 flags=0x40 -> 2 (Global) windowOut=0
kind=17 win=77 flags=0 -> 1 (ExactWindow) windowOut=77
kind=24 win=3735928559 flags=0 -> 1 (ExactWindow) windowOut=3735928559
kind=7 win=0 flags=0 -> 0 (ProcessScoped) windowOut=0
kind=26 win=0 flags=0 -> 0 (ProcessScoped) windowOut=0
```

`PerformQuit` + global 仍是 `GlobalSystemSurface`：本轮任务只授权修
`ConfigureWindow`/`CloseWindow`，该分类对控制类 kind 无可观察 dispatch
变化（R002 F1 已记录），留给后续独立工单。

## 4. 新增回归

`misc/test_app_input_route_contract.py` 增加：

- `ConfigureWindow`/`CloseWindow` × window 0/73 × flag off/on 共 8 个断言：
  - flag off + window 0 → `RejectWindowRequired`
  - flag off + window 73 → `ExactWindow`（window=73）
  - flag on + window 0 → `RejectGlobalOwner`
  - flag on + window 73 → `RejectGlobalOwner`
- 有效的 `Tap` global 路由（window 0 与 window 44）保持
  `GlobalSystemSurface`，防止修复过度收紧。
- 源码顺序断言：`KindRequiresExactWindow(record->kind)` 必须出现在 generic
  `if (globalSurface) ...` 之前。

## 5. 命令与真实退出码

```text
$ python3 -m unittest -v misc.test_app_input_route_contract
Ran 3 tests in 0.616s
OK
exit 0

$ python3 -m unittest -v misc.test_cydiasubstrate_provider_contract
Ran 2 tests in 1.486s
OK
exit 0

$ cc -std=c11 -Wall -Wextra -Werror -Iinclude misc/macws_protocol_test.c -lm -o /tmp/macws_protocol_test
$ /tmp/macws_protocol_test
macws protocol validators: PASS
exit 0

$ cc -std=c11 -Wall -Wextra -Werror -Iinclude /tmp/macws_route_audit.c -o /tmp/macws_route_audit
$ /tmp/macws_route_audit
（输出见 §3）
exit 0

$ git diff --check
exit 0
```

测试与 probe 均在 `/tmp` 编译/运行，未触碰 package 构建。

## 6. 限制

- 仅主机纯 C 行为测试；没有设备、WindowServer、Dock 或 AppKit 运行。
- 未运行 package build/部署。按 R002 review，该 header 进入构建后仍需一次
  新的 package rebuild 才能作为发布产物验收；本轮任务明确禁止构建。
- F2（record-form TargetProbe 无消费者）、F3（非窗口 kind 的 taxonomy）、
  F4（ModifierSnapshot 两阶段判定）未改动。
- 未验证真实 producer 是否会发送 “exact-window kind + global flag”；本轮修复
  的是契约不变量与防御性接受边界。
- 后端并行修改的其他文件未被读取、修改或提交。

## 7. 提交

- 代码+测试：`4bcf20d`（`include/macws_app_input_route.h`、
  `misc/test_app_input_route_contract.py`）。
- 本文与该轮 result 文档为随后的显式 docs 提交。

(handover to codex now)
