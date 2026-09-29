// tstate.c — attach to a pid, dump thread states (PC/SP/FP/LR) and the
// VM regions covering the shared-cache window. Pure Mach APIs, no KRW.
// Build: clang -arch arm64 -isysroot <iOS sdk> -miphoneos-version-min=14.0 -O2 -o tstate tstate.c
// On device: ldid -Hsha256 -S<ent> tstate ; jbctl trustcache add <cdhash>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <mach/mach.h>
#include <mach/arm/thread_state.h>
#include <mach-o/dyld_images.h>

/* mach_vm_region lives in mach_vm.h on iOS SDKs but the prototype may be
 * hidden; declare it explicitly. */
extern kern_return_t mach_vm_region(vm_map_t map, mach_vm_address_t *addr,
    mach_vm_size_t *size, vm_region_flavor_t flavor, vm_region_info_t info,
    mach_msg_type_number_t *cnt, mach_port_t *obj);
extern kern_return_t mach_vm_read_overwrite(vm_map_t map,
    mach_vm_address_t addr, mach_vm_size_t size, mach_vm_address_t data,
    mach_vm_size_t *outsize);
extern kern_return_t mach_vm_region_recurse(vm_map_t map,
    mach_vm_address_t *addr, mach_vm_size_t *size, natural_t *depth,
    vm_region_recurse_info_t info, mach_msg_type_number_t *cnt);

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc < 2) { fprintf(stderr, "usage: %s <pid> [base-hex]\n", argv[0]); return 1; }
    pid_t pid = atoi(argv[1]);
    mach_vm_address_t base = argc > 2 ? strtoull(argv[2], NULL, 16) : 0x180000000ULL;

    task_t tp = MACH_PORT_NULL;
    kern_return_t kr = task_for_pid(mach_task_self(), pid, &tp);
    printf("[*] task_for_pid(%d) kr=%d tp=%u\n", pid, kr, tp);
    if (kr != KERN_SUCCESS) return 1;

    thread_act_array_t ths = NULL;
    mach_msg_type_number_t nth = 0;
    kr = task_threads(tp, &ths, &nth);
    printf("[*] task_threads kr=%d n=%u\n", kr, nth);
    for (unsigned i = 0; kr == KERN_SUCCESS && i < nth; i++) {
        thread_basic_info_data_t bi;
        mach_msg_type_number_t cnt = THREAD_BASIC_INFO_COUNT;
        if (thread_info(ths[i], THREAD_BASIC_INFO, (thread_info_t)&bi, &cnt) == KERN_SUCCESS)
            printf("[thr%u] run_state=%d flags=0x%x suspend=%d cpu=%u.%us\n",
                   i, bi.run_state, bi.flags, bi.suspend_count,
                   bi.cpu_usage / 1000000, (bi.cpu_usage / 1000) % 1000);
        arm_thread_state64_t stt;
        cnt = ARM_THREAD_STATE64_COUNT;
        if (thread_get_state(ths[i], ARM_THREAD_STATE64, (thread_state_t)&stt, &cnt) == KERN_SUCCESS)
            printf("[thr%u] pc=%llx sp=%llx fp=%llx lr=%llx x16=%llx\n", i,
                   (unsigned long long)arm_thread_state64_get_pc(stt),
                   (unsigned long long)stt.__sp, (unsigned long long)stt.__fp,
                   (unsigned long long)stt.__lr, (unsigned long long)stt.__x[16]);
        mach_port_deallocate(mach_task_self(), ths[i]);
    }

    /* resolve image owning pc via TASK_DYLD_INFO */
    task_dyld_info_data_t dinfo;
    mach_msg_type_number_t dcnt = TASK_DYLD_INFO_COUNT;
    if (task_info(tp, TASK_DYLD_INFO, (task_info_t)&dinfo, &dcnt) == KERN_SUCCESS &&
        dinfo.all_image_info_addr) {
        mach_vm_address_t a = dinfo.all_image_info_addr;
        mach_vm_size_t rd = 0;
        struct dyld_all_image_infos ai;
        if (mach_vm_read_overwrite(tp, a, sizeof(ai), (mach_vm_address_t)&ai, &rd) == KERN_SUCCESS) {
            printf("[*] images n=%u dyld=%llx\n", ai.infoArrayCount,
                   (unsigned long long)ai.dyldImageLoadAddress);
            for (uint32_t i = 0; i < ai.infoArrayCount && i < 400; i++) {
                struct dyld_image_info di;
                mach_vm_read_overwrite(tp, (mach_vm_address_t)ai.infoArray + i * sizeof(di),
                                       sizeof(di), (mach_vm_address_t)&di, &rd);
                /* read first 8 bytes = mach header magic only; use load addr + filesize
                 * rough range: read slide & name ptr */
                char nm[128] = {0};
                mach_vm_size_t nrd = 0;
                mach_vm_read_overwrite(tp, (mach_vm_address_t)di.imageFilePath,
                                       sizeof(nm) - 1, (mach_vm_address_t)nm, &nrd);
                printf("[img%u] load=%llx path=%s\n", i,
                       (unsigned long long)di.imageLoadAddress, nm);
            }
        }
    }

    /* walk VM regions over the shared cache window */
    mach_vm_address_t addr = base;
    int n = 0;
    for (int i = 0; i < 40 && addr < base + 0x140000000ULL; ) {
        mach_vm_size_t size = 0;
        vm_region_basic_info_data_64_t info;
        mach_msg_type_number_t cnt = VM_REGION_BASIC_INFO_COUNT_64;
        mach_port_t obj = MACH_PORT_NULL;
        kr = mach_vm_region(tp, &addr, &size, VM_REGION_BASIC_INFO_64,
                            (vm_region_info_t)&info, &cnt, &obj);
        if (kr != KERN_SUCCESS) { addr += 0x4000; continue; }
        printf("[vm] %llx-%llx prot=%x/%x off=%llx obj=%u\n",
               (unsigned long long)addr, (unsigned long long)(addr + size),
               info.protection, info.max_protection,
               (unsigned long long)info.offset, obj);
        if (++n > 40) break;
        addr += size;
        i++;
    }

    /* recurse into the submap covering the shared region to list what is
     * actually populated inside it */
    {
        mach_vm_address_t s = base;
        int cnt2 = 0;
        while (s < base + 0x100000000ULL && cnt2 < 200) {
            mach_vm_address_t a2 = s;
            mach_vm_size_t sz2 = 0;
            natural_t depth = 1;
            vm_region_submap_info_data_64_t ri;
            mach_msg_type_number_t ic = VM_REGION_SUBMAP_INFO_COUNT_64;
            kr = mach_vm_region_recurse(tp, &a2, &sz2, &depth,
                                        (vm_region_recurse_info_t)&ri, &ic);
            if (kr != KERN_SUCCESS) { printf("[sub] recurse kr=%d at %llx\n", kr, (unsigned long long)a2); break; }
            printf("[sub] %llx-%llx prot=%x/%x depth=%u sub=%d off=%llx\n",
                   (unsigned long long)a2, (unsigned long long)(a2+sz2),
                   ri.protection, ri.max_protection, depth, ri.is_submap,
                   (unsigned long long)ri.offset);
            s = a2 + sz2;
            cnt2++;
        }
    }
    printf("[*] done\n");
    return 0;
}
