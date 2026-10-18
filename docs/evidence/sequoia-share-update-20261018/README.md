# 10-18更新分享包与现场备份审计

本轮仅离线读取、流式校验、选择性提取及IDA分析；没有运行供应脚本／二进制、连接iPad、部署或控制其他Agent。iCloud两个来源目录无写入；临时源码、二进制和IDA数据库在`/tmp/macpad-share-review-20261018/`。仓库保留本说明、结构化核验、必要反汇编和项目运行脚本diff，不复制用户数据库／原始日志／个人文件。

结论已合并进[15复盘§7](../../porting/sequoia-24G90-share-review-20261015.md#7-2026-10-18新增材料对第一版结论的补充)与[13交接§5](../../porting/ventura-22F82-evening-handoff-20261015.md#5-10-18补充更新包怎样帮助当前13)，本目录用于追溯证据，不另建一套项目总述。

## 1. 输入与可信范围

| 输入 | 本轮身份 | 用途／限制 |
| --- | --- | --- |
| 第一版 | 仓库ignored目录`analysis/macPad-15.6 2.1-分享包-20261013`及10-15已保存证据 | 比较基线，保持原位 |
| 更新发布包 | iCloud `macPad-15.6.1-分享包-20261013`，含`UPDATE-20261016.md` | 已脱敏发布材料；不是只新增Markdown，payload／清单已整体更新 |
| 现场备份 | iCloud `15.6提取包`，10-17归档与稍后metadata | live、non-atomic；不是发布包、完整越狱备份或已验收restore image |

`UPDATE-20261016.md` SHA256为`1b552b07a87af31d4d505088a2e0e1570875470b0eba24a7616bdf8fa6fcc455`。完整阅读了更新README、两份UPDATE、ADAPTATION、安装说明、PRIVACY、新Safari／墙纸交接及现场README。结构化清单做程序核验；参考源码按相关函数和新旧diff阅读。附录16份代码包含历史源码和截断节选，不能声称每份均可独立编译或对应每个生产二进制。

来源说明自称的RE/runtime结果，未随包提供原始反汇编／日志／帧时，均为**来源方报告**。本轮源码确认仅描述实现，不证明装载、生效或根因。本轮RE-confirmed仅适用于下述已记录身份的IDA输入。没有本机M1 acceptance。

## 2. 更新发布包完整性

- [checksums.json](checksums.json)：37项均匹配，含payload SHA256 `40b3a6cd6b01564fdac790a586ef46be9b3c3af24621f593b9ba56764d5888e5`，压缩大小11,504,204,008。
- [curated-archive.json](curated-archive.json)：完整流式扫描476,371成员；161,829目录、15,038 symlink、276,587普通文件、22,917 hardlink。每个成员路径在inventory中；每个普通文件读完并核SHA256，无内容不匹配，无inventory未见路径。读到zstd stream EOF；没有全量恢复链接／属主或安装验证。
- [inventory-diff.json](inventory-diff.json)：旧475,270、新476,371；新增1,106、删除5、574路径记录变化。包含uid/gid、link、hardlink和size/hash差异，不能直接等同574个二进制功能变化。
- 选择性提取293普通文件及[extra-extraction.json](extra-extraction.json)的5个输入。只写普通文件，不恢复tar链接、不跟随归档绝对路径、不执行输入代码。
- [outer-files.json](outer-files.json)：外层说明／附录／工具完整hash清单。

dyld、双cache、atlas/map、pager两sidecar、guard／sources／enable在新旧inventory中相同，且其新版普通文件实际hash通过；因此旧dyld/sidecar RE与22,818,485 pointers静态比较无需当作新机制重做。主cache `c2ef6966…0c5b3`、`.01` `e6a7e6c0…1b040`；完整hash见清单和原审计。

新增路径以CoreTypes（816条）、Cryptex布局／资源及设置载体为主；不能从“补齐资源”推导系统全面无缺失。新增hostd与runtime参考有助审计，但原`sequoia-hook-source`的`config/production.mk`、Diagnostics目录及若干被引用headers仍缺，不能称完整可重建树。

## 3. 实际新版IDA复核

13337／13338均connection refused；本轮使用现有IDA Pro 9.3批处理＋Hex-Rays，没有借旧ServerHealth声称当前MCP在线。数据库在`/tmp`，输入SHA由IDA记录。

| 输入 | SHA256 | 本轮结果 |
| --- | --- | --- |
| exec helper | `fc30f01564d8eb23a5b4772d1a63533437e1d56fd03bfc719f869ee33942695a` | [identity](ida/exec/identity.json)、[函数与指令](ida/exec/functions.json)、[允许peer表](ida/exec/allowed-peers.json) |
| WS arm64 libmachook | `d047ce94aebfa9898298a5de6d67a9d51db013784af2004b5dea0925c70b8c69` | [identity](ida/ws-hook/identity.json)、[函数与指令](ida/ws-hook/functions.json) |

**RE-confirmed via exec helper +0x4f08/+0x4fd4：**peer枚举终止比较由`CMP X21,#0xA`改为`#0xB`，增加一个允许peer。IDA读取table含12项，新增Appearance Settings carrier；依旧使用真实路径、root-owned／非group/other可写普通文件与proc身份检查。`watch`、`stopped`、`grant`反编译逻辑与旧版一致，函数地址不变；多数指令差异是常量／字符串地址搬移，不能称完全逐字节相同。`grant`仍调用真实jbctl、等待并复核新exec generation；未见本轮新PMAP实现。这里没有分析更晚现场备份中不同hash的helper，不能把此结论延伸到它。

**RE-confirmed via libmachook +0x7b74/+0x7b84/+0x607c：**新版仍通过MSFindSymbol查`_audit_token_check_tcc_access`，调用MSHookFunction指向`MOV W0,#1; RET`。这是实际新版静态安装路径，不是仅参考源码残留；无来源设备live命中证据。更新说明的“真实验证保留”不能证明所有旧override都删除，当前Ventura继续禁止复制此绕过。

## 4. 源码证据定位

以下`P`为`/tmp/macpad-share-review-20261018/curated`，`D`为来源外层`docs/AI修复参考代码`。hash与路径可由外层清单、inventory及提取记录重建。源码内“RE-confirmed/runtime-confirmed”注释仍是来源自述，未升级成本轮运行证据。

| 实现／位置 | 读到的合同 | 不能据此声称 |
| --- | --- | --- |
| `P/reference-source/sequoia-hook-source/libmachook/mac_hooks.m:12172`、`:24908` | exact SkyLight UUID/return-site识别原port set；EVFILT_MACHPORT／fflags0就绪后退出人工pacing，不接收消息 | 22F82已具同瓶颈、性能提升在本机复现 |
| 同文件`:23389`、`:23507` | private GPU event请求/返回转换；真实native status、输出数量和资源回收 | 所有GPUevent与timeline语义通过 |
| `iogpu15-compute-packet.h:5,101`及render/mixed对应diff | 删除一个padding零值约束，显式资源容量与临时buffer推导同时扩展 | 任意packet可兼容、删除检查天然正确 |
| `Compatibility/MacWSG14Empty.m:127`及`MacWSMetalFXANE.m` | real shared-event fallback、scheduled signal、ANE编译及load/unload/purge，策略文件与生命周期 | NoCopy与tensor复制等价、M1支持ANE适配、所有wait/signal正确 |
| `P/reference-source/host15-source-20261008/macwshostd/main.m:1642,3539` | 本次reopen后新metrics；无回应且仍有窗口保留进程报错 | 每个应用复用／文稿处理已验收 |
| `P/host-overlay/var/jb/usr/macOS/bin/cleanup_browser15.py:31`与`macos_gui.sh:989` | WS-dependent清理按exact路径＋lstart重查；原生MobileSafari路径不匹配 | lstart就是uniqueid/idversion、任意旧CGS客户端可被发现 |
| `macws_settings_sign.py:298,322,351` | 全bundle内容／链接／stat／xattr／verifier输入＋boot snapshot，失效回真实codesign | 不需dynamic trust、native app所有签名语义自动通过 |
| `D/g14-nv12.excerpt.x` | 双plane identity在作用域保存／finally恢复 | 实际视频后段呈现全部正常 |
| `D/wallpaper-native-image-server.c:26,79` | provider peer/path、source目录／普通文件／权限与frame index检查，源hash＋index缓存、原子rename | 无TOCTOU／完整安全审计、专门硬件解码已验收 |
| `D/macws_wallpaper15.current.py:225,267` | 本次provider日志marker与frame witness；同WS PID列表；2秒稳定、有限Dock替换见证 | 日志／PID就是物理scanout或uniqueid证明 |

两份外层生产脚本的边界也要保留：`macws_wallpaper15.current.py`与payload控制器hash相同；`macos_gui-wallpaper.excerpt.sh`在CODE-MANIFEST标注源hash `7183cb8d…b4b76d`，payload完整`macos_gui.sh`为`7d77cc2f…fdde3`。共同函数能在源码找到，但不能将不同完整source identity称作同一部署快照。

供应报告四帧byte-equal、512帧回归、设置7.3s→4.1–4.4s、Safari两轮重开／静态蓝iMac等，均未有本轮原始见证。General空白、任意墙纸切换、视频后段、最小化、长期内存仍有未完边界。

## 5. 现场`.partial`：完整压缩流，不是完整可恢复快照

[raw-archive.json](raw-archive.json)独立读取结果：

```text
compressed_bytes=31985354592
sha256=abb990272ca58cba521523def93eee0994c1cfba934ff1b9e9605bcf4fa81475
uncompressed_bytes=69159198720
members=582178
gzip_eof=true; error=null
```

压缩hash与附带metadata结果的`prior_archive_sha256_reported`一致。tar末尾剩余读取10,240字节均为零；gzip trailer/CRC通过。`.partial`名称本身不能证明截断，也不能因为可读就称安装可靠。

来源transfer记录原文：

```text
SSH_TAR_EXIT=1
READ_BYTES=69159198720
```

stderr包含`XATTR support is not available`、socket忽略和`file changed as we read it`。该流是在服务仍运行时采集，文件可能跨代际；它没有独立恢复测试。备份范围也不包含完整bootstrap／所有外部容器和symlink目标。

[raw-metadata.json](raw-metadata.json)：582,284 entry，582,177 entry带错误；**全部错误operation为`acl`、errno=2**。metadata gzip完整且压缩SHA `7f8c04c25bd9a455b02bff00bb8efbb24dd9901067beb570afa3a782107d6854`匹配附带结果。其未压缩JSONL hash另为`efe65826…19d31`，两者不是冲突。未据errno臆测缺的是哪个工具／文件；没有ACL完整保真证据，也不能把这些错误误称为582,177内容损坏。metadata稍后采集、路径数与tar不同，不可强行一一配成原子恢复清单。

## 6. 现场备份相对10-16发布包的新线索

[raw-runtime-comparison.json](raw-runtime-comparison.json)：选定78路径；76普通文件完整内容hash，其中64相同、12不同；另外双cache为hardlink，目标需另核。没有对所有个人数据逐字节比较，也未打印／提取用户历史。新脚本路径扫描见[raw-only-runtime-paths.json](raw-only-runtime-paths.json)。

dyld、pager guard／sources／enable／sidecars与发布包相同；双cache的hardlink目标另经完整读取，SHA均与发布包一致。变化包含双架构libmachook、exec helper、hostd与8份启动／设置／墙纸脚本。二进制hash差异没有自动归因成源码功能变化，本轮未RE更晚现场二进制。

已保存并阅读的8份[项目源码diff](runtime-diffs/)提供如下**source-confirmed**线索：

- `macws_wallpaper15.py`增加guard：绑定WS PID/path/lstart，15秒观察、缺失45秒后才尝试恢复、每600秒最多2次且相隔至少120秒；遵守GUI transaction lock和自身flock，selection／generation变化复核；只重启拥有的墙纸链，随WS代际退出。这是有预算的自愈实现，非当前画面成功或定时重启一切的理由。
- `macos_gui.sh`启用该guard，并把iconservicesagent从UID0改UID501/GID20、移除`runAsRoot`，提供权限受限的session cache/temp/user目录。注释称24G90原服务拒绝root身份下的普通用户请求、20C65 confstr返回EIO；**其因果仍属来源报告**，本轮没有那份原服务反汇编／日志。对13的启发是核对daemon真正预期身份及目录生产者，不能“全用root最安全”。
- Settings composition SPECS从3项扩到10项，含General、About、Network、Bluetooth、AirDrop/Handoff、Date/Time、Localization；General组合state移到签名bundle之外，增添严格native signing／composition adoption和回滚；权限修复限定code identity与目标组件。这里仅证明配置扩展，不能撤回10-16报告的General空白或声称所有面板、网络设置功能通过。
- `macws_system_app_prepare.py`把`AppRBSWire15.dylib`及Activity Monitor专用scope加入实际依赖信任，限定普通文件、root属主和不可group/other写。可借鉴“main能exec不等于被注入依赖已恢复trust”的冷启动合同；不能把process-scope库存在视为隔离已经正确。

WindowManagement15独立控制脚本的源码与双cache hardlink目标另见[追加文件核验](raw-additional-files.json)。其存在本身不代表最小化已通过，须结合下述验收边界阅读。

该脚本确实由现场`macos_gui.sh`的prepare/start/stop路径调用，不应继续当作10-16说明中的纯临时文件。它以Dock环境启动原版24G90 WindowManager，声明三个MachServices，并注入`AppRBSWire15.dylib`。但`loaded_pid()`只核对进程路径，未验证service table或一次真实WindowManagement请求；打印的`domain='user/501'`和`client_route='88->501 for this service only'`是常量，实际命令用`user/foreground`，**输出文本不证明路由和服务已工作**。现有材料没有本轮最小化／恢复的运行见证。借鉴时尤其避免重犯“进程出现／服务名字出现即ready”的错误。

## 7. 验证与保存原则

此次是文档／审计变化，没有改运行代码，因此没有重跑设备pipeline或全工程测试。验证限于交付hash、归档解码／成员内容、元数据聚合、相关源码diff、IDA实际输入与输出、JSON和文档链接／空白检查。当前Ventura的离线provider修复与全套测试非全绿状态单独引用[10-18证据](../provider-and-appinput-route-contract-20261018.md)，没有冒称本轮重测。

源输入、原作者材料、coordination／relay未移动或修改。不新增一份重复总述到Archive；旧10-15证据作为历史保留，本轮只增量记录。供应包没有独立M1、第二台clean install或全部功能运行见证，恢复15仍按原复盘的分层阶梯执行。

审计工具保存为[audit_update.py](audit_update.py)、[IDA导出器](ida_export.py)与[peer表导出器](ida_peers.py)。审计用zstandard 0.25.0依赖放`/tmp/macpad-share-review-20261018/deps`，未写iCloud、未添加到工程运行依赖。脚本不导入供应Python，只按普通文件读取。

`runtime-diffs/`与`reference-diffs/`是供阅读的源码对照，展示时清除了行尾空白，不作为可直接apply的补丁；原文件身份以SHA256记录为准。最终[validation.json](validation.json)记录归档／hash、13份供应Python仅语法解析、4份shell仅`bash -n`以及文档链接检查，均未执行供应生命周期代码。
