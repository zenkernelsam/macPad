// test_sender_observation.c — host-side contract test for the D044
// record-only sender observation core (MacWSSenderObservation.c).
//
// Compile:  cc -DMACWS_SENDER_OBS_TEST -o /tmp/test_sender_obs \
//             misc/test_sender_observation.c libmachook/MacWSSenderObservation.c
//
// Covers:
//   default-off stores/emits nothing, counts calls+drops
//   armed -> fixed 144B records in ring + raw-written to log (mode0600)
//   simultaneous first emission across N threads (single-fd, ordered,
//   all records intact) + complete-record publication
//   TLS nested correlation: gate->disp->impl share one invocation id;
//   standalone inner-boundary call gets own id + STANDALONE flag;
//   no stale pairing after unwind
//   bounded write_all: partial writes / full-pipe EAGAIN failure,
//   bounded passes, no spin
//   log open validation: O_NOFOLLOW symlink rejection, directory
//   rejection, mode-0600 creation
//   live disarm (ceases record capture — snaps counter flat), budget cap
//   REAL wrapper invokes: errno envelope end-to-end (orig leaves errno,
//   orig sets errno, orig observes restored incoming errno, clobbering
//   env hooks), record capture fields, post-disarm orig still executes
//   log open negatives: symlink / FIFO / device / over-permissive file
//   prologue pins byte-equal the analysis image + corruption mismatch

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define MACWS_SENDER_OBS_TEST 1
#include "../libmachook/MacWSSenderObservation.h"

extern const char *g_macws_sender_obs_sentinel_path;
extern const char *g_macws_sender_obs_log_path;
int macws_sender_obs_write_all(int fd, const void *buf, unsigned long sz);

static int failures;
#define CHECK(cond, msg) do { \
    if (cond) { printf("  PASS %s\n", msg); } \
    else { printf("  FAIL %s\n", msg); failures++; } \
} while (0)

static MacWSSenderObsRecord tmpl_for(uint8_t site) {
    MacWSSenderObsRecord r;
    memset(&r, 0, sizeof(r));
    r.site = site;
    r.rc = -7;
    r.audit_size = 0x34;
    memset(r.token, 0xA0 + site, 32);
    return r;
}

/* ---- concurrency: N threads emit simultaneously ---- */
static void *emit_worker(void *arg) {
    long site = (long)arg;
    MacWSSenderObsRecord r = tmpl_for((uint8_t)site);
    macws_sender_obs_emit(&r);
    return NULL;
}

/* ---- fake originals / env for real-invoke tests ---- */
struct fake_audit {                     /* container layout as the gate */
    char pad0[4]; uint32_t size;        /* reads it: +4 size            */
    char pad1[0xC]; uint8_t body[32];   /* +0x14 token (32B)            */
    char tail[8];
};
static struct fake_audit g_fake_audit;
static void *g_fake_cell;               /* the gCurMsgAudit-style cell  */
static void **g_fake_slot = &g_fake_cell; /* void *** the env points at */
static uint8_t g_fake_desc[40];
static const uint8_t *g_fake_descp = g_fake_desc;
static volatile uint64_t g_fake_lazy;
static volatile uint64_t *g_fake_lazyp = &g_fake_lazy;
static int g_pre_ran, g_post_ran;
static uint32_t g_post_aeuid, g_post_apid;

static int fake_orig_sets2(void *a, void *b) { (void)a;(void)b; errno = EBADF; return -1; }
static int fake_orig_chk2(void *a, void *b)  { (void)a;(void)b; return errno == EDOM ? 7 : -9; }
static int fake_orig_keep3(void *a, void *b, void *c) { (void)a;(void)b;(void)c; return 42; }

