# macOS 15.6.1 rootfs 安装与启动 runbook（iPadOS 16.3 / 20D47）

状态：构建与脚本就绪于 2026-09-25；尚未在设备上执行过。
阅读对象：在 iPad 上操作的人。照抄命令即可，每步写了预期输出和失败分支。

## 你手里的两个文件（飞牛同步 macPad_iOS/）

| 文件 | 大小 | 说明 |
|---|---|---|
| `macos-15.6.1-rootfs.tar` | 19 GB | 15.6.1 (24G90) 完整 rootfs |
| `com.kdt.macosbooter_0.3.4_iphoneos-arm64.deb` | 5.6 MB | tweak 包（VM 交叉编译，含全部 15.6.1 补丁） |

备用：`install_rootfs_15.sh`、`arm64ify_macho.py`、`exec_to_dylib.py`
（这三个 iPad 上 git 同步 repo 后就有，同步文件夹里的只是备份）。

## Step 0 — 文件落到 iPad

飞牛 App 里把两个文件下载到 iPad 本地（比如 iCloud Drive/文件 App 可见目录），
然后终端里确认路径。本文假设放在 `/var/mobile/Documents/`：

```bash
ls -lh /var/mobile/Documents/macos-15.6.1-rootfs.tar \
       /var/mobile/Documents/com.kdt.macosbooter_0.3.4_iphoneos-arm64.deb
```

## Step 1 — 前置依赖 + 同步代码

**装 python3 + 确认 ElleKit**（工具链硬依赖，postinst 里大量 .py 脚本）：
Sileo 里搜 `python3` 安装（procursus 源），或终端：

```bash
sudo apt update && sudo apt install -y python3
ls -l /var/jb/usr/bin/python3   # 确认存在
```

> 2026-09-25 踩坑记录：`dpkg -i` 报 `python3: not found` + `Unresolved legacy
> MacWS boot launch configuration` = python3 未装，包处于半配置状态。
> 装完 python3 后**重跑一次 `dpkg -i`** 即可续上（不是重新来过）。

**同步代码**（拿 misc/ 新脚本）：

```bash
cd /var/jb/var/mobile/MacWSBootingGuide
git fetch origin && git reset --hard origin/main
```

预期：HEAD 落在 `a417d41` 或更新。
**没克隆过 repo 也没关系**：`install_rootfs_15.sh` 会优先用同目录下的
`arm64ify_macho.py`/`exec_to_dylib.py`（同步文件夹里三个脚本放一起即可），
只有 `mount_bindfs`/`run_bash.sh`/`postinst.sh` 需要 deb 已安装提供。

> **全新安装（本设备没装过 13.4 macPad）**：脚本会自动检测——`/var/mnt`
> 不存在就创建、无旧 rootfs 可备份、iOS 注入组件（TweakLoader/
> systemhook/CydiaSubstrate）改从 `/var/jb` 的 ElleKit 收割。
> 已知缺口：`OSXvnc-server`（chroot 内 VNC）只存在于作者的旧 rootfs 包，
> 新装没有它——但主显示路径是 **MacWSHost.app**（deb 装到
> `/var/jb/Applications/`，DisplayStream 直取合成好的 surface），
> VNC 只是诊断 fallback。要 VNC 的话以后可从旧 rootfs 包补。

## Step 2 — 装 deb（先装包，再装 rootfs，顺序别反）

```bash
sudo dpkg -i /var/mobile/Documents/com.kdt.macosbooter_0.3.4_iphoneos-arm64.deb
```

预期：一堆 `add_all_trustcache`/签名日志，结尾不报错。
这一步同时会对现有 13.4 rootfs 幂等跑一遍 postinst——正常现象。

## Step 3 — 装 15.6.1 rootfs

```bash
sudo bash misc/install_rootfs_15.sh /var/mobile/Documents/macos-15.6.1-rootfs.tar
```

脚本自动做：预检（内核 20D47、磁盘 ≥25GB）→ 解包到
`/var/mnt/rootfs-15.new` → chown root:wheel → 校验 24G90 →
arm64ify（WindowServer/Installer Progress/bash）→ 转换 launchservicesd →
换 rootfs（旧的留 `rootfs-13.4.bak`）→ 收割 iOS 注入组件 →
bind `/var/jb` → postinst（命中 24G90 分支注册新 dyld 缓存 hash）→
冒烟测试 `run_bash.sh -c "echo hi"`。

