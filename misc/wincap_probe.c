// Capture a window's composited image via CGWindowListCreateImage —
// WindowServer renders the window's surface for the capture, so the
// returned pixels prove whether the window's surface ever received
// content.  Saves PNG under /tmp/.
//
// Usage: wincap_probe <windowID> [windowID ...]
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <string.h>

typedef uint32_t CGWindowID;
typedef struct __CGImage *CGImageRef;
typedef struct __CFArray *CFArrayRef;
typedef const void *CFTypeRef;
typedef long CFIndex;
typedef struct __CFString *CFStringRef;
typedef struct __CFURL *CFURLRef;
typedef struct __CGImageDestination *CGImageDestinationRef;
typedef struct __CFArray *CFArrayRef2;
typedef const void *CFDictionaryRef;

// CGRect = 4 doubles
typedef CGImageRef (*WindowListCreateImage_t)(double x, double y, double w,
                                              double h, uint32_t option,
                                              CGWindowID wid, uint32_t imgOpt);
typedef CFArrayRef (*WindowListCreate_t)(uint32_t option, CGWindowID rel);
typedef CFIndex (*CFArrayGetCount_t)(CFArrayRef);
typedef const void *(*CFArrayGetValueAtIndex_t)(CFArrayRef, CFIndex);
typedef void *(*CFDictionaryGetValue_t)(const void *, const void *);
typedef CFStringRef (*CFStringCreateWithCString_t)(void *, const char *,
                                                   unsigned);
typedef CFURLRef (*CFURLCreateWithFileSystemPath_t)(void *, CFStringRef, long,
                                                    int);
typedef struct __CGImageDestination *CGImageDestinationRef;
typedef CGImageDestinationRef (*ImageDestCreateWithURL_t)(CFURLRef, CFStringRef,
                                                        size_t, void *);
typedef void (*ImageDestAddImage_t)(CGImageDestinationRef, CGImageRef, void *);
typedef int (*ImageDestFinalize_t)(CGImageDestinationRef);
typedef void (*CFRelease_t)(CFTypeRef);
typedef size_t (*ImageGetW_t)(CGImageRef);
typedef void (*CFShow_t)(CFTypeRef);

static void *cg, *cf, *io;
static void *sym(void *h, const char *n) {
    void *p = dlsym(h, n);
    if (!p) fprintf(stderr, "missing %s\n", n);
    return p;
}

int main(int argc, char **argv) {
    cg = dlopen("/System/Library/Frameworks/CoreGraphics.framework/CoreGraphics",
                RTLD_LAZY | RTLD_GLOBAL);
    cf = dlopen("/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation",
                RTLD_LAZY | RTLD_GLOBAL);
    io = dlopen("/System/Library/Frameworks/ImageIO.framework/ImageIO",
                RTLD_LAZY | RTLD_GLOBAL);
    if (!cg || !cf || !io) {
        fprintf(stderr, "dlopen: %s\n", dlerror());
        return 1;
    }
    WindowListCreateImage_t CreateImage_p =
        (WindowListCreateImage_t)sym(cg, "CGWindowListCreateImage");
    WindowListCreate_t ListCreate_p =
        (WindowListCreate_t)sym(cg, "CGWindowListCreate");
    CFArrayGetCount_t Cnt_p = (CFArrayGetCount_t)sym(cf, "CFArrayGetCount");
    CFArrayGetValueAtIndex_t At_p =
        (CFArrayGetValueAtIndex_t)sym(cf, "CFArrayGetValueAtIndex");
    ImageGetW_t W_p = (ImageGetW_t)sym(cg, "CGImageGetWidth");
    ImageGetW_t H_p = (ImageGetW_t)sym(cg, "CGImageGetHeight");
    CFStringCreateWithCString_t Str_p =
        (CFStringCreateWithCString_t)sym(cf, "CFStringCreateWithCString");
    CFURLCreateWithFileSystemPath_t Url_p =
        (CFURLCreateWithFileSystemPath_t)sym(cf,
                                             "CFURLCreateWithFileSystemPath");
    ImageDestCreateWithURL_t Dst_p =
        (ImageDestCreateWithURL_t)sym(io, "CGImageDestinationCreateWithURL");
    ImageDestAddImage_t Add_p =
        (ImageDestAddImage_t)sym(io, "CGImageDestinationAddImage");
    ImageDestFinalize_t Fin_p =
        (ImageDestFinalize_t)sym(io, "CGImageDestinationFinalize");
    CFRelease_t Rel_p = (CFRelease_t)sym(cf, "CFRelease");
    CFShow_t Show_p = (CFShow_t)sym(cf, "CFShow");

    // first list all onscreen windows so caller can map ids
    if (ListCreate_p && argc < 2) {
        CFArrayRef wins = ListCreate_p(
            0x1 /*kCGWindowListOptionOnScreenOnly*/, 0);
        fprintf(stderr, "onscreen windows: %ld\n",
                wins ? Cnt_p(wins) : -1L);
        if (wins && Show_p) Show_p(wins);
    }
    for (int i = 1; i < argc; i++) {
        CGWindowID wid = (CGWindowID)strtoul(argv[i], NULL, 0);
        // kCGWindowListOptionIncludingWindow = 1<<4? use kCGWindowListOptionAll
        // + explicit id: option = kCGWindowListOptionIncludingWindow(0x10) does
        // not exist; public API uses kCGWindowListOptionIncludingWindow=0x10? —
        // actually kCGWindowListOptionIncludingWindow == 1<<4?  Known public:
        //   kCGWindowListOptionAll=0, OnScreenOnly=1, OnScreenAboveWindow=2,
        //   OnScreenAboveSystem=4, OnScreenBelowWindow=8,
        //   OnScreenBelowSystem=16, IncludingWindow=32? use 1|0x10.
        CGImageRef img = CreateImage_p(1.0/0.0, 1.0/0.0, 0, 0,
                                       0x20 /*IncludingWindow*/ | 0x1, wid,
                                       0 /*kCGWindowImageDefault*/);
        if (!img)
            img = CreateImage_p(1.0/0.0, 1.0/0.0, 0, 0, 0x20, wid, 0);
        if (!img) {
            fprintf(stderr, "wid %u: NULL\n", wid);
            continue;
        }
        fprintf(stderr, "wid %u: %zux%zu\n", wid, W_p(img), H_p(img));
        char path[256];
        snprintf(path, sizeof(path), "/tmp/wincap_%u.png", wid);
        CFStringRef ps = Str_p(NULL, path, 0x8000100);
        CFURLRef url = Url_p(NULL, ps, 0, 0);
        CFStringRef ty = Str_p(NULL, "public.png", 0x8000100);
        CGImageDestinationRef dst = Dst_p(url, ty, 1, NULL);
        if (dst) {
            Add_p(dst, img, NULL);
            fprintf(stderr, "wrote %s fin=%d\n", path, Fin_p(dst));
            Rel_p(dst);
        }
        Rel_p(img);
    }
    return 0;
}
