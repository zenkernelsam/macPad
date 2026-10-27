# D125 frontend — independent complete patch and UI acceptance inventory

The user explicitly directed that all new product patches, before and after
the panic, be installed and tested on the iPad. This task is read-only and
complements D124's build/deploy ownership. Do not send device commands, edit
sources, build, change Git, or modify runtime state.

1. Reconcile all frontend-relevant changes since the device's last proven
   source/build deployment, including `3c4d581`, `4bcf20d`, R008 package
   acceptance, D043-D048 observer integration, and current dirty source. Use
   exact commit ancestry, package timestamps/payload, stored device hashes and
   reports. Produce a table: included in verified installed package, installed
   separately, source-only/unbuilt, or documentation/offline-only. Do not
   infer installation from a commit or local artifact timestamp.
2. Audit the R003 relay gate and route F1 implementation against the package's
   actual `MacWSHost.app` and both architecture identities. Identify focused
   tests and the concrete on-device witness required to accept exact-window
   input. Reuse reopen freshness and do not invent another freshness path.
3. Review D124's product-patch manifest for omissions or unsafe mixing. D113/
   D120 lifecycle code is test-only and must remain outside the product `.deb`.
4. Save a concise read-only report and relay result. End with
   `(handover to codex now)` on its own line.
