# HANDOVER — 2026-09-28：macOS 15.6.1 缓存 536 EINVAL 最后一公里

目标读者：接替调查的另一个 AI（有 1M 上下文）。请先读
`docs/porting/dyld-15.6.1-state.md` 顶部摘要、`kernel-syscall536-re-handover.md`
（2026-09-28 新增段含完整 EINVAL 站点表）与 `HANDOVER-REPLY-2026-09-28-536.md`
（iOS 缓存已打通的配方——同机制、可对照）。

回复写到：`docs/porting/HANDOVER-REPLY-2026-09-28-populate.md`。

---

## 0. 任务一句话

macOS dyld（chroot 进 `/var/mnt/rootfs`）调 syscall 536
`__shared_region_map_and_slide_2_np` 提交 `files=[main_cache]`（单文件 8 条
mapping，全部 VA∈[0x180000000,0x22560C000) ⊂ region）仍返回 **EINVAL(22)**。
**定位是哪一条内核校验/enter 站点产出 22，并给出最小修复**——是本次唯一目标。

成功后验收标准：`/var/mnt/rootfs` 里 `DYLD_SHARED_CACHE_DIR=/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld` 的
`chroot . /bin/echo HELLO` 不再打印 `syscall to map cache into shared region failed`
（下一阶段错误——"code signature registration failed" 或 libSystem 加载错——都算过线）。

---

## 1. 环境事实（全部实测/RE 确认，可直接引用）

- 设备 iPad13,11 (M1/T8103)，iPadOS 16.3，Dopamine rootless，KASLR 本 boot
  **slide=0x241c4000**（findkslide.py 结果；本机 KASLR 跨重启疑似不变——地址
  与上次 boot 相同，可直接用）。
- 内核 IDB：IDA Pro MCP **Instance2**（kc_raw，base 0xfffffe0007004000）。
  dyld IDB：**Instance1**（dyld_15.6.1 arm64e thin）。
- 536 真身：sysent → `sub_8459134`（包装，narg=2）→
  `sub_8459570`（校验/setup）→ `sub_8061EF0`（populate 外壳）→
  `sub_80623D4`（逐条 enter worker）。**populate 任何返回 >3 → 包装层一律
  EINVAL**；`sub_80623D4` 的 LABEL_103 → `return 4`（对齐/代际类）、
  LABEL_104 → 原始 errno（14/17/29 等）——全被掩成 22。
- Region 模型：`task+0x3E8` = bound region；队列头 `off_A9F2300`(+slide)；
  dedup 键 = `+0x18` rootdir vnode + kind(+0x20/+0x24) + 若干 flag byte；
  `+118`=stale（去重跳过）、`+0x30`=pop30（≠-1 → populate 直接 return 5 =
  "已填充"毒化，半失败会永久污染该 rootdir region）。
  当前队列：node0=iOS 系统 region（pop30=0x28094000）；
  node1=残留中毒（stale=1 已隔离）；node2=chroot rootfs region
  （`+0x18`=rootfs vnode `0xfffffe13ea90c430`，pop30=-1，submap 空）。
- kind 表（`sub_8060FD0`）：a4=1&kind=0x100000C → base 0x180000000,
  **size=0x100000000（4GB，写死）**；a4=0&kind=12 → 1GB@0x40000000；
  a4=0&kind∈{0x200000C,0x100000C} → 0xA6000000@0x1A000000。
  **⚠️ 结构性约束：macOS 15.6.1 缓存总跨度 ≈0x12C75C000≈4.77GB > 4GB
  region——main 可放，`.01` 尾部 VA 0x22560C000..0x2AC75C000 越界必然失败。
  先只求 [main] 能灌入（libSystem 在主分片，够 CLI），.01 另案。**
- vnode/mount/UBC/blob 已验证全绿（本 boot 重新验过）：v_type=VREG、
  ubc+8 非空、`ubc+0x50` cs_blob 已挂（cachereg fcntl F_ADDSIGS；
  main cso=0xa160c000/css=0x50c000，.01 cso=0x83150000/css=0x41c000；
  main 8 条映射 end 全部 ≤cso 恰好覆盖）、`VSHARED_DYLD` 标志已置、
  与 rootdir 同 mount、uid=0。
- `sms+44 & 0x10`（prot flag）→ 跳过 blob 检查且 populate 走匿名 enter
  （v39=0 而非 fileglob+72 的 pager）。**但改缓存文件的 prot 字段会破
  CodeDirectory 页哈希 → SIGKILL，文件级修改不可行**——只能从 dyld 侧改
  sms（见 §4 已备方案）。
- dyld files[]/mappings[] 构建点（thin 文件偏移=虚地址）：
  files[] 12B/rec @ 0x35538 循环（{+0 fd,+4 count,+8 sf_slide}）；
  `0x3552c LDR W9,[X19,#0x1780]` = files[0].sf_slide；
  `0x354ec ADD W9,W28,#1` → files_count（W28=subcache 数）；
  sms 逐条 48B copy @ 0x35634-0x3564c（源 = X19+0x15E0+i*0x1C0 预制块）；
  dyn 条目 @ 0x35670-0x35680（VA=[X19+0x11D0], prots={1,1}）；
  `0x35690 MOV X3,X26` → `0x35694 BL __shared_region_map_and_slide_2_np`。
