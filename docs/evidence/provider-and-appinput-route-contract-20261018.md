# Provider 能力修复与 AppInput exact PID/window 契约（2026-10-18，离线）

范围：前端/provider 离线边界。**没有启动 iPad、没有部署、没有运行 WindowServer、没有操作任何设备侧文件。** 本轮明确不复制分享包的恒真 TCC hook、assert 绕过、24G90 pager/sidecar 或 M2/G14 ABI；上述源码均未改动。

## 1. 源码身份

| 文件 | 状态 | SHA-256 |
| --- | --- | --- |
| `misc/cydiasubstrate_macos.m`（修复后） | 修改 | `6ac796c78f11fce18806840a20276308c782dbc77603c7781fc55d35eeaa7c2b` |
| `misc/cydiasubstrate_macos.m`（HEAD `6e70353`，修复前） | 基线 | `7b58e1d177349b98a4891e2230a48d3152505e94999e44a1191c6997009fffad` |
| `include/macws_app_input_route.h` | 新增 | `d21f6b421ab5357723c1a3facd9b876b5d40fb7b122a9ca74107902ca1daeb71` |
| `libmachook/AppInputBridge.m` | 修改 | `e794623b6130deb440f9272a8d54ca05afffd4940e38673ec232c0726c64a32b` |
| `misc/test_cydiasubstrate_provider_contract.py` | 新增 | `27fad86b5293efb5d06808076e789ebee3e4f5504dc69a4cb2cd54e2bc28f7be` |
| `misc/test_app_input_route_contract.py` | 新增 | `91dbfdedb5cb9c9f5ce979ca97f816c08c0f13ac3603ba7ea773d8b109dce5c2` |

修复前的 provider 哈希与此前 [角色审计](sequoia-share-audit-20261015/roles/provider-contract-probe.json) 记录的 `provider_source_sha256` 完全一致，说明本轮反例对照的输入就是当时复现能力缺口的同一份源码。

## 2. Provider 修复内容

### 2.1 LINKEDIT 局部符号换算

旧实现为每个 segment 重算 `seg_slide = mh - seg.vmaddr`，再令
`le_lo = seg.vmaddr + seg_slide`，结果退化为 `mh`；随后用
`slide + symoff` / `slide + dataoff` 访问符号表和 export trie。对普通
Mach-O，`vmaddr - fileoff` 在各 segment 间相同，偏移碰巧成立；对 dyld
shared cache 镜像，两者不同，访问落到无关内存，局部符号返回 NULL。

修复后的唯一换算：

```text
linkedit_map = slide + __LINKEDIT.vmaddr
pointer      = linkedit_map + (file_offset - __LINKEDIT.fileoff)
bounds       = [fileoff, fileoff + filesize)   // filesize，而非 vmsize
slide        = mach_header - __TEXT.vmaddr
```

- export trie 与 `LC_SYMTAB` 都走该换算；
- `n_value` 仍按 `slide + n_value` 转为运行时地址；
- `N_TYPE == N_SECT` 接受局部与外部定义符号（参考 provider 也不要求 `N_EXT`）；
- 字符串表访问增加 `strsize` 边界与 NUL 终止检查；
- 缺少 `__TEXT`/`__LINKEDIT` 时失败关闭，不猜测地址。

本地 22F82 SkyLight 切片（`analysis/SkyLight_22F82`，未入 Git）提供真实布局反例：

```text
__TEXT     vmaddr 0x1850e5000  fileoff 0
__LINKEDIT vmaddr 0x22193c000  fileoff 0x4753c1 (4674049)
LC_SYMTAB  symoff 0x479d90  stroff 0x4cb45c
nm -a:  _CGXSenderCanSynthesizeEvents    t 0x1853fd464
        _audit_token_check_tcc_access    t 0x185425da0
```

`__LINKEDIT.vmaddr - fileoff` 与 `__TEXT.vmaddr` 相差约 9 GB。旧公式在该
布局下必然访问错误地址。此处仅为**文件布局核验**，不是 22F82 运行时验收。

### 2.2 MSHookMessageEx

旧实现只读取原 IMP，然后 `(void)imp;` 丢弃 replacement。新实现：

