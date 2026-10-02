// run_dbg.c — spawn child suspended, mark it CS_DEBUGGED via jbctl
// (jailbreakd path — does NOT need libjailbreak KRW primitives), then
// task_resume and wait. Fallback for run_nocskill when KRW is broken.
// Now also installs an exception port so child EXC_* events dump the
// faulting thread's PC + mach_exception_data (fault addr / guard code).
//
// Build: clang -arch arm64 -isysroot ~/theos/sdks/iPhoneOS16.5.sdk \
//          -miphoneos-version-min=14.0 -O2 -o run_dbg run_dbg.c
// Sign : ldid -Hsha256 -Srun_nocskill.entitlements.plist run_dbg
//        jbctl trustcache add <cdhash>; chmod 755
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <spawn.h>
#include <pthread.h>
#include <dlfcn.h>
#include <sys/wait.h>
#include <unistd.h>
extern int ptrace(int _request, pid_t _pid, void *_addr, int _data);
#ifndef PT_ATTACH
#define PT_ATTACH 10
#endif
#ifndef PT_CONTINUE
#define PT_CONTINUE 7
#endif
#include <signal.h>
#include <errno.h>
#include <mach/mach.h>
#include <mach/arm/thread_state.h>

/* newer SDKs make arm_thread_state64_t opaque; define a plain-layout
 * compat type and alias it for THIS file only. */
typedef struct {
    uint64_t __x[29];
    uint64_t __fp, __lr, __sp, __pc;
    uint32_t __cpsr;
    uint32_t __pad;
} arm_ts64_compat_t;
typedef struct {
    uint32_t __exception;
    uint32_t __esr;
    uint64_t __far;
} arm_es64_compat_t;
typedef struct {
    uint32_t __pagein_error;
} arm_pi_compat_t;
#define arm_thread_state64_t     arm_ts64_compat_t
#define arm_exception_state64_t  arm_es64_compat_t
#define arm_pagein_state_t       arm_pi_compat_t

/* mach_vm_region not exported on iOS SDK — declare manually;
 * vm_region.h (via mach.h) already provides the info structs/macros */
typedef unsigned long long mach_vm_address_t;
typedef unsigned long long mach_vm_size_t;
extern kern_return_t mach_vm_region(vm_map_t, mach_vm_address_t *,
    mach_vm_size_t *, vm_region_flavor_t, vm_region_info_t,
    mach_msg_type_number_t *, mach_port_t *);
extern kern_return_t mach_vm_read_overwrite(vm_map_t, mach_vm_address_t,
    mach_vm_size_t, mach_vm_address_t, mach_vm_size_t *);

extern char **environ;
extern int csops(pid_t, unsigned int, void *, size_t);

static mach_port_t g_exc_port = MACH_PORT_NULL;
static pid_t g_child_pid;
static int g_traced = 0;

/* RUN_DBG_CSUNKILL poller state — direct kread/kwrite of proc_ro->p_csflags
 * (proc_getcsflags reads 0 post-exec; the field is at proc_ro+0x1C). */
static pid_t g_unkill_pid;
static uint64_t (*g_pf)(int);
static uint64_t (*g_kr64)(uint64_t);
static uint32_t (*g_kr32)(uint64_t);
static int (*g_kw32)(uint64_t, uint32_t);

#define PROC_RO    0x18
#define RO_CSFLAGS 0x1C

