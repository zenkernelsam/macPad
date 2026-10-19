#include "d206_pass_wrapper.h"

#include <errno.h>

int d206_wrap_pass(int (*orig)(int, double), int x, double d) {
    int s0 = errno;
    d141_emit(D141K_PASS_ENTER, 0, 0);
    errno = s0;                      /* caller's errno reaches orig    */
    int r = orig(x, d);              /* exactly once, typed signature  */
    int s1 = errno;
    d141_emit(D141K_PASS_EXIT, 0, 0);
    errno = s1;                      /* orig's errno reaches caller    */
    return r;
}
