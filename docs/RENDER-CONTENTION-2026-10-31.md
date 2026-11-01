# Render contention and visual investigation — 2026-10-31

## Installed change

GpuTrace now defaults to CPU mask 16: an atomic view-serial witness bypasses the repeated `g_XfM` readiness lock in EnsureTransform when that exact view has a fully completed valid transform. All transform data reads/writes retain their existing locks. The witness is cleared under the writer lock before a new transform is built and when SetSixDof resets the cache; it is published only after successful completion. Invalid/no-controller transforms do not publish readiness. No player/pose/graphics data is approximated or cached beyond the existing view lifetime.

The prior singleton lookup improvement remains enabled. Other CPU candidates (bits 1, 2, 4, 8) remain off. `vr cpufast 0` provides an in-session diagnostic rollback, and `vr cpufast 16` restores this optimization. There is no persistent experimental setting or machine-specific path in the change.

## Measurements

Hidden OpenXR simulator, 3286x3555 output, renderScale 1, AFW mode 4 / DLSS quality index 3, both simulated controllers tracked and hands rendered. Simulator throughput is not headset/VD delivered FPS. Reported intervals are wall delivery intervals, not CPU-only durations.

The latest SLOT35 is a different position from the earlier benchmark save. Initial tests there moved into the canal and are invalid. Normal menu/Continue loading followed by `loadgame 31` established the earlier bridge scene. Direct loading from the title screen left acknowledgement stuck; those attempts produced no valid benchmark.

Stable bridge runs (20 s each, no added AFW refusals):

| Mode | Accepted FPS |
|---|---:|
| Baseline A | 101.160 |
| View snapshot only (8) | 100.606 |
| Transform readiness only (16) | 104.570 |
| Both (24), A | 104.361 |
| Baseline B | 101.475 |
| Both (24), B | 104.533 |

Readiness B reached 103.069 FPS but overlapped offline CSV analysis, so it is not primary evidence. A later fixed-view pair after looking around gave 100.009 baseline versus 103.649 readiness. Its subsequent B pair moved/turned and is invalid (do not cite its apparent 135.968 FPS gain).

Background input was then disabled with temporary `in_controlInactiveWindow 0`; headset/controller poses and the renderer remained active. Player position/direction stayed stable. The second orientation was heavier:

| Mode | Accepted FPS | Mean interval ms | p95 ms | p99 ms | >13 ms |
|---|---:|---:|---:|---:|---:|
| Baseline A | 85.262 | 11.720 | 12.787 | 13.429 | 3.06% |
| Readiness A | 89.860 | 11.066 | 12.061 | 12.771 | 0.68% |
| Baseline B | 84.662 | 11.769 | 12.811 | 13.626 | 2.95% |
| Readiness B | 89.652 | 11.106 | 12.217 | 12.866 | 0.74% |

All four had zero new AFW refusals. Approximately 5.6% mean throughput improvement and 0.66 ms lower mean delivery interval in this orientation. p95/p99 still exceed 11.11 ms: stable 90 is not established. Changing direction materially changes workload; this patch does not remove that dependence.

The fixed-view benchmark now rejects movement beyond 15 cm or forward-vector change of about 3 degrees, writes an invalid marker, and restores the prior mask in its existing finally block. This avoids accepting uncontrolled movement as a speedup. Background-input isolation is a simulator test measure, not a production setting.

## Visual investigation

The user's VD screenshot shows 85 FPS, Game 5 ms, decoding 11 ms, HEVC 10-bit at 200 Mbps, spacewarp disabled. The saved real-headset heartbeat also captures about 68.7 game Presents/s, with xrEndFrame averaging 8.110 ms and outside-Present averaging 4.742 ms. These observations do not establish a CPU-only cause or prove a decoder fault.

Captured both eye buffers looking up, stationary and during native-controller camera turns. Compared AFW against alternating native-eye submission with the same DLSS quality and resolution (`vr stereo 1`, then restored `vr afw 4`). The severe sky-to-midground smearing and two squares were not reproduced in the inspected raw eye captures. This does not exonerate AFW, DLSS, runtime timewarp or video delivery; there is no verified visual fix in this build. No speculative sky replacement, particle removal, graphics reduction or codec/runtime change was installed.

Initial HMD-only pitch changes do not turn the engine camera in this simulator's keep-game-axis mode because desktop mouse injection is disabled. The actual upward/turning comparisons therefore used native simulated controller look inputs above the dead zone. The early `sky_afw` capture did not point upward and is not used as evidence; use `sky_afw_real`, `sky_native`, `sky_turn_fast_afw` and `sky_turn_fast_native`.

## Validation

Release build and five existing regression tests pass. The readiness path was exercised through an explicit `gpu6dof 0` / `gpu6dof 1` reset before the isolated tests, and both tracked hand transforms resumed. Existing tests cover surrounding renderer contracts; they are not a proof of every possible concurrent engine schedule. The final rebuild changes only the default CPU mask from 0 to 16; live measurements used the same path enabled with the diagnostic command.

Evidence/backups are in this directory. Original DLL/PDB, mod INI, engine configuration and the user's latest headset log were preserved. Final installation and restoration hashes are recorded separately.

Final installation: DLL SHA256 `43FCCCDFEDFA6B098524921226D2C1E8DC19706862CC48BA8150020A463356F3` matches the built file. SLOT35 was loaded successfully again before closing, and its checkpoint timestamp remains 2026-10-31 18:31:15. The simulator is closed, its four marker files removed, no pending command files remain, and mod INI / engine config hashes match their pre-test backups exactly. The final binary differs from the live-tested candidate only in selecting mask 16 at startup. Full headset/VD validation is still outstanding.