static void *unkill_thread(void *arg)
{
    (void)arg;
    uint64_t proc = 0, ro = 0;
    uint32_t last = 0xffffffff;
    int z = 0;
    struct timespec t0; clock_gettime(CLOCK_MONOTONIC, &t0);
    for (;;) {                          /* keep clearing CS_KILL for ~45s */
        struct timespec t1; clock_gettime(CLOCK_MONOTONIC, &t1);
        if (t1.tv_sec - t0.tv_sec > 45) break;
        if (!proc) { proc = g_pf(g_unkill_pid); if (!proc) { usleep(500); continue; } }
        ro = g_kr64(proc + PROC_RO);           /* exec swaps proc_ro — re-read */
        if (ro & ~0x7fffffffffffULL)           /* PAC tag in top bits -> strip */
            ro = 0xffff800000000000ULL | (ro & 0x7fffffffffffULL);
        if (!ro) { usleep(50); continue; }
        uint32_t f = g_kr32(ro + RO_CSFLAGS);
        if (f != last) {
            fprintf(stderr, "[unkill] csflags %#x -> %#x\n", last, f);
            last = f;
        }
        if (f & 0x00000300u) {          /* CS_HARD|CS_KILL */
            g_kw32(ro + RO_CSFLAGS, f & ~0x00000300u);
            fprintf(stderr, "[unkill] cleared 0x300 csflags %#x\n", f);
        }
        if (f == 0) { if (++z > 2000000) break; continue; } else z = 0;
        usleep(20);
    }
    fprintf(stderr, "[unkill] done\n");
    return NULL;
}

static void print_extended(vm_map_t task, mach_vm_address_t where, const char *phase)
{
    mach_vm_address_t address = where;
    mach_vm_size_t size = 0;
    vm_region_extended_info_data_t info;
    mach_msg_type_number_t count = VM_REGION_EXTENDED_INFO_COUNT;
    mach_port_t object = MACH_PORT_NULL;
    kern_return_t kr = mach_vm_region(task, &address, &size, VM_REGION_EXTENDED_INFO,
                                      (vm_region_info_t)&info, &count, &object);
    if (kr == KERN_SUCCESS)
        fprintf(stderr, "[%s] 0x%llx..0x%llx tag=%u resident=%u external=%u shadow=%u mode=%u ref=%u\n",
                phase, address, address + size, info.user_tag, info.pages_resident,
                info.external_pager, info.shadow_depth, info.share_mode, info.ref_count);
    else
        fprintf(stderr, "[%s] region kr=%d requested=0x%llx\n", phase, kr, where);
    if (object != MACH_PORT_NULL) mach_port_deallocate(mach_task_self(), object);
}

/* exception_raise msgid 2405 layout (EXCEPTION_DEFAULT|MACH_EXCEPTION_CODES):
 *   header(24) | body_cnt(4) | thread_desc(12) | task_desc(12)
 *   NDR(8) | exception(4) | pad(4) | code[2](16)
 */
