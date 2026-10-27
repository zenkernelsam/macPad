# D094 frontend task

Bounded read-only current-generation pixel witness. Do not edit source,
build, deploy, restart, inject events, use trustcache/KRW/TCC, or write Git.

Use the already accepted Host/VNC/readback mechanism to capture one current
frame, calculate `nonblack` and `mean`, and correlate the frame with the
current `macws_final_composite.state` sequence and WS generation. If the
mechanism cannot be used without mutation or unapproved credentials, stop
and document the missing prerequisite instead of improvising. Write
`docs/evidence/devin-d094-frontend-20261019.md` and
`coordination/_relay/devin-094-frontend-result.md`, ending exactly with:
`(handover to codex now)`
