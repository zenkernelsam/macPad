# macOS 15.6.1 rootfs 安装与启动 runbook（iPadOS 16.3 / 20D47）

状态：2026-09-25 更新；本设备实测记录见各步的踩坑注记。
用法：在 iPad 上打开本文档，**逐段复制 bash 块执行**。全程 Filza 终端或
NewTerm/SSH 均可（脚本已内置 PATH 兜底）。

## 需要下载的文件

| 文件 | 大小 | 说明 |
|---|---|---|
| `macos-15.6.1-rootfs.tar` | ~19-20 GB | 15.6.1 (24G90) 完整 rootfs |
| `com.kdt.macosbooter_0.3.4_iphoneos-arm64.deb` | ~5.9 MB | tweak 包（含预拆分 libmachook + mount_bindfs） |
| `install_rootfs_15.sh` | ~16 KB | 安装脚本 |
| `arm64ify_macho.py` + `exec_to_dylib.py` | 各几 KB | 脚本会自定位同目录下的它们 |

注意 Nextcloud 里可能存在**无扩展名的旧版重复文件**——认准带后缀、
日期最新的版本（deb ≈ 5,880,544 字节，脚本 ≈ 16 KB）。

## Step 0 — 进入下载目录

文件管理器/Filza 下载位置一般是 File Provider 路径。先 cd 进去：

```bash
cd "/var/mobile/Containers/Shared/AppGroup/1B2AD29A-2C34-4770-8C-E11CD02312FF/File Provider Storage/macPad_iOS"
ls -lh
```

（路径如果不同，用 Filza 长按文件夹 → 属性 复制真实路径。）
确认能看到 `macos-15.6.1-rootfs.tar` 和 `.deb`。

## Step 1 — 装依赖 + 清理半装状态（懒人脚本）

用 `ipad_fix_deps.sh`（同步文件夹里有）。它做两件事：

```bash
# 情况 A：dpkg -i 曾失败、Sileo 提示"没安装好"挡着别的软件 —— 先卸载干净：
sudo bash ipad_fix_deps.sh --uninstall

# 情况 B：dpkg 正常，只补依赖：
sudo bash ipad_fix_deps.sh
```

脚本内容：卸 deb（清掉 dpkg 半配置状态 + `dpkg --configure -a` +
`apt -f install` 解锁队列）→ 逐包装依赖（某个包名在源里没有会跳过，
不会整体失败）→ 给缺的工具造兜底（`strings` 用 python3 现场造一个，
`chflags`/`lipo`/`plutil` 从 `/usr/bin` 系统路径软链）→ 最后打印
验证清单，关键工具齐了会告诉你直接跑 Step 2。

如果坚持手动装：`sudo apt install -y python3 ldid coreutils grep gawk findutils tar binutils uikittools file-cmds`（`binutils`/`file-cmds` 名字在你的源里可能不存在，删了重跑，shim 会兜底）。

## Step 2 — 装 deb

```bash
sudo dpkg -i com.kdt.macosbooter_0.3.4_iphoneos-arm64.deb
```

预期输出：`LEGACY-BOOT current` → `LC_BUILD_VERSION ... -> macOS 13.0`
→ `patched libmachook.dylib` → `Split libmachook ...`（或直接跳过拆分，
用包内预拆分的 arm64 thin）→ 一堆 trustcache 注册日志 → 无 error 退出。

若之前装过留下半配置状态，dpkg -i 会自动续上——不用先卸载。

## Step 3 — 装 15.6.1 rootfs

```bash
sudo bash install_rootfs_15.sh macos-15.6.1-rootfs.tar
```

（在你 cd 进的下载目录里跑；脚本会优先用同目录的两个 .py helper，
不需要先克隆 repo。）

脚本自动做：工具预检（缺什么一次列全 + 给 apt 命令）→ 创建
`/var/mnt` → 解包到 `/var/mnt/rootfs-15.new`（**19GB，解压要几分钟到
十几分钟，屏幕保持解锁**）→ chown root:wheel → 校验 24G90 →
arm64ify WindowServer/Installer Progress/bash → 转换 launchservicesd →
就位 `/var/mnt/rootfs`（本机全新安装，无旧 rootfs 可留备份）→
从 `/var/jb` ElleKit 收割 TweakLoader/systemhook/CydiaSubstrate →
生成 `master.passwd` → bind `/var/jb` → postinst（命中 24G90 分支
注册新 dyld 缓存 hash）→ 冒烟测试。

**成功标志**：最后输出 `hi`（`chdir: No such file or directory` 无害）。

失败分支：
- `FAIL: missing tools ...` → 按它打印的 apt 命令装，重跑
- `FAIL: missing <path>` → tar 不完整，重传 tar
- `SMOKE-FAIL` → `sudo oslog | grep -i amfi` 看拒绝原因，发我日志

## Step 4 — 首次启动（coexist，最安全）

