# D107 backend — exact existing kernel artifact inventory

Offline only. Reconcile D064/D068/D069's claimed live kernelcache hash match
against later claims that the target T8103 kernelcache is absent. Inventory
the named saved img4/extracted binaries and exact UUID/hash/runtime-match
artifacts; do not identify hardware from a misleading basename alone.
Preserve raw identity output and provenance. If exact code is available,
extract the bounded mac_syscall/precheck/name-match/handler-return disassembly
at the established offsets and distinguish what is statically proved from
runtime policy registration. If absent, name the missing extraction stage.
Also scope mac_late vs unload: does static code prove policy state persists
for the boot epoch, or only prohibit late registration? No policy snapshot,
KRW, device, helper, syscall, source/build/Git action. Correct contradictions
with evidence; do not infer no-match from errno103. Save D107 and handover.
