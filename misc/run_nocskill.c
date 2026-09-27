// run_nocskill.c — spawn child suspended, clear CS_HARD|CS_KILL in its
// proc_ro->p_csflags via Dopamine KRW, then resume the task and wait.
//
// Build: clang -arch arm64 -isysroot ~/theos/sdks/iPhoneOS16.5.sdk \
//          -miphoneos-version-min=14.0 -O2 -o run_nocskill run_nocskill.c
// Sign : ldid -Hsha256 -Srun_nocskill.entitlements.plist run_nocskill
//        jbctl trustcache add <cdhash>; chmod 755
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <dlfcn.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <mach/mach.h>
#include <mach/arm/thread_state.h>
#include <time.h>

extern char **environ;

/* libjailbreak primitives (loaded lazily via dlopen) */
typedef int      (*jb_checkin_t)(char **, char **, char **, bool *);
typedef int      (*jb_init_t)(void);
typedef uint64_t (*kread64_t)(uint64_t);
typedef uint32_t (*kread32_t)(uint64_t);
typedef int      (*kwrite32_t)(uint64_t, uint32_t);
typedef uint64_t (*proc_self_t)(void);

static kread64_t  kread64;
static kread32_t  kread32;
static kwrite32_t kwrite32;

/* kernel text slide (IDB -> runtime), measured 2026-09-27 */
#define KSLIDE 0x158B4000ULL
/* pidhash table base/mask globals (IDB VAs + slide) */
#define PIDHASH_TBL (0xfffffe00079874D0ULL + KSLIDE)
#define PIDHASH_MSK (0xfffffe00079874D8ULL + KSLIDE)

/* struct proc / proc_ro field offsets (xnu-8792) */
#define PROC_PID   0x60   /* p_pid */
#define PROC_RO    0x18   /* p_proc_ro */
#define PROC_HNEXT 0xA0   /* hash-chain next (p_hash) */
#define RO_TASK    0x08   /* pr_task */
#define RO_CSFLAGS 0x1C   /* p_csflags */

static uint64_t find_proc(pid_t pid)
{
    uint64_t table = kread64(PIDHASH_TBL);
    uint64_t mask  = kread64(PIDHASH_MSK);
    uint64_t cur   = kread64(table + (mask & (uint64_t)pid) * 8);
    for (int hops = 0; cur && hops < 512; hops++) {
        if (kread32(cur + PROC_PID) == (uint32_t)pid && kread64(cur + PROC_RO) != 0)
            return cur;
        cur = kread64(cur + PROC_HNEXT);
    }
    return 0;
}

/* walk child vm map incl. submap internals (for dynregion/shared-cache check) */
static void dump_vm(mach_port_t tp)
{
    vm_address_t addr = 0;
    int n = 0;
    printf("[vmmap] walking child task map:\n");
    for (;;) {
        vm_size_t size = 0;
        vm_region_basic_info_data_64_t info;
        mach_msg_type_number_t cnt = VM_REGION_BASIC_INFO_COUNT_64;
        mach_port_t obj = MACH_PORT_NULL;
        kern_return_t kr = vm_region_64(tp, (vm_address_t *)&addr,
            (vm_size_t *)&size, VM_REGION_BASIC_INFO_64,
            (vm_region_info_t)&info, &cnt, &obj);
        if (kr != KERN_SUCCESS) { printf("[vmmap] kr=%d at %llx\n", kr, (unsigned long long)addr); break; }
        printf("[vm] %llx-%llx prot=%x/%x sh=%d off=%llx\n",
               (unsigned long long)addr,
               (unsigned long long)(addr + size),
               info.protection, info.max_protection, info.shared,
               (unsigned long long)info.offset);
        if (++n > 200) { printf("[vmmap] ...\n"); break; }
        addr += size;
    }
    printf("[vmmap] done\n");
}

