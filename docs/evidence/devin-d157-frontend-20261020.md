# D157 — does final T16 cover D145 A2 (parked-producer saturation flood)?

Frozen inputs: D152 packet I2 (T16), frozen `d141_svctime.c` bound
rule, D145 Gate A row A2. Arithmetic/proposal only — no packet edits.

## 1. What A2 required

D145 A2: a producer parked holding a claim is a head-of-line blocker —
drain stops at the hole (A1), producers keep claiming until
`head − tail == CAP`, then every further emit drops. The stall bound
must be (a) documented and (b) *tested*: the flood condition, not just
the first refusal.

## 2. What final T16 exercises

Schedule (packet :39-57): 255 committed → P parks at claim 255 →
drain 255 (tail=255, head=256) → 255 more land (claims 256..510;
`c−tail = 1..255 < CAP` — arithmetic verified) → one emit refuses at
c=511 (`511−255 = 256 ≥ CAP`) → release → drain 256.

Arithmetic all verified: `emitted==511`, `dropped==1`,
`drained==511`, `attempts==512`, mid-park drain returns 0.

## 3. Coverage verdict — partial

- **Covered**: bound *trigger* — the first refused `c+CAP` claim,
  exact arithmetic, refusal doesn't disturb the parked claim,
  pre-claim refusal confirmed at `:103-107` (bound check before CAS;
  dropped attempts never consume a claim index — `g_head` unchanged).
- **NOT covered**: the **flood** — A2's sustained-saturation property.
  T16 refuses exactly once, then releases P. Nothing exercises N
  consecutive drops while the hole persists, nor asserts that a flood
  leaves `g_head`/`g_emitted` untouched. A latent bug that drops the
  *first* overflow correctly but corrupts on the second (e.g. head
  advance, counter drift, slot write) would pass T16.

## 4. Proposed T16 amendment (minimal — insert between steps 5 and 6)

**Step 5b — saturation flood (P still parked, tail=255, head=511):**
emit `M` more attempts (suggest `M = 256 = CAP` for exhaustive flood).
Every attempt loads `c=511`, sees `511−255 = 256 ≥ CAP` → drop.
Assertions:
- `dropped == 1 + M`
- `emitted == 510` (unchanged — refusals never emit)
- `g_head == 511` observable (or infer: refusal count exact ⇒ no
  claim consumed)
- optional second mid-park drain still returns 0.

Then existing step 6 (release) → `emitted == 511`, step 7 drain 256.

**Amended totals**: `attempts == 512 + M`, `emitted == 511`,
`dropped == 1 + M`, `drained == 511`.
Conservation: `511 + (1+M) == 512 + M` — exact.

## 5. Documentation facet

A2's "documented" half: add one contract line (fold into D1 or D5
family): *"a producer that dies or stalls holding a claim permanently
blocks the ring at the CAP bound; every subsequent emit drops counted
— the ring never corrupts, and the dropped counter is the honest
record of the stall."* Producer-death is the symmetric case of D5's
consumer-death freeze; both bounds deserve explicit lines.

## Verdict

T16 covers A2's arithmetic trigger but not its flood property.
One-step amendment (5b) + one contract line closes the row — proposed
text above for packet-owner application.