**成功标志**：最后输出 `hi`（`chdir: No such file or directory` 无害）。

失败分支：
- `FAIL: missing ...` → tar 不完整，重传
- `SMOKE-FAIL` → `sudo oslog | grep -i amfi` 看拒绝原因，发我日志；
  恢复：`bash misc/cleanup_all.sh`，回滚见文末

## Step 4 — 首次启动（coexist，最安全）

```bash
sudo bash /var/jb/usr/macOS/bin/macos_gui.sh start coexist
sudo oslog | grep "AMFI\|debugbydcmmc\|WindowSer\|MTL\|Metal\|CURSOR-ABI\|IOMFB-ABI\|QC-FRAMEINFO\|STUB_FIX\|COEXIST"
```

coexist = iPad 屏继续显示 iOS，macOS 画面走 VNC（`OSXvnc-server`，
localhost + 指针代理已配好）。这是验证链路的第一步。

**预期日志行**（逐条对应移植点）：
`CURSOR-ABI resolved` `IOMFB-ABI resolved` `QC-FRAMEINFO`
`STUB_FIX repaired-early` `COEXIST verified kern_SwapEnd BL`
`CANCEL-COMPLETION observer` `PREREGISTER ... AGXMetal13_3`

出现 `variant miss` / `no variant matched` = 某补丁没命中，把该行发我。

## Step 5 — 画面验证的两种路径

**路径 A：MTLSim（原作者演示用的，出画面概率最高）**

当前 WindowServer.plist 强制 `MACWS_AGX_NATIVE=1`（直碰 IOGPU，
会撞 `0xe00002c2` 内核拒绝——13.4 上就没解）。要回退到模拟器渲染：

```bash
sudo /var/jb/usr/bin/plutil -remove EnvironmentVariables.MACWS_AGX_NATIVE \
    /var/jb/usr/macOS/LaunchDaemons/com.apple.WindowServer.plist
sudo bash /var/jb/usr/macOS/bin/macos_gui.sh restart coexist
```

然后 VNC 连 `localhost`（iPad 上装个 VNC viewer，或 Mac 上
`ssh -L 5900:localhost:5900 -p 2222 root@<iPad>` 再连）看画面。
已知缺陷：毛玻璃/vibrancy 渲染黑块、重 GPU 应用跑不动——那是
sim 驱动的固有限制，不是 port 的锅。

**路径 B：整屏独占（exclusive）**

```bash
sudo bash /var/jb/usr/macOS/bin/macos_gui.sh start exclusive
```

macOS WindowServer 直接驱动物理屏（停 SpringBoard/backboardd）。
脚本自带警告：这条 GPU 路径在本机上最易 panic——**先确认 coexist
有画面再上 exclusive**。退回 iOS 界面：`macos_gui.sh start coexist`
会自动 reload SpringBoard/backboardd。

## 验收标准（证据纪律）

| 层级 | 证据 |
|---|---|
| rootfs 装好 | `run_bash.sh -c "sw_vers"` 输出 15.6.1 |
| hooks 命中 | oslog 里各 `resolved/verified` 行，无 variant miss |
| WindowServer 活着 | `ps aux | grep WindowServer` 有 PID |
| **出画面** | VNC 截图非全零 / exclusive 屏亮 —— 唯一算数的验收 |
| 输入 | 触屏手势/VNC 指针移动鼠标 |

## 回滚到 13.4

```bash
sudo bash /var/jb/var/mobile/MacWSBootingGuide/misc/cleanup_all.sh
umount /var/mnt/rootfs/var/jb 2>/dev/null
mv /var/mnt/rootfs /var/mnt/rootfs-15.failed
mv /var/mnt/rootfs-13.4.bak /var/mnt/rootfs
mkdir -p /var/mnt/rootfs/var/jb
/var/jb/usr/local/bin/mount_bindfs /var/jb /var/mnt/rootfs/var/jb
bash /var/jb/usr/macOS/bin/postinst.sh
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
5. **DefaultLocalDB/dslocal 缺失**（无 sudo 拷不过来）：影响
   dscl/用户查询，不影响首屏。已从旧 rootfs 收割兜底。
6. **dyld .atlas/.map**：15.6.1 新增缓存伴生文件，若在 dyld-map
   阶段挂掉，再注册它们的 CDHash（VM 上看是无签名数据文件）。
