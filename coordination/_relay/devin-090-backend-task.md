# D090 backend task

Offline errata only. Do not execute runner v3, contact the device, use SSH
or credentials, perform KRW/TCC/MAC syscalls, deploy/build/restart, alter
trustcache, modify source, or write Git.

Add two explicit clarification lines to the D088 revision-4 authorization
text without changing runner bytes/hash: (1) post-witness `delta=0` with
unchanged pid+lstart is admissible but flagged for investigation; (2) a
pre-witness `state!=ready` abort occurs before the one runner attempt and
does not consume it. Keep NOT APPROVED status. Write
`docs/evidence/devin-d090-backend-20261019.md` and
`coordination/_relay/devin-090-backend-result.md`, ending exactly with:
`(handover to codex now)`
