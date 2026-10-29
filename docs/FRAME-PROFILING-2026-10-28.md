# Dishonored 2 VR frame profiling — 2026-10-28

This investigation adds opt-in CPU scopes, completed-stereo intervals, memory sampling, D3D12 AFW timestamps, and reproducible benchmark/trace analysis. It preserves the existing rendering defaults. Two copy/conversion experiments and one CPU scan experiment were implemented and tested. None has established a repeatable overall throughput improvement; all remain disabled by default.

## Conditions and evidence boundaries

Working tree: `Q:/Modding Community/dishonored-2-dlss-afw`, branch `codex/dlss-afw`, base `5ba6a00`. The pre-investigation working DLL is backed up in `local/frame-profile/baseline.dll` (SHA256 `AE54EFFA0C941EDBA2B0C5EBDB692D4902449F71A989C813FFAE3DFB7F887CF8`). The earlier native recovery DLL remains in `local/afw-live-check/DishonoredVR.dll`.

Scene: the same Edge of the World balcony save, sword and crossbow visible. RTX 5090; game rendering 3286×3555 per eye, OpenXR output 3072×3264 per eye; mock runtime at 240 Hz, game cap requested at 300. Stationary comparisons use the same camera; warmup is seven seconds and the first three seconds of heartbeat observations are discarded. Rates use heartbeat file timestamps and completed stereo counter deltas, not polling frequency or raw Presents. No captures, builds or ETW traces run inside the primary timing windows.

The simulator establishes application throughput, counters and API behaviour. It cannot establish physical-headset compositor drops/reprojection, gaze tracking, visual comfort or end-to-end motion latency. Those measurements are unavailable, not zero. `1000 / stereo FPS` is wall time per completed pair, not GPU busy time or motion-to-photon latency.

The referenced `DLSS_IMPLEMENTATION.md` and `AFW_RENDERING_GUIDE.md` were cross-checked against the existing implementation. Their engine-specific constants are not adopted here. Toolkit compatibility was checked against the installed fork's matching source, not another game's rendering implementation.

## Execution map and instrumentation

`docs/results/frame-profile-scopes.csv` maps 92 explicit scope sites to source locations. Additional named waits are routed through `WaitTrace`. Raw CSV rows identify thread ID, Present epoch, scope name, start time, inclusive wall duration and exclusive duration. Exclusive duration subtracts only instrumented children on that thread. Do not sum inclusive parents or overlapping worker threads.

- Engine workers: camera/view updates, deferred graphics recording, constant-buffer/palette handling, temporal capture and DLSS evaluation. DLSS evaluation is CPU submission time here; it is not DLSS GPU execution time.
- Present thread: graphics bookkeeping, stereo scheduling, commands/diagnostics, UI, XR pose/input and eye copies, AFW input preparation and D3D11/D3D12 bridge, controller-to-game input.
- XR pump: runtime wait/begin/end and keepalive work. Accepted native/AFW pair intervals are recorded separately; mono/keepalive submissions do not count as completed stereo throughput.
- Export worker: writes completed CPU captures asynchronously after in-flight scopes drain. No per-function disk writes or blocking graphics readbacks are added to the render loop.

CPU capture uses bounded per-thread buffers, a sampling stride and an optional detail switch for draw/map hooks. Status files record drops. The first two seconds are excluded to avoid first-use buffer allocation. Fine capture is diagnostic only; it can perturb scheduling and is not used as proof of speedup. An initial saturated capture with millions of dropped events was rejected.

Memory CSVs sample process private/working set and DXGI local usage/budget once per second, with explicit validity flags. Private bytes are not VRAM usage.

## Stationary results and rejected optimisations

One binary (`FE00261FADAF63138DF7C3F97BA304A7CD025C787BB8CB1A45643C6F3675436D`), profiler disabled, Toolkit absent:

| Renderer | Repeated completed stereo FPS | Wall ms/pair |
|---|---:|---:|
| Native stereo | 58.206 / 58.040 | 17.180 / 17.230 |
| Existing AFW + DLAA, original colour copy | 92.700 / 92.786 | 10.787 / 10.777 |
| AFW + DLAA, direct colour reference | 92.044 / 92.811 | 10.864 / 10.775 |

The existing AFW path is about 59.6% faster than native in this stationary scene. This is confirmation of the earlier AFW improvement, **not a new gain produced by profiling**. In the AFW copy runs, 2,516 and 2,519 accepted pairs respectively exactly matched fresh renders, reconstructed eyes, TAA dispatches and Presents, with no refusal or held simulation tick. Native required two scene dispatches/Presents per completed stereo pair.