static int init_jb(void)
{
    void *h = dlopen("/var/jb/basebin/libjailbreak.dylib", RTLD_NOW);
    if (!h) { fprintf(stderr, "dlopen: %s\n", dlerror()); return -1; }
    jb_checkin_t checkin = (jb_checkin_t)dlsym(h, "jbclient_process_checkin");
    jb_init_t    init    = (jb_init_t)   dlsym(h, "jbclient_initialize_primitives");
    kread64  = (kread64_t) dlsym(h, "kread64");
    kread32  = (kread32_t) dlsym(h, "kread32");
    kwrite32 = (kwrite32_t)dlsym(h, "kwrite32");
    if (!checkin || !init || !kread64 || !kread32 || !kwrite32) {
        fprintf(stderr, "dlsym missing primitive\n");
        return -1;
    }
    char *a = NULL, *b = NULL, *c = NULL; bool d = false;
    checkin(&a, &b, &c, &d);
    init();
    return 0;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc < 2) {
        fprintf(stderr, "usage: %s <binary> [args...]\n", argv[0]);
        return 1;
    }
    if (init_jb()) return 1;

    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_START_SUSPENDED);

    pid_t pid = 0;
    int rc = posix_spawn(&pid, argv[1], NULL, &attr, &argv[1], environ);
    if (rc) { fprintf(stderr, "posix_spawn: %s\n", strerror(rc)); return 1; }
    printf("[*] spawned pid=%d (suspended)\n", pid);

    uint64_t proc = find_proc(pid);
    if (!proc) {
        fprintf(stderr, "[!] proc not found for pid %d — killing\n", pid);
        kill(pid, SIGKILL);
        return 1;
    }
    uint64_t ro    = kread64(proc + PROC_RO);
    uint32_t flags = kread32(ro + RO_CSFLAGS);
    uint32_t want  = flags & ~0x300U;      /* CS_HARD|CS_KILL */
    kwrite32(ro + RO_CSFLAGS, want);
    uint32_t got   = kread32(ro + RO_CSFLAGS);
    printf("[*] proc=%llx ro=%llx csflags %08x -> %08x\n",
           (unsigned long long)proc, (unsigned long long)ro, flags, got);

    /* resume: task-level suspend needs task_resume on the task port */
    mach_port_name_t tp = MACH_PORT_NULL;
    kern_return_t kr = task_for_pid(mach_task_self(), pid, &tp);
    printf("[*] task_for_pid kr=%d port=%u\n", kr, tp);
    if (kr == KERN_SUCCESS) {
        kr = task_resume(tp);
        printf("[*] task_resume kr=%d\n", kr);
    } else {
        fprintf(stderr, "[!] task_for_pid denied — child left suspended\n");
        kill(pid, SIGKILL);
        return 1;
    }

    /* exception-port listener: capture the child's fatal exception
     * (EXC_BAD_ACCESS etc) — gives us PC + fault address that chroot
     * crash-reporter never logs. Reply KERN_FAILURE so normal handling
     * continues (CrashReporter still sees it).
     * NOTE: task_set_exception_ports on another task gets the PARENT
     * SIGKILL'd by AMFI on this device — opt-in via MACWS_EXC=1 only. */
    mach_port_t excp = MACH_PORT_NULL;
    int want_exc = (getenv("MACWS_EXC") != NULL);
    if (want_exc) {
        mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &excp);
        kern_return_t exr = task_set_exception_ports(tp,
            EXC_MASK_BAD_ACCESS | EXC_MASK_BAD_INSTRUCTION | EXC_MASK_ARITHMETIC |
            EXC_MASK_EMULATION | EXC_MASK_SOFTWARE | EXC_MASK_BREAKPOINT,
            excp, EXCEPTION_STATE | MACH_EXCEPTION_CODES, ARM_THREAD_STATE64);
        printf("[*] task_set_exception_ports kr=%d\n", exr);
    }

    struct __attribute__((aligned(8))) {
        mach_msg_header_t hdr;
        mach_msg_body_t   body;
        mach_msg_port_descriptor_t thread;
        mach_msg_port_descriptor_t task;
        NDR_record_t      ndr;
        exception_type_t  exctype;
        mach_msg_type_number_t codeCnt;
        int64_t           code[2];
        int               flavor;
        mach_msg_type_number_t stCnt;
        arm_thread_state64_t st;
        char              pad[512];
    } excmsg;
    struct { mach_msg_header_t hdr; NDR_record_t ndr; kern_return_t ret; char pad[256]; } excrsp;
    int got_exc = 0;

    /* CS flags watchdog: any later execve in the child (e.g. `chroot` ->
     * execve(/bin/echo)) re-ORs 0x300 into p_csflags inside the kernel,
     * strictly BEFORE the new image's first userspace fault.  dyld's own
     * pages are signed-valid, so the first *invalid* fault is ~ms out —
     * re-clear on every observed set, racing at ~20us granularity. */
    int st = 0, rearm = 0;
    int dumped = 0, abrt = 0;
    struct timespec t0, now;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (;;) {
        clock_gettime(CLOCK_MONOTONIC, &now);
        double el = (now.tv_sec - t0.tv_sec) + (now.tv_nsec - t0.tv_nsec) / 1e9;
        uint64_t p2 = find_proc(pid);
        if (p2) {
            uint64_t ro2 = kread64(p2 + PROC_RO);
            if (ro2) {
                uint32_t f = kread32(ro2 + RO_CSFLAGS);
                if (f & 0x300) {
                    kwrite32(ro2 + RO_CSFLAGS, f & ~0x300U);
                    if (++rearm < 8)
                        printf("[*] re-clear csflags %08x -> %08x\n",
                               f, kread32(ro2 + RO_CSFLAGS));
                }
            }
        }
        /* nonblocking exception receive */
        if (want_exc) {
        memset(&excmsg, 0, sizeof(excmsg));
        kern_return_t mr = mach_msg(&excmsg.hdr,
            MACH_RCV_MSG | MACH_RCV_TIMEOUT, 0, sizeof(excmsg),
            excp, 0, MACH_PORT_NULL);
        if (mr == MACH_MSG_SUCCESS && !got_exc) {
            got_exc = 1;
            printf("[exc] type=%d code={%llx,%llx} pc=%llx lr=%llx sp=%llx x0=%llx x8=%llx x16=%llx\n",
                   excmsg.exctype,
                   (unsigned long long)excmsg.code[0],
                   (unsigned long long)excmsg.code[1],
                   (unsigned long long)arm_thread_state64_get_pc(excmsg.st),
                   (unsigned long long)excmsg.st.__lr,
                   (unsigned long long)excmsg.st.__sp,
                   (unsigned long long)excmsg.st.__x[0],
                   (unsigned long long)excmsg.st.__x[8],
                   (unsigned long long)excmsg.st.__x[16]);
            excrsp.hdr.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_MOVE_SEND_ONCE, 0);
            excrsp.hdr.msgh_size = sizeof(excrsp.hdr) + sizeof(NDR_record_t) + sizeof(kern_return_t);
            excrsp.hdr.msgh_remote_port = excmsg.hdr.msgh_remote_port;
            excrsp.hdr.msgh_local_port = MACH_PORT_NULL;
            excrsp.hdr.msgh_id = excmsg.hdr.msgh_id + 100;
            excrsp.ndr = excmsg.ndr;
            excrsp.ret = KERN_FAILURE;
            mach_msg_send(&excrsp.hdr);
        }
        }
        if (waitpid(pid, &st, WNOHANG) == pid) break;
        usleep(20);
        if (!dumped && el >= 2.0) {   /* ~2s: child may be spinning in probe cave */
            dumped = 1;
            dump_vm(tp);
            /* enumerate child threads -> run_state (4=uninterruptible) + pc */
            thread_act_array_t ths = NULL;
            mach_msg_type_number_t nth = 0;
            kern_return_t tkr = task_threads(tp, &ths, &nth);
            if (tkr == KERN_SUCCESS) {
                for (unsigned i = 0; i < nth; i++) {
                    thread_basic_info_data_t bi;
                    mach_msg_type_number_t cnt = THREAD_BASIC_INFO_COUNT;
                    if (thread_info(ths[i], THREAD_BASIC_INFO,
                                    (thread_info_t)&bi, &cnt) == KERN_SUCCESS)
                        printf("[thr%u] run_state=%d flags=0x%x suspend=%d sleep=%d\n",
                               i, bi.run_state, bi.flags, bi.suspend_count, bi.sleep_time);
                    arm_thread_state64_t stt;
                    cnt = ARM_THREAD_STATE64_COUNT;
                    if (thread_get_state(ths[i], ARM_THREAD_STATE64,
                                         (thread_state_t)&stt, &cnt) == KERN_SUCCESS)
                        printf("[thr%u] pc=%llx sp=%llx fp=%llx\n", i,
                               (unsigned long long)arm_thread_state64_get_pc(stt),
                               (unsigned long long)stt.__sp,
                               (unsigned long long)stt.__fp);
                    mach_port_deallocate(mach_task_self(), ths[i]);
                }
                vm_deallocate(mach_task_self(), (vm_address_t)ths, nth * sizeof(ths[0]));
            } else printf("[thr] task_threads kr=%d\n", tkr);
        }
        if (el >= 45.0 && !abrt) { /* request crash report first */
            abrt = 1;
            printf("[*] sending SIGABRT for crash report\n");
            kill(pid, SIGABRT);
        }
        if (el >= 48.0) {  /* kill the spinner */
            printf("[*] killing spinner\n");
            kill(pid, SIGKILL);
        }
        if (el > 52.0) break;
    }
    if (WIFSIGNALED(st))  printf("[+] child SIGNALED %d (re-clears=%d)\n", WTERMSIG(st), rearm);
    else                  printf("[+] child exited rc=%d (re-clears=%d)\n", WEXITSTATUS(st), rearm);
    return 0;
}
