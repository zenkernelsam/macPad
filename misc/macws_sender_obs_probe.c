// macws_sender_obs_probe.c — D048 disposable arm64e one-shot probe.
//
// Helper-level ABI/forwarding validation of the REAL
// MacWSSenderObservation invoke bodies (mac_hooks.m forwards these
// exact functions).  No SkyLight hooking, no provider changes, no
// WindowServer interaction: mock originals stand in for the three
// RE-confirmed boundaries; a nested fake gate original calls the
// dispatcher invoke exactly as the genuine gate calls the (hooked)
// dispatcher in WindowServer.
//
// Packet-owned paths: /tmp/macws_d048_probe_sentinel + .log — created
// and removed by this probe only.  Compiled with the .c's
// MACWS_SENDER_OBS_TEST seam so the paths are redirectable; that macro
// never enters the production .theos object (verified by build log).

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MACWS_SENDER_OBS_TEST 1
#include "../libmachook/MacWSSenderObservation.h"

extern const char *g_macws_sender_obs_sentinel_path;
extern const char *g_macws_sender_obs_log_path;

static int failures;
#define CHECK(cond, msg) do { \
    if (cond) printf("  PASS %s\n", msg); \
    else { printf("  FAIL %s\n", msg); failures++; } \
} while (0)

/* ---- fake audit container as the gate itself reads it ---- */
struct fake_audit { char pad0[4]; uint32_t size;
                    char pad1[0xC]; uint8_t body[32]; char tail[8]; };
static struct fake_audit g_audit;
static void *g_cell;                 /* the gCurMsgAudit-style cell  */
static void **g_slot = &g_cell;
static uint8_t g_desc[40];
static const uint8_t *g_descp = g_desc;
static volatile uint64_t g_lazy;
static volatile uint64_t *g_lazyp = &g_lazy;
static int g_post_ran;
static uint32_t g_post_euid, g_post_pid;

/* ---- mock originals (PAC-signed fn ptrs on arm64e) ---- */
static int mo_impl(void *a, void *s) {
    (void)a; (void)s;
    return 0;                       /* like impl: deny path */
}
static int mo_disp(void *a, void *d, void *impl) {
    (void)d;
    /* the real dispatcher calls the impl on cache miss — emulate */
    return macws_sender_obs_impl_invoke(mo_impl, a, impl, &g_lazyp);
}
static int mo_gate(void *a, void *b) {
    (void)a; (void)b;
    /* the real gate calls the dispatcher — emulate the chain */
    return macws_sender_obs_disp_invoke(mo_disp, g_audit.body,
                                        g_desc, (void *)mo_impl);
}
static int mo_errno_chk(void *a, void *b) { (void)a;(void)b; return errno == EDOM ? 7 : -9; }
static int mo_errno_set(void *a, void *b) { (void)a;(void)b; errno = EBADF; return -1; }

static unsigned pre_hook(void *ctx) { (void)ctx; errno = EFAULT; return 0xAB; }
static void post_hook(void *ctx, const MacWSSenderObsRecord *rec,
                      uint32_t euid, uint32_t pid, unsigned tok) {
    (void)ctx; (void)rec; (void)tok;
    g_post_ran++; g_post_euid = euid; g_post_pid = pid;
    errno = EAGAIN;
}

