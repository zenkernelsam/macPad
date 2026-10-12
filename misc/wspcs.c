// Dump each WS thread's PC + wait primitive by task_threads/thread_get_state.
// Needs task_for_pid (project entitlements include task-for-pid-allow).
#include <stdio.h>
#include <stdlib.h>
#include <mach/mach.h>
#include <mach/thread_status.h>
#include <mach/machine/thread_status.h>
#include <mach/thread_info.h>

extern kern_return_t task_for_pid(mach_port_name_t task, pid_t pid,
                                  mach_port_name_t *target);

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <pid>\n", argv[0]); return 2; }
    pid_t pid = (pid_t)atoi(argv[1]);
    task_t task;
    kern_return_t kr = task_for_pid(mach_task_self(), pid, &task);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "task_for_pid(%d) -> 0x%x\n", pid, kr);
        return 1;
    }
    thread_act_array_t threads;
    mach_msg_type_number_t count = 0;
    kr = task_threads(task, &threads, &count);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "task_threads -> 0x%x\n", kr);
        return 1;
    }
    fprintf(stderr, "pid=%d threads=%d\n", pid, count);
    for (mach_msg_type_number_t i = 0; i < count; i++) {
        thread_basic_info_data_t bi;
        mach_msg_type_number_t bcount = THREAD_BASIC_INFO_COUNT;
        thread_info(threads[i], THREAD_BASIC_INFO,
                    (thread_info_t)&bi, &bcount);
        arm_thread_state64_t state;
        mach_msg_type_number_t scount = ARM_THREAD_STATE64_COUNT;
        kern_return_t skr = thread_get_state(
            threads[i], ARM_THREAD_STATE64,
            (thread_state_t)&state, &scount);
        if (skr == KERN_SUCCESS) {
            char name[64] = {0};
            thread_extended_info_data_t ei;
            mach_msg_type_number_t ecount = THREAD_EXTENDED_INFO_COUNT;
            if (thread_info(threads[i], THREAD_EXTENDED_INFO,
                            (thread_info_t)&ei, &ecount) == KERN_SUCCESS)
                snprintf(name, sizeof(name), "%s", ei.pth_name);
            fprintf(stderr,
                    "th%u pc=0x%llx lr=0x%llx sp=0x%llx state=%d "
                    "sys=%u.%03u name=%s\n",
                    i, state.__pc, state.__lr, state.__sp,
                    bi.run_state,
                    bi.system_time.seconds,
                    bi.system_time.microseconds / 1000,
                    name);
        } else {
            fprintf(stderr, "th%u get_state err 0x%x\n", i, skr);
        }
        mach_port_deallocate(mach_task_self(), threads[i]);
    }
    vm_deallocate(mach_task_self(), (vm_address_t)threads,
                  count * sizeof(threads[0]));
    return 0;
}
