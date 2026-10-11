// Minimal probe replicating HIServices' launchservicesd request shape:
// create mach-service connection to com.apple.coreservices.launchservicesd
// (libmachook maps it to the macosbooter name), send
// {command:100, session:<asid>, clientversion:0x10900000}, print the reply.
// Run via launchdchrootexec so bootstrap context and insertion match Dock.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <mach/mach.h>
#include <servers/bootstrap.h>
#include <xpc/xpc.h>

typedef xpc_connection_t (*create_mach_service_fn)(
    const char *, dispatch_queue_t, uint64_t);

int main(int argc, char **argv) {
    setbuf(stdout, NULL);
    const char *name = argc > 1
        ? argv[1] : "com.apple.coreservices.launchservicesd";
    fprintf(stderr, "probe: creating mach-service connection to %s\n", name);
    // Control: resolve the (post-interpose) name through the raw bootstrap
    // lookup so we can tell "name never resolves" from "connection invalidated".
    const char *mapped = argc > 4
        ? argv[4] : "com.apple.macosbooter.coreservices.launchservicesd";
    mach_port_t servicePort = MACH_PORT_NULL;
    kern_return_t kr = bootstrap_look_up(bootstrap_port, mapped, &servicePort);
    fprintf(stderr, "probe: bootstrap_look_up(%s) kr=%#x port=%#x\n",
            mapped, kr, servicePort);

    create_mach_service_fn create_mach_service = (create_mach_service_fn)
        dlsym(RTLD_DEFAULT, "xpc_connection_create_mach_service");
    if (!create_mach_service) {
        fprintf(stderr, "probe: xpc_connection_create_mach_service missing\n");
        return 2;
    }
    xpc_connection_t conn = create_mach_service(name, NULL, 0);
    if (!conn) { fprintf(stderr, "probe: create returned NULL\n"); return 2; }
    xpc_connection_set_event_handler(conn, ^(xpc_object_t ev) {
        fprintf(stderr, "probe: event %s\n",
                xpc_get_type(ev) == XPC_TYPE_ERROR
                    ? xpc_dictionary_get_string(ev, XPC_ERROR_KEY_DESCRIPTION)
                    : "non-error");
    });
    xpc_connection_activate(conn);

    xpc_object_t req = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_int64(req, "command", argc > 2 ? atoi(argv[2]) : 100);
    xpc_dictionary_set_int64(req, "session",
        argc > 3 ? strtoll(argv[3], NULL, 0) : 0x4D5753);
    xpc_dictionary_set_uint64(req, "clientversion", 0x10900000);

    xpc_object_t reply =
        xpc_connection_send_message_with_reply_sync(conn, req);
    char *desc = xpc_copy_description(reply);
    fprintf(stderr, "probe: reply: %s\n", desc ? desc : "(null)");
    free(desc);
    return reply == XPC_ERROR_CONNECTION_INVALID ? 3 : 0;
}
