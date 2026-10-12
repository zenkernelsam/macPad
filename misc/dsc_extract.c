// Extract dylibs from a dyld_shared_cache using Apple's own
// dsc_extractor.bundle (present inside the rootfs at /usr/lib/).
// Usage: dsc_extract <cache> <outputdir> [dylib-substring-filter]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

typedef int (*extract_progress_t)(const char *cache_path,
                                  const char *output_dir,
                                  void (^progress)(unsigned current, unsigned total));

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s <cache> <outdir> [filter]\n", argv[0]); return 2; }
    const char *filter = argc > 3 ? argv[3] : NULL;
    void *b = dlopen("/usr/lib/dsc_extractor.bundle", RTLD_LAZY | RTLD_LOCAL);
    if (!b) { fprintf(stderr, "dlopen bundle: %s\n", dlerror()); return 1; }
    extract_progress_t fn = (extract_progress_t)dlsym(
        b, "dyld_shared_cache_extract_dylibs_progress");
    if (!fn) { fprintf(stderr, "no extract symbol\n"); return 1; }
    __block const char *f = filter;
    int rc = fn(argv[1], argv[2], ^(unsigned cur, unsigned total) {
        if ((cur & 0x3f) == 0) fprintf(stderr, "extract %u/%u\n", cur, total);
        (void)f;
    });
    fprintf(stderr, "extract rc=%d\n", rc);
    return rc;
}