- `class_getInstanceMethod` 查找（传元类时同样覆盖类方法）；
- `class_replaceMethod(_class, sel, imp, types)` 真正安装；
- 仅在*被请求的类*上覆盖继承方法，父类实现不被修改；
- 先通过 `*result` 发布原 IMP，再安装 replacement，replacement 可安全调用原实现；
- class/selector/IMP/type encoding 缺失时失败关闭：不添加伪造类型的方法，`*result`
  为 NULL；
- 失败路径写入 `MACWSHook: MSG-REFUSED ...` 标记（`write(2)`）。

明确限制（也写入源码注释）：方法没有可 pin 的稳定 prologue，因此没有
`MSHookFunction` 那样的 UUID/offset permit；libobjc 自行刷新方法缓存，本
provider 不额外提供线程静默协议；类型编码只检查存在性，不校验签名等价。
现存 7 个 `MSHookMessageEx` 调用点已逐一审计：均为诊断或兼容适配
（MetalFX/AGX buffer 观察、LSPlugInKitProxy 签名 entitlement 恢复、
`_LSDefaults` 存储路径隔离），都转发原实现，没有恒真 TCC、assert 或
validation 绕过。

## 3. AppInput exact PID/window 契约

新增 `include/macws_app_input_route.h`，把记录接受/路由不变量收敛为纯 C
函数 `MacWSAppInputRouteClassify(record, selfPID, dockEndpoint, &windowID)`：

| 不变量 | 拒绝原因 |
| --- | --- |
| `MACWS_INPUT_MAGIC` | `RejectMagic` |
| version 支持该 kind | `RejectVersionKind` |
| `targetPID == selfPID`（端点 socket 名即 `<pid>`） | `RejectTargetPID` |
| 非零 frame、有限 geometry、tilt∈[-1,1] | `RejectCoordinates` |
| source 枚举范围 | `RejectSource` |
| ConfigureWindow/CloseWindow/RelativePointer 必须带非零 CGWindowID | `RejectWindowRequired` |
| RelativePointer 不得声明 global surface；SystemGesture 仅 Dock + global surface | `RejectGlobalOwner` |

接受路由为 `ProcessScoped` / `ExactWindow` / `GlobalSystemSurface`。
`AppInputBridge.m` 的 `MacWSInputRecordIsValid` 现在先调用该契约，再执行各
kind 的 payload 校验；不再重复 `targetPID != getpid()` 或窗口非零判断。
下游仍保留 `-[NSApplication windowWithWindowNumber:]` 的实时窗口解析，契约
只证明记录声称的 window ID，存在性仍由 AppKit 进程内查询决定。

## 4. 真实命令、输出与退出码

### 4.1 focused tests（新增）

```text
$ python3 -m unittest -v misc.test_app_input_route_contract misc.test_cydiasubstrate_provider_contract
test_contract_header_is_self_contained ... ok
test_production_uses_the_contract_and_keeps_exact_window_lookup ... ok
test_route_classification_matrix ... ok
test_provider_source_and_behavior ... ok
test_provider_source_contract ... ok
Ran 5 tests in 7.062s
OK
exit 0
```

### 4.2 provider 正/负对照（同一合成 cache 布局 fixture）

修复前（HEAD 源码）：

```text
cache-local=0x0 expected=0x7fae75517120
cache-exported=0x0 expected=0x7fae75517200
method-hook before=3 after=3 original=0x104341880
original-callable=3
VERDICT=provider-contract-gaps-present
exit 0
```

修复后：

```text
cache-local=0x7fb3c3417120 expected=0x7fb3c3417120
cache-exported=0x7fb3c3417200 expected=0x7fb3c3417200
method-hook before=3 after=7 original=0x1009b5880
original-callable=3
VERDICT=provider-contract-satisfied
exit 0
```

fixture 使用 `__TEXT.vmaddr=0x1000, __LINKEDIT.vmaddr=0xA000,
fileoff=0x3000`，刻意让 fileoff 与 vmaddr 不满足同一恒等式；局部符号为
无 `N_EXT` 的 `N_SECT`。对照程序在 `/tmp`，未入库。

### 4.3 编译与既有门禁

