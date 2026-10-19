/* D206 — analysis-only typed PASS wrapper.
 *
 * d141_wrap_pass's uintptr_t(void*) callback cannot represent the
 * services-pass ABI `int f(int, double)` (D205). This candidate is the
 * typed contract only: no MSHookFunction, no permit, no binary offset,
 * no runtime symbol, no WS/SkyLight/sender invocation. Emission uses
 * the accepted d141_emit exclusively; collection uses d141_drain.
 *
 * Contract:
 *  - emits PASS_ENTER (msgid=0, aux=0), calls orig exactly once, emits
 *    PASS_EXIT, returns orig's int unchanged.
 *  - errno: caller's errno reaches orig; orig's errno reaches caller —
 *    emits are bracketed with save/restore (emit is not guaranteed
 *    errno-neutral: it calls clock_gettime/pthread_threadid_np).
 *  - no locks, allocation, write, or blocking call around orig (D9/D10).
 *  - a non-returning orig (fatal path) yields no PASS_EXIT — by
 *    definition, documented not bypassed.
 */
#ifndef D206_PASS_WRAPPER_H
#define D206_PASS_WRAPPER_H

#include "../d141-svctime-candidate/d141_svctime.h"

int d206_wrap_pass(int (*orig)(int, double), int x, double d);

#endif /* D206_PASS_WRAPPER_H */
