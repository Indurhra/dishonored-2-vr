# DLSS recovery and frame-time investigation — 2026-10-28

## Outcome and limits
Compiled and installed candidate SHA256 F784B028B9D41307A3E200FEE085A7D3229EB78A1F042071BBA8514BCCC4669A. Original DLL, PDB, INI and logs are recoverable under local/dlss-recovery/before-*.

The original headset session fell back with `AFW requires matching DLSS evaluation`. Its exact preceding DLSS failure was not preserved, so its specific trigger remains unconfirmed. The old code permanently disabled DLSS after a transient unmatched stage and subsequently overwrote its failure reason. This candidate provides bounded recovery and persistent diagnostics; it is not proof that the original trigger has been eliminated.

## Changes
- DlssLive: retry transient missing matching depth/temporal input/final-evaluation events up to three times. Replenish the budget after 120 successful evaluations. Reset backend/history generation on failure; append cause and state to DVR_dlss_failure.txt. Hard NGX or incompatible-region failures and exhausted retries retain native fallback.
- AfwLive: reject packets from obsolete DLSS generations. Keep eye/frame/viewport matching checks.
- VRRuntime and Commands: preserve DLSS failure details in fallback logs and heartbeat.
- ViewMatrixHook: identity eye rotation means no rotation is needed, not an invalid camera. Previously it prevented recording the held-eye target for valid parallel-eye poses. This was reproduced in the simulator and fixed. It is not established as the original physical-headset failure.
- Commands: replace unsafe diagnostic player-pointer dereferences with checked memory snapshots. A loading crash was traced to the heartbeat writer; the final build successfully loaded the same save afterward.

Recovery may hitch during backend recreation. Repeated or incompatible failures still fall back safely; no claim of hitch-free recovery or headset-confirmed image quality is made.

## Final candidate performance
Same loaded Karnaca scene, stationary camera, same graphics, output 2820 x 2876 per eye. Simulator pacing 240 Hz, game limit 300; observed throughput well below either cap. Quality input viewport 1880 x 1917. Each run 20 seconds with warm-up excluded by the existing benchmark analyser. Native and first Quality run also used a 16-second CPU profile, stride four. Repeat Quality was unprofiled.

| Run | Completed stereo pairs/s | Mean interval ms | Present / fresh / rebuilt / accepted |
|---|---:|---:|---|
| Native | 61.587 | 16.237 | 1980 Presents / 990 native pairs |
| AFW + DLSS Quality | 98.347 | 10.168 | 1581 / 1581 / 1581 / 1581 |
| Quality repeat | 94.019 | 10.636 | 1513 / 1513 / 1513 / 1513 |

AFW reduced stereo interval by approximately 34.5–37.4%, increasing throughput 52.7–59.7%. Both AFW samples had zero refused reconstructions and zero held submissions. Native used two engine Presents/temporal scene passes per pair; AFW used one fresh pass plus one reconstruction per pair. These counters demonstrate the second full scene pass is skipped. This compares the combined AFW + Quality path against native; it does not isolate DLSS's contribution from AFW's.

These are simulator submission measurements, not new fpsVR/headset results or GPU timestamp measurements. Compositor drops, headset reprojection, full GPU cost and perceived latency/image quality require a physical-headset run. The earlier fpsVR session cannot establish the final candidate's performance.

## CPU findings
Final capture, excluding first two seconds:
- Completed stereo interval p95/p99: native 17.758 / 18.873 ms; Quality 11.500 / 12.519 ms.
- Whole hooked Present CPU duration: native 0.605 ms per Present (two per pair); Quality 0.998 ms per pair.
- Main.OnPresent inclusive mean: native 0.504 ms per Present; Quality 0.873 ms per pair.
- SwordContact.ScanTargets: Quality mean 1.114 ms, p95 1.171 ms, max 1.318 ms, 33 sampled calls. This is periodic work, not a cost paid on every frame.

Most elapsed interval is outside the Present callback, including engine/render work and waits. The profile does not yet separate all engine CPU work from GPU/driver waits. Sword scanning contributes spikes but cannot explain the entire frame time. Inclusive scopes overlap and must not be summed. No speculative sword/contact changes were made.

## Validation and evidence
- Release DLL/PDB build completed; NGX/D3D11 live smoke passed, covering actual output readback, state restoration, per-eye inputs, all four upscaling modes, transient recovery, generation changes, invalid rectangles, missing final passes and exhausted retry fallback.
- Temporal frame-contract ctest passed; changed-file whitespace check passed.
- Final candidate loaded gameplay and completed native/Quality/Quality-repeat mode changes without DLSS shutdown. Earlier identity-fix candidate also passed simulated head/hand movement and pause/resume checks; do not pool its results as final-binary measurements.
- Raw final benchmark files: local/frame-profile/recovery_final_{native,quality,repeat}.jsonl and accompanying metadata/status files.
- Selected machine-readable results: docs/results/dlss-recovery-2026-10-28.json.
- CPU distributions: local/dlss-recovery/final-intervals.json; raw game DVR_cpu_recovery_final_*.csv.
- Crash and earlier failure evidence, smoke-test log, original files and final session status: local/dlss-recovery/.

Game was quit normally after testing. Original INI restored byte-for-byte (SHA256 F9FB4425C339F592B7366BA26BE2AD00F2CABD98EA313601B83BEDC4D53E7FC4); AFW + DLSS Quality preference retained. Temporary mock runtime/HMD/input files moved into the evidence folder. Trigger helper unchanged.

Next validation: normal headset session with fpsVR, same scene and mode, include pause/load and head/controller motion. If DLSS falls back, retain DVR_dlss_failure.txt and DishonoredVR.log before another launch. This now supplies the cause that was missing from the original incident.
