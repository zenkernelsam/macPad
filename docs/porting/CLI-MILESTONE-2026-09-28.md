# CLI Milestone — rootfs macOS 命令行打通 (2026-09-28)

## 验收结果（本 boot 实测）

| 命令 | 结果 | 证据 |
|---|---|---|
| `chroot . /bin/echo HELLO` | ✅ `HELLO` rc=0 | shim 符号足够 |
| `chroot . /bin/cat /etc/shells` | ✅ 完整文件内容 | 修复 `fileno`/`fread` 后 |
| `chroot . /bin/cat /etc/aliases` | ✅ | 483B 文件全量输出 |
| `chroot . /bin/sh -c "echo VIA_SH"` | ✅ `VIA_SH` rc=0 | trampoline→execv→msh 全链通 |
| `chroot . /bin/sh -c "echo A;/bin/echo C;echo B"` | ✅ `A C B` | 外部命令 fork+execve 可用 |
| `chroot . /bin/sh -c "cat /etc/shells"` | ✅ 内容输出 | 子命令文件 IO 正常 |
| `chroot . /bin/sh -c "echo x>/tmp/f;cat /tmp/f"` | ✅ `x` | `>` 重定向生效 |
| `chroot . /bin/sh -c "cd /usr/bin;pwd"` | ✅ `/usr/bin` | cd/pwd（msh 内部 cwd 跟踪） |
| `chroot . /bin/date` | ✅ rc=0 | 符号齐了，输出为 `%+`（strftime 默认格式解析小毛病，未深挖） |
| `chroot . /bin/sh -c "cat < file"` | ⚠️ 空输出 rc=0 | `<` 重定向对 cat 无量产（待查） |
| 管道 `a | b` | ❌ | msh 未实现 pipe（`|` 被当成文件名） |
| `/bin/ls` | ❌ | 还需 `libutil.dylib`+`libncurses.5.4.dylib` 桩 + ~40 符号 |
| `/bin/bash` | ❌ | 需 libncurses + 完整 libc |
| 536 灌缓存 | ❌ 仍 EINVAL | 子进程从未成功映射 macOS 缓存 → 走磁盘 shim |

## 当前架构事实

- **libSystem 永远来自磁盘 shim**（邻居静态结论：`gate1=DyldCache+0xA8` 只看 `DYLD_*`
  path-override env；`gate2=Security+0x1A` AMFI bit9 本机为 0；launchd+chroot 下无 path
  override ⇒ 恒走磁盘 shim）。子进程日志每次都有
  `dyld cache '(null)' not loaded: syscall to map cache into shared region failed`。
- shim 当前 uuid `DB301AD9`（arm64e slice；重构建后会变）。
- 判别 shim/缓存：`DYLD_PRINT_LIBRARIES=1`，libSystem uuid = shim 当前 uuid（非 `D161E41A`）。

## 关键修复记录（本轮新增）

1. **静态 Mach-O 不能 exec**：`-static` 无 dylinker 的 arm64e 可执行文件被内核 veto（SIGKILL 137，
   posix_spawn 报 Bad executable）。p536probe 历史上也从未裸跑成功（此前 `rc=0` 是 `|head` 掩码假象）。
   ⇒ msh 必须动态链接（强制 dep 到 shim：`-Wl,-u,_exit` + 链接 `libSystem.B.dylib`）。
2. **LC_MAIN 入口参数在寄存器**：dyld 以函数调用方式进入 `_start(argc,argv,envp,apple)`
   （x0-x3），不是 stack 布局。`mov sp` 读的是垃圾。
3. **Darwin arm64 fork ABI**：`SYS_fork` 双返回值 — 父 `(x0=childpid,x1=0)`，子 `(x0=childpid,x1=1)`。
   x1==1 判子进程（不是 x0==0）。之前的 `s1(SYS_fork)` 取 x0 → 父子都拿 pid → 子从不走 child 分支。
4. **`__stdoutp`/`__stderrp`/`__stdinp` 不能是 NULL**：cat 用 `fileno(stdout)` 拿 fd 再 `write()`，
   NULL→0→写进 stdin。现在直接编码 fd 值：`__stdoutp=(FILE*)1, __stderrp=(FILE*)2, __stdinp=0`；
   `fileno(f)=(int)f`；stdio 函数用 `_fdof(f,dflt)` 解析。
5. **`fread/getc/fwrite` 必须走 FILE* 对应的真实 fd**（fdopen 返回 fd-as-FILE*）。
6. **`fstat` 走真 syscall 339**（arm64 上即 INODE64 布局）；`_fstat$INODE64/_stat/_stat$INODE64/_lstat$INODE64`
   用 `__asm__("_sym$INODE64")` 别名导出。
7. **macOS `/bin/sh` 是 trampoline**：readlink `/private/var/select/sh` → 白名单校验
   {bash,csh,dash,ksh,sh,tcsh,zsh} → `execv("/bin/<name>")`。**自定义 shell 名会被拒绝回落到 /bin/bash**。
   做法：select/sh → `/bin/dash`，并把 `/bin/dash` 内容替换为 msh（原 dash 备份 `dash.orig`）。

## msh（`tmp/shim/msh.c`）

