// Create a remote CAContext, attach a solid-colour layer, commit, and
// intercept mach_msg via a classic __interpose section to prove whether
// the commit is sent to the render server and what comes back.
#import <dlfcn.h>
#import <stdio.h>
#import <string.h>
#import <Foundation/Foundation.h>
#import <objc/message.h>
#import <mach/mach.h>
#import <QuartzCore/QuartzCore.h>

typedef mach_msg_return_t (*mach_msg_fn)(mach_msg_header_t *, mach_msg_option_t,
    mach_msg_size_t, mach_msg_size_t, mach_port_name_t, mach_msg_timeout_t,
    mach_port_name_t);
static mach_msg_fn orig_mach_msg = NULL;

static mach_msg_return_t hook_mach_msg(mach_msg_header_t *m,
    mach_msg_option_t o, mach_msg_size_t ss, mach_msg_size_t rl,
    mach_port_name_t rn, mach_msg_timeout_t t, mach_port_name_t np) {
    if (o & MACH_SEND_MSG) {
        fprintf(stderr, "MSG-SEND id=0x%x bits=0x%x remote=%u local=%u size=%u\n",
                m->msgh_id, m->msgh_bits, m->msgh_remote_port,
                m->msgh_local_port, ss);
    }
    mach_msg_return_t r = orig_mach_msg(m, o, ss, rl, rn, t, np);
    if ((o & (MACH_SEND_MSG|MACH_RCV_MSG)) == (MACH_SEND_MSG|MACH_RCV_MSG))
        fprintf(stderr, "  -> rc=0x%x replyId=0x%x\n", r, m->msgh_id);
    return r;
}

typedef mach_msg_return_t (*mach_msg_ovwr_fn)(mach_msg_header_t *,
    mach_msg_option_t, mach_msg_size_t, mach_msg_size_t, mach_port_name_t,
    mach_msg_timeout_t, mach_port_name_t, mach_msg_header_t *,
    mach_msg_size_t);
static mach_msg_ovwr_fn orig_mach_msg_ovwr = NULL;
static mach_msg_return_t hook_mach_msg_ovwr(mach_msg_header_t *m,
    mach_msg_option_t o, mach_msg_size_t ss, mach_msg_size_t rl,
    mach_port_name_t rn, mach_msg_timeout_t t, mach_port_name_t np,
    mach_msg_header_t *rm, mach_msg_size_t trl) {
    if (o & MACH_SEND_MSG) {
        fprintf(stderr, "OVWR-SEND id=0x%x bits=0x%x remote=%u local=%u size=%u\n",
                m->msgh_id, m->msgh_bits, m->msgh_remote_port,
                m->msgh_local_port, ss);
    }
    mach_msg_return_t r = orig_mach_msg_ovwr(m, o, ss, rl, rn, t, np, rm, trl);
    mach_msg_header_t *rh = rm ? rm : m;
    if ((o & (MACH_SEND_MSG|MACH_RCV_MSG)) == (MACH_SEND_MSG|MACH_RCV_MSG))
        fprintf(stderr, "  -> ovwr rc=0x%x replyId=0x%x\n", r, rh->msgh_id);
    return r;
}

struct interpose_pair { const void *replacement; const void *replacee; };
__attribute__((used))
static struct interpose_pair _interpose_pairs[]
    __attribute__((section("__DATA,__interpose"))) = {
    { (const void *)hook_mach_msg, (const void *)mach_msg },
    { (const void *)hook_mach_msg_ovwr, (const void *)mach_msg_overwrite },
};

__attribute__((constructor)) static void grab_orig(void) {
    orig_mach_msg = (mach_msg_fn)dlsym(RTLD_NEXT, "mach_msg");
    orig_mach_msg_ovwr = (mach_msg_ovwr_fn)dlsym(RTLD_NEXT, "mach_msg_overwrite");
}

int main(void) {
    @autoreleasepool {
        fprintf(stderr, "orig_mach_msg=%p hook=%p (self send test)\n",
                orig_mach_msg, hook_mach_msg);
        mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE,
                           &(mach_port_name_t){0});

        void *qc = dlopen("/System/Library/Frameworks/QuartzCore.framework/QuartzCore",
                          RTLD_LAZY | RTLD_GLOBAL);
        if (!qc) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }
        Class CAContext = NSClassFromString(@"CAContext");
        Class CALayer = NSClassFromString(@"CALayer");
        if (!CAContext || !CALayer) return 1;

        // Resolve the real option key strings exported by QuartzCore.
        const char *keySyms[] = {
            "_kCAContextDisplayId", "_kCAContextDisplayName",
            "_kCAContextDisplayable", "_kCAContextDisplayFilter",
            "_kCADisplayId", "_kCAContextIgnoresHitTest",
        };
        NSString *displayKey = nil;
        for (int i = 0; i < 6; i++) {
            void *sym = dlsym(qc, keySyms[i] + 1); // strip leading _
            if (sym) {
                CFStringRef s = *(CFStringRef *)sym;
                fprintf(stderr, "  %s -> %s\n", keySyms[i] + 1,
                        s ? CFStringGetCStringPtr(s, kCFStringEncodingUTF8) : "(null)");
                if (i == 0 && s) displayKey = (__bridge NSString *)s;
            }
        }
        if (!displayKey) displayKey = @"CAContextDisplayId";

        // Display-bound context: same path WindowServer's own surfaces use.
        id opts = @{ displayKey: @1 };
        id ctx = ((id(*)(id,SEL,id))objc_msgSend)(
            CAContext, sel_registerName("remoteContextWithOptions:"), opts);
        if (!ctx)
            ctx = ((id(*)(id,SEL))objc_msgSend)(CAContext,
                                               sel_registerName("remoteContext"));
        fprintf(stderr, "ctx=%p contextId=%u\n", ctx,
                ctx ? ((unsigned(*)(id,SEL))objc_msgSend)(
                          ctx, sel_registerName("contextId")) : 0);
        if (!ctx) return 2;

        id layer = ((id(*)(id,SEL))objc_msgSend)(CALayer, sel_registerName("layer"));
        ((void(*)(id,SEL,CGRect))objc_msgSend)(layer, sel_registerName("setFrame:"),
                                              CGRectMake(100, 100, 400, 300));
        CGColorRef color = CGColorCreateGenericRGB(1, 0, 0, 1);
        ((void(*)(id,SEL,id))objc_msgSend)(layer, sel_registerName("setBackgroundColor:"),
                                          (__bridge id)color);
        ((void(*)(id,SEL,float))objc_msgSend)(layer, sel_registerName("setOpacity:"), 1.0f);
        ((void(*)(id,SEL,id))objc_msgSend)(ctx, sel_registerName("setLayer:"), layer);
        fprintf(stderr, "layer set, committing...\n");
        id txnCls = NSClassFromString(@"CATransaction");
        if (txnCls) {
            ((void(*)(id,SEL))objc_msgSend)(txnCls, sel_registerName("flush"));
            ((void(*)(id,SEL))objc_msgSend)(txnCls, sel_registerName("commit"));
        }
        // keep the context (and its server-side registration) alive while we
        // take a screenshot externally
        fprintf(stderr, "committed; holding 20s for capture\n");
        sleep(20);
        fprintf(stderr, "done\n");
        CGColorRelease(color);
    }
    return 0;
}