int main(void) {
    const char *sent = "/tmp/macws_d048_probe_sentinel";
    const char *log  = "/tmp/macws_d048_probe_log";
    g_macws_sender_obs_sentinel_path = sent;
    g_macws_sender_obs_log_path = log;
    unlink(sent); unlink(log);
    memset(&g_audit, 0, sizeof g_audit);
    g_audit.size = 0x34;
    for (int i = 0; i < 32; i++) g_audit.body[i] = (uint8_t)(0x60 + i);
    memset(g_desc, 0x33, sizeof g_desc);
    g_cell = &g_audit;
    MacWSSenderObsGateEnv env; memset(&env, 0, sizeof env);
    env.audit_slot = &g_slot; env.right_desc = &g_descp;
    env.pre_orig = pre_hook; env.post_orig = post_hook;

    printf("== d048 isolated probe (arm64e, real invoke bodies) ==\n");

    /* -- default-off: origs run, errno envelope, zero capture -- */
    uint64_t sn0 = macws_sender_obs_snaps();
    errno = EDOM;
    int r = macws_sender_obs_gate_invoke(mo_errno_chk, 0, 0, &env);
    CHECK(r == 7 && errno == EDOM, "disarmed gate: orig ran via PAC ptr, errno intact");
    CHECK(macws_sender_obs_snaps() == sn0, "disarmed: zero record captures");
    CHECK(macws_sender_obs_seq() == 0, "disarmed: nothing stored");
    CHECK(g_post_ran == 1, "env hooks still ran (umbrella path preserved)");

    /* -- arm + nested gate->disp->impl chain -- */
    int sfd = open(sent, O_CREAT | O_WRONLY, 0600); close(sfd);
    g_post_ran = 0;
    errno = EDOM;
    uint64_t n0 = 0; (void)macws_sender_obs_ring(&n0);
    r = macws_sender_obs_gate_invoke(mo_gate, 0, 0, &env);
    CHECK(r == 0, "nested chain rc verbatim (impl deny -> 0)");
    CHECK(errno == EDOM, "nested chain: caller sees orig errno");
    uint64_t n = 0;
    const MacWSSenderObsRecord *ring = macws_sender_obs_ring(&n);
    CHECK(n == n0 + 3, "3 chain records published");
    /* Nested emits land innermost-first (each wrapper emits post-orig),
       so the 3 records arrive I,D,G in the ring — locate by site, not
       position.  This also matches live-WS ordering. */
    const MacWSSenderObsRecord *G = 0, *D = 0, *I = 0;
    for (uint64_t i = n - 3; i < n; i++) {
        if (ring[i].site == 1) G = &ring[i];
        if (ring[i].site == 2) D = &ring[i];
        if (ring[i].site == 3) I = &ring[i];
    }
    int ok = (G && D && I);
    if (ok && !(G->flags & MACWS_SENDER_OBS_F_CHAIN_ROOT)) ok = 0;
    if (ok && (D->flags & (MACWS_SENDER_OBS_F_CHAIN_ROOT|MACWS_SENDER_OBS_F_STANDALONE))) ok = 0;
    if (ok && (I->flags & (MACWS_SENDER_OBS_F_CHAIN_ROOT|MACWS_SENDER_OBS_F_STANDALONE))) ok = 0;
    if (ok && !(G->invocation && G->invocation == D->invocation &&
                D->invocation == I->invocation)) ok = 0;
    if (ok && (G->magic != 0x4d534f42 || G->token[0] != 0x60 ||
               G->desc[0] != 0x33 || G->rc != 0)) ok = 0;
    if (ok && I->arg_ptr != (uint64_t)(uintptr_t)mo_impl) ok = 0;
    CHECK(ok, "chain records: shared invocation, verbatim fields, fn-ptr identity");
    /* euid @+0x18 -> body[4], pid @+0x28 -> body[0x14] */
    {
        uint32_t eu, pi; memcpy(&eu, g_audit.body + 4, 4);
        memcpy(&pi, g_audit.body + 0x14, 4);
        CHECK(g_post_euid == eu && g_post_pid == pi,
              "post_orig scalars (euid/pid) forwarded pre-orig");
    }
    /* -- errno: orig-set forwarded over hooks' clobber -- */
    errno = EINVAL;
    r = macws_sender_obs_gate_invoke(mo_errno_set, 0, 0, &env);
    CHECK(r == -1 && errno == EBADF, "orig errno EBADF forwarded, clobber hidden");

    /* -- log: 0600 regular, exact record bytes -- */
    struct stat st;
    CHECK(stat(log, &st) == 0 && S_ISREG(st.st_mode) &&
          (st.st_mode & 0777) == 0600, "log 0600 regular");
    CHECK(st.st_size == (off_t)(n * sizeof(MacWSSenderObsRecord)) ||
          st.st_size == (off_t)((n + 1) * sizeof(MacWSSenderObsRecord)),
          "log holds written records");

    /* -- disarm: capture ceases, orig still runs -- */
    unlink(sent);
    sn0 = macws_sender_obs_snaps();
    uint64_t d0 = macws_sender_obs_dropped();
    errno = EDOM;
    r = macws_sender_obs_gate_invoke(mo_errno_chk, 0, 0, &env);
    CHECK(r == 7 && errno == EDOM, "disarm: orig still ran, errno intact");
    CHECK(macws_sender_obs_snaps() == sn0, "disarm: capture ceased");
    CHECK(macws_sender_obs_dropped() > d0, "disarm: emit dropped-counted");

    unlink(sent); unlink(log);
    printf("%s (failures=%d)\n", failures ? "FAIL" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