The direct-colour experiment replaces one HDR clone/copy with a retained input reference at a verified temporal command-list boundary. Synthetic tests verify that a subsequent overwrite cannot race the evaluation. It did not improve throughput, so `vr dlssdirectcolor 0` remains the default.

A second binary (`9E1234F9402887ADC00836577F178F96CFDE45152A0F40D4137ED237681AF94C`) tested exact identity depth/motion conversion bypass at DLAA resolution. Off: 91.668 / 91.698 FPS. On: 92.175 / 91.557 FPS. The small inconsistent difference does not establish a gain. `vr afwidentity 0` remains the default. Its eligibility checks require equal source/active/output dimensions, matching single-sample formats and subpixel jitter with a rounding margin; scaled views and edge jitter retain the shader. GPU readback tests verify exact identity output and safe toggling.

## CPU findings

Coarse captures `final_cpu_native` and `final_cpu_copy`, stride 1, detail off, zero dropped rows:

| Accepted stereo interval (ms) | Mean | Median | p95 | p99 | Worst |
|---|---:|---:|---:|---:|---:|
| Native | 17.228 | 17.104 | 18.530 | 19.181 | 22.163 |
| AFW + DLAA | 10.759 | 10.648 | 11.787 | 12.293 | 17.110 |

These are wall intervals between accepted pairs under profiling. Native Present callback inclusive mean was 0.631 ms per Present; AFW was 1.063 ms per pair. The rest of the interval is outside that callback and includes engine/driver/GPU/pacing interactions; it is not all removable mod CPU work.

AFW per-call main-thread exclusive means: bridge 0.287 ms, graphics OnPresent bookkeeping 0.249 ms, input handling 0.170 ms, XR input sync 0.100 ms, desktop Present 0.099 ms, overlay 0.054 ms, AFW adapter 0.029 ms. DLSS CPU evaluation across workers averaged 0.266 ms, p95 0.349 ms, with one 6.703 ms outlier. The raw rows retain the thread/epoch of that outlier; it does not prove 6.7 ms of GPU execution or establish a recurring stall.

Profiler-overhead check on that binary: unprofiled AFW mean throughput 92.743 FPS versus 92.942 during the coarse capture; native 58.123 versus 57.950. Differences are within about 0.3%, including run noise. This is no detected material coarse-profiler throughput penalty, not proof of zero overhead. Fine-hook capture overhead is evaluated separately and must not be extrapolated from these numbers.

## GPU findings and critical-path limits

The D3D12 backend records GPU timestamps around the real plugin warp and output copy, reuses query/readback slots only after completed fences, and uses that queue's own frequency. No CPU fence wait/readback is introduced into normal rendering. Synthetic alternating-eye tests validate samples and allocator reuse.

`final_gpu_copy`, after two-second warmup:

| Span | Mean ms | p95 ms | Interpretation |
|---|---:|---:|---|
| D3D12 AFW warp | 0.953 | 0.960 | Real plugin execution on its queue |
| D3D12 output copy | 0.0295 | 0.0299 | Copy/transition span |
| D3D11 bridge elapsed | 1.811 | 2.139 | Includes dependency waits and overlaps the D3D12 spans |

Do **not** add the bridge span to the warp/copy spans. D3D11 DLAA and input-conversion timestamp spans returned implausibly tiny values in the hooked live context despite passing the standalone timestamp test. These two live measurements are **unvalidated and excluded from GPU cost claims**. They remain a profiling limitation requiring a GPU capture or driver-supported profiler.

Earlier command-list timing measured summed ExecuteCommandList spans of 6.100 ms/native Present (about 12.2 ms/pair) versus 7.914 ms/AFW Present/pair. These are partial GPU intervals, excluding other immediate work and inter-list gaps. They must not be presented as complete GPU frame time, nor added blindly to nested stages.

The largest proven saving is the missing second full scene render. Correction from the subsequent draw-level audit: dynamic resolution already reduces main-scene geometry and multiple compute passes before DLSS. Full-size texture allocations did not mean those pixels were all shaded. The SR adapter nevertheless copied full allocations and converted full-size depth, and retained native temporal/upscale work. See `DLSS-ACTIVE-SCENE-2026-10-28.md` for the corrected evidence and follow-up changes.

## Toolkit: loading is not useful scene foveation