- 零外部符号（除强制 `-u _exit` 挂 shim 依赖以走 dyld 通道）。
- `-c "cmd"`、脚本文件、交互 REPL（`msh$ `）。
- 内建：`cd pwd echo env exit`；外部命令按 `/bin:/usr/bin:/sbin:/usr/sbin` 搜索，fork+execve+wait4。
- 重定向：`>` `>>` `<` `2>`（内建也支持，用 dup 保存恢复）。
- 分号分隔多命令。**无 pipe/`&&`/引号转义/变量展开/job control**。

## Shim 符号面（`tmp/shim/libSystem_shim.c`，~170 导出）

- 原有 echo/cat/sh 10+41+10 集合（raw syscall 实现）。
- 本轮新增：`fstat`真实现、`fstat$INODE64`/`stat`/`stat$INODE64`/`lstat$INODE64` 别名、
  `strlcat_chk`/`strlcpy_chk`/`stpcpy_chk`、`strftime`(UTC 子集)、`localtime`(UTC civil_from_days)、
  `mktime`、`strptime` 简化、`gettimeofday`(syscall 116)、`clock_gettime`、`time_`、
  `printf`/`snprintf`/`vsnprintf`/`asprintf`(`_fmtcore` 引擎)、`puts`、`abort`、`errx`、
  `atoi/strtoq/strtoul/strspn/strcspn/strchr/strstr/strpbrk/strcpy/strdup/strncasecmp/strcasecmp/strcoll/strncpy系`、
  `memcpy/memmove/memset/memchr/bzero/memset_pattern16?`(见源码)、`malloc_type_*`、`reallocf`、
  `fork/vfork/dup2/pipe/waitpid/execve/kill/killpg/raise/getpid/getppid/getuid/geteuid/getgid/getegid/
   chdir/umask/ioctl/isatty`、`signal/sigaction/sigprocmask/sigsetmask/sigsuspend`(桩)、
  `compat_mode/getlogin/setenv/unsetenv/nl_langinfo/sysctlbyname/wcwidth/mbrtowc/
   user_from_uid/group_from_gid/getbsize/getxattr/listxattr/uuid_unparse_upper/strmode/
   realpath_chk/__assert_rtn`、acl_* 桩、`fts_*` 桩、`getopt_long`、`pututxline`、`syslog$EX ext` 桩、
  `ungetc`、`setvbuf`、`fclose`(真 close)、`humanize_number` 在 shim 内（libutil 需求仍缺独立 dylib）。
- 调试开关：`-DSHIM_DEBUG` 时 `DBG('X')` 打字母流到 fd2（open=O/P,read=r+数字,fstat=S,fwrite=W,
  getc=G,feof=e,close=c,fdopen=d,fcntl=F,setbuf=b,write=w）。

## 构建/部署配方（严格顺序）

```bash
# host 构建（产出 fat arm64+arm64e）
cd ~/Desktop/macPad/tmp/shim && bash build_shim.sh
# → libSystem.B.dylib  libdyld.dylib  msh

scp libSystem.B.dylib msh root@192.168.64.1:/var/mobile/

# device（PATH 前缀略）— FS 写 → 签名 → TC（cachereg 与 shim 无关，顺序安全）
R=/var/mnt/rootfs
cp /var/mobile/libSystem.B.dylib $R/usr/lib/libSystem.B.dylib
cp /var/mobile/msh $R/bin/msh; cp /var/mobile/msh $R/bin/dash   # dash 被 msh 覆盖
[ -f $R/bin/dash.orig ] || cp $R/bin/dash.orig 备份已存在
chmod 755 ...
for f in $R/usr/lib/libSystem.B.dylib $R/bin/msh $R/bin/dash; do
  /var/jb/usr/bin/ldid -Hsha256 -S/var/jb/usr/macOS/bin/entitlements.plist $f
  for H in $(python3 /var/mobile/nm/cdhash_slices.py $f|awk '{print $NF}'); do
    /var/jb/basebin/jbctl trustcache add $H; done; done
ln -sfn /bin/dash $R/private/var/select/sh
```

测试：

```bash
cd $R
timeout 15 env -i PATH=/usr/bin:/bin chroot . /bin/sh -c "echo ok; cat /etc/shells" </dev/null
```

## 遗留问题（下一步）

- **536 对子进程仍 EINVAL** → macOS 缓存从没灌进 chroot region。邻居静态结论显示当前
  exec 路径恒走磁盘 shim；要在缓存路径跑通需要另行解 populate（或接受 shim 路线把 libc 补齐）。
- `ls`：还需 `libutil.dylib`/`libncurses.5.4.dylib` 两个桩 dylib（写最小导出即可）+ ls 的 ~40
  个剩余 libc 符号（`fts_*` 若做真实现才能列目录；当前桩=列不出东西但不崩）。
- `date` rc=0 但输出 `%+`：它的默认 fmt 串处理细节问题。
- `cat < file`（stdin 重定向）空输出——child dup2 后 cat 的 stdin 路径待查。
- msh 无管道/变量/`&&`。
- 真 `dash`/`bash`/`zsh`：需要几乎完整 libc + libncurses + libutil —— 若继续做真 shell，
  性价比最高的路径是**解决 536**让真 macOS libSystem 从缓存加载，而不是无限扩 shim。