- sysctl：`vm.shared_region_trace_level`(已设 3)、
  `vm.shared_region_unnest_logging`(=2)、`vm.shared_region_destroy_delay`(300)。
  trace 事件去哪读**未知**——无 ktrace/kdump；若你找到读取途径可省大量猜测。

## 2. 本 boot 实测矩阵（全部 `errno=0x16` 除非注明）

| files[] | 其余条件 | 结果 |
|---|---|---|
| [main,.01,dyn]（plat 原版） | blob+VSHARED 齐 | EINVAL |
| [main,dyn@0x78000000]（filescount1+dynoff） | 同上 | EINVAL |
| [main]（filescount1+nodyn） | 同上 | EINVAL |
| [.01,dyn] | — | EFAULT（.01 VA 越出 4GB region，结构性） |
| [dyn]（fc0） | — | 成功（匿名 enter 正常） |

今晨（上一 boot，region 有残留）file-backed enter 在全尺寸 0x67f5c000
**成功过**——但 region 里已有条目，疑似走了 merge/replace 路径。
**本次 boot 空 region 下 file-backed enter 从未成功过**。

## 3. 已排除（别再查）

- 区域绑定/无 region（check_np 会建）、pop30 毒化（实测 -1）
- blob 缺失/覆盖不足（cachereg 挂了且范围覆盖全部映射）
- sf_slide 未对齐（macOS=0x20000000 对齐；slide0 补丁无差异）
- VREG/ubc/VSHARED/mount/uid/可读位
- sms[0].foff==0 合规（+44&0x10 是给"跳过 blob"的旁路，不是必需）

## 3.5 新线索（handover 提交后又挖到——最优先验证！）

**`sf_slide` 随机化可能就是当前 EINVAL 的根因**：

- setup @0x8459684 解码确认内核 56B rec 的 `+0x10 = sf_slide`（用户
  12B rec 第 3 字段低 u32）；populate 用它做 `event+4` 代际校验
  （首条非零 slide 种入 region+4，后续 rec 的 slide 必须 ==它或 ==0）。
- wrapper 里 `slide = rand32() % files[0].sf_slide & ~0x3FFF` 被加到
  **每条** sms 的 VA(+0)和 slide(+24)。我们的 sf_slide=0x20000000
  → X∈[0,512MB) 随机。main 尾 VA=0x22560C000，region 顶 0x280000000，
  **X > 0x5AA34000(≈55% 概率）→ 越界 → enter EFAULT → EINVAL**。
  且越界点永远在 populate 中途 → 部分条目先进去再被 rollback →
  解释"有时成功有时 EINVAL"的全部随机性（今晨成功=小 slide 运气）。
- **判别实验已构建未测**：`/var/mobile/dyld_sf0e.bin` =
  crossarch+slide0+e5entry+e5cave（files[0].sf_slide=0 → 禁用随机化，
  VA 固定取优选地址）。预测：errno 22→0，536 通过到 fsignatures 阶段。
- ⚠️ 部署时设备正卡在 **exec-veto cascade**（连 dyld_plat 都 rc=137），
  restore_env.sh 跑过了但 spawn 测试 `proc not found`——**先恢复再测**。

## 4. 建议下一步（按性价比排序）

0. **跑 `dyld_sf0e.bin`**（§3.5）：部署三连跑，看 stderr 首个 8B 是否变 0
   或进入下一阶段错误。**这是当前性价比最高的验证**。
1. **找到 populate 的真实返回码**。`sub_80623D4` 返回 4/errno。入口顶部的
   `event+4 vs rec+16` 代际检查和 `*(u16*)(v26+16)&0x3FFF`（rec+16 疑似
   16K 对齐字段）都→4。**去读 setup(sub_8459570）里 56B rec+16 到底填什么**，
   再对照 dyld 提交的 12B rec 推断字段语义——很可能就是失败点。
2. **file enter `sub_8017E5C`**（vm_map_enter_mem_object,55KB 反编译已存
   `/tmp/8017e5c.json` 或重新 decompile）：枚举它的 return 站点
   （4/17/29/v23）与进入条件，对照我们的参数
   `(submap, &off0, size=0x67f5c000, 0,0, flags=0x2002|v42, 0, pager)`。
   重点：`*(a8+72)`（pager 内部 obj+0x28 尺寸）是否 ≥ foff+size。
3. **确认 main vnode 的 fileglob+72 pager 是否存在**（KRW：fd→fileproc→fglob
   或用 `vp` 反查）。若 NULL → 走匿名路径还是成功，则另有原因。
4. dyld 侧给所有 file sms 的 `+44 |= 0x10`（强制匿名 enter）：patch 点是
   sms copy 循环 `0x35640 STR Q0,[X16,#0x20]` 之后对 `[X16,#0x2C]` OR 0x10
   ——NOP 填充 cave 在 `0x38d08`（44B 够用，参考 `e5cave` 现成模板）。
   若匿名路径成功 ⇒ file 路径（8017E5C）是凶手；若仍 EINVAL ⇒
   失败在 populate 顶部 rec 校验或 setup。
5. **trace 读取**：`vm.shared_region_trace_level=3` 已开，事件走 kdebug。
   找能读 kdebug 的工具（`ktrace`/`kdebug`/`trace`，或
   `log show --predicate 'subsystem == "com.apple.kernel"'`）。
6. 工程替代方案（若 4GB 墙确认无解）：
   a) KRW 数据写 `region+0x40` size=0x140000000 并同步 submap max_offset +
      task map 预留窗口——纯数据写可行但脆；
   b) 只灌 [main]（=4GB 内）放弃 .01 —— libSystem 在主分片，CLI 可用，
      缺 .01 里的框架的 App 会挂；
   c) 退回 13.4 缓存（本就是为此选的小缓存）。
   **先把 [main] 的 EINVAL 解决，再讨论 .01。**

