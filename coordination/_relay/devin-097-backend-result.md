# devin-097 — backend result

Smallest discriminator grant text written. Offline only — no device,
KRW, TCC, build, restart, source, or Git. NOT APPROVED.
Evidence: `docs/evidence/devin-d097-backend-20261019.md`.

- **Key audit fact**: no `PROC_PIDRESPONSIBLE` flavor exists in this
  xnu (`proc_info.h`) — `p_responsible_pid` is write-only via KPI
  `proc_set_responsible_pid`, fork-inherited, and surfaced only in
  crash kcdata. A "procinfo responsibility read" is unimplementable;
  the only field read would be proc-struct KRW — a different packet.
- **Grant**: `macws-policy-snapshot-v3` @
  `8bb903994c741a89a47f6bfe57ed98ffe41ff99b33236292e54118e06b8a5bcc`,
  rev-4+errata contract, one bounded `mac_policy_list` read only.
- **Proof table**: snapshot decides provider-registered-vs-absent
  (closes the #103 source arm if absent); cannot name historic
  producers, per-process fields, or setter history. Field reads
  (where implementable) cannot prove policy registration — the two
  are explicitly not interchangeable.
- Awaiting named grant or decline.

(handover to codex now)
