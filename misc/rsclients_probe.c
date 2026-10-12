// Enumerate CARenderServer clients + statistics from inside the chroot.
// CARenderServerGetClientProcessId(0, i) -> pid of render-server client i
// CARenderServerGetStatistics(&arr)    -> CFArray of CFDictionary stats
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <string.h>
#include <mach/mach.h>
#include <CoreFoundation/CoreFoundation.h>

typedef pid_t (*GetClientPid_t)(mach_port_t port, uint32_t idx);
typedef int64_t (*GetStats_t)(mach_port_t port, void *out);
typedef int64_t (*GetFrameCounter_t)(mach_port_t port);
typedef int64_t (*GetDirtyCount_t)(mach_port_t port);
typedef int64_t (*GetDirtyCountIdx_t)(mach_port_t port, uint32_t idx);
typedef CFArrayRef (*CopyDicts_t)(mach_port_t port);

int main(void) {
    void *qc = dlopen("/System/Library/Frameworks/QuartzCore.framework/QuartzCore",
                      RTLD_LAZY | RTLD_GLOBAL);
    if (!qc) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }
    GetClientPid_t getClientPid = (GetClientPid_t)dlsym(qc, "CARenderServerGetClientProcessId");
    GetStats_t getStats = (GetStats_t)dlsym(qc, "CARenderServerGetStatistics");
    GetFrameCounter_t getFrame = (GetFrameCounter_t)dlsym(qc, "CARenderServerGetFrameCounter");
    GetDirtyCount_t getDirty = (GetDirtyCount_t)dlsym(qc, "CARenderServerGetDirtyFrameCount");
    GetDirtyCountIdx_t getDirtyIdx = (GetDirtyCountIdx_t)dlsym(qc, "CARenderServerGetDirtyFrameCountByIndex");
    fprintf(stderr, "syms pid=%p stats=%p frame=%p dirty=%p dirtyIdx=%p\n",
            getClientPid, getStats, getFrame, getDirty, getDirtyIdx);

    fprintf(stderr, "=== render-server clients ===\n");
    if (getClientPid) {
        for (uint32_t i = 0; i < 96; i++) {
            pid_t p = getClientPid(0, i);
            fprintf(stderr, "client[%u] pid=%d\n", i, p);
            if (p <= 0 && i > 8) break;
        }
    }
    if (getFrame) fprintf(stderr, "frameCounter=%lld\n", (long long)getFrame(0));
    if (getDirty) fprintf(stderr, "dirtyFrameCount=%lld\n", (long long)getDirty(0));
    if (getDirtyIdx) {
        for (uint32_t i = 0; i < 24; i++) {
            int64_t d = getDirtyIdx(0, i);
            fprintf(stderr, "dirtyFrameCount[%u]=%lld\n", i, (long long)d);
        }
    }
    CopyDicts_t copyDicts = (CopyDicts_t)dlsym(qc, "CARenderServerCopyDictionaries");
    fprintf(stderr, "copyDicts=%p\n", copyDicts);
    if (copyDicts) {
        CFArrayRef dicts = copyDicts(0);
        fprintf(stderr, "dicts=%p\n", dicts);
        if (dicts) {
            CFIndex n = CFArrayGetCount(dicts);
            fprintf(stderr, "dict count=%ld\n", (long)n);
            for (CFIndex i = 0; i < n && i < 20; i++) {
                CFTypeRef d = CFArrayGetValueAtIndex(dicts, i);
                CFStringRef s = CFCopyDescription(d);
                if (s) {
                    char buf[8192];
                    CFStringGetCString(s, buf, sizeof(buf), kCFStringEncodingUTF8);
                    fprintf(stderr, "dict[%ld]=%s\n", (long)i, buf);
                    CFRelease(s);
                }
            }
            CFRelease(dicts);
        }
    }
    if (getStats) {
        void *arr = NULL;
        int64_t n = getStats(0, &arr);
        fprintf(stderr, "stats count=%lld arr=%p\n", (long long)n, arr);
        if (arr && n > 0 && n < 256) {
            CFTypeRef *dicts = (CFTypeRef *)arr;
            for (int64_t i = 0; i < n; i++) {
                CFTypeRef d = dicts[i];
                if (!d) { fprintf(stderr, "stat[%lld]=NULL\n", (long long)i); continue; }
                CFTypeID tid = CFGetTypeID(d);
                fprintf(stderr, "stat[%lld] ref=%p typeID=%lu\n",
                        (long long)i, d, (unsigned long)tid);
                CFStringRef s = CFCopyDescription(d);
                if (s) {
                    char buf[8192];
                    CFStringGetCString(s, buf, sizeof(buf), kCFStringEncodingUTF8);
                    fprintf(stderr, "  =%s\n", buf);
                    CFRelease(s);
                }
            }
        }
    }
    return 0;
}