Installed fork: `OpenXR-Toolkit-PSVR2-v1.1.1-x64`, Toolkit DLL SHA256 `D8BA566C58E6B60F9FC93EF836DE51D0C90305CDABB95248C1AE9B6506B08B6B`, source commit `d2b4a6b6f46c3598825837fa69ee47eb6c4b5b5a` in `local/frame-profile/toolkit-reference` ([matching source](https://github.com/Robissu64/OpenXR-Toolkit-PSVR2/tree/d2b4a6b6f46c3598825837fa69ee47eb6c4b5b5a)).

The layer is enabled only in this game process through the diagnostic `DVR_toolkit.txt`; no global API-layer registration is changed. Fresh logs and loaded modules verify the actual layer, RTX 5090 graphics device and mock runtime. The mock lacked `xrDestroyAction`; implementing that missing mandatory function fixed the initial instance-creation failure. Gaze tracking is not exposed by this mock; tests use fixed foveation, Quality preset.

External registry edits did not reach the values seen by the game: in-process reads returned ERROR_FILE_NOT_FOUND for all three targeted values while external tools showed values. The precise cause of the differing registry views is not established. `vr toolkitvrs status|0|1|restore` now diagnoses and changes only this application's `vrs`, `vrs_quality` and `eye_tracking` values in the game's own HKCU view. Originals are backed up before the first write and explicitly restored. A successful registry write is still not treated as evidence of rendering activation.

Earlier `tk_vrs_*` and `tk_start_vrs_*` runs were invalid as enabled-foveation tests: ETW showed effective `vrs=0`. The corrected in-process setting produced actual mask updates and EnableVariableRateShading events.

In native gameplay, the corrected trace recorded 340 enable events, all immediately following 3072×3264 RGBA8-sRGB target candidates. The 3286×3555 scene-sized targets and 1643×1777 targets were considered but not enabled. Matching source explains two restrictions:

1. Layer render-target callbacks return outside the xrBeginFrame/xrEndFrame window. Much of this mod's engine work finishes before that window begins in Present.
2. VRS candidate aspect ratio must match within 0.01. Scene ratio 3286/3555 = 0.9243 differs from XR 3072/3264 = 0.9412 by about 0.0168, so it fails that filter.

AFW's separate D3D12 plugin device is not the OpenXR D3D11 graphics binding. Toolkit's D3D11 foveation does not automatically cover the AFW compute warp. Fixing this requires compatible scene-level integration and correct per-eye classification; arbitrarily changing aspect ratios or spreading a single eye's shading mask would risk stereo/image correctness.

Raw ETW files and decoded summaries are in `local/frame-profile/toolkit-active-native.*` and `toolkit-active-afw.*`. Trace collection is intentionally separate from FPS benchmarking.

Corrected Toolkit comparisons, fixed Quality foveation, profiler disabled:

| Renderer | Toolkit loaded, foveation off | Toolkit loaded, foveation on |
|---|---:|---:|
| Native | 56.417 FPS | 56.414 FPS (repeat 54.383) |
| AFW + DLAA, first pair | 88.295 FPS | 87.981 FPS |
| AFW + DLAA, second pair | 86.301 FPS | 86.181 FPS |

There is no demonstrated throughput benefit. The later samples drift down in both conditions, so a precise percentage penalty is not justified. VRS reaches the final output copies, not the expensive scene passes. GPU command-list spans with active Toolkit were 6.293 ms/native Present and 8.282 ms/AFW pair, also not evidence of a saving relative to the earlier no-Toolkit partial spans. These are separate diagnostic captures, not a claim of total GPU busy time.

The AFW trace recorded 526 enable events on 3072×3264 targets and none on the 789 scene-sized candidates. No driver error was logged during those samples. This is API/mask evidence for activation on those targets; a hardware shader-invocation counter was not captured.

A Toolkit-enabled exit recorded an execute-null access violation after the normal quit command. The stack includes Toolkit, D3D11 and the loader's xrDestroySwapchain path, reached from XRSession shutdown in DllMain. This points to teardown ordering, but does not isolate the exact invalid function pointer. Preserved evidence: `local/frame-profile/toolkit-exit-crash.log` and `toolkit-exit-symbols.txt`. Toolkit is not accepted for normal use with this candidate. The three in-process registry values were restored to their original absence before exit (`toolkit-process-restored.txt`); the external registry test values were restored separately. No global layer registration was changed.

## Reproduce the profiling

Use the matching build/PDB and preserve the save/configuration first. The benchmark helper assumes the game is already in the matching gameplay scene and refuses menu/loading-screen captures or reused tags. Set up the existing mock runtime override and 3072×3264 HMD configuration before launching; remove diagnostic overrides afterward for a real headset. Do not use the mock to assess physical comfort.

From the repository root:

```powershell
./tools/benchmark_frame_profile.ps1 -Tag repeat_native -Mode 0 -Seconds 30 -Identity 0 -DirectColor 0
./tools/benchmark_frame_profile.ps1 -Tag repeat_afw -Mode 1 -Seconds 30 -Identity 0 -DirectColor 0
./tools/benchmark_frame_profile.ps1 -Tag repeat_native_b -Mode 0 -Seconds 30
./tools/benchmark_frame_profile.ps1 -Tag cpu_afw -Mode 1 -Seconds 25 -Profile 'vr cpuprofile {tag} 20 1'
./tools/benchmark_frame_profile.ps1 -Tag gpu_afw -Mode 1 -Seconds 25 -Profile 'vr dlssprofile {tag} 20'
./tools/benchmark_frame_profile.ps1 -Tag lists_afw -Mode 1 -Seconds 25 -Profile 'vr gpulisttiming {tag} 20'
python tools/analyze_frame_benchmarks.py
python tools/summarize_cpu_profile.py 'Q:/SteamLibrary/steamapps/common/Dishonored2/DVR_cpu_cpu_afw.csv' local/frame-profile/cpu-afw-summary.json
python tools/summarize_gpu_lists.py 'Q:/SteamLibrary/steamapps/common/Dishonored2/DVR_gpu_lists_afw.csv'
```

For targeted detail: send `vr cpuprofile NAME 8 128 1` through a uniquely named `DVR_cmd_*.txt` in the game directory. Check the `.status.txt` for zero drops. Do not use stride-sampled call counts as total engine call counts. `tools/analyze_profile_runs.py` aggregates selected investigation captures and retains the five worst rows per scope; `tools/summarize_cpu_profile.py` retains the thread-separated distributions.

Toolkit diagnostics require a deliberate process-local layer request and restart. `vr toolkitvrs status` writes actual process-visible values. `vr toolkitvrs 1` selects fixed Quality; `0` disables VRS while retaining the layer; `restore` restores the three original values from `DVR_toolkit_config_backup.txt`. Always restore before closing the diagnostic session. Merely changing the external registry is insufficient in this environment.

ETW provider: `{cbf3adcd-42b1-4c38-830b-91980af201f5}`, all keywords, level 255. Use short separate captures, decode with `tracerpt -of XML -lr`, then `tools/summarize_toolkit_trace.py`. ETW tracing is heavy and is not enabled during benchmark comparisons. Query enable events together with candidate dimensions; menu visibility is insufficient evidence.

## Ranked bottlenecks and next engineering work

1. **Scene work and draw submission.** The native path spends two engine renders per accepted pair. AFW demonstrably removes one. A later draw-level audit establishes that SR already reduces main-scene shading resolution; the earlier inference of full-resolution upstream shading was wrong. Geometry submission, shadows, full-output processing and adapter overhead do not scale with scene pixel count. See the follow-up report.
2. **AFW reconstruction and inter-API dependency span.** About 0.95 ms of D3D12 warp plus a broader 1.81 ms bridge elapsed span. Shared resources/fences are persistent, not recreated each frame. The span includes overlapping work and waits, so removing fences without a new ownership protocol would introduce corrupt frames rather than a valid optimisation.
3. **Retained native temporal work / adapter overhead.** Quality-to-Ultra-Performance settings reduce active scene dimensions, but retained native passes and full-size adapter work consume some savings. Validate GPU cost and throughput separately; the follow-up report measures both without treating texture allocation dimensions as shaded dimensions.
4. **Mod CPU and spikes.** Present-side mod work averages roughly 1.06 ms in AFW; much of the wall interval is elsewhere. Graphics bookkeeping and input handling are measurable, but deleting pose/controller work to inflate FPS would violate the task. Rare DLSS CPU outliers need correlation with a proper engine/driver trace before attribution.
5. **Toolkit interception overhead without scene savings.** Current foveation affects final eye copies and does not establish useful savings. Keep it disabled for this candidate. Integrating it safely would also require resolving the shutdown failure.

The two measured copy/conversion experiments are not accepted optimisations. No additional fixed percentage gain is claimed. The candidate is a profiling/diagnostic build retaining the proven AFW saving; it is not a completed early-resolution rewrite or a headset-quality release.

## Automated checks and limitations

Release compilation passed. Executed tests passed for nested/exclusive CPU accounting, thread separation, accepted-stereo intervals, busy refusal, bounded/asynchronous export, detail filtering and memory validity; AFW identity conversion/crop/jitter/state ownership; persistent D3D11/D3D12 resources with 12 alternating-eye identity/translation frames and nine completed timestamp samples; DLAA plus all four DLSS SR modes, both eyes, colour roundtrip, deferred ordering and missing/stale-input fallback.

Profiler buffers retain up to 200,000 rows per participating thread after a capture (about 8 MB per thread for row storage); a restart releases this diagnostic allocation. It does not exist before a thread first participates. The profiled memory figures therefore include this overhead. Fine capture remains optional and disabled by default.

Remaining checks include valid live DLSS GPU timings, complete GPU busy-time attribution, frame-exact movement replay, physical-headset dropped/reprojected frames, gaze-based foveation, comfort and fast-motion/disocclusion quality. Existing loading/camera-cut and opaque-backend resize limitations remain as recorded in `DLSS-AFW-IMPLEMENTATION.md`; this profiling pass does not certify them.

## Independent ETW GPU estimate

Portable PresentMon 2.6.0 was downloaded from the [official release](https://github.com/GameTechDev/PresentMon/releases/tag/v2.6.0), with a valid Intel Corporation Authenticode signature and SHA256 `B2A706BC6AD475749E3B7E3409263AA1E6906D45BDCF993F6DBC0F660188F1AF`. It was run as a console capture; no service/installer or overlay was installed. `presentmon-native.csv` and `presentmon-afw.csv` target only Dishonored2.exe, use 20-second windows and discard two seconds.

| PresentMon metric (ms, per desktop Present) | Native mean / p95 | AFW mean / p95 |
|---|---:|---:|
| FrameTime | 8.618 / 11.151 | 10.909 / 12.103 |
| GPUTime | 5.526 / 7.345 | 8.702 / 9.946 |
| GPUBusy | 4.738 / 5.308 | 8.393 / 8.997 |
| GPUWait | 0.788 / 2.738 | 0.308 / 1.493 |

Native has two desktop Presents per accepted stereo pair; AFW has one. The rough native pair-normalised GPU-busy estimate is 9.475 ms versus 8.393 ms for AFW. This is ETW attribution to desktop-presented frames, not independently correlated headset-frame GPU completion, and it does not validate the suspicious DLSS stage timer. Do not reinterpret PresentMon's desktop display/drop metrics as headset compositor metrics. Its CPUBusy field is also not a sum of active CPU execution across all engine threads.

The [upstream documentation](https://github.com/GameTechDev/PresentMon) describes GPU scheduling accuracy limits. The recorded GraphicsDrivers HwSchMode value was 1; no scheduling setting was changed. Cross-queue attribution and full per-eye frame correlation remain limitations. This independent estimate supports reduced GPU work per completed stereo pair, but the much larger throughput improvement also involves eliminating an entire second engine frame and its scheduling costs.

Reproduce with the local portable executable: `--process_name Dishonored2.exe --timed 20 --terminate_after_timed --no_console_stats --no_track_input --session_name DVRFrameProfile --output_file <unique.csv> --v2_metrics`. Run separately from the other profilers. `tools/summarize_presentmon_profile.py` generates the saved summary.

## Recurring CPU spike isolated

Additional nested scopes isolate the input-handling spike to `SwordContact.ScanTargets`, called every 100 ms. Stationary capture with the original order: 168 scans, mean 1.107 ms, p95 1.168 ms. Head-aim servo averaged 0.0107 ms, sword gesture recognition 0.0035 ms, and there were no synthetic SendInput calls in that stationary window. This rejects the earlier suspicion that input injection itself caused that spike.

An optional `vr swordcfg earlydistance 1` experiment moves the existing distance rejection before the read-only weapon-ownership lookup, retaining the same radius, refresh frequency and checks for nearby targets. It reduced 168 scans to mean 0.956 ms, p95 1.037 ms; input-handler p95 fell from 1.151 to 1.012 ms. This is about 0.15 ms saved on a scan, roughly 0.015 ms amortised per AFW frame. Whole-pair wall time did not improve in those captures (10.741 versus 10.786 ms). It is therefore **not an established FPS optimisation** and defaults to off. No controller work is skipped or rate-limited.

The final source now includes three optional experiments: direct colour reference, identity depth/motion bypass and early distance rejection. All remain off unless explicitly selected for diagnostics. The first two showed no repeatable throughput benefit; the third reduces a measured function cost but has not established an overall frame-time gain.


## Final candidate, movement and restoration

Final compiled and installed DLL SHA256: `553EFABC02E8B7DAF73729FF62EB182CE9840013E9C3C05EE368D7648C233ADE`. Matching DLL/PDB are saved in `local/frame-profile/final-candidate/`. The final change made profiler sampling stride atomic; it does not change rendering defaults. Release compilation and the profiler smoke test passed after that change.

Exact-final-binary, profiling disabled, stationary runs:

| Run | Completed stereo FPS | Wall ms/pair |
|---|---:|---:|
| Native | 54.407 | 18.380 |
| AFW + DLAA, scan experiment off A | 87.029 | 11.490 |
| AFW + DLAA, scan experiment on | 87.402 | 11.441 |
| AFW + DLAA, scan experiment off B | 91.803 | 10.893 |

Each AFW interval had exactly one fresh render, reconstruction, scene TAA dispatch and Present per accepted pair, with zero refusals and held ticks. Native had 2,953 Presents for 1,476 accepted pairs; the one-Present boundary difference is consistent with sampling between eyes. The off/on/off scan comparison did not reproduce the earlier isolated 96.424 FPS result. Drift between the off brackets exceeds the apparent on-versus-first-off benefit; no overall scan optimisation is accepted. All three experimental switches remain off. These final-session values supersede any suggestion that the final DLL itself was measured at exactly the earlier 58/92 FPS values.

Long movement runs on the immediately preceding binary (`8F851ED8CD37588B405EDFDD3E0DA698606B76BD2296260462F04885822684AC`, before the atomic profiler-only fix) used the same periodic 8-degree head sway and 5-degree hand jitter. Approximately 100 seconds were analysed per condition: native 57.402 FPS / 17.421 ms per pair; AFW 92.580 FPS / 10.802 ms. Native completed 5,766 pairs from 11,532 Presents; AFW completed 9,216 pairs from exactly 9,216 fresh renders/reconstructions/scene dispatches/Presents, with zero refusals or held ticks. This is phase-averaged periodic motion, not frame-exact replay: simulator phase follows the clock, and game animations differ. Shorter motion runs varied more, so the longer samples are retained alongside them.

The analyser flags mixed native intervals and mismatched AFW counters. `candidate_native_b` contains 49 extra Presents and is excluded from clean comparisons. Raw per-run metadata records each DLL hash; different binaries and sessions are not silently pooled.

Memory during the coarse captures (decimal GB, means, 18 valid samples after warmup):

| Renderer | Process private | Working set | DXGI local video usage | Local budget |
|---|---:|---:|---:|---:|
| Native | 11.071 | 4.728 | 4.930 | 32.944 |
| AFW + DLAA | 14.410 | 4.727 | 7.975 | 32.944 |

These include diagnostic allocations and are not a leak test. Later AFW captures reached roughly 8.75 GB local usage; allocations and capture history differ. No exhausted-video-memory diagnosis is supported by these samples.

The final session returned to native stereo and exited through the normal quit command; no process remained and no new crash was recorded in the session log. Original INI and game configuration were restored byte-for-byte, verified by hashes. Simulator override/input/HMD files were removed. Toolkit is disabled and its targeted settings were restored. The tested candidate remains installed with Native Stereo as the default; AFW + DLSS can be selected in the existing controller menu. No new rendering-quality acceptance is implied.

Evidence: `docs/results/frame-profile-2026-10-28.json` contains selected counters, timings, memory, metadata and worst-call records; `docs/results/frame-profile-scopes.csv` maps the scopes. Raw CPU/GPU captures are also archived in `local/frame-profile/raw/`, with test output in `local/frame-profile/tests/`. `local/frame-profile/tracked-source-checkpoint.patch` preserves tracked changes; new source/report/tool files are separately archived alongside it. No commits or pushes were made.

Before normal-use acceptance, a focused physical-headset test must check native versus AFW + DLAA at the same scene: stereo depth, head-turn response, moving hands/weapons, disocclusions and compositor statistics. Toolkit should remain disabled. Valid live DLSS GPU stage timing, further scene/temporal cost reduction, loading/camera-cut history behaviour and Toolkit teardown remain unresolved engineering work, not completed validation.
