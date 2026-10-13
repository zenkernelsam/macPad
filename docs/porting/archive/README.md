# docs/porting archive — what lives here and why

Consolidated 2026-10-13. `docs/porting/` grew to ~40 flat files across three
eras; this archive keeps the historical record intact while the top level
holds only the CURRENT canonical set:

```text
docs/porting/
  PROJECT-ARCHAEOLOGY.md                          ← read first: full timeline
  ARCHAEOLOGY-15.6.1.md                           ← detailed 15.6.1 roots story
  ventura-22F82-bringup-state.md                  ← live state file (update this)
  ventura-22F82-session-reproduction-20261013.md  ← operational runbook
  TOOLS-AND-PORTING.md                            ← toolchain + porting playbook
  archive/                                        ← you are here
```

## Layout

- `macos15-roots/` — everything from the frozen macOS 15.6.1 / 24G90
  branch (09-24 → 10-04): the HANDOVER/REPLY agent dialogue, dyld state
  log, syscall-536 saga, STATIC-* analyses, guard/PMAP wall, shim CLI
  milestone, patch ledger. The frozen branch's own summary is
  `macos15-roots/HANDOVER-MACOS15-FROZEN-2026-10-04.md`; the condensed
  story is `../../ARCHAEOLOGY-15.6.1.md`.
- `handover-chain/` — cross-agent handovers that are not 15.6.1-specific:
  the Ventura pivot decision (10-04), the x86 dev-host handover (10-11),
  and the era-1 workflow prompts.
- `files-app-macpad_iOS-20261004/` — pre-existing device-inventory dump.

## Reference caveats

- Files in `macos15-roots/` cite each other as `docs/porting/<name>.md`
  from when they lived at the top level. Where the cited file is itself
  archived, the intended target is the same filename in this directory.
  Live docs (`ARCHAEOLOGY-15.6.1.md`, `PROJECT-ARCHAEOLOGY.md`) name the
  archive path explicitly.
- Source comments updated on 10-13 point at `archive/macos15-roots/...`.
- Nothing here was deleted; every file is a `git mv`, history preserved.
- Do NOT "fix" these documents — they are the historical record. New
  findings go in the top-level live docs.