static void *exc_thread(void *arg)
{
    union { char b[0x600]; mach_msg_header_t h; } req, rep;
    for (;;) {
        memset(&req, 0, sizeof req);
        kern_return_t kr = mach_msg(&req.h, MACH_RCV_MSG, 0, sizeof req,
                                  g_exc_port, 0, MACH_PORT_NULL);
        if (kr) { fprintf(stderr, "[exc] mach_msg rcv kr=%d\n", kr); continue; }
        fprintf(stderr, "[exc] exception msg id=%d size=%u\n",
                req.h.msgh_id, req.h.msgh_size);

        unsigned char *p = req.b;
        unsigned body_cnt; memcpy(&body_cnt, p + 24, 4);
        unsigned off = 24 + 4 + body_cnt * 12;
        /* descriptors: mach_msg_port_descriptor_t, port NAME at +0 */
        mach_port_t thr = MACH_PORT_NULL, tsk = MACH_PORT_NULL;
        if (body_cnt >= 1) memcpy(&thr, p + 24 + 4 + 0, 4);
        if (body_cnt >= 2) memcpy(&tsk, p + 24 + 4 + 12 + 0, 4);
        unsigned exception = 0;
        unsigned long long code0 = 0, code1 = 0;
        memcpy(&exception, p + off + 8, 4);
        memcpy(&code0, p + off + 16, 8);
        memcpy(&code1, p + off + 24, 8);
        fprintf(stderr, "[exc] type=%u code0=0x%llx code1=0x%llx thr=%u tsk=%u\n",
                exception, code0, code1, thr, tsk);
        unsigned flags = 0;
        int cs_rc = csops(g_child_pid, 0, &flags, sizeof(flags));
        fprintf(stderr, "[exc] csops pid=%d rc=%d flags=0x%x errno=%d\n",
                g_child_pid, cs_rc, flags, cs_rc ? errno : 0);
        if (tsk != MACH_PORT_NULL && code1) print_extended(tsk, code1, "vmext");

        if (thr != MACH_PORT_NULL) {
            arm_thread_state64_t st;
            mach_msg_type_number_t cnt = ARM_THREAD_STATE64_COUNT;
            if (thread_get_state(thr, ARM_THREAD_STATE64,
                                 (thread_state_t)&st, &cnt) == 0) {
                fprintf(stderr,
                    "[exc] pc=0x%llx lr=0x%llx sp=0x%llx cpsr=0x%x\n",
                    st.__pc, st.__lr, st.__sp, st.__cpsr);
                fprintf(stderr, "[exc] x0=%llx x1=%llx x2=%llx x3=%llx\n",
                    st.__x[0], st.__x[1], st.__x[2], st.__x[3]);
                fprintf(stderr, "[exc] x4=%llx x5=%llx x16=%llx x30=lr\n",
                    st.__x[4], st.__x[5], st.__x[16]);
                for (int i = 0; i < 28; i += 4)
                    fprintf(stderr, "[exc] x%-2d=%016llx x%-2d=%016llx x%-2d=%016llx x%-2d=%016llx\n",
                            i, st.__x[i], i + 1, st.__x[i + 1],
                            i + 2, st.__x[i + 2], i + 3, st.__x[i + 3]);
                fprintf(stderr, "[exc] x28=%016llx fp=%016llx\n",
                        st.__x[28], st.__fp);
#ifdef ARM_EXCEPTION_STATE64
                {
                    arm_exception_state64_t es;
                    mach_msg_type_number_t ecnt = ARM_EXCEPTION_STATE64_COUNT;
                    kern_return_t ekr = thread_get_state(thr, ARM_EXCEPTION_STATE64,
                        (thread_state_t)&es, &ecnt);
                    if (ekr == 0)
                        fprintf(stderr, "[exc] exc_state far=0x%llx esr=0x%x exception=%u\n",
                                es.__far, es.__esr, es.__exception);
                    else
                        fprintf(stderr, "[exc] exception_state kr=%d\n", ekr);
                }
#endif
#ifdef ARM_PAGEIN_STATE
                {
                    arm_pagein_state_t ps;
                    mach_msg_type_number_t pcnt = ARM_PAGEIN_STATE_COUNT;
                    kern_return_t pkr = thread_get_state(thr, ARM_PAGEIN_STATE,
                        (thread_state_t)&ps, &pcnt);
                    if (pkr == 0)
                        fprintf(stderr, "[exc] pagein_state pagein_error=%d\n", ps.__pagein_error);
                    else
                        fprintf(stderr, "[exc] pagein_state kr=%d\n", pkr);
                }
#endif
                /* map the exact faulting PC: prove (or refute) the dyld
                 * load-base inference behind the recorded offset */
                if (tsk != MACH_PORT_NULL) {
                    mach_vm_address_t a = st.__pc;
                    mach_vm_size_t sz = 0;
                    struct vm_region_basic_info_64 info;
                    mach_port_t obj = MACH_PORT_NULL;
                    mach_msg_type_number_t ic = VM_REGION_BASIC_INFO_COUNT_64;
                    kern_return_t vr = mach_vm_region(tsk, &a, &sz,
                        VM_REGION_BASIC_INFO_64, (vm_region_info_t)&info,
                        &ic, &obj);
                    if (vr == KERN_SUCCESS) {
                        int inside = sz != 0 && st.__pc >= a &&
                                     st.__pc - a < sz;
                        fprintf(stderr,
                            "[pcmap] req_pc=0x%llx entry=0x%llx..0x%llx prot=%x/%x off=0x%llx inside=%d\n",
                            st.__pc, a, a + sz, info.protection,
                            info.max_protection, info.offset, inside);
                        if (inside && (info.protection & VM_PROT_READ) &&
                            (info.protection & VM_PROT_EXECUTE)) {
                            unsigned char buf[32];
                            mach_vm_address_t rs = st.__pc >= 16 ? st.__pc - 16 : st.__pc;
                            if (rs < a) rs = a;
                            mach_vm_size_t want = sz - (rs - a);
                            if (want > sizeof buf) want = sizeof buf;
                            mach_vm_size_t got = 0;
                            kern_return_t rk = mach_vm_read_overwrite(tsk, rs,
                                want, (mach_vm_address_t)buf, &got);
                            fprintf(stderr, "[pcmap] read @0x%llx kr=%d len=%llu\n",
                                    rs, rk, (unsigned long long)got);
                            if (rk == KERN_SUCCESS && got && got <= sizeof buf) {
                                fprintf(stderr, "[pcmap] bytes:");
                                for (mach_vm_size_t i = 0; i < got; i++)
                                    fprintf(stderr, " %02x", buf[i]);
                                fprintf(stderr, "\n");
                            }
                        }
                    } else {
                        fprintf(stderr, "[pcmap] region kr=%d req_pc=0x%llx\n",
                                vr, st.__pc);
                    }
                    if (obj != MACH_PORT_NULL)
                        mach_port_deallocate(mach_task_self(), obj);

                    /* TOP_INFO: does the faulting region hold dirty private
                     * (COW) pages? That decides anon-vs-file CS reject. */
                    {
                        mach_vm_address_t ta = st.__pc & ~0x3fffULL;
                        mach_vm_size_t tsz = 0;
                        vm_region_top_info_data_t ti;
                        mach_msg_type_number_t tc = VM_REGION_TOP_INFO_COUNT;
                        mach_port_t tobj = MACH_PORT_NULL;
                        kern_return_t tr = mach_vm_region(tsk, &ta, &tsz,
                            VM_REGION_TOP_INFO, (vm_region_info_t)&ti,
                            &tc, &tobj);
                        if (tr == KERN_SUCCESS)
                            fprintf(stderr,
                                "[topinfo] 0x%llx..0x%llx private_res=%u shared_res=%u share_mode=%u\n",
                                ta, ta + tsz, ti.private_pages_resident,
                                ti.shared_pages_resident, ti.share_mode);
                        else
                            fprintf(stderr, "[topinfo] kr=%d\n", tr);
                        if (tobj != MACH_PORT_NULL)
                            mach_port_deallocate(mach_task_self(), tobj);
                    }
                }
            } else fprintf(stderr, "[exc] thread_get_state failed\n");
        }

        /* walk vm regions around the fault address in the CHILD task */
        if (tsk != MACH_PORT_NULL && code1) {
            mach_vm_address_t va = code1 & ~0x3fffULL;
            for (int i = 0; i < 40; i++) {
                mach_vm_size_t sz = 0;
                struct vm_region_basic_info_64 info;
                mach_port_t obj = 0;
                mach_msg_type_number_t ic = VM_REGION_BASIC_INFO_COUNT_64;
                mach_vm_address_t a = va;
                kern_return_t vr = mach_vm_region(tsk, &a, &sz,
                    VM_REGION_BASIC_INFO_64, (vm_region_info_t)&info,
                    &ic, &obj);
                if (vr) { fprintf(stderr, "[vm] hole at 0x%llx\n", va);
                          va += 0x4000; if (va > code1+0x80000) break; continue; }
                fprintf(stderr,
                    "[vm] 0x%llx..0x%llx prot=%x/%x off=0x%llx shared=%d\n",
                    a, a+sz, info.protection, info.max_protection,
                    info.offset, info.shared);
                va = a + sz;
                if (va > code1 + 0x80000) break;
            }
            /* also dump regions around dyld base guess (fault page base) */
            mach_vm_address_t lo = (code1 & ~0xfffffULL) - 0x100000;
            mach_vm_address_t a2 = lo;
            fprintf(stderr, "[vm] --- around image base 0x%llx ---\n", lo);
            for (int i = 0; i < 60; i++) {
                mach_vm_size_t sz = 0;
                struct vm_region_basic_info_64 info;
                mach_port_t obj = 0;
                mach_msg_type_number_t ic = VM_REGION_BASIC_INFO_COUNT_64;
                mach_vm_address_t a = a2;
                kern_return_t vr = mach_vm_region(tsk, &a, &sz,
                    VM_REGION_BASIC_INFO_64, (vm_region_info_t)&info,
                    &ic, &obj);
                if (vr) { a2 += 0x4000; if (a2 > lo+0x600000) break; continue; }
                fprintf(stderr,
                    "[vm] 0x%llx..0x%llx prot=%x/%x off=0x%llx\n",
                    a, a+sz, info.protection, info.max_protection,
                    info.offset);
                a2 = a + sz;
                if (a2 > lo + 0x600000) break;
            }
        }

        /* VADIFF_VA/VADIFF_LEN/VADIFF_FILE/VADIFF_OFF: read the child VA while
         * it is frozen at the exception and diff against a file range, to
         * detect whether the faulting page is a dirty COW shadow copy. */
        if (tsk != MACH_PORT_NULL) {
            const char *vva = getenv("VADIFF_VA"), *vln = getenv("VADIFF_LEN"),
                       *vfl = getenv("VADIFF_FILE"), *vfo = getenv("VADIFF_OFF");
            if (vva && vln && vfl && vfo) {
                mach_vm_address_t va = strtoull(vva, 0, 16);
                mach_vm_size_t n = (mach_vm_size_t)strtoull(vln, 0, 0);
                uint64_t fo = strtoull(vfo, 0, 16);
                char *buf = malloc(n);
                mach_vm_size_t got = 0;
                kern_return_t rk = mach_vm_read_overwrite(tsk, va, n,
                    (mach_vm_address_t)buf, &got);
                fprintf(stderr, "[vadiff] read 0x%llx kr=%d out=%llu\n",
                        va, rk, (unsigned long long)got);
                FILE *f = fopen(vfl, "rb");
                if (rk == KERN_SUCCESS && f) {
                    char *want = malloc(n);
                    fseek(f, (long)fo, SEEK_SET);
                    size_t fr = fread(want, 1, n, f);
                    fclose(f);
                    size_t lim = got < fr ? got : fr, nd = 0;
                    for (size_t i = 0; i < lim; i++)
                        if (buf[i] != want[i]) {
                            if (nd < 16)
                                fprintf(stderr, "[vadiff] +%#zx got %02x want %02x\n",
                                        i, (unsigned char)buf[i],
                                        (unsigned char)want[i]);
                            nd++;
                        }
                    fprintf(stderr, "[vadiff] diff bytes: %zu (of %zu)\n", nd, lim);
                    free(want);
                }
                free(buf);
            }
        }

        /* RUN_DBG_HOLD=<sec>: keep the child frozen in exception delivery
         * (no reply) so kernel-side state (vm_map/object chain) can be
         * inspected read-only via KRW from another process. */
        const char *hold = getenv("RUN_DBG_HOLD");
        if (hold) {
            int secs = atoi(hold);
            if (secs > 0) {
                fprintf(stderr, "[exc] holding child for %ds (no reply yet)\n", secs);
                sleep((unsigned)secs);
            }
        }

        /* reply: EXC_GUARD (type 12) is a *notification* raised on the syscall
         * return path (e.g. MAP_FIXED mmap that deallocates a gap) — the
         * syscall already succeeded, so claim KERN_SUCCESS to resume.
         * Everything else gets KERN_FAILURE → normal signal delivery. */
        memset(&rep, 0, sizeof rep);
        rep.h.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_MOVE_SEND_ONCE, 0);
        rep.h.msgh_remote_port = req.h.msgh_remote_port;
        rep.h.msgh_local_port = MACH_PORT_NULL;
        rep.h.msgh_id = req.h.msgh_id + 100;
        rep.h.msgh_size = (mach_msg_size_t)(24 + 8 + 4 + 4);
        /* body starts at +24: NDR(8) then ret_code(4) */
        memcpy(rep.b + 24, req.b + off, 8);
        *(int *)(rep.b + 24 + 8) = (exception == 12) ? KERN_SUCCESS : KERN_FAILURE;
        if (exception == 12)
            fprintf(stderr, "[exc] EXC_GUARD notification -> reply KERN_SUCCESS\n");
        kr = mach_msg(&rep.h, MACH_SEND_MSG, rep.h.msgh_size, 0,
                      MACH_PORT_NULL, 2000, MACH_PORT_NULL);
        if (kr) fprintf(stderr, "[exc] reply kr=%d\n", kr);
    }
    return NULL;
}

