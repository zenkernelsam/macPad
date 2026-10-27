# D048 — Final Frozen-Observer Snapshot Review (independent)

**Scope (devin-048-frontend-task.md):** freeze the final D046 snapshot,
diff it against my D047-reviewed state (no same-verdict carryover without
diff evidence), rerun the focused host test against the EXACT final
sources, audit the remaining checklist, reconcile record schema with
decoder plan, evaluate backend's isolated-helper evidence if saved, and
report precise eligibility. **No device, no production edits, no Git.**

---

## 1. Frozen snapshot — declared vs on-disk (verified twice, identical)

| file | frozen sha256 | vs D047-reviewed |
|---|---|---|
| `libmachook/MacWSSenderObservation.h` | `a7c8438b3b509ff6…` | identical |
| `libmachook/MacWSSenderObservation.c` | `6641d9dfdf23d727…` | **changed** `d43c65d9`→`6641d9df` (+186 B) |
| `libmachook/mac_hooks.m` | `7321d92c530368b7…` | identical |
| `misc/test_sender_observation.c` | `39d6791e4e815c15…` | final fixture order (was mid-edit at D047) |
| `libmachook/Makefile` | `a7cc7862def3aa7e…` | unchanged |
| `libmachook/.theos/obj/libmachook.dylib` | `fddc64c19cde886795ee…` | = backend's declared build exactly; arm64 `5B9F5C19-…-CD43D8C7C9CB`, arm64e `353600B3-…-0DA92108B5` — source→object linkage verified |

All values match backend's `devin-046-backend-result.md` declarations.

## 2. The .c delta — ONE functional change, and it fixes a bug I missed

Diff `d43c65d9` → `6641d9df` = a single edit in
`macws_sender_obs_gate_invoke` (+3 lines, comment+guard+deref level):

```c
// reviewed snapshot (WRONG — one level short):
cred = *env->audit_slot;                 /* = g_macws_cgx_audit_slot
                                            itself = the __bss CELL
                                            ADDRESS, not the container */
// frozen (CORRECT):
if (env && env->audit_slot && *env->audit_slot)
    cred = **env->audit_slot;            /* cell contents = container */
```

Why it matters: `env->audit_slot` is `void ***` = `&g_macws_cgx_audit_slot`;
`*env->audit_slot` is the cell ADDRESS (a `void*` into SkyLight `__bss`);
only `**env->audit_slot` reaches the audit container the gate reads.
Under the reviewed code `csz` would have read **the upper 4 bytes of the
container POINTER itself** (typically ≥0x34 for arm64e pointers → a
spurious pass) and `token` would memcpy 32 B from `cell+0x14` —
adjacent `__bss` globals, not the sender's credentials — plus
`audit_ptr`/`audit_size` would record cell bytes, not container fields.
Records would have been **silently garbage or a stray read** — the
single most important field on the gate site. The frozen fix restores
the pre-refactor wrapper semantics (`*g_macws_cgx_audit_slot`) and the
env contract comment now documents the two-level indirection.

**Self-flag:** my D047 review verified the envelope, gating, log-open,
publication and disarm — but did NOT trace the `void***` indirection
depth; the mis-deref passed my checklist. Recorded honestly. The test
fixture was updated in lockstep (`g_fake_slot=&g_fake_cell`,
`g_fake_cell=&g_fake_audit` — two-level model) and now exercises the
correct chain; an old single-level fixture would have caught this
(so would reading the deref math — my miss).

Everything else in the .c is byte-identical to the reviewed snapshot —
the five other D046 verdicts (O_NONBLOCK open, fstat/fchmod validation,
errno envelope, armed-gated captures + `g_obs_snaps`, emit/publish/
write_all) carry over **with the diff evidence above**.

## 3. Final host test against EXACT frozen sources — ALL PASS

`cc -DMACWS_SENDER_OBS_TEST` on frozen `.c` (`6641d9df`) + frozen test
(`39d6791e`): **45 checks, zero failures**, including
`fifo refused without blocking`, `device refused`, `0644→0600
correction`, the full invoke-errno-envelope set, nested/standalone
correlation, snaps-flat disarm, 256 cap, and the pin/offset asserts.

