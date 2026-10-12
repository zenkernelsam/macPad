// Capture the virtual display through CGDisplayCreateImage — WindowServer
// renders the display contents itself, so the returned image shows exactly
// what the server-side compositor produced (independent of the host-side
// presentation path).  Saves PNG(s) under /tmp/.
//
// Usage: dispcap_probe [outPrefix]
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <string.h>

typedef uint32_t CGDirectDisplayID;
typedef uint32_t CGWindowID;
typedef struct __CGImage *CGImageRef;
typedef struct __CFArray *CFArrayRef;
typedef const void *CFTypeRef;
typedef long CFIndex;
typedef struct __CFString *CFStringRef;
typedef struct __CFURL *CFURLRef;
typedef struct __CGImageDestination *CGImageDestinationRef;

typedef int (*GetActiveDisplayList_t)(uint32_t max, CGDirectDisplayID *list,
                                      uint32_t *count);
typedef CGImageRef (*DisplayCreateImage_t)(CGDirectDisplayID disp);
typedef CGImageRef (*DisplayCreateImageForRect_t)(CGDirectDisplayID disp,
                                                  double x, double y,
                                                  double w, double h);
typedef size_t (*ImageGetW_t)(CGImageRef);
typedef CFStringRef (*CFStringCreateWithCString_t)(void *alloc, const char *s,
                                                   unsigned enc);
typedef CFURLRef (*CFURLCreateWithFileSystemPath_t)(void *alloc, CFStringRef s,
                                                    long style, int isDir);
typedef CGImageDestinationRef (*ImageDestCreateWithURL_t)(CFURLRef url,
                                                        CFStringRef type,
                                                        size_t count, void *o);
typedef void (*ImageDestAddImage_t)(CGImageDestinationRef d, CGImageRef i,
                                    void *p);
typedef int (*ImageDestFinalize_t)(CGImageDestinationRef d);
typedef void (*CFRelease_t)(CFTypeRef);

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
        fprintf(stderr, "dlopen failed: cg=%p cf=%p io=%p err=%s\n", cg, cf, io,
                dlerror());
        return 1;
    }
    GetActiveDisplayList_t CGGetActiveDisplayList_p =
        (GetActiveDisplayList_t)sym(cg, "CGGetActiveDisplayList");
    DisplayCreateImage_t CGDisplayCreateImage_p =
        (DisplayCreateImage_t)sym(cg, "CGDisplayCreateImage");
    ImageGetW_t CGImageGetWidth_p = (ImageGetW_t)sym(cg, "CGImageGetWidth");
    ImageGetW_t CGImageGetHeight_p = (ImageGetW_t)sym(cg, "CGImageGetHeight");
    CFStringCreateWithCString_t CFStringCreateWithCString_p =
        (CFStringCreateWithCString_t)sym(cf, "CFStringCreateWithCString");
    CFURLCreateWithFileSystemPath_t CFURLCreateWithFileSystemPath_p =
        (CFURLCreateWithFileSystemPath_t)sym(cf,
                                             "CFURLCreateWithFileSystemPath");
    ImageDestCreateWithURL_t CGImageDestinationCreateWithURL_p =
        (ImageDestCreateWithURL_t)sym(io, "CGImageDestinationCreateWithURL");
    ImageDestAddImage_t CGImageDestinationAddImage_p =
        (ImageDestAddImage_t)sym(io, "CGImageDestinationAddImage");
    ImageDestFinalize_t CGImageDestinationFinalize_p =
        (ImageDestFinalize_t)sym(io, "CGImageDestinationFinalize");
    CFRelease_t CFRelease_p = (CFRelease_t)sym(cf, "CFRelease");
    if (!CGGetActiveDisplayList_p || !CGDisplayCreateImage_p) return 1;

    CGDirectDisplayID list[16];
    uint32_t count = 0;
    int rc = CGGetActiveDisplayList_p(16, list, &count);
    fprintf(stderr, "displays rc=%d count=%u:", rc, count);
    for (uint32_t i = 0; i < count; i++) fprintf(stderr, " %u", list[i]);
    fprintf(stderr, "\n");

    const char *prefix = argc > 1 ? argv[1] : "/tmp/dispcap";
    for (uint32_t i = 0; i < count; i++) {
        CGImageRef img = CGDisplayCreateImage_p(list[i]);
        if (!img) {
            fprintf(stderr, "display %u: CGDisplayCreateImage -> NULL\n",
                    list[i]);
            continue;
        }
        fprintf(stderr, "display %u: image %zux%zu\n", list[i],
                CGImageGetWidth_p ? CGImageGetWidth_p(img) : 0,
                CGImageGetHeight_p ? CGImageGetHeight_p(img) : 0);
        char path[256];
        snprintf(path, sizeof(path), "%s_%u.png", prefix, list[i]);
        CFStringRef ps = CFStringCreateWithCString_p(NULL, path, 0x8000100);
        CFURLRef url =
            CFURLCreateWithFileSystemPath_p(NULL, ps, 0 /*POSIX*/, 0);
        CFStringRef ty =
            CFStringCreateWithCString_p(NULL, "public.png", 0x8000100);
        CGImageDestinationRef dst =
            CGImageDestinationCreateWithURL_p(url, ty, 1, NULL);
        if (dst) {
            CGImageDestinationAddImage_p(dst, img, NULL);
            int ok = CGImageDestinationFinalize_p(dst);
            fprintf(stderr, "wrote %s finalize=%d\n", path, ok);
            CFRelease_p(dst);
        }
        CFRelease_p(img);
    }
    return 0;
}