```text
$ xcrun --sdk macosx clang -arch arm64  ... -c misc/cydiasubstrate_macos.m   # exit 0
$ xcrun --sdk macosx clang -arch arm64e ... -c misc/cydiasubstrate_macos.m   # exit 0
$ make THEOS=/Users/ciscohe/theos -C libmachook -j4                           # exit 0
  output: libmachook/.theos/obj/libmachook.dylib (arm64 + arm64e)
  UUID arm64  224E8EB3-84BB-3372-91AC-2ACCC6015E42
  UUID arm64e CB416C6F-EA69-32FD-8D18-D6B83BE89619
  SHA-256 63e9a7fc27822b1a19a38c13e27b0707a95af445c663ffcdf28e9fcd221febb3

$ cc -std=c11 -Wall -Wextra -Werror -Iinclude misc/macws_protocol_test.c -lm
$ /tmp/macws_protocol_test
macws protocol validators: PASS                                           # exit 0

$ python3 misc/audit_runtime_switches.py
runtime-switch audit OK: 309 source/plist env names, 86 source flag files,
467 total recorded entries                                              # exit 0

$ git diff --check                                                       # exit 0
```

相关既有输入测试（`discover` 方式运行，避免本地模块导入路径误报）：
`test_keyboard_snapshot_wire`、`test_text_input_bridge`、
`test_keyboard_text_input_route`、`test_hardware_keyboard_contract`、
`test_game_pointer_lock_contract`、`test_touch_momentum_cancellation`、
`test_pointer_activation_contract` 均通过。

`test_atomic_pointer_click.test_proxy_uses_same_transaction_and_real_position`
失败，原因是 `mac_hooks.m` 中的适配器文本与断言不一致；本轮未修改
`mac_hooks.m`，属**既有基线失败**，不计入本轮回归。

完整 `misc/test_*.py` 套件：

```text
Ran 708 tests in 324.412s
FAILED (failures=14, errors=5, skipped=14)
exit 1
```

失败/错误集中在既有主机 MetalTexel/NoCopy/IOSurface 探针
（`test_texture_sampling_probe`、`test_texture_stride_probe_contract`、
`test_iosurface_protection_contract`、`test_nocopy_abi`）、
`test_vscode_webview_lifecycle` 的 webview 并发用例、`test_metal_diagnostic_hot_path`
以及上文 `test_atomic_pointer_click` 基线。本轮新增的两个测试与所有 AppInput/
provider 相关测试均通过；未把该套件称为 green。

## 5. 未完成/限制

- **没有设备验收。** 契约与 provider 只在 macOS 主机上编译/运行；未验证
  22F82 WindowServer 实际加载的 provider，也未验证 Dock/Terminal 的真实输入。
- **MSHookMessageEx 语义**在主机 libobjc 上验证（含继承方法、父类不变、
  原 IMP 可调用、元类类方法、缺失 selector 失败关闭）；22F82 libobjc 的
  行为未在本轮运行。ABI 由 Apple runtime 保证，但仍需设备侧最小探针确认。
- **LINKEDIT 修复**使用合成 cache 布局与本机真实 SkyLight 切片的静态布局
  核验；没有在真实已加载 22F82 进程中调用 `MSFindSymbol`。
- `libmachook` 已离线编译成功，但未打包、未签名、未部署；`cydiasubstrate_macos.m`
  不属于 Theos 子工程，本轮只做了主机原生与 macOS arm64/arm64e 对象编译。
- `AppInputBridge.m` 只做了源码级集成检查；没有运行真实的 NSWindow
  `windowWithWindowNumber:` 解析。

## 6. 清理

- 未创建设备侧文件、marker、进程或 launchd job。
- 对照程序、旧 provider 副本与日志均在 `/tmp`，不属于仓库；构建产物位于
  被忽略的 `libmachook/.theos/`。
- 未使用用户在本轮提供的 SSH 凭据；该值未写入任何文件。

## 7. 结论

`MSHookMessageEx` 从 no-op 变为可验证的 exact-class swizzle；局部符号解析
改为 `LINKEDIT.vmaddr/fileoff` 换算；AppInput 的 exact PID/window 与
global-surface 规则成为可编译、可断言的纯契约。以上均为离线能力修复，
不构成 Ventura 设备侧输入验收，也不改变 R008 的 TCC transport 边界。

(handover to codex now)