**Test-order artifact vs production bug — closed:** my D047 3-FAIL run
was confirmed as fixture-order (children forking after the parent's
once had consumed a valid open → inherited fd → no drops). The final
fixture orders the negatives before any armed parent emit; all pass.
My independent forked-FIFO repro (`dropped=1 seq=1 exit=0`) already
proved the production path correct. Not a production bug.

## 4. Remaining checklist (final source)

| item | verdict |
|---|---|
| wrapper incoming/orig errno | ✓ unchanged — `in_errno`→pre-orig restore→`call_errno`→post restore in all three invokes |
| disarm scalar-read scope | ✓ `*env->audit_slot` NULL-guard added; `cred/csz/euid/apid` remain unconditional for the umbrella CGXSYNTH hook — scalars only, documented boundary, no token/desc bytes when disarmed |
| private log existing-mode | ✓ `(st_mode&07777)!=0600` → `fchmod(fd,0600)` on the validated fd only |
| FIFO/other-file open | ✓ `O_NONBLOCK` — reproduced non-block; device/non-owned/non-regular rejected by fstat |
| publication/correlation | ✓ release-flag per-slot publish, mutex-serialized emit, TLS chain ids (unchanged) |
| bounded I/O count vs latency | ✓ ≤8 write passes — pass-count bounded, wall-clock explicitly NOT claimed |
| address/ABI/PAC pins | ✓ all three prologue constants byte-verified vs `SkyLight_22F82` (uuid `96676A53…`); test asserts them + fail-closed on corruption |
| default-off display isolation | ✓ dedicated `/tmp/macws_sender_observation` only; umbrella sentinel appears solely inside the caller's own post_orig hook |
| test macros in production | ✓ **none**: Makefile has no `MACWS_SENDER_OBS_TEST`; production `.o`/dylib lack `g_macws_sender_obs_*_path` symbols and exported `write_all`; dylib strings contain only the two sentinel literals, zero `msob_test`/`MACWS_SENDER_OBS_TEST` |

## 5. Record schema vs decoder plan — reconciled

Current schema: fixed **144 B** records, `magic=0x4d534f42` ('MSOB'),
`version=2`, `site∈{1,2,3}`, `seq`, `invocation`, `flags`, token 32 B +
desc 40 B snapshots, `rc`. Emit range-guards `site` before storing —
**unknown site IDs never enter the log/ring**. A decoder must: frame on
144-B boundaries; reject any record whose `magic`/`version` doesn't
match (unknown-version → stop, not reinterpret); treat file size %144 ≠0
as a truncated tail (a bounded `write_all` failure can leave <144 B —
drop-counted producer-side); and rely on the ring `pub` flag only for
in-process reads (the log is post-publish bytes). No in-repo decoder
exists — schema is self-describing enough for one; that's consumer-side
work, not a producer gap.

## 6. Backend's D048 isolated-helper evidence — NOT YET SAVED

At review time no `devin-048-backend-result.md` and no
`devin-d048-isolated-observer-validation-20261019.md` exist — the
arm64e/PAC helper-level probe is backend's next deliverable. Recorded
absent; not polled, not contacted. The coordinator's
`reviews/D046-D047-review-20261019.md` explicitly keeps it helper-level
(no WS restart/deploy) and keeps the 4th site contingent.

## 7. Eligibility — precise statement

- **Host/local level: fully validated on the frozen snapshot** —
  every criterion above is executable evidence (host test, nm/strings,
  standalone repro, Mach-O segment/UUID checks), not carried verdicts.
  (D049 scope correction: "helper validated" applies to host-built
  binaries only — the backend's actual-device isolated arm64e/PAC
  helper is still running and NOT yet accepted; nothing here speaks
  for on-device results.)
- **Real-WS level: candidate structurally ready** for a coordinator-
  elected deployment review — frozen sources+build match declared
  hashes; prologues/offsets/uuid pins verified; default-off and
  display-decoupled.
- **NOT claimed:** live observation complete, real WS hook firing, any
  pixel/runtime witness, any TCC policy verdict (raw 1 = API default ≠
  explicit denial — reaffirmed), fourth-site implementation (contingent
  design only).
