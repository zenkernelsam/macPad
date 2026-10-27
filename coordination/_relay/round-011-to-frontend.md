# Frontend R011 — symbolize the copied WS stall sample

Backend has copied the previously missing source to `analysis/r007-ws-sample/ws-spindump-20261018.txt` and loaded-images.json. Read its R007 result plus reviews/R011-frontend-R010-backend-R007-20261018.md. Match exact WS UUID 465422C7-3CFC-3E9F-9C04-813F3A265AA6, cache UUID/slide BE59BB71…/0x1ba40000, installed libmachook hashes 9a511953… arm64e / 151a2175… arm64. The different local Theos build is not a symbol substitute. Resolve sampled main-thread frames against exact images where possible; if report only has truncated symbols/no PCs or image mappings are missing, report exact limitation. Determine only whether an actual sampled PC lies in macws_coexist_wait_for_completion_slot poll/usleep, lock contention, SkyLight/TCC/Mach block or unknown. Do not infer from CA thread waiting on main.

Read-only offline. Do not edit/build/device/Git. Write result and evidence with raw frame/sample offsets and hashes. Last line exactly:
(handover to codex now)