static void walk_live_map(mach_port_t task, int stop_count)
{
    const mach_vm_address_t end = 0x2ac760000ULL;
    mach_vm_address_t a = 0x180000000ULL;
    fprintf(stderr, "[live] stop=%d walk 0x%llx..0x%llx\n", stop_count, a, end);
    for (int i = 0; i < 256 && a < end; i++) {
        mach_vm_address_t requested = a;
        mach_vm_size_t sz = 0;
        struct vm_region_basic_info_64 info;
        mach_port_t obj = 0;
        mach_msg_type_number_t ic = VM_REGION_BASIC_INFO_COUNT_64;
        kern_return_t vr = mach_vm_region(task, &a, &sz,
            VM_REGION_BASIC_INFO_64, (vm_region_info_t)&info, &ic, &obj);
        if (vr) {
            fprintf(stderr, "[live] region lookup kr=%d at 0x%llx\n", vr, requested);
            break;
        }
        if (obj != MACH_PORT_NULL) mach_port_deallocate(mach_task_self(), obj);
        if (a > requested)
            fprintf(stderr, "[live] gap 0x%llx..0x%llx\n", requested, a < end ? a : end);
        if (a >= end) break;
        if (!sz || a + sz <= a) {
            fprintf(stderr, "[live] invalid region size at 0x%llx\n", a);
            break;
        }
        fprintf(stderr, "[live] 0x%llx..0x%llx prot=%x/%x off=0x%llx shared=%d resv=%d\n",
                a, a + sz, info.protection, info.max_protection,
                info.offset, info.shared, info.reserved);
        a += sz;
    }
}

