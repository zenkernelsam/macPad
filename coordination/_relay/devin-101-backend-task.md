# D101 backend — getter semantics and observable errno

Read D100 and audit two inference risks with exact raw disassembly:

1. The caller accepts getter rv==0 and caller/responsible token equality.
   Does that prove an absent provider registration entry returns self, or
   only that the caller supports self-responsible success? Without exact
   getter/provider implementation, label absent-entry semantics THEORY.
2. The errno logging helper demonstrably reads errno after a nonzero getter.
   Correct the claim that userspace cannot recover errno: application control
   flow collapses failures but the saved runtime error log preserves errno.
   Distinguish errno value from the kernel branch that produced it.

Check the dispatch/entitlement gate for the setter against exact addresses;
its existence does not authorize invoking it. Save a concise D101 errata
with supporting raw instruction excerpts, not another authorization review.
No device/probe/setter/KRW/TCC execution, builds, source edits or Git writes.
End result with (handover to codex now).
