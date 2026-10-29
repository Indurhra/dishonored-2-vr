# DLSS active-scene optimisation — 2026-10-28

## Corrected diagnosis

The previous profiling report inferred full-resolution scene shading from full-size texture allocations. A draw-level audit disproved that inference. Dishonored 2's existing dynamic-resolution controls already reduce the active scene viewport before geometry shading and several compute passes. Across three sampled Presents, native had 11,115 large geometry draws at 3286x3555; Ultra Performance had 10,633 at 1095x1185. Main compute dispatches changed from 206x223 to 69x75 groups. Counts vary with engine scheduling and animation; the viewport change, not identical draw counts, establishes reduced raster dimensions.

Remaining 512x512 shadow work, some full-output passes, geometry submission and AFW reconstruction do not shrink with the main scene pixel count. Reducing texture allocation alone would not remove those costs. Raw evidence is in `local/early-scene/native-passes.txt`, `ultra-passes.txt` and `pass-summary.json`.

## Implemented changes

- Read the actual scale from the same per-view constant-buffer shadow used to identify the eye/frame. Derive the active rectangle from the engine allocation, then verify it against the actual tone-pass viewport before DLSS evaluation.
- Snapshot HDR colour, motion and post-tonemap colour only inside that rectangle. Allocate and decode depth at the active size. Copy unfiltered current colour back only within the populated rectangle. Preserve full-size output, native temporal history, UI and separate-eye DLSS history.
- Refuse invalid scales or disagreement among temporal, colour and tone rectangles. Existing missing-input/final-pass protection returns to native stereo and releases dynamic-resolution ownership.
- Restrict DLSS draw-boundary detection to the verified non-indexed three-vertex fullscreen draws. The former path queried shader state and locked the shared shader-hash map for thousands of unrelated scene draws in SR modes.
- Diagnostic A/B controls: `vr dlsscompact 0|1` and `vr dlssfullscreenscan 0|1`; both default to 1. They are not user-facing quality settings. Native and DLAA do not use the reduced SR rectangle.

This does not skip the engine's original TAA dispatch or spatial upscale. Those remain for fallback/history correctness; removing them safely is separate work. It does not change scene detail, FOV, output resolution, shadow settings or DLSS model preset.

## Validation and measurements

Release build and live-adapter GPU smoke tests pass for DLAA and all four SR modes, both eyes, colour roundtrip, state restoration, reverse CPU recording versus GPU execution order, missing/stale inputs and invalid/mismatched active rectangles. Live Quality and Balanced have matching active sizes, full output and zero DLSS fallback/missing-depth errors. The repeated measurements and restoration details are recorded below.

## Controlled performance results

Same final executable, SHA256 `37B9483EA3D3E591591BCD802D1C233E12248EC42AAC8E6AFCBE8C73C62D8F8F`; Toolkit disabled, MockXR at 240 Hz, requested engine cap 300, unchanged Edge of the World scene and graphics settings. The output remains 3072x3264 per headset eye, from the engine's 3286x3555 output. Captures and GPU tracing run separately from FPS benchmarks. Each FPS run warms for seven seconds and discards the first three seconds of observations.

Quality-mode A/B controls isolate the two changes without replacing the DLL:

| Input copies | Shader-boundary search | Completed stereo FPS |
|---|---|---:|
| Full allocation | Every scene draw | 88.096 / 88.606 |
| Active rectangle | Every scene draw | 88.531 |
| Full allocation | Fullscreen triangles only | 92.141 |
| Active rectangle | Fullscreen triangles only | 92.029 / 93.822 |

The combined change improves the repeated Quality mean from 88.351 to 92.926 FPS, about 5.2%, or mean wall time from 11.318 to 10.762 ms/pair. Most demonstrated throughput benefit comes from avoiding unrelated draw-state queries/shared-lock lookups. Cropping alone does not establish an FPS improvement here. The earlier separate-binary 84-to-94 result overstated the isolated improvement because session variation was mixed in; use this controlled comparison instead.

Final mode sweep, all new optimisations enabled:

| Mode | Active scene / eye | Completed stereo FPS | Wall ms/pair |
|---|---|---:|---:|
| Native stereo | 3286x3555 | 58.209 | 17.180 |
| AFW + DLAA | 3286x3555 | 91.521 | 10.927 |
| AFW + Quality | 2191x2370 | 93.910 | 10.648 |
| AFW + Balanced | 1906x2062 | 92.896 | 10.765 |
| AFW + Performance | 1643x1778 | 93.193 | 10.730 |
| AFW + Ultra Performance | 1095x1185 | 93.341 | 10.713 |

All measured AFW intervals have fresh renders = reconstructions = accepted stereo pairs = scene TAA dispatches = Presents, with zero refusals or held simulation ticks. Native has two scene dispatches/Presents per pair, within one boundary Present. DLSS evaluated both eyes with zero missing-depth/fallback errors across the sweep. Transition refusals during renderer changes are excluded by warmup, not presented as zero lifetime refusals.

Ultra Performance is not faster than Quality in this scene. These are application-throughput measurements in the simulator, not physical-headset display FPS or compositor drops. The large native-to-AFW improvement is predominantly the previously established missing second render, not a new 60% optimisation.

## Independent GPU estimate

PresentMon 2.6.0 ETW captures target Dishonored2.exe, run for 15 seconds and exclude the first two seconds. Shader-boundary filtering is enabled in every GPU comparison. Metrics below are mean GPU busy time attributed per desktop Present; AFW has one Present per accepted stereo pair. Cross-queue attribution remains an estimate, and these are not DLSS-only GPU timings or headset motion-to-photon times.

