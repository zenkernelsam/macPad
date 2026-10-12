// Walk each thread's frame chain in a remote task and symbolize PCs
// against the loaded image list.  Needs task_for_pid entitlements.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mach/mach.h>
#include <mach/task_info.h>
#include <mach/thread_status.h>
#include <mach/thread_info.h>
#include <mach-o/dyld_images.h>
#include <mach-o/loader.h>

extern kern_return_t task_for_pid(mach_port_name_t task, pid_t pid,
                                  mach_port_name_t *target);

struct image { unsigned long long base, end, slide; char path[256]; };
static struct image g_images[1024];
static int g_nimages;
static task_t g_task;

static int rmem(unsigned long long addr, void *buf, unsigned size) {
    vm_size_t out = 0;
    return vm_read_overwrite(g_task, addr, size,
                             (vm_address_t)buf, &out) == KERN_SUCCESS
        ? (int)out : -1;
}

static const char *symbolize(unsigned long long pc,
                             unsigned long long *off_out) {
    int best = -1;
    for (int i = 0; i < g_nimages; i++) {
        if (pc >= g_images[i].base && pc < g_images[i].end &&
            (best < 0 || g_images[i].base > g_images[best].base))
            best = i;
    }
    if (best >= 0) {
        *off_out = pc - g_images[best].slide;
        return g_images[best].path;
    }
    return NULL;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <pid> [thread-name]\n",
                            argv[0]); return 2; }
    pid_t pid = (pid_t)atoi(argv[1]);
    const char *want = argc > 2 ? argv[2] : NULL;
    if (task_for_pid(mach_task_self(), pid, &g_task) != KERN_SUCCESS) {
        fprintf(stderr, "task_for_pid failed\n"); return 1;
    }
    struct task_dyld_info dyld;
    mach_msg_type_number_t dcount = TASK_DYLD_INFO_COUNT;
    task_info(g_task, TASK_DYLD_INFO, (task_info_t)&dyld, &dcount);
    struct dyld_all_image_infos infos;
    rmem(dyld.all_image_info_addr, &infos, sizeof(infos));
    struct dyld_image_info *list = calloc(infos.infoArrayCount,
                                          sizeof(*list));
    rmem((unsigned long long)infos.infoArray, list,
         infos.infoArrayCount * sizeof(*list));
    for (unsigned i = 0; i < infos.infoArrayCount && g_nimages < 1024; i++) {
        unsigned long long base =
            (unsigned long long)list[i].imageLoadAddress;
        struct mach_header_64 mh;
        if (rmem(base, &mh, sizeof(mh)) < 0 || mh.magic != MH_MAGIC_64)
            continue;
        unsigned long long end = base, text_vmaddr = 0;
        struct load_command lc;
        unsigned long long cur = base + sizeof(mh);
        for (uint32_t c = 0; c < mh.ncmds; c++) {
            if (rmem(cur, &lc, sizeof(lc)) < 0) break;
            if (lc.cmd == LC_SEGMENT_64) {
                struct segment_command_64 seg;
                if (rmem(cur, &seg, sizeof(seg)) > 0) {
                    if (!text_vmaddr) text_vmaddr = seg.vmaddr;
                    unsigned long long loaded_end =
                        base + (seg.vmaddr - text_vmaddr) + seg.vmsize;
                    if (loaded_end > end) end = loaded_end;
                }
            }
            cur += lc.cmdsize;
        }
        g_images[g_nimages].base = base;
        g_images[g_nimages].end = end;
        g_images[g_nimages].slide = base - text_vmaddr;
        rmem((unsigned long long)list[i].imageFilePath,
             g_images[g_nimages].path, 255);
        g_nimages++;
    }
    thread_act_array_t threads;
    mach_msg_type_number_t count = 0;
    task_threads(g_task, &threads, &count);
    for (mach_msg_type_number_t i = 0; i < count; i++) {
        thread_extended_info_data_t ei;
        mach_msg_type_number_t ecount = THREAD_EXTENDED_INFO_COUNT;
        char name[64] = "?";
        if (thread_info(threads[i], THREAD_EXTENDED_INFO,
                        (thread_info_t)&ei, &ecount) == KERN_SUCCESS)
            snprintf(name, sizeof(name), "%s", ei.pth_name);
        thread_identifier_info_data_t ii;
        mach_msg_type_number_t icount = THREAD_IDENTIFIER_INFO_COUNT;
        unsigned long long tid64 = 0;
        if (thread_info(threads[i], THREAD_IDENTIFIER_INFO,
                        (thread_info_t)&ii, &icount) == KERN_SUCCESS)
            tid64 = ii.thread_id;
        if (want && !strstr(name, want)) {
            mach_port_deallocate(mach_task_self(), threads[i]);
            continue;
        }
        arm_thread_state64_t st;
        mach_msg_type_number_t scount = ARM_THREAD_STATE64_COUNT;
        if (thread_get_state(threads[i], ARM_THREAD_STATE64,
                             (thread_state_t)&st, &scount) != KERN_SUCCESS)
            continue;
        unsigned long long pc = (unsigned long long)arm_thread_state64_get_pc(st) & 0x000000ffffffffffULL;
        unsigned long long sp = (unsigned long long)arm_thread_state64_get_sp(st) & 0x000000ffffffffffULL;
        unsigned long long fpv = (unsigned long long)arm_thread_state64_get_fp(st) & 0x000000ffffffffffULL;
        printf("\nth%u [%s] tid=%llu pc=%#llx sp=%#llx fp=%#llx\n", i,
               name, tid64, pc, sp, fpv);
        printf("  x0=%#llx x1=%#llx x2=%#llx x3=%#llx x4=%#llx "
               "x5=%#llx x6=%#llx x7=%#llx\n",
               st.__x[0], st.__x[1], st.__x[2], st.__x[3], st.__x[4],
               st.__x[5], st.__x[6], st.__x[7]);
        unsigned long long off;
        const char *img = symbolize(pc, &off);
        printf("  pc -> %s+0x%llx\n", img ? img : "?", off);
        unsigned long long fp = fpv;
        unsigned long long frame[8];
        if (rmem(fp, frame, sizeof(frame)) > 0) {
            printf("  raw fp: %016llx %016llx %016llx %016llx\n",
                   frame[0], frame[1], frame[2], frame[3]);
        } else {
            printf("  rmem(fp=%#llx) failed\n", fp);
        }
        for (int depth = 0; depth < 24 && fp; depth++) {
            if (rmem(fp, frame, sizeof(frame)) < 0) { printf("  rmem fail at fp=%#llx\n", fp); break; }
            unsigned long long lr = frame[1] & 0x000000ffffffffffULL;
            if (lr < 0x100000000 || lr > 0x200000000) { printf("  lr=%#llx out of range, stop\n", lr); break; }
            const char *li = symbolize(lr, &off);
            printf("  #%02d lr=%#llx %s+0x%llx\n", depth, lr,
                   li ? li : "?", off);
            fp = frame[0];
        }
        mach_port_deallocate(mach_task_self(), threads[i]);
    }
    return 0;
}
