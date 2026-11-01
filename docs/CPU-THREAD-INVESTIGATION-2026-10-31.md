# CPU thread investigation, 2026-10-31

The Windows kernel CPU recorder could not start: WPR returned 0xc5585011 (profiling privilege unavailable). No ETW trace was collected. A game-only native sampler was used instead, with no desktop input, no global runtime changes, and no elevation prompt.

The sampler polls game thread states and briefly suspends selected threads to copy their registers and stack, resuming before unwinding and resolving symbols. Thread states can change before capture. Counts are investigative evidence, not CPU percentages or a proven critical path. Some system/driver symbols are nearest exported names and must not be treated as exact functions.

The 30-second capture recorded 6,497 samples with zero errors. Total suspension across all sampled threads was 243.379 ms, maximum individual suspension 0.5014 ms. Unsampled runs before/after delivered 93.517/92.306 accepted AFW frames per second; sampled run delivered 90.866. This method measurably perturbs the game.

Repeated NtQueryVirtualMemory / VirtualQuery stacks led through Game::LocalPlayer into a frequently called mod hook. Other samples identified transform/view mutex contention. No percentage attribution or proof that either accounts for every long frame is made.

Candidate: replace the per-call VirtualQuery of the supported executable's fixed GameLocal pointer slot with a fault-guarded read. Read the current value every call; never cache the player or world object. Keep the supported-build check and all dynamic-object checks. Diagnostic command `vr cpusingleton 0|1` permits same-binary comparisons.

Tests use Continue at the canal bridge, fixed simulated head/controllers, AFW renderer 4, DLSS quality 3, 2633x3428 eye size, renderScale 1, CPU cache mask 0, motion statistics off. These are simulator delivery measurements, not Virtual Desktop or headset timing.

## Same-binary alternating results

All four 25-second trials had zero additional AFW refusals. Exclude the first two seconds of profiler data. The profiler writes asynchronously; copy its CSV files after completion, not immediately after capture time expires.

| Mode | Accepted FPS | Mean interval ms | p95 ms | p99 ms | >13 ms |
|---|---:|---:|---:|---:|---:|
| Original A | 92.480 | 10.783 | 12.452 | 14.045 | 3.10% |
| Guarded read A | 99.927 | 10.013 | 11.691 | 13.135 | 1.13% |
| Original B | 93.131 | 10.722 | 12.393 | 13.823 | 2.80% |
| Guarded read B | 99.872 | 10.008 | 11.701 | 13.239 | 1.13% |

The repeatable gain is approximately 7.6% on average, with roughly 0.74 ms lower average delivery interval. These are wall intervals including waits, not CPU-only work durations. p95 still exceeds the 11.11 ms delivery budget for 90 FPS. Do not claim stable 90 or extrapolate directly to real-headset/VD FPS.

The guarded read is now the default. `vr cpusingleton 0` selects the original lookup for diagnosis until restart; this is not a persisted end-user setting. Dynamic player/camera memory checks remain unchanged. There are no graphics or game-simulation changes. The other four CPU cache candidates remain off.

## Next CPU target

Investigate contention in EnsureTransform / BeginSixDof and RenderView. A per-view immutable transform snapshot could remove repeated locks on render workers, but must preserve eye identity, controller pose matching, reset/configuration invalidation and safe publication. The earlier combined cache mask was inconclusive; test view-read caching individually before introducing a larger transform redesign. Do not remove locks without an ownership/publication contract.

Engine/driver work also appears in the samples. The current evidence does not establish its critical-path contribution, nor support a claim that single-pass stereo is the next best change. Full kernel CPU/context-switch tracing remains unavailable without Windows profiling privilege.

Raw evidence and backups: `<workspace>\output\d2-etw-20261031`.
## Final validation and installation

Release build and all five existing CTest regression checks passed; diff whitespace check passed. These existing tests cover the surrounding renderer contracts, not every possible engine-pointer fault. Code review confirms unsupported-build rejection, per-call current-pointer reads and exception handling remain. A fresh process with the guarded read enabled by default passed startup, menus, Continue loading and actual canal gameplay. Final 25-second unprofiled simulator verification: 98.477 accepted FPS, zero additional AFW refusals, both hand transforms active. Real-headset movement/combat and repeated level transitions still need user validation.

Installed DLL SHA256: `C7FA823A9C472E5C77465A0FCF2F1C022EB8E95A5A167AD0BEEE917A1BE5987A`, matching the built DLL. The game is closed, no pending command files remain, and the four simulator marker files were removed. Mod INI and engine configuration were restored byte for byte against pre-test backups. No system OpenXR runtime change or desktop input was used. The optimization adds no machine-specific paths, graphics changes or installer requirements. Diagnostic sampler/PerfView tools remain in the local evidence folder and are not part of the public setup.
