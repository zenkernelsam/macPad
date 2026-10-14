#pragma once

// Bounded loader-call observation for the CGXSYNTH gate diagnostic.
// mac_hooks.m wraps the ORIGINAL macws_orig_sender_can_synth call in
// Enter/Exit; the Diagnostics dlsym/dlopen interposers report through the
// Note* entry points, which are no-ops outside an active thread-local
// context.  No public API result, errno, or loader error state is changed.
// DIAGNOSTIC ONLY — armed by the same runtime-diagnostics sentinel as the
// gate hook; never a fallback, never a behavior change.

// Returns the per-process observation sequence for correlation in gate
// logs.  Nested Enter increments depth without starting a new sequence.
unsigned MacWSLoaderObservationEnter(void);
void MacWSLoaderObservationExit(void);

// Called by the existing Diagnostics interposers after the real loader
// call completes.  Preserve caller errno.  Records are capped per context.
void MacWSLoaderObservationNoteDlsym(void *handle, const char *symbol,
                                     void *result);
void MacWSLoaderObservationNoteDlopen(const char *path, int mode,
                                      void *result);

// Test seam: replace the line sink (NULL restores stderr).  The sink
// receives one bounded line per record.  Not used by production paths.
void MacWSLoaderObservationSetSink(void (*sink)(const char *line));