static unsigned fake_pre(void *ctx) { (void)ctx; g_pre_ran++; errno = EFAULT; return 0xABCD; }
static void fake_post(void *ctx, const MacWSSenderObsRecord *rec,
                      uint32_t euid, uint32_t pid, unsigned tok) {
    (void)ctx; (void)rec; (void)tok;
    g_post_ran++; g_post_aeuid = euid; g_post_apid = pid;
    errno = EAGAIN;                        /* hook may clobber freely */
}

static MacWSSenderObsGateEnv fake_env(void) {
    MacWSSenderObsGateEnv e;
    memset(&e, 0, sizeof e);
    e.audit_slot = &g_fake_slot;
    e.right_desc = &g_fake_descp;
    e.pre_orig = fake_pre;
    e.post_orig = fake_post;
    return e;
}

/* ---- nested-chain worker: gate->disp->impl on one thread ---- */
static void *chain_worker(void *arg) {
    long base = (long)arg;
    uint64_t inv = 0;
    MacWSSenderObsRecord g = tmpl_for(1), d = tmpl_for(2), m = tmpl_for(3);
    uint8_t f1 = macws_sender_obs_chain_enter(1, &inv);
    g.invocation = inv; g.flags |= f1; g.rc = (int)base;
    macws_sender_obs_emit(&g);
    uint64_t inv2 = 0;
    (void)macws_sender_obs_chain_enter(2, &inv2);
    d.invocation = inv2; d.rc = (int)base + 1;
    macws_sender_obs_emit(&d);
    uint64_t inv3 = 0;
    (void)macws_sender_obs_chain_enter(3, &inv3);
    m.invocation = inv3; m.rc = (int)base + 2;
    macws_sender_obs_emit(&m);
    macws_sender_obs_chain_exit();
    macws_sender_obs_chain_exit();
    macws_sender_obs_chain_exit();
    return NULL;
}