```bash
sudo bash /var/jb/usr/macOS/bin/macos_gui.sh start coexist
sudo oslog | grep "AMFI\|debugbydcmmc\|WindowSer\|MTL\|Metal\|CURSOR-ABI\|IOMFB-ABI\|QC-FRAMEINFO\|STUB_FIX\|COEXIST"
```

**预期日志行**（逐条对应移植点）：
`CURSOR-ABI resolved` `IOMFB-ABI resolved` `QC-FRAMEINFO`
`STUB_FIX repaired-early` `COEXIST verified kern_SwapEnd BL`
`CANCEL-COMPLETION observer` `PREREGISTER ... AGXMetal13_3`

出现 `variant miss` / `no variant matched` = 某补丁没命中，把该行发我。

## Step 5 — 画面验证

主显示路径是 **MacWSHost.app**（deb 已装到 `/var/jb/Applications/`，
DisplayStream 直取合成 surface）。⚠️ 本机全新安装，**没有
OSXvnc-server**（它只存在于作者的旧 rootfs 包）——coexist 模式下
VNC 诊断路径不会有 server，画面验证走 MacWSHost 或 exclusive。

**路径 A：MTLSim（原作者演示用的，出画面概率最高）**

当前 WindowServer.plist 强制 `MACWS_AGX_NATIVE=1`（直碰 IOGPU，
会撞 `0xe00002c2` 内核拒绝——13.4 上就没解）。回退模拟器渲染：

```bash
sudo /var/jb/usr/bin/plutil -remove EnvironmentVariables.MACWS_AGX_NATIVE \
    /var/jb/usr/macOS/LaunchDaemons/com.apple.WindowServer.plist
sudo bash /var/jb/usr/macOS/bin/macos_gui.sh restart coexist
```

（plutil 若不存在：`apt install plutil`，或者用 Filza 直接编辑那个
plist 删掉 MACWS_AGX_NATIVE 键。）

**路径 B：整屏独占（exclusive）**

```bash
sudo bash /var/jb/usr/macOS/bin/macos_gui.sh start exclusive
```

WindowServer 直接驱动物理屏（停 SpringBoard/backboardd）。脚本自带
警告：这条 GPU 路径最易 panic——**先确认别处有画面再上**。
退回 iOS 界面：`macos_gui.sh start coexist` 会 reload 系统 UI。

## 验收标准（证据纪律）

| 层级 | 证据 |
|---|---|
| rootfs 装好 | `bash /var/jb/usr/macOS/bin/run_bash.sh -c "sw_vers"` 输出 15.6.1 |
| hooks 命中 | oslog 里各 `resolved/verified` 行，无 variant miss |
| WindowServer 活着 | `ps aux | grep WindowServer` 有 PID |
| **出画面** | MacWSHost/exclusive 屏亮 —— 唯一算数的验收 |
| 输入 | 触屏手势移动鼠标 |

## 卸载/回滚（全新安装：没有 13.4 可回退，就是删干净）

```bash
sudo bash /var/jb/var/mobile/MacWSBootingGuide/misc/cleanup_all.sh   # 有 repo 时
# 没 repo 就直接手动：
sudo launchctl unload /var/jb/usr/macOS/LaunchDaemons/*.plist 2>/dev/null
sudo pkill -f WindowServer 2>/dev/null
sudo pkill -f OSXvnc 2>/dev/null
umount /var/mnt/rootfs/var/jb 2>/dev/null
sudo rm -rf /var/mnt/rootfs /var/mnt/rootfs-15.new
sudo dpkg -r com.kdt.macosbooter
reboot    # trustcache/mount/进程归零
```

## 已知风险（证据型，非猜测）

1. **0xe00002c2 AGX blocker**（最大项）：chroot 直开 IOGPU 建
   heap/queue 被 iPadOS 16.3 内核拒绝，13.4 上未解。MTLSim 回退绕过它
   （GPU UC 由 iOS 原生宿主进程持有）。
2. **arm64e-exec**：若 iOS 普遍拒绝 arm64e macOS 可执行（不止
   WS/InstallerProgress），冒烟测试会 exec 失败——把安装器里的
   arm64ify 列表扩大即可。
3. **IOSurface 修复刻意未移植**：15.6.1 client 布局大变，宁可走
   stock 也不猜（fail-closed）。若日志出现 IOSurface 相关断言，
   那时再重推导。
4. **launchservicesd 现场转换**：若 shim 的 dlopen_entry_point 需要
   的不止 LC_MAIN，看该 daemon 自己的崩溃日志——隔离在它一个 job。
5. **DefaultLocalDB/dslocal 缺失**（root-only 拷不过来）：影响
   dscl/用户查询，不影响首屏。
6. **dyld .atlas/.map**：15.6.1 新增缓存伴生文件，若在 dyld-map
   阶段挂掉，再注册它们的 CDHash（VM 上看是无签名数据文件）。
