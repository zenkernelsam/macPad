// macwstccdprobe — bounded client-context witness for the private Ventura
// system-tccd transport.  This binary runs inside the chroot via
// launchdchrootexec, which injects libmachook, so
// "com.apple.tccd.system" resolves through the same
// macws_private_chroot_service_name mapping that WindowServer's
// TCC.framework calls use.  It is the readiness witness
// start_tccd_system consults before declaring the endpoint usable.
//
//   macwstccdprobe ping
//       Bounded round-trip: creates the mach-service connection, posts a
//       minimal dictionary and waits at most PING_TIMEOUT_SECONDS for a
//       reply.  Exit 0 only when the daemon answers with a real XPC
//       dictionary (an error singleton, a malformed reply, a timeout or a
//       connection failure all fail closed with distinct nonzero codes).
//       A dictionary reply proves registration, reachability and a live
//       responder in one observation; it does NOT assert policy.
//
//   macwstccdprobe preflight [service]
//       Calls the genuine TCCAccessPreflightWithAuditToken SPI against the
//       caller's own audit token and prints the raw result plus the caller
//       identity (pid/uid/audit token/entitlement view).  Exit 0 when the
//       SPI itself executed — the returned kTCCAccessPreflightResult is a
//       datum for the log, not a readiness verdict.  Combine with
//       MACWS_LSXPC_TRACE=1 in the environment to have libmachook's
//       existing DYLD_INTERPOSE dump the exact request and reply
//       dictionaries for schema qualification.
//
// The probe never edits TCC state, never fabricates replies and never
// touches the native iOS daemon: it only connects to the mapped private
// endpoint and reads what the real responder returns.

#include <dispatch/dispatch.h>
#include <dlfcn.h>
#include <mach/mach.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <xpc/xpc.h>
#include <CoreFoundation/CoreFoundation.h>

#define PING_TIMEOUT_SECONDS 5
#define WATCHDOG_ALARM_SECONDS 15

static const char *kClientServiceName = "com.apple.tccd.system";
static const char *kDefaultPreflightService = "kTCCServicePostEvent";

static const char *xpc_type_name(xpc_type_t type) {
    if (type == XPC_TYPE_DICTIONARY) return "dictionary";
    if (type == XPC_TYPE_ERROR)      return "error";
    if (type == XPC_TYPE_CONNECTION) return "connection";
    if (type == XPC_TYPE_STRING)     return "string";
    if (type == XPC_TYPE_BOOL)       return "bool";
    if (type == XPC_TYPE_INT64)      return "int64";
    if (type == XPC_TYPE_UINT64)     return "uint64";
    if (type == XPC_TYPE_NULL)       return "null";
    return "other";
}

// 0 = real dictionary reply (the only ready verdict).
// 2 = transport error (connection_invalid / interrupted / peer unavailable).
// 3 = reply arrived but was neither dictionary nor a known error object.
// 4 = no reply inside the bounded window.
// 5 = could not create/resume the mach-service connection at all.
static int run_ping(void) {
    alarm(WATCHDOG_ALARM_SECONDS);
    xpc_connection_t connection =
        xpc_connection_create_mach_service(kClientServiceName, NULL, 0);
    if (connection == NULL) {
        printf("TCCPING create-failed service=%s\n", kClientServiceName);
        return 5;
    }
    dispatch_semaphore_t finished = dispatch_semaphore_create(0);
    __block xpc_object_t reply = NULL;
    xpc_connection_set_event_handler(connection, ^(xpc_object_t event) {
        (void)event;   // connection-state events are handled by the sync API
    });
    xpc_connection_resume(connection);

    xpc_object_t message = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_string(message, "macws-probe", "ping");

    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        reply = xpc_connection_send_message_with_reply_sync(connection, message);
        dispatch_semaphore_signal(finished);
    });

    long waited = dispatch_semaphore_wait(
        finished, dispatch_time(DISPATCH_TIME_NOW,
                                (int64_t)PING_TIMEOUT_SECONDS * NSEC_PER_SEC));
    if (waited != 0) {
        printf("TCCPING timeout service=%s after=%ds\n",
               kClientServiceName, PING_TIMEOUT_SECONDS);
        return 4;
    }
    if (reply == NULL) {
        printf("TCCPING null-reply service=%s\n", kClientServiceName);
        return 3;
    }
    xpc_type_t type = xpc_get_type(reply);
    char *description = xpc_copy_description(reply);
    if (type == XPC_TYPE_ERROR) {
        printf("TCCPING transport-error service=%s reply=%s\n",
               kClientServiceName, description ?: "(no description)");
        free(description);
        return 2;
    }
    printf("TCCPING reply-type=%s service=%s\n",
           xpc_type_name(type), kClientServiceName);
    if (description != NULL) {
        printf("TCCPING reply=%s\n", description);
        free(description);
    }
    return type == XPC_TYPE_DICTIONARY ? 0 : 3;
}

static void print_caller_identity(void) {
    audit_token_t token;
    memset(&token, 0, sizeof(token));
    mach_msg_type_number_t count = TASK_AUDIT_TOKEN_COUNT;
    kern_return_t kr = task_info(mach_task_self(), TASK_AUDIT_TOKEN,
                               (task_info_t)&token, &count);
    printf("TCCPREFLIGHT pid=%d uid=%d audit-rc=%d", getpid(), getuid(), kr);
    if (kr == KERN_SUCCESS) {
        printf(" audit=");
        for (size_t i = 0; i < 8; i++) {
            printf("%08x", token.val[i]);
        }
    }
    printf("\n");
}

static int run_preflight(const char *service) {
    print_caller_identity();
    void *tcc = dlopen("/System/Library/PrivateFrameworks/TCC.framework/TCC",
                       RTLD_NOW);
    typedef int64_t (*PreflightAuditFn)(CFStringRef, audit_token_t,
                                        CFDictionaryRef);
    PreflightAuditFn preflight = tcc
        ? (PreflightAuditFn)dlsym(tcc, "TCCAccessPreflightWithAuditToken")
        : NULL;
    printf("TCCPREFLIGHT tcc-framework=%p preflight-symbol=%p\n",
           tcc, (void *)preflight);
    if (preflight == NULL) {
        return 6;
    }
    audit_token_t token;
    memset(&token, 0, sizeof(token));
    mach_msg_type_number_t count = TASK_AUDIT_TOKEN_COUNT;
    if (task_info(mach_task_self(), TASK_AUDIT_TOKEN,
                  (task_info_t)&token, &count) != KERN_SUCCESS) {
        printf("TCCPREFLIGHT audit-token unavailable\n");
        return 6;
    }
    CFStringRef serviceName = CFStringCreateWithCString(
        NULL, service, kCFStringEncodingUTF8);
    int64_t result = preflight(serviceName, token, NULL);
    printf("TCCPREFLIGHT service=%s result=%lld\n",
           service, (long long)result);
    CFRelease(serviceName);
    return 0;
}

int main(int argc, char **argv) {
    const char *mode = argc > 1 ? argv[1] : "ping";
    if (strcmp(mode, "ping") == 0) {
        return run_ping();
    }
    if (strcmp(mode, "preflight") == 0) {
        return run_preflight(argc > 2 ? argv[2] : kDefaultPreflightService);
    }
    fprintf(stderr,
            "usage: macwstccdprobe <ping|preflight [service]>\n");
    return 64;
}
