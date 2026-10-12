// List a task's loaded Mach-O images via task_dyld_info + remote read.
#include <stdio.h>
#include <stdlib.h>
#include <mach/mach.h>
#include <mach/task_info.h>
#include <mach-o/dyld_images.h>
#include <mach-o/loader.h>

extern kern_return_t task_for_pid(mach_port_name_t task, pid_t pid,
                                  mach_port_name_t *target);

static int read_remote(task_t task, vm_address_t addr, void *buf,
                       mach_msg_type_number_t size) {
    mach_msg_type_number_t out = 0;
    kern_return_t kr = vm_read_overwrite(
        task, addr, size, (vm_address_t)buf, &out);
    return kr == KERN_SUCCESS ? (int)out : -1;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <pid> [addr]\n", argv[0]);
        return 2; }
    pid_t pid = (pid_t)atoi(argv[1]);
    unsigned long long want = argc > 2 ? strtoull(argv[2], 0, 0) : 0;
    task_t task;
    if (task_for_pid(mach_task_self(), pid, &task) != KERN_SUCCESS) {
        fprintf(stderr, "task_for_pid failed\n"); return 1;
    }
    struct task_dyld_info dyld;
    mach_msg_type_number_t dcount = TASK_DYLD_INFO_COUNT;
    if (task_info(task, TASK_DYLD_INFO, (task_info_t)&dyld, &dcount)
            != KERN_SUCCESS) {
        fprintf(stderr, "task_dyld_info failed\n"); return 1;
    }
    struct dyld_all_image_infos infos;
    if (read_remote(task, dyld.all_image_info_addr, &infos,
                    sizeof(infos)) < 0) {
        fprintf(stderr, "read infos failed\n"); return 1;
    }
    struct dyld_image_info *list = calloc(infos.infoArrayCount,
                                          sizeof(*list));
    if (read_remote(task, (vm_address_t)infos.infoArray, list,
                    (mach_msg_type_number_t)(infos.infoArrayCount *
                                             sizeof(*list))) < 0) {
        fprintf(stderr, "read array failed\n"); return 1;
    }
    for (unsigned i = 0; i < infos.infoArrayCount; i++) {
        char path[512] = {0};
        read_remote(task, (vm_address_t)list[i].imageFilePath, path,
                    sizeof(path) - 1);
        vm_address_t base = (vm_address_t)list[i].imageLoadAddress;
        if (want && want >= base) {
            struct mach_header_64 mh;
            if (read_remote(task, base, &mh, sizeof(mh)) > 0 &&
                mh.magic == MH_MAGIC_64) {
                struct load_command lc;
                vm_address_t cur = base + sizeof(mh), end = 0;
                for (uint32_t c = 0; c < mh.ncmds; c++) {
                    if (read_remote(task, cur, &lc, sizeof(lc)) < 0) break;
                    if (lc.cmd == LC_SEGMENT_64) {
                        struct segment_command_64 seg;
                        if (read_remote(task, cur, &seg, sizeof(seg)) > 0 &&
                            want >= base + seg.vmaddr &&
                            want < base + seg.vmaddr + seg.vmsize)
                            end = base + seg.vmaddr + seg.vmsize;
                    }
                    cur += lc.cmdsize;
                }
                if (end) {
                    printf("0x%llx -> %s +0x%llx (base 0x%llx)\n",
                           want, path, want - base,
                           (unsigned long long)base);
                }
            }
        } else {
            printf("0x%llx  %s\n", (unsigned long long)base, path);
        }
    }
    free(list);
    return 0;
}
