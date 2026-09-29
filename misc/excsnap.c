// excsnap.c — attach to pid, install exception port, print first exception's
// pc/fault-addr, reply KERN_FAILURE so normal crash path continues.
// usage: excsnap <pid>  (run before target crashes)
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <mach/mach.h>
#include <mach/arm/thread_state.h>

#include <spawn.h>
extern char **environ;

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc < 2) { fprintf(stderr, "usage: %s <pid> | %s -s <binary> [args...]\n", argv[0], argv[0]); return 1; }
    task_t tp;
    kern_return_t kr;
    pid_t pid;
    if (!strcmp(argv[1], "-s")) {
        int rc = posix_spawn(&pid, argv[2], NULL, NULL, &argv[2], environ);
        if (rc) { fprintf(stderr, "spawn rc=%d\n", rc); return 1; }
        printf("[*] spawned pid=%d\n", pid);
    } else {
        pid = atoi(argv[1]);
    }
    for (int i = 0; i < 2000; i++) {
        kr = task_for_pid(mach_task_self(), pid, &tp);
        if (kr == KERN_SUCCESS) break;
    }
    printf("[*] task_for_pid kr=%d\n", kr);
    if (kr) { kill(pid, SIGKILL); return 1; }

    mach_port_t excp;
    kr = mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &excp);
    kr = task_set_exception_ports(tp,
        EXC_MASK_BAD_ACCESS | EXC_MASK_BAD_INSTRUCTION | EXC_MASK_ARITHMETIC |
        EXC_MASK_EMULATION | EXC_MASK_SOFTWARE | EXC_MASK_BREAKPOINT,
        excp, EXCEPTION_STATE | MACH_EXCEPTION_CODES, ARM_THREAD_STATE64);
    printf("[*] set_exception_ports kr=%d\n", kr);
    if (kr) { kill(pid, SIGKILL); return 1; }
    if (!strcmp(argv[1], "-s")) {
        kr = task_resume(tp);
        printf("[*] task_resume kr=%d\n", kr);
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
        char              pad[256];
    } msg;
    struct { mach_msg_header_t hdr; NDR_record_t ndr; kern_return_t ret; char pad[128]; } rsp;

    for (int i = 0; i < 4; i++) {
        memset(&msg, 0, sizeof(msg));
        kr = mach_msg(&msg.hdr, MACH_RCV_MSG | MACH_RCV_TIMEOUT, 0,
                      sizeof(msg), excp, 20000, MACH_PORT_NULL);
        if (kr != MACH_MSG_SUCCESS) { printf("[*] recv timeout kr=%d\n", kr); break; }
        printf("[exc] type=%d code={%llx,%llx} pc=%llx lr=%llx sp=%llx x0=%llx x8=%llx x16=%llx\n",
               msg.exctype, (unsigned long long)msg.code[0], (unsigned long long)msg.code[1],
               (unsigned long long)arm_thread_state64_get_pc(msg.st),
               (unsigned long long)msg.st.__lr, (unsigned long long)msg.st.__sp,
               (unsigned long long)msg.st.__x[0], (unsigned long long)msg.st.__x[8],
               (unsigned long long)msg.st.__x[16]);
        rsp.hdr.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_MOVE_SEND_ONCE, 0);
        rsp.hdr.msgh_size = sizeof(rsp.hdr) + sizeof(NDR_record_t) + sizeof(kern_return_t);
        rsp.hdr.msgh_remote_port = msg.hdr.msgh_remote_port;
        rsp.hdr.msgh_local_port = MACH_PORT_NULL;
        rsp.hdr.msgh_id = msg.hdr.msgh_id + 100;
        rsp.ndr = msg.ndr;
        rsp.ret = KERN_FAILURE;
        mach_msg_send(&rsp.hdr);
    }
    return 0;
}
