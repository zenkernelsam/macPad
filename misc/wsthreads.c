// List threads of a pid with per-thread CPU time via proc_pidinfo.
// Two snapshots Ns apart -> which threads burn CPU.
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <mach/mach.h>
extern int proc_pidinfo(int pid, int flavor, uint64_t arg, void *buffer, int buffersize);
#define PROC_PIDLISTTHREADS 6
#define PROC_PIDTHREADID64INFO 15
struct proc_threadinfo {
    uint64_t pth_user_time;
    uint64_t pth_system_time;
    int32_t pth_cpu_usage;
    int32_t pth_policy;
    int32_t pth_run_state;
    int32_t pth_flags;
    int32_t pth_sleep_time;
    int32_t pth_curpri;
    int32_t pth_priority;
    int32_t pth_maxpriority;
    char pth_name[64];
};

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <pid> [sleep]\n", argv[0]); return 2; }
    pid_t pid = (pid_t)atoi(argv[1]);
    int nap = argc > 2 ? atoi(argv[2]) : 3;
    for (int round = 0; round < 2; round++) {
        static uint64_t tids[512];
        int n = proc_pidinfo(pid, PROC_PIDLISTTHREADS, 0, tids, sizeof(tids));
        if (n <= 0) { fprintf(stderr, "listthreads err %d\n", n); return 1; }
        int cnt = n / 8;
        if (cnt > 512) cnt = 512;
        fprintf(stderr, "--- round %d: %d threads ---\n", round, cnt);
        for (int i = 0; i < cnt; i++) {
            struct proc_threadinfo ti;
            memset(&ti, 0, sizeof(ti));
            int sz = proc_pidinfo(pid, PROC_PIDTHREADID64INFO, tids[i],
                                  &ti, sizeof(ti));
            fprintf(stderr, "  tid=%llu sz=%d user=%.3fs sys=%.3fs state=%d name=%.60s\n",
                    (unsigned long long)tids[i], sz,
                    ti.pth_user_time / 1e6, ti.pth_system_time / 1e6,
                    ti.pth_run_state, ti.pth_name);
        }
        
        if (round == 0) sleep((unsigned)nap);
    }
    return 0;
}