int main(int argc, char **argv)
{
    setvbuf(stderr, NULL, _IONBF, 0);
    if (argc < 2) {
        fprintf(stderr, "usage: %s <binary> [args...]\n", argv[0]);
        return 1;
    }

    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_START_SUSPENDED);

    pid_t pid = 0;
    int rc = posix_spawn(&pid, argv[1], NULL, &attr, &argv[1], environ);
    if (rc) { fprintf(stderr, "posix_spawn: %s\n", strerror(rc)); return 1; }
    g_child_pid = pid;
    fprintf(stderr, "[*] spawned pid=%d (suspended)\n", pid);

    char pidstr[16];
    snprintf(pidstr, sizeof pidstr, "%d", pid);
    char *jbargv[] = { "/var/jb/basebin/jbctl", "proc_set_debugged", pidstr, NULL };
    pid_t jp = 0;
    int jrc = posix_spawn(&jp, jbargv[0], NULL, NULL, jbargv, environ);
    if (jrc == 0) { int st; waitpid(jp, &st, 0); jrc = WIFEXITED(st) ? WEXITSTATUS(st) : -1; }
    fprintf(stderr, "[*] jbctl rc=%d\n", jrc);

    mach_port_name_t tp = MACH_PORT_NULL;
    kern_return_t kr = task_for_pid(mach_task_self(), pid, &tp);
    fprintf(stderr, "[*] task_for_pid kr=%d port=%u\n", kr, tp);
    if (kr != KERN_SUCCESS) { kill(pid, SIGKILL); return 1; }

    /* pre-exec snapshot: walk shared-region range of the freshly-spawned
     * (still-suspended, pre-chroot) iOS task */
    if (getenv("RUN_DBG_PREWALK")) {
        mach_vm_address_t a = 0x180000000ULL;
        fprintf(stderr, "[pre] region walk 0x180000000..\n");
        for (int i = 0; i < 40 && a < 0x300000000ULL; i++) {
            mach_vm_size_t sz = 0;
            struct vm_region_basic_info_64 info;
            mach_port_t obj = 0;
            mach_msg_type_number_t ic = VM_REGION_BASIC_INFO_COUNT_64;
            kern_return_t vr = mach_vm_region(tp, &a, &sz,
                VM_REGION_BASIC_INFO_64, (vm_region_info_t)&info, &ic, &obj);
            if (vr) { fprintf(stderr, "[pre] hole@0x%llx\n", a); a += 0x8000000; continue; }
            fprintf(stderr, "[pre] 0x%llx..0x%llx prot=%x/%x off=0x%llx shared=%d\n",
                    a, a+sz, info.protection, info.max_protection,
                    info.offset, info.shared);
            a += sz;
        }
    }

    /* install exception port on the child task */
    kr = mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &g_exc_port);
    if (kr == 0) {
        kr = mach_port_insert_right(mach_task_self(), g_exc_port,
                                  g_exc_port, MACH_MSG_TYPE_MAKE_SEND);
        if (kr == 0) {
            kr = task_set_exception_ports(tp,
                    EXC_MASK_BAD_ACCESS | EXC_MASK_BAD_INSTRUCTION |
                    EXC_MASK_ARITHMETIC | EXC_MASK_EMULATION |
                    EXC_MASK_SOFTWARE | EXC_MASK_BREAKPOINT |
                    EXC_MASK_SYSCALL | EXC_MASK_MACH_SYSCALL |
                    EXC_MASK_RPC_ALERT | EXC_MASK_GUARD | EXC_MASK_CRASH,
                    g_exc_port,
                    EXCEPTION_DEFAULT | MACH_EXCEPTION_CODES,
                    ARM_THREAD_STATE64);
            fprintf(stderr, "[*] set_exc_ports kr=%d\n", kr);
            if (kr == 0) {
                pthread_t t; pthread_create(&t, NULL, exc_thread, NULL);
                pthread_detach(t);
            }
        } else fprintf(stderr, "[*] insert_right kr=%d\n", kr);
    } else fprintf(stderr, "[*] port_alloc kr=%d\n", kr);

    /* RUN_DBG_CSUNKILL=1: poll the child's proc csflags via libjailbreak KRW;
     * as soon as CS_KILL appears (exec applies it), clear KILL+KILLED so a
     * later cs_invalid_page rejection delivers an exception, not SIGKILL. */
    if (getenv("RUN_DBG_PTRACE")) {
        int prc = ptrace(PT_ATTACH, pid, NULL, 0);
        fprintf(stderr, "[*] ptrace(PT_ATTACH) rc=%d errno=%d\n", prc, errno);
        g_traced = (prc == 0);
    }

    if (getenv("RUN_DBG_CSUNKILL")) {
        void *jl = dlopen("/var/jb/basebin/libjailbreak.dylib", RTLD_NOW);
        if (jl) {
            void (*ini)(void) = dlsym(jl, "jbclient_initialize_primitives");
            g_pf   = dlsym(jl, "proc_find");
            g_kr64 = dlsym(jl, "kread64");
            g_kr32 = dlsym(jl, "kread32");
            g_kw32 = dlsym(jl, "kwrite32");
            if (ini && g_pf && g_kr64 && g_kr32 && g_kw32) {
                ini();
                g_unkill_pid = pid;
                pthread_t t;
                pthread_create(&t, NULL, unkill_thread, NULL);
                pthread_detach(t);
            }
        }
    }

    if (g_traced) {
        kr = task_resume(tp);
        int crc = ptrace(PT_CONTINUE, pid, (void *)1, 0);
        fprintf(stderr, "[*] task_resume kr=%d ptrace(PT_CONTINUE) rc=%d errno=%d\n",
                kr, crc, errno);
    } else {
        kr = task_resume(tp);
        fprintf(stderr, "[*] task_resume kr=%d\n", kr);
    }

    int status = 0, stop_count = 0;
    const char *limit_env = getenv("RUN_DBG_STOP_LIMIT");
    int stop_limit = limit_env ? atoi(limit_env) : 0;
    for (;;) {
        waitpid(pid, &status, WUNTRACED);
        if (WIFSTOPPED(status)) {
            int ssig = WSTOPSIG(status);
            fprintf(stderr, "[*] child STOPPED sig=%d\n", ssig);
            if (g_traced) {
                int crc = ptrace(PT_CONTINUE, pid, (void *)1, 0);
                fprintf(stderr, "[*] ptrace(PT_CONTINUE) rc=%d errno=%d\n", crc, errno);
                continue;
            }
            /* sig==0 is the spawn-suspend residue — resume and keep waiting.
             * Our dyld cave stops with SIGSTOP(17)/SIGUSR1(30). */
            if (ssig != SIGSTOP && ssig != SIGUSR1) { kill(pid, SIGCONT); continue; }
            ++stop_count;
            if (getenv("RUN_DBG_LIVEWALK")) {
                mach_port_name_t live = MACH_PORT_NULL;
                kern_return_t live_kr = task_for_pid(mach_task_self(), pid, &live);
                fprintf(stderr, "[live] task_for_pid kr=%d old=%u new=%u\n", live_kr, tp, live);
                if (live_kr == KERN_SUCCESS) {
                    mach_port_deallocate(mach_task_self(), tp);
                    tp = live;
                    unsigned flags = 0;
                    int cs_rc = csops(pid, 0, &flags, sizeof(flags));
                    fprintf(stderr, "[live] csops rc=%d flags=0x%x errno=%d\n",
                            cs_rc, flags, cs_rc ? errno : 0);
                    print_extended(tp, 0x1ee188000ULL, "liveext");
                    walk_live_map(tp, stop_count);
                }
            }
            if (getenv("RUN_DBG_KILLONSTOP") || (stop_limit > 0 && stop_count >= stop_limit)) {
                fprintf(stderr, "[*] killing child\n");
                kill(pid, SIGKILL);
                waitpid(pid, &status, 0);
                fprintf(stderr, "[*] child SIGNALED %d\n", WTERMSIG(status));
                break;
            }
            kill(pid, SIGCONT);
            continue;
        }
        if (WIFEXITED(status))   fprintf(stderr, "[*] child exited rc=%d\n", WEXITSTATUS(status));
        if (WIFSIGNALED(status)) {
            unsigned int cf = 0;
            int crc = csops(pid, 0, &cf, sizeof(cf));
            fprintf(stderr, "[*] child SIGNALED %d csops rc=%d flags=0x%x\n",
                    WTERMSIG(status), crc, cf);
        }
        break;
    }
    return 0;
}
