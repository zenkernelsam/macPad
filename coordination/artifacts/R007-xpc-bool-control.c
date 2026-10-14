#include <xpc/xpc.h>
#include <stdio.h>

/* Host API control only: no service, policy, or remote request is made. */
int main(void) {
    xpc_object_t d = xpc_dictionary_create(NULL, NULL, 0);
    printf("missing: value_present=%d bool=%d\n",
           xpc_dictionary_get_value(d, "result") != NULL,
           xpc_dictionary_get_bool(d, "result"));
    xpc_dictionary_set_bool(d, "result", false);
    printf("explicit_false: value_present=%d bool=%d\n",
           xpc_dictionary_get_value(d, "result") != NULL,
           xpc_dictionary_get_bool(d, "result"));
    xpc_dictionary_set_bool(d, "result", true);
    printf("explicit_true: value_present=%d bool=%d\n",
           xpc_dictionary_get_value(d, "result") != NULL,
           xpc_dictionary_get_bool(d, "result"));
    xpc_release(d);
    return 0;
}
