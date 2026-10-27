# D093 — decision packet: `macws-policy-snapshot-v3`

One-page summary for the user's authorization decision. Nothing here
executes or approves; the runner is frozen and stays frozen without
the named grant.

## Already closed (D091 health read + D070 baseline)

| Fact | Evidence |
|---|---|
| Same desktop generation since checkpoint | WS 10447, tccd 883/10300, identical lstarts — no restart/panic |
| Desktop actively producing | `state=ready`, `validated-final-composite`, seq 6055→11041 |
| Target identity matches packet | macOS 13.4.1/**22F82** rootfs, kernel 22.3.0, iPad13,11/**T8103** |
| SSH mechanics work on the authorized path | port 22, `sshpass -e`, clean exits |

## What the grant uniquely answers

One bounded read-only kernel observation:
**is a `Quarantine`-named MAC policy registered in `mac_policy_list`
at snapshot instant, and are its precheck (`ops+0x388`) / syscall
(`ops+0x3a8`) slots populated?**

- **Absent on a completed walk** ⇒ handler-registration arm closed for
  the entire boot epoch (`mac_late=1`); historic `errno 103` reduces
  to no-match or foreign precheck veto.
- **Present** ⇒ only that the dispatch arm is possible; invocation is
  not proven either way.

No other pending artifact can answer this — the live table is
runtime-only (zeroed in the image), and PAC-encoded stubs block
offline caller enumeration (D072).

## Packet identity (what the grant binds to)

- Name: **`macws-policy-snapshot-v3`**
- Runner sha256: **`8bb903994c741a89a47f6bfe57ed98ffe41ff99b33236292e54118e06b8a5bcc`**
- Text: D088 rev-4 + D090 errata — one attempt total, 16 fail-closed
  STOPs, sanitized output, `root`-over-ssh principal, `sshpass -e`,
  numbered steps, dual whitelists, pid+lstart/sequence/panic/residue
  witnesses, admissible-absence = completed walk,
  `physrw-unarmed`→`missing-authorization`.

## Explicitly out of scope (the grant does NOT cover)

Helper executables; trustcache add/remove/clear; deployment; restart
or launchd actions; source/Git writes; a second or modified run; reads
outside the declared kernel set; commands outside both whitelists;
event injection; pixel capture; sentinel files. Any deviation voids
admissibility (D069 precedent).

## Decision

**Grant** `macws-policy-snapshot-v3` by name → the single open kernel
fact gets measured on a verified-healthy same-epoch target.
**Decline/defer** → the question stays open at this exact status; no
offline work remains that can close it.