| Mode / input preparation | GPU busy ms |
|---|---:|
| Quality, full-size copies | 7.087 / 7.128 |
| Quality, active-rectangle copies | 6.874 / 6.969 |
| Balanced, active rectangle | 6.668 |
| Performance, active rectangle | 6.930 |
| Ultra Performance, active rectangle | 6.282 |

At matched Quality, cropping lowers the repeated GPU-busy estimate by about 0.186 ms (2.6%). Quality-to-Ultra reduces it by roughly 9% in these samples, but completed stereo throughput stays near 93 FPS. GPU elapsed time includes more waiting in Ultra; fixed CPU/engine submission work and other non-scaling costs limit throughput. This supports a bottleneck outside scene pixel shading, not a claim that every remaining millisecond has been attributed. Native TAA, final output processing, geometry/shadows and AFW costs remain; DLSS work itself is not proportional simply to input pixel count.

The first separate-binary sweep is retained in the JSON for context, with explicit hashes; it is not pooled with the controlled A/B result. Hardware utilisation, clocks, scheduling and scene animation can affect small differences, so the table does not establish a universal ranking between Balanced and Performance.

## Movement, fallback and image-quality limits

Sixty-second periodic-motion tests used 8-degree mock head sway and 5-degree hand jitter. After warmup/discard, Quality completed 4,930 pairs at 87.589 FPS (11.417 ms/pair), and Ultra completed 5,216 at 91.003 FPS (10.989 ms/pair). Every accepted pair matched one fresh render/reconstruction/scene dispatch/Present; no refusals or held ticks occurred in those windows. The paths are periodic but not frame-exact: phase follows the clock and game animation differs. These runs establish functional stress coverage and show scene/motion variation; they are not repeated controlled evidence of a universal Ultra-over-Quality gain.

Both Quality eye captures fill the expected output and retain scene/weapon scale without a visible crop or blank border. The Ultra captures were taken immediately after switching mock motion back to neutral. The held-eye image shows foreground breakup around weapon/hand edges in that abrupt pose transition. This is a visible limitation, not accepted clarity. These unmatched captures do not isolate whether the artifact changes with cropping, and the optimisation does not claim to fix the existing AFW disocclusion/motion behaviour. Settled and moving physical-headset checks remain necessary, especially for Ultra Performance. Prefer evaluating Quality or DLAA first when clarity is the priority.

After the Ultra run, `vr dlsslive 0` deliberately removed DLSS inputs. AFW automatically returned to Native Stereo with the explanation `AFW requires matching DLSS evaluation`. Heartbeat confirms synchronized stereo enabled, `rs 0`, engine/output 3286x3555 and increasing native accepted pairs. Evidence: `local/early-scene/automatic-native-fallback.txt` and `automatic-native-dlss.txt`. The latter retains the last DLSS dimensions while reporting disabled; those stale diagnostic dimensions are not the active native scene size.

Loading/camera-cut history acceptance, physical-headset drops/reprojection, visual comfort and end-to-end latency remain outside completed validation. No Toolkit changes were made; it remains disabled because of the earlier compatibility findings. Native TAA and the original spatial upscale remain as noted above.

## Deployment, recovery and reproduction

The final compiled DLL and matching PDB are saved in `local/early-scene/final-candidate/` and the DLL is installed in the game folder. Pre-change DLL/PDB and settings are in `local/early-scene/`; its DLL hash is `553EFABC02E8B7DAF73729FF62EB182CE9840013E9C3C05EE368D7648C233ADE`. Stop the game before restoring a recovery DLL.

The game exited normally. Original INI and game configuration were restored byte-for-byte and checked by hash. Simulator override/input/HMD files were removed. Native Stereo remains the default. All earlier rejected experiments remain off; the two new validated optimisations default on when their DLSS path applies. No commits, pushes or proprietary binary redistribution were performed.

Reproduce after loading the same save with the existing mock setup, from the repository root (use unique tags):

```powershell
./tools/benchmark_dlss_modes.ps1 -Tag repeat_native_a -Mode 0
./tools/benchmark_dlss_modes.ps1 -Tag repeat_quality_full -Mode 2 -Compact 0 -FullscreenScan 0
./tools/benchmark_dlss_modes.ps1 -Tag repeat_quality_fast -Mode 2 -Compact 1 -FullscreenScan 1
./tools/benchmark_dlss_modes.ps1 -Tag repeat_balanced -Mode 3
./tools/benchmark_dlss_modes.ps1 -Tag repeat_performance -Mode 4
./tools/benchmark_dlss_modes.ps1 -Tag repeat_ultra -Mode 5
./tools/benchmark_dlss_modes.ps1 -Tag repeat_native_b -Mode 0
./tools/measure_dlss_gpu.ps1 -Tag gpu_repeat_quality -Mode 2
python tools/analyze_frame_benchmarks.py
python tools/analyze_dlss_active_scene.py
```

Mode 1 is AFW + DLAA. The benchmark helper verifies gameplay and records DLL hashes, switches, active DLSS status and counter deltas. GPU captures must run separately from FPS measurements and require the already-downloaded official portable PresentMon executable in `local/frame-profile/`. Its process-access warning did not prevent capture of the actual Dishonored2.exe PID; the raw CSVs confirm that process. Do not use its desktop display/drop fields as headset statistics.

Raw traces, screenshots, logs and build/test records are in `local/early-scene/`; FPS observations and mode metadata are in `local/frame-profile/early_*`. The tracked summary is `docs/results/dlss-active-scene-2026-10-28.json`, including GPU distributions and per-run hashes. Source recovery is preserved as a tracked-file patch plus a separate archive of new source/report/tool files in `local/early-scene/`.