## 5. 设备操作手册（照抄即可）

```sh
export PATH="/var/jb/usr/sbin:/var/jb/usr/bin:/var/jb/sbin:/var/jb/bin:/usr/sbin:/usr/bin:/sbin:/bin:/var/mobile:$PATH"
# 1) 若一切 chroot exec 都 rc=137（exec-veto cascade，当前即此态）：
#    先 bash /var/mobile/restore_env.sh，不行就重启 + 重新越狱激活。
# 2) blob 挂载（每次重启必做，fd 需保持打开——脚本后台挂着别杀）：
/var/mobile/cachereg /var/mnt/rootfs/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld/dyld_shared_cache_arm64e \
                     /var/mnt/rootfs/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld/dyld_shared_cache_arm64e.01 &
# 3) 构建/签名/部署：
cd ~/Desktop/macPad/analysis/dyldwork
python3 build_dyld.py /tmp/OUT.bin crossarch <keys...>     # ⚠️ 必须含 crossarch，否则 SIGSYS
scp OUT.bin → /var/mobile/
ldid -Hsha256 -S/var/jb/usr/macOS/bin/entitlements.plist OUT.bin
python3 /var/mobile/nm/cdhash_slices.py OUT.bin | awk '{print $3}' | while read H; do jbctl trustcache add $H; done
cp OUT.bin /var/mnt/rootfs/usr/lib/dyld && chmod 755 /var/mnt/rootfs/usr/lib/dyld
# 4) 测试：
cd /var/mnt/rootfs && timeout 15 env -i PATH=/usr/bin:/bin \
  DYLD_SHARED_CACHE_DIR=/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld \
  /var/jb/usr/bin/chroot . /bin/echo HELLO
# errno 字节探针：加 e5entry+e5cave，stderr 前 8B little-endian = errno
```

## 6. 血泪清单（每个都炸过，别重蹈）

- **无 crossarch → exec 直接 SIGSYS(140)，零输出**。所有自建变体必须带。
- **ubc+0x38 是锁指针不是 cs_blobs**——写它 = `Invalid/destroyed mutex`
  panic（昨天那次就是我写的）。
- **内核 text kwrite64 会挂死**（PPL）——只能改数据字段，不能 patch 指令。
- **改缓存文件任何字节（含 prot 字段）→ 页哈希不匹配 → SIGKILL(137)**。
  文件在 blob 挂载期间完全不可改。
- 多次 CS 失败 exec 后**全局 exec-veto cascade**：连已知好的 dyld 都 137，
  `restore_env.sh` 或重启恢复。
- `hasexisting`/`prereuse`/`dynoff`/`accessor` 是诊断补丁——**前两个会跳过
  check_np 的 region 绑定语义**；不要进生产配方。
- `.01` VA 越界 4GB region 是结构性的——别为它调 EINVAL 方向。
- 测试命令是 `/bin/echo`（不是 /usr/bin/echo）。
- `jbctl trustcache print` 是错误命令名（应为 `trustcache info`），
  会误导以为 TC 为空。
- dyld 解释器路径 = `/var/mnt/rootfs/usr/lib/dyld`（部署目标）。

## 7. 关键文件

| 路径 | 说明 |
|---|---|
| `analysis/dyldwork/build_dyld.py` | patch key 大全（含 e5 探针/smsdump） |
| `/var/mobile/dyld_plat.bin` | 近原版 dyld（曾推进到 fsignatures 失败点） |
| `/var/mobile/dyld_m1e.bin` | crossarch+filescount1+nodyn+e5（[main]+errno） |
| `/var/mobile/srw5.py` | region 队列遍历（slide=0x241c4000, 带 pop30/stale） |
| `/var/mobile/cachereg` | F_ADDSIGS blob 挂载器（源码 ~Desktop/macPad/misc/） |
| `/var/mobile/post_reboot_final.sh` | 重启恢复全配方 |
| `docs/porting/kernel-syscall536-re-handover.md` | 全部 EINVAL 站点表 |
