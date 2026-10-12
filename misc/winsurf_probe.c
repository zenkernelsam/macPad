// Enumerate per-connection CGS surfaces for a set of windows.
// For each window id on argv: resolve owner connection, then ask WS for
// that connection's surface list (count, ids, front-buffer IOSurface id,
// bounds).  Proves whether the owning process ever created/bound a
// backing surface for its windows.
//
// Usage: winsurf_probe <windowID> [windowID ...]
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <string.h>

static void *sl;

typedef int (*MainConn_t)(void);
typedef int (*GetWindowOwner_t)(int cid, uint32_t wid, int *outCid);
typedef int (*GetSurfaceCount_t)(int cid, int targetCid, int *count);
typedef int (*GetSurfaceList_t)(int cid, int targetCid, int max, uint32_t *list, int *count);
typedef int (*GetFrontBuffer_t)(int cid, uint32_t surface, uint32_t *iosid);
typedef int (*GetSurfaceBounds_t)(int cid, uint32_t surface, void *bounds);

static MainConn_t SLSMainConnectionID_p;
static GetWindowOwner_t SLSGetWindowOwner_p;
static GetSurfaceCount_t SLSGetSurfaceCount_p;
static GetSurfaceList_t SLSGetSurfaceList_p;
static GetFrontBuffer_t SLSGetSurfaceFrontBufferID_p;
static GetSurfaceBounds_t SLSGetSurfaceBounds_p;

int main(int argc, char **argv) {
    sl = dlopen("/System/Library/PrivateFrameworks/SkyLight.framework/SkyLight",
                RTLD_LAZY | RTLD_GLOBAL);
    if (!sl) { fprintf(stderr, "dlopen SkyLight: %s\n", dlerror()); return 1; }
    SLSMainConnectionID_p = (MainConn_t)dlsym(sl, "SLSMainConnectionID");
    SLSGetWindowOwner_p = (GetWindowOwner_t)dlsym(sl, "SLSGetWindowOwner");
    SLSGetSurfaceCount_p = (GetSurfaceCount_t)dlsym(sl, "SLSGetSurfaceCount");
    SLSGetSurfaceList_p = (GetSurfaceList_t)dlsym(sl, "SLSGetSurfaceList");
    SLSGetSurfaceFrontBufferID_p = (GetFrontBuffer_t)dlsym(sl, "SLSGetSurfaceFrontBufferID");
    SLSGetSurfaceBounds_p = (GetSurfaceBounds_t)dlsym(sl, "SLSGetSurfaceBounds");
    fprintf(stderr, "syms main=%p owner=%p count=%p list=%p fb=%p bounds=%p\n",
            SLSMainConnectionID_p, SLSGetWindowOwner_p, SLSGetSurfaceCount_p,
            SLSGetSurfaceList_p, SLSGetSurfaceFrontBufferID_p, SLSGetSurfaceBounds_p);
    if (!SLSMainConnectionID_p) return 1;

    int cid = SLSMainConnectionID_p();
    fprintf(stderr, "own cid=%d\n", cid);

    for (int i = 1; i < argc; i++) {
        uint32_t wid = (uint32_t)strtoul(argv[i], NULL, 0);
        int owner = 0;
        int rc = SLSGetWindowOwner_p ? SLSGetWindowOwner_p(cid, wid, &owner) : -99;
        fprintf(stderr, "window %u -> ownerCid=%d rc=%d\n", wid, owner, rc);
        if (rc != 0 || owner <= 0) continue;

        int scount = -1;
        int src = SLSGetSurfaceCount_p ? SLSGetSurfaceCount_p(cid, owner, &scount) : -99;
        fprintf(stderr, "  owner %d surfaceCount=%d rc=%d\n", owner, scount, src);
        if (scount <= 0) continue;
        uint32_t *list = calloc((size_t)scount, sizeof(uint32_t));
        int got = 0;
        int lrc = SLSGetSurfaceList_p ? SLSGetSurfaceList_p(cid, owner, scount, list, &got) : -99;
        fprintf(stderr, "  surfaceList rc=%d got=%d\n", lrc, got);
        for (int s = 0; s < got; s++) {
            uint32_t iosid = 0;
            int frc = SLSGetSurfaceFrontBufferID_p
                ? SLSGetSurfaceFrontBufferID_p(cid, list[s], &iosid) : -99;
            /* bounds: 4-float rect is typical; request into 32-byte buffer */
            uint8_t bounds[32]; memset(bounds, 0, sizeof(bounds));
            int brc = -99;
            if (SLSGetSurfaceBounds_p)
                brc = SLSGetSurfaceBounds_p(cid, list[s], bounds);
            float *bf = (float *)bounds;
            fprintf(stderr, "    surface[%d] id=%u frontIOS=%u frc=%d bounds=(%.0f,%.0f %.0fx%.0f) brc=%d\n",
                    s, list[s], iosid, frc, bf[0], bf[1], bf[2], bf[3], brc);
        }
        free(list);
    }
    return 0;
}