int main(void) {
    const char *sent = "/tmp/msob_test_sentinel";
    const char *log  = "/tmp/msob_test_log";
    g_macws_sender_obs_sentinel_path = sent;
    g_macws_sender_obs_log_path = log;
    unlink(sent); unlink(log);

    printf("== default-off ==\n");
    errno = 0;
    CHECK(!macws_sender_observation_enabled(), "sentinel absent -> disabled");
    MacWSSenderObsRecord t = tmpl_for(1);
    errno = EACCES;
    macws_sender_obs_emit(&t);
    CHECK(errno == EACCES, "errno preserved across emit");
    CHECK(macws_sender_obs_site_calls(1) == 1, "call counted while disarmed");
    CHECK(macws_sender_obs_seq() == 0, "nothing stored while disarmed");
    CHECK(macws_sender_obs_dropped() == 1, "drop counted while disarmed");

    printf("== symlink rejection (O_NOFOLLOW) in forked child ==\n");
    /* The log open is a process-lifetime single attempt (pthread_once),
       so the rejection case runs in a child: its own first emit tries
       the symlinked path, fails, latches -1, counts dropped — while the
       parent's once state stays pristine for the success path below. */
    {
        int sfd0 = open(sent, O_CREAT | O_WRONLY, 0644); close(sfd0);
        pid_t kid = fork();
        if (kid == 0) {
            symlink("/tmp/msob_test_realfile", log);
            uint64_t dk = macws_sender_obs_dropped();
            macws_sender_obs_emit(&t);
            _exit((macws_sender_obs_dropped() == dk + 1 &&
                   macws_sender_obs_seq() == 1) ? 0 : 1);
        }
        int st_ = 0; waitpid(kid, &st_, 0);
        CHECK(WIFEXITED(st_) && WEXITSTATUS(st_) == 0,
              "child: symlinked log refused, record stored, drop counted");
        unlink(log);
    }

    printf("== log-open negatives in forked children ==\n");
    /* Each child forks BEFORE the parent's own pthread_once open, so
       its once-state is pristine and it exercises its own log path.
       The sentinel already exists (created above), so emits are armed. */
    {
        /* FIFO: O_NONBLOCK open fails ENXIO (or fstat rejects) — the
           key property is NO HANG; child exits promptly either way. */
        pid_t kid = fork();
        if (kid == 0) {
            g_macws_sender_obs_log_path = "/tmp/msob_test_fifo";
            unlink("/tmp/msob_test_fifo");
            mkfifo("/tmp/msob_test_fifo", 0600);
            uint64_t dk = macws_sender_obs_dropped();
            macws_sender_obs_emit(&t);
            _exit(macws_sender_obs_dropped() == dk + 1 ? 0 : 1);
        }
        int st_ = 0; waitpid(kid, &st_, 0);
        CHECK(WIFEXITED(st_) && WEXITSTATUS(st_) == 0,
              "fifo log path refused without blocking, drop counted");
        unlink("/tmp/msob_test_fifo");
    }
    {
        /* device: /dev/null opens fine but fstat !S_ISREG -> refused */
        pid_t kid = fork();
        if (kid == 0) {
            g_macws_sender_obs_log_path = "/dev/null";
            uint64_t dk = macws_sender_obs_dropped();
            macws_sender_obs_emit(&t);
            _exit(macws_sender_obs_dropped() == dk + 1 ? 0 : 1);
        }
        int st_ = 0; waitpid(kid, &st_, 0);
        CHECK(WIFEXITED(st_) && WEXITSTATUS(st_) == 0,
              "device log path refused (non-regular), drop counted");
    }
    {
        /* over-permissive owned regular file -> fchmod-corrected to
           0600 on the validated fd, then used */
        pid_t kid = fork();
        if (kid == 0) {
            g_macws_sender_obs_log_path = "/tmp/msob_test_perm";
            int pf = open("/tmp/msob_test_perm", O_CREAT | O_WRONLY, 0644);
            close(pf);
            uint64_t dk = macws_sender_obs_dropped();
            macws_sender_obs_emit(&t);
            struct stat ps;
            int ok = (macws_sender_obs_dropped() == dk &&
                      stat("/tmp/msob_test_perm", &ps) == 0 &&
                      (ps.st_mode & 0777) == 0600 &&
                      ps.st_size == (off_t)sizeof(MacWSSenderObsRecord));
            _exit(ok ? 0 : 1);
        }
        int st_ = 0; waitpid(kid, &st_, 0);
        CHECK(WIFEXITED(st_) && WEXITSTATUS(st_) == 0,
              "0644 owned log corrected to 0600 on validated fd");
        unlink("/tmp/msob_test_perm");
    }

    printf("== armed: store + raw write ==\n");
    /* First armed emit in THIS process opens the real log once. */
    MacWSSenderObsRecord t2 = tmpl_for(2); t2.rc = 0;
    macws_sender_obs_emit(&t);               /* seq 0 -> stored + logged */
    macws_sender_obs_emit(&t2);              /* seq 1 */
    uint64_t n = 0;
    const MacWSSenderObsRecord *ring = macws_sender_obs_ring(&n);
    CHECK(n == 2, "two records published");
    CHECK(ring[0].magic == 0x4d534f42 && ring[0].version == 2,
          "record magic/version=2");
    CHECK(ring[0].site == 1 && ring[0].rc == -7 && ring[0].audit_size == 0x34,
          "fields verbatim");
    CHECK(ring[0].token[0] == 0xA1 && ring[1].token[0] == 0xA2,
          "token bytes verbatim");
    CHECK(sizeof(MacWSSenderObsRecord) == 144, "record ABI 144B");
    struct stat lst;
    CHECK(stat(log, &lst) == 0 && S_ISREG(lst.st_mode) &&
          (lst.st_mode & 0777) == 0600, "log created mode 0600 regular");
    CHECK(lst.st_size == 2 * (off_t)sizeof(MacWSSenderObsRecord),
          "log holds exactly 2 records");

    printf("== nested chain correlation ==\n");
    pthread_t th[2];
    pthread_create(&th[0], NULL, chain_worker, (void *)100);
    pthread_create(&th[1], NULL, chain_worker, (void *)200);
    pthread_join(th[0], NULL); pthread_join(th[1], NULL);
    /* 2 chains x 3 records = 6 new records, seq now 8 */
    ring = macws_sender_obs_ring(&n);
    CHECK(n == 8, "6 chain records stored (n=8 total)");
    /* verify each chain: gate flags CHAIN_ROOT, all 3 share invocation */
    int chains_ok = 1;
    for (uint64_t i = 2; i < 8; i += 3) {
        const MacWSSenderObsRecord *g = &ring[i], *d = &ring[i+1], *m = &ring[i+2];
        if (!(g->flags & MACWS_SENDER_OBS_F_CHAIN_ROOT)) chains_ok = 0;
        if (!(g->invocation == d->invocation &&
              d->invocation == m->invocation)) chains_ok = 0;
        if (g->invocation == 0) chains_ok = 0;
        if (d->flags & (MACWS_SENDER_OBS_F_CHAIN_ROOT |
                        MACWS_SENDER_OBS_F_STANDALONE)) chains_ok = 0;
        if (m->flags & (MACWS_SENDER_OBS_F_CHAIN_ROOT |
                        MACWS_SENDER_OBS_F_STANDALONE)) chains_ok = 0;
    }
    CHECK(chains_ok, "nested chain shares invocation id, root flagged");
    CHECK(ring[2].invocation != ring[5].invocation,
          "two chains get distinct invocation ids");

    printf("== standalone inner-boundary call ==\n");
    /* depth==0 at a disp site -> own id + STANDALONE, not stale gate id */
    uint64_t inv_s = 0;
    uint8_t fs = macws_sender_obs_chain_enter(2, &inv_s);
    MacWSSenderObsRecord st = tmpl_for(2);
    st.invocation = inv_s; st.flags |= fs;
    macws_sender_obs_emit(&st);
    macws_sender_obs_chain_exit();
    ring = macws_sender_obs_ring(&n);
    CHECK(n == 9, "standalone record stored");
    CHECK(ring[8].flags & MACWS_SENDER_OBS_F_STANDALONE,
          "standalone disp call flagged");
    CHECK(ring[8].invocation != ring[2].invocation &&
          ring[8].invocation != ring[5].invocation,
          "standalone gets fresh id, no stale pairing");
    /* after unwind, next enter gets a NEW id (depth cleared) */
    uint64_t inv_next = 0;
    (void)macws_sender_obs_chain_enter(2, &inv_next);
    CHECK(inv_next != inv_s, "id cleared after unwind to depth 0");
    macws_sender_obs_chain_exit();

    printf("== real wrapper invoke: errno envelope + captures ==\n");
    /* Set up a fake audit container matching the gate's own read
       pattern (+4 size, +0x14 token, +0x18 euid, +0x28 pid). */
    memset(&g_fake_audit, 0, sizeof g_fake_audit);
    g_fake_audit.size = 0x34;
    for (int i = 0; i < 32; i++) g_fake_audit.body[i] = (uint8_t)(0x40 + i);
    memset(g_fake_desc, 0x5A, sizeof g_fake_desc);
    g_fake_cell = &g_fake_audit;
    g_fake_lazy = 0;
    MacWSSenderObsGateEnv env = fake_env();
    uint64_t n_before_inv = 0; (void)macws_sender_obs_ring(&n_before_inv);
    uint64_t snaps0 = macws_sender_obs_snaps();

    /* orig leaves errno: caller must see the ORIGINAL's errno (which is
       the restored incoming one), hooks may clobber freely */
    errno = EDOM;
    int rr = macws_sender_obs_gate_invoke(fake_orig_chk2, 0, 0, &env);
    CHECK(rr == 7, "gate invoke: orig saw restored incoming errno (EDOM)");
    CHECK(errno == EDOM, "gate invoke: caller sees orig errno (unchanged)");
    /* orig sets errno: caller sees orig's, not incoming or hook's */
    errno = EINVAL;
    rr = macws_sender_obs_gate_invoke(fake_orig_sets2, 0, 0, &env);
    CHECK(rr == -1 && errno == EBADF,
          "gate invoke: orig errno forwarded (EBADF), hooks' clobber hidden");
    CHECK(g_pre_ran == 2 && g_post_ran == 2, "env hooks ran inside envelope");
    {
        /* euid @+0x18 == body[4..7], pid @+0x28 == body[0x14..0x17] */
        uint32_t eu, pi; memcpy(&eu, g_fake_audit.body + 4, 4);
        memcpy(&pi, g_fake_audit.body + 0x14, 4);
        CHECK(g_post_aeuid == eu && g_post_apid == pi,
              "post_orig received pre-orig audit scalars (no token bytes)");
    }
    /* record captured: last ring record is the sets2 gate record */
    {
        const MacWSSenderObsRecord *gr = macws_sender_obs_ring(&n);
        CHECK(n == n_before_inv + 2, "two gate-invoke records published");
        const MacWSSenderObsRecord *g1 = &gr[n - 1];
        CHECK(g1->site == 1 && (g1->flags & MACWS_SENDER_OBS_F_CHAIN_ROOT) &&
              (g1->flags & MACWS_SENDER_OBS_F_AUDIT) &&
              (g1->flags & MACWS_SENDER_OBS_F_DESC) &&
              g1->invocation != 0 && g1->rc == -1 &&
              g1->token[0] == 0x40 && g1->desc[0] == 0x5A,
              "gate record: chain root + audit + desc verbatim, rc=-1");
        CHECK(macws_sender_obs_snaps() == snaps0 + 2,
              "record-capture counter advanced by armed invokes");
    }
    /* disp/impl invokes: same envelope, nested under a gate chain id */
    errno = EDOM;
    rr = macws_sender_obs_disp_invoke(fake_orig_keep3, 0, 0, (void *)0x55);
    CHECK(rr == 42 && errno == EDOM, "disp invoke: errno forwarded");
    rr = macws_sender_obs_impl_invoke(fake_orig_sets2, 0, (void *)0x77,
                                      &g_fake_lazyp);
    CHECK(rr == -1 && errno == EBADF, "impl invoke: orig errno forwarded");

    printf("== simultaneous first emission (8 threads) ==\n");
    pthread_t wk[8];
    for (long i = 0; i < 8; i++)
        pthread_create(&wk[i], NULL, emit_worker, (void *)((i % 3) + 1));
    for (int i = 0; i < 8; i++) pthread_join(wk[i], NULL);
    ring = macws_sender_obs_ring(&n);
    CHECK(n == 21, "all 8 concurrent emits published (n=21)");
    int seqs_ok = 1;
    uint8_t seen[256] = {0};
    for (uint64_t i = 0; i < 21; i++) {
        if (ring[i].seq >= 21 || seen[ring[i].seq]) seqs_ok = 0;
        seen[ring[i].seq] = 1;
        if (ring[i].magic != 0x4d534f42) seqs_ok = 0;
    }
    CHECK(seqs_ok, "concurrent emits: unique seqs, records intact");

    printf("== bounded write_all ==\n");
    /* full nonblocking pipe -> write fails EAGAIN -> bounded -1, no spin */
    int pfd[2]; pipe(pfd);
    fcntl(pfd[1], F_SETFL, O_NONBLOCK);
    uint8_t fill[65536]; memset(fill, 1, sizeof fill);
    while (write(pfd[1], fill, sizeof fill) > 0) {}
    int wr = macws_sender_obs_write_all(pfd[1], fill, 144);
    CHECK(wr == -1, "write_all fails bounded on full pipe (EAGAIN)");
    errno = ENOENT;
    wr = macws_sender_obs_write_all(pfd[1], fill, 0);
    CHECK(wr == 0 && errno == ENOENT, "zero-length write trivially ok");
    close(pfd[0]); close(pfd[1]);

    printf("== budget cap ==\n");
    for (int i = 0; i < 300; i++) macws_sender_obs_emit(&t);
    uint64_t stored = 0;
    (void)macws_sender_obs_ring(&stored);
    CHECK(stored == 256, "published records capped at 256");

    printf("== live disarm ==\n");
    unlink(sent);
    uint64_t d0 = macws_sender_obs_dropped();
    macws_sender_obs_emit(&t2);
    CHECK(macws_sender_obs_dropped() == d0 + 1, "post-disarm emit dropped");
    CHECK(macws_sender_obs_site_calls(2) > 0, "calls still counted post-disarm");
    /* D046: disarm ceases record capture entirely — wrapper performs no
       token/desc/lazy reads (snaps flat) yet the original still runs
       with an intact errno envelope. */
    {
        uint64_t sn = macws_sender_obs_snaps();
        errno = EDOM;
        int r2 = macws_sender_obs_gate_invoke(fake_orig_chk2, 0, 0, &env);
        CHECK(r2 == 7 && errno == EDOM,
              "disarmed gate invoke: orig ran, errno envelope intact");
        CHECK(macws_sender_obs_snaps() == sn,
              "disarmed invoke: zero record captures (snaps flat)");
        r2 = macws_sender_obs_disp_invoke(fake_orig_keep3, 0, 0, 0);
        CHECK(r2 == 42 && macws_sender_obs_snaps() == sn,
              "disarmed disp invoke: orig ran, no capture");
    }

    printf("== prologue pins vs analysis image ==\n");
    FILE *f = fopen("analysis/SkyLight_22F82", "rb");
    if (!f) { printf("  SKIP analysis image not found\n"); }
    else {
        const struct { uint64_t off; const uint32_t *exp; const char *nm; } pins[] = {
            { MACWS_SENDER_OBS_OFF_GATE, kMacWSSenderObsGatePrologue, "gate" },
            { MACWS_SENDER_OBS_OFF_DISP, kMacWSSenderObsDispPrologue, "disp" },
            { MACWS_SENDER_OBS_OFF_IMPL, kMacWSSenderObsImplPrologue, "impl" },
        };
        char nm[64];
        for (int i = 0; i < 3; i++) {
            uint32_t got[4] = {0};
            fseek(f, (long)pins[i].off, SEEK_SET);
            size_t r2 = fread(got, 4, 4, f);
            snprintf(nm, sizeof nm, "%s prologue bytes == pin", pins[i].nm);
            CHECK(r2 == 4 && memcmp(got, pins[i].exp, 16) == 0, nm);
        }
        uint32_t bad[4];
        memcpy(bad, kMacWSSenderObsGatePrologue, 16);
        bad[0] ^= 1;
        uint32_t img[4];
        fseek(f, (long)MACWS_SENDER_OBS_OFF_GATE, SEEK_SET);
        (void)fread(img, 4, 4, f);
        CHECK(memcmp(img, bad, 16) != 0,
              "corrupted pin no longer matches (fail-closed basis)");
        fclose(f);
    }

    printf("== corrected global offsets (D043 frontend review) ==\n");
    CHECK(MACWS_SENDER_OBS_OFF_AUDIT_SLOT   == 0x53AC11B8ull,
          "audit slot offset corrected");
    CHECK(MACWS_SENDER_OBS_OFF_RIGHT_DESC == 0x4ECE66C8ull,
          "rightDesc offset corrected");
    CHECK(MACWS_SENDER_OBS_OFF_LAZY_CHECKER == 0x53AC1B50ull,
          "lazyChecker offset corrected");

    printf("\n%s (failures=%d)\n", failures ? "FAIL" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
