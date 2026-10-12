// Probe the QuartzCore render server (inside WindowServer) through the
// private CARenderServer* SPIs.  Every call is a mach round-trip to the
// render server, so a successful return proves the client->WS channel is
// alive; a hang/timeout proves it is not.
//
// Usage:
//   renderserver_debug_probe                 dump flags + values 0..31
//   renderserver_debug_probe set <key> <val> set debug value
//   renderserver_debug_probe flags <mask> <bits> set debug flags
//
// Read-only by default.  Requires libmachook so that the CARenderServer
// bootstrap name is rewritten to com.apple.macosbooter.CARenderServer.
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <mach/mach.h>
#include <mach/mach_time.h>

typedef uint32_t (*GetDebugFlags_t)(double timeout);
typedef uint64_t (*GetDebugValue_t)(double timeout, uint32_t key);
typedef void (*SetDebugValue_t)(double timeout, uint32_t key, uint64_t value);
typedef void (*SetDebugFlags_t)(double timeout, uint32_t mask, uint32_t flags);

int main(int argc, char **argv) {
    void *qc = dlopen("/System/Library/Frameworks/QuartzCore.framework/QuartzCore",
                      RTLD_LAZY | RTLD_GLOBAL);
    if (!qc) { fprintf(stderr, "dlopen QuartzCore: %s\n", dlerror()); return 1; }

    typedef mach_port_t (*GetServerPort_t)(void);
    typedef mach_port_t (*GetServerPort2_t)(int arg);
    GetServerPort_t getPort0 = (GetServerPort_t)dlsym(qc, "CARenderServerGetServerPort");
    GetServerPort_t getPortU = (GetServerPort_t)dlsym(qc, "CARenderServerGetUniqueServerPort");
    mach_port_t sp = getPort0 ? getPort0() : MACH_PORT_NULL;
    mach_port_t spu = getPortU ? getPortU() : MACH_PORT_NULL;
    mach_port_type_t t1 = 0, t2 = 0;
    kern_return_t k1 = sp ? mach_port_type(mach_task_self(), sp, &t1) : -1;
    kern_return_t k2 = spu ? mach_port_type(mach_task_self(), spu, &t2) : -1;
    fprintf(stderr, "serverPort=%p -> %#x type=%#x kr=%#x | uniquePort=%p -> %#x type=%#x kr=%#x\n",
            getPort0, sp, t1, k1, getPortU, spu, t2, k2);
    GetDebugFlags_t getFlags = (GetDebugFlags_t)dlsym(qc, "CARenderServerGetDebugFlags");
    GetDebugValue_t getValue = (GetDebugValue_t)dlsym(qc, "CARenderServerGetDebugValue");
    SetDebugValue_t setValue = (SetDebugValue_t)dlsym(qc, "CARenderServerSetDebugValue");
    SetDebugFlags_t setFlags = (SetDebugFlags_t)dlsym(qc, "CARenderServerSetDebugFlags");
    fprintf(stderr, "syms: getFlags=%p getValue=%p setValue=%p setFlags=%p\n",
            getFlags, getValue, setValue, setFlags);
    if (!getFlags || !getValue || !setValue || !setFlags) return 1;

    if (argc >= 2 && !strcmp(argv[1], "set") && argc == 4) {
        uint32_t key = (uint32_t)strtoul(argv[2], NULL, 0);
        uint64_t val = strtoull(argv[3], NULL, 0);
        fprintf(stderr, "SET value[%u] = %llu ...\n", key, (unsigned long long)val);
        setValue(2.0, key, val);
        fprintf(stderr, "SET done; readback=%llu\n",
                (unsigned long long)getValue(2.0, key));
        return 0;
    }
    if (argc >= 2 && !strcmp(argv[1], "flags") && argc == 4) {
        uint32_t mask = (uint32_t)strtoul(argv[2], NULL, 0);
        uint32_t bits = (uint32_t)strtoul(argv[3], NULL, 0);
        fprintf(stderr, "SET flags mask=%#x bits=%#x ...\n", mask, bits);
        setFlags(2.0, mask, bits);
        fprintf(stderr, "SET done; readback=%#x\n", getFlags(2.0));
        return 0;
    }

    {
        uint64_t t0 = mach_absolute_time();
        uint32_t f = getFlags(2.0);
        uint64_t t1 = mach_absolute_time();
        fprintf(stderr, "flags=%#x (call took %llu ticks)\n", f,
                (unsigned long long)(t1 - t0));
    }
    for (uint32_t k = 0; k < 32; k++) {
        uint64_t v = getValue(2.0, k);
        if (v) fprintf(stderr, "value[%u] = %llu (0x%llx)\n", k,
                       (unsigned long long)v, (unsigned long long)v);
    }
    fprintf(stderr, "done\n");
    return 0;
}
