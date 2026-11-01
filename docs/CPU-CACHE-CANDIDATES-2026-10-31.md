# CPU cache candidates — 2026-10-31

Status: A/B testing completed; **no CPU candidate performance-accepted**. A separate bounded AFW-transition recovery fix is installed. All four candidates default OFF (`vr cpufast 0`). No graphics settings or persistent headset/runtime settings were changed. Do not advertise an FPS improvement or enable these in a public setup until representative comparisons pass.

Benchmark candidate SHA256: `4E8FA1CB4FEDEAF5DE0C88AA46045AE8F68E824BC274CD55468609618779D355`.

## Candidates

`vr cpufast <mask>` is a nonpersistent diagnostic command. Bits can be combined; 0 selects the previous paths.

| Bit | Change | Safety boundary |
| --- | --- | --- |
| 1 | 64-entry rather than 8-entry thread-local shader layout cache | Existing shader-creation generation invalidation; copied layouts |
| 2 | Reuse identical matrix factorisation | Fixed 64-entry thread-local cache; compares all 16 float bit patterns; no resource or pose identity assumptions |
| 4 | Avoid republishing the same context/view serial under a mutex on every draw | Cache witness only after successful publication; generation invalidated when FinishCommandList removes shared state |
| 8 | Reuse a coherent camera snapshot per recording thread | Acquire/release publication generation, mutex on refresh, reader-owned copy; no racing reads of mutable matrices |

Fine profiler scopes were added around EnsureTransform, BeginSixDof and TransformInstanceCb. Detailed collection remains opt-in. No new per-draw logging or disk writes were added.

## Validation completed

Release build passed. All five CTest contracts passed, including input rewrites, forced cache collisions, signed zero and NaN bit patterns, serial/reset invalidation, thread independence, and 20,000 coherent snapshot publications with four concurrent readers. Diff whitespace check passed. The portable benchmark script parses successfully and requires a fresh heartbeat, active gameplay, AFW submissions, active hand transforms and both controllers tracked. It restores the pre-test CPU mode in a finally block.

## Measurements and limitations

The initial canal-view capture had zero dropped profiler records and approximately 89.8 accepted AFW submissions/second. Accepted-frame interval mean was 11.133 ms, p95 12.104 ms, p99 12.854 ms, maximum 17.864 ms. This is application submission timing, **not pure CPU execution time** or compositor display timing.

Subsequent inspection showed BOTH controllers were untracked and the hand transforms were inactive (`xf 0/0`, `cbhit 0`). Thus the capture cannot establish the cost or benefit of the proposed hand/draw CPU optimisations. It is retained as a clearly limited baseline, not an accepted performance result.

After restart, Continue returned to the user's canal-side location. An attempt to capture `cpu0_a` was rejected because normal gameplay classification had not settled. The loaded stationary scene remained classified as a menu with only one player state and no tracked controllers. A temporary `menuinput 1` diagnosed the condition; `menuinput 0` was restored. Investigate this startup/classification issue separately with tracked controllers before changing its production rule.

## Next acceptance steps

Wake both controllers, verify `input ... valid 1` for both, `gpu6dof ... xf 1/...`, `screen ... kind play`, and increasing AFW accepted counters. Keep headset/controller pose stationary. Use `tools/benchmark_cpu_caches.ps1` with explicit GameDir and OutputDir, unique tags, and masks 0/1/2/4/8/15. Compare interleaved repeats in the same session, verify unchanged renderer/resolution/pose and no new fallbacks or refusals. Inspect sparse detailed profiles separately from throughput measurements. Do not sum overlapping worker-thread durations.

Before enabling a release default: test both-eye/hand correctness, moving controllers, combat, pause/resume and save reload, plus native-stereo and another supported runtime. Retain or reject each candidate based on repeatable frametime results. The public setup must not ship diagnostic command files, profiling captures, user saves or benchmark-only overrides.

## Tracked-controller follow-up and installed recovery fix

Both controllers became tracked and hand transforms active. Captures used the same candidate DLL, renderer and resolution. All listed captures recorded zero additional AFW refusals. Representative submitted FPS observations:

| Candidate | Observed FPS |
| --- | --- |
| Disabled | 73.44, 78.40, 77.93 |
| All four | 75.74, 77.83 |
| Larger shader-layout cache | 79.54, 74.95 |
| Exact matrix reuse | 77.76 |
| Serial publication witness | 77.53 |
| Camera snapshot | 78.13 |

Head direction changed substantially in the early runs and between some later runs. These are observations, not controlled estimates of improvement. The apparent layout-cache benefit did not repeat. None establishes stable 90 FPS or justifies a public default; all four remain OFF. The final detailed tracked capture used stride 128 and dropped zero records. Repeated draw interception, BeginSixDof and Unmap handling remain useful investigation targets. Worker durations overlap and cannot be summed into a critical-path CPU frame time.

A distinct persistent-drop bug was reproduced: after menu transitions, AFW reported missing matching eye/frame data while DLSS remained enabled. The old recovery scheduler checked only DLSS's transient-failure flag, so this AFW failure permanently selected native stereo. The new classification explicitly permits bounded recovery for missing AFW frame data and for transient DLSS input loss. Backend, adapter, frustum and matrix failures remain non-retryable, even if a stale DLSS transient flag is present. The existing 2/5/15-second retry budget, settled-gameplay gate, quality preservation and explicit-user-command cancellation remain intact. Success clears the failure classification.

Release build and all five tests passed after this fix, including new AFW failure-classification cases. Installed DLL SHA256: `41872D3CFA454353B2B1A9F986100A76470A1BBF5F3BCF63BF692E9F1F97C83F`. Prior benchmark DLL/PDB are preserved in the session evidence directory. Live recovery validation passed: at 17:04:30 the controlled menu transition produced the same missing-matching-frame fallback and scheduled a retry. It remained deferred through the menu/loading period. Gameplay resumed at 17:05:44; at 17:05:45 automatic attempt 1 restored quality 3. The subsequent heartbeat showed 1,132 accepted AFW reconstructions with native pair submissions no longer accumulating. No manual renderer-enable command was used after the induced failure. Evidence: recovery-verified.log and recovery-verified-heartbeat.txt in the session directory. This verifies this transition recovery, not all combat/runtime scenarios.

Final state: game left running in the Continue save, normal menu mode restored, menuinput 0, cpufast 0. No pending diagnostic command files remained. DishonoredVR.ini SHA256 matches the pre-work backup (57A946362A22E508E0197B3B38B2449F31BD8DCE7D3BF0E61129FE017961165E).
