// CAContext remote-context creation probe - uses performSelector to avoid
// private-header requirements.
#import <dlfcn.h>
#import <stdio.h>
#import <Foundation/Foundation.h>
#import <objc/message.h>

int main(void) {
    @autoreleasepool {
        void *qc = dlopen("/System/Library/Frameworks/QuartzCore.framework/QuartzCore",
                          RTLD_LAZY | RTLD_GLOBAL);
        if (!qc) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }
        Class CAContext = NSClassFromString(@"CAContext");
        fprintf(stderr, "CAContext class=%p\n", CAContext);
        if (!CAContext) return 1;
        id ctx = ((id(*)(id,SEL))objc_msgSend)(CAContext, sel_registerName("remoteContext"));
        fprintf(stderr, "remoteContext -> %p\n", ctx);
        if (ctx) {
            unsigned cid_ = ((unsigned(*)(id,SEL))objc_msgSend)(ctx, sel_registerName("contextId"));
            fprintf(stderr, "contextId=%u\n", cid_);
        }
        id ctx2 = ((id(*)(id,SEL,id))objc_msgSend)(CAContext, sel_registerName("remoteContextWithOptions:"), nil);
        fprintf(stderr, "remoteContextWithOptions:nil -> %p\n", ctx2);
        fflush(stderr);
        sleep(1);
    }
    return 0;
}
